#include "HighlightCapture.h"

#include <Arduino.h>
#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_system.h>

#include <algorithm>
#include <cstring>

#include "clippings/ClippingText.h"

namespace {
constexpr size_t MAX_FILE_BYTES = 96 * 1024;
constexpr size_t MAX_PAGE_BYTES = 16 * 1024;
constexpr unsigned long BUILD_TIMEOUT_MS = 30000;

// Fixed buffers avoid retaining five pages of text or allocating a JSON document.
class ExportWriter {
  HalFile& file;
  char buffer[512];
  size_t used = 0;
  size_t total = 0;
  bool ok = true;

 public:
  explicit ExportWriter(HalFile& file) : file(file) {}
  bool flush() {
    if (used && file.write(buffer, used) != used) ok = false;
    used = 0;
    return ok;
  }
  void put(char c) {
    if (!ok) return;
    if (++total > MAX_FILE_BYTES) {
      ok = false;
      return;
    }
    buffer[used++] = c;
    if (used == sizeof(buffer)) flush();
  }
  void text(const char* value) {
    while (*value) put(*value++);
  }
  void escaped(const char* value, size_t length) {
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
      const auto c = static_cast<unsigned char>(value[i]);
      if (c == '"' || c == '\\') {
        put('\\');
        put(c);
      } else if (c < 32) {
        text("\\u00");
        put(HEX_DIGITS[c >> 4]);
        put(HEX_DIGITS[c & 15]);
      } else {
        put(c);
      }
    }
  }
  void quoted(const std::string& value) {
    if (value.size() > 4096) ok = false;
    put('"');
    escaped(value.data(), value.size());
    put('"');
  }
  void number(int value) {
    char digits[16];
    snprintf(digits, sizeof(digits), "%d", value);
    text(digits);
  }
};

bool writePageText(const Page& page, ExportWriter& json, ExportWriter& plain) {
  size_t bytes = 0;
  uint32_t previousEnd = UINT32_MAX;
  bool previousHyphen = false;
  bool afterDropCap = false;
  uint16_t maxWords = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* block = static_cast<const PageLine&>(*element).getBlock();
    if (!block || !block->valid() || block->wordCount() > 4096) return false;
    maxWords = std::max(maxWords, block->wordCount());
  }
  // At most 8 KB, reused across this page's lines and released before the next page.
  auto order = makeUniqueNoThrow<uint16_t[]>(std::max<uint16_t>(maxWords, 1));
  if (!order) {
    LOG_ERR("HL", "OOM: %u word indices", maxWords);
    return false;
  }
  for (const auto& element : page.elements) {
    if (element->getTag() == TAG_PageDropCap) {
      const char* text = static_cast<const PageDropCap&>(*element).getText();
      const size_t length = strlen(text);
      if (bytes + length > MAX_PAGE_BYTES) return false;
      json.escaped(text, length);
      plain.text(text);
      bytes += length;
      afterDropCap = true;
      continue;
    }
    if (element->getTag() != TAG_PageLine) continue;
    const auto* block = static_cast<const PageLine&>(*element).getBlock();
    if (!block || !block->valid()) return false;
    const auto count = block->wordCount();
    if (count == 0) continue;
    bool known = true;
    for (uint16_t i = 0; i < count; ++i) {
      order[i] = i;
      known &= block->wordSourceRange(i).start != UINT32_MAX;
    }
    if (known) {
      std::sort(order.get(), order.get() + count, [block](uint16_t a, uint16_t b) {
        const auto sa = block->wordSourceRange(a).start;
        const auto sb = block->wordSourceRange(b).start;
        return sa == sb ? a < b : sa < sb;
      });
    }
    bool paragraph = false;
    for (uint16_t n = 0; n < count; ++n) {
      const auto i = order[n];
      paragraph |= block->wordStartsParagraph(i);
      const char* word = block->wordText(i);
      while (*word == ' ' || *word == '\t') ++word;
      if (clippingText::hasEmSpacePrefix(word)) word += 3;
      if (!clippingText::hasVisibleText(word)) continue;
      size_t length = strlen(word);
      const auto source = block->wordSourceRange(i);
      const bool hyphen = block->wordHasDiscretionaryHyphen(i);
      if (hyphen && length && word[length - 1] == '-') --length;
      char separator = '\0';
      if (bytes && !afterDropCap) {
        if (paragraph)
          separator = '\n';
        else if (previousEnd != UINT32_MAX && source.start != UINT32_MAX) {
          if (source.start > previousEnd) separator = ' ';
        } else if (!previousHyphen)
          separator = ' ';
      }
      if (bytes + length + (separator != '\0') > MAX_PAGE_BYTES) return false;
      if (separator) {
        json.escaped(&separator, 1);
        plain.put(separator);
        ++bytes;
      }
      json.escaped(word, length);
      for (size_t j = 0; j < length; ++j) plain.put(word[j]);
      bytes += length;
      previousEnd = source.end;
      previousHyphen = hyphen;
      afterDropCap = false;
      paragraph = false;
    }
  }
  return true;
}

struct Position {
  int spine;
  int page;
};

std::string chapterTitle(const Epub& epub, int spine) {
  const int index = epub.getTocIndexForSpineIndex(spine);
  return index >= 0 ? epub.getTocItem(index).title : std::string(tr(STR_CHAPTER)) + " " + std::to_string(spine + 1);
}
}  // namespace

bool highlights::capture(const std::shared_ptr<Epub>& epub, Section& current, const int spine, GfxRenderer& renderer,
                         const ReaderRenderSpec& spec) {
  const unsigned long started = millis();
  std::unique_ptr<Section> adjacent;
  int adjacentSpine = -1;
  auto sectionAt = [&](int index) -> Section* {
    if (index == spine) {
      adjacent.reset();
      adjacentSpine = -1;
      return &current;
    }
    if (adjacentSpine != index) {
      // Section builders share the book's CSS parser. Only one may own it at a time.
      current.suspendBuild();
      adjacent.reset();
      adjacent = makeUniqueNoThrow<Section>(epub, index, renderer);
      adjacentSpine = index;
      if (!adjacent) {
        LOG_ERR("HL", "OOM: adjacent chapter");
        return nullptr;
      }
      adjacent->loadSectionFile(spec);
    }
    return adjacent.get();
  };
  // -1 requests the chapter's tail; positive targets build only as far as needed.
  auto ensurePage = [&](Section& target, int page) {
    if (page >= 0 && page < target.pageCount) return true;
    // loadSectionFile() does not set isBuildComplete() for a finalized cache.
    if (target.isBuildComplete() || (!target.isBuilding() && !target.isPartial() && target.pageCount > 0)) return true;
    if (!target.isBuilding()) {
      GfxRenderer::FrameBufferLoan loan(renderer);
      if (!target.startBuild(spec)) return false;
    }
    while (!target.isBuildComplete() && (page < 0 || target.pageCount <= page)) {
      if (millis() - started > BUILD_TIMEOUT_MS || !target.buildSomeMore(2)) return false;
      delay(1);
    }
    return true;
  };

  Position positions[5];
  int before = 0;
  Position cursor{spine, current.currentPage};
  positions[2] = cursor;
  for (int n = 1; n <= 2; ++n) {
    if (cursor.page > 0) {
      --cursor.page;
    } else {
      do {
        if (--cursor.spine < 0) break;
        Section* target = sectionAt(cursor.spine);
        if (!target || !ensurePage(*target, -1)) {
          LOG_ERR("HL", "Cannot build preceding highlight pages");
          return false;
        }
        cursor.page = target->pageCount - 1;
      } while (cursor.page < 0);
      if (cursor.spine < 0) break;
    }
    positions[2 - n] = cursor;
    ++before;
  }
  int after = 0;
  cursor = positions[2];
  for (int n = 1; n <= 2; ++n) {
    ++cursor.page;
    for (;;) {
      Section* target = sectionAt(cursor.spine);
      if (!target || !ensurePage(*target, cursor.page)) {
        LOG_ERR("HL", "Cannot build following highlight pages");
        return false;
      }
      if (cursor.page < target->pageCount) break;
      cursor.page = 0;
      if (++cursor.spine >= epub->getSpineItemsCount()) break;
    }
    if (cursor.spine >= epub->getSpineItemsCount()) break;
    positions[2 + n] = cursor;
    ++after;
  }
  if (!Storage.ensureDirectoryExists("/Highlights")) return false;
  char id[33];
  snprintf(id, sizeof(id), "%08lx%08lx%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  const std::string base = std::string("/Highlights/") + id;
  const std::string jsonTmp = base + ".json.tmp";
  const std::string textTmp = base + ".txt.tmp";
  HalFile jsonFile = Storage.open(jsonTmp.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  HalFile textFile = Storage.open(textTmp.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  ScopedCleanup cleanup([&] {
    jsonFile.close();
    textFile.close();
    Storage.remove(jsonTmp.c_str());
    Storage.remove(textTmp.c_str());
  });
  if (!jsonFile || !textFile) return false;
  auto jsonWriter = makeUniqueNoThrow<ExportWriter>(jsonFile);
  auto textWriter = makeUniqueNoThrow<ExportWriter>(textFile);
  if (!jsonWriter || !textWriter) {
    LOG_ERR("HL", "OOM: export buffers");
    return false;
  }
  auto& json = *jsonWriter;
  auto& plain = *textWriter;
  json.text("{\"id\":");
  json.quoted(id);
  json.text(",\"book_title\":");
  json.quoted(epub->getTitle());
  json.text(",\"author\":");
  json.quoted(epub->getAuthor());
  json.text(",\"book_path\":");
  json.quoted(epub->getPath());
  json.text(",\"chapter\":");
  json.quoted(chapterTitle(*epub, spine));
  json.text(",\"page\":");
  json.number(current.currentPage + 1);
  json.text(",\"pages\":[");
  plain.text(epub->getTitle().c_str());
  plain.text(" — ");
  plain.text(epub->getAuthor().c_str());
  plain.text("\n\n");
  for (int index = 2 - before; index <= 2 + after; ++index) {
    const auto position = positions[index];
    Section* target = sectionAt(position.spine);
    auto page = target ? target->loadPage(position.page) : nullptr;
    if (!page) {
      LOG_ERR("HL", "Cannot read highlight page %d/%d", position.spine, position.page);
      return false;
    }
    const std::string chapter = chapterTitle(*epub, position.spine);
    if (index != 2 - before) json.put(',');
    json.text("{\"chapter\":");
    json.quoted(chapter);
    json.text(",\"spine\":");
    json.number(position.spine);
    json.text(",\"page\":");
    json.number(position.page + 1);
    json.text(index == 2 ? ",\"current\":true,\"text\":\"" : ",\"current\":false,\"text\":\"");
    plain.text(chapter.c_str());
    plain.text(" — ");
    plain.text(tr(STR_HIGHLIGHT_PAGE_LABEL));
    plain.put(' ');
    plain.number(position.page + 1);
    if (index == 2) {
      plain.put(' ');
      plain.text(tr(STR_HIGHLIGHT_CAPTURED_PAGE));
    }
    plain.put('\n');
    if (!writePageText(*page, json, plain)) {
      LOG_ERR("HL", "Highlight page exceeds text limit or cannot be decoded");
      return false;
    }
    json.text("\"}");
    plain.text("\n\n");
  }
  json.text("]}\n");
  if (!json.flush() || !plain.flush()) return false;
  if (!jsonFile.close() || !textFile.close()) return false;
  if (!Storage.rename(textTmp.c_str(), (base + ".txt").c_str())) return false;
  // The .json rename publishes the durable queue record only after both exports exist.
  if (!Storage.rename(jsonTmp.c_str(), (base + ".json").c_str())) {
    Storage.remove((base + ".txt").c_str());
    return false;
  }
  LOG_INF("HL", "Saved %d pages as %s", before + after + 1, id);
  return true;
}
