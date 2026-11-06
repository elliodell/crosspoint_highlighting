#include <Epub/Section.h>
#include <HalStorage.h>

#include <cassert>
#include <iostream>

#include "highlights/HighlightCapture.h"

std::string exported(const char* extension) {
  for (const auto& [path, node] : fake::files) {
    if (path.ends_with(extension)) return std::string(node->bytes.begin(), node->bytes.end());
  }
  return "";
}
void noExports() {
  assert(exported(".json").empty());
  assert(exported(".txt").empty());
  assert(exported(".tmp").empty());
}
std::shared_ptr<Epub> makeBook(std::vector<std::vector<std::string>> chapters) {
  auto book = std::make_shared<Epub>();
  book->chapters = std::move(chapters);
  book->cacheChapters();
  return book;
}
void assertOneBuilder(const Epub& book) {
  assert(book.maxActiveBuilders <= 1);
  assert(book.cssViolations == 0);
}

struct ElementSpec {
  std::vector<TextBlock::Word> words;
  std::string dropCap;
};
ElementSpec line(const std::string& text, uint32_t offset, bool paragraph, bool sourceContinuation = false) {
  return {{{text, {offset, offset + static_cast<uint32_t>(text.size())}, false, paragraph, sourceContinuation}}, ""};
}
ElementSpec cap(const char* text) { return {{}, text}; }
void pageLayout(const std::shared_ptr<Epub>& book, int spine, int page, std::vector<ElementSpec> elements) {
  book->pageFactories[{spine, page}] = [elements = std::move(elements)] {
    auto result = std::make_unique<Page>();
    for (const auto& item : elements) {
      if (!item.dropCap.empty()) {
        auto element = std::make_unique<PageDropCap>();
        element->text = item.dropCap;
        result->elements.push_back(std::move(element));
      } else {
        auto element = std::make_unique<PageLine>();
        element->block.words = item.words;
        result->elements.push_back(std::move(element));
      }
    }
    return result;
  };
}
std::vector<std::string> pageTexts(const std::string& json) {
  std::vector<std::string> result;
  size_t offset = 0;
  while ((offset = json.find("\"text\":\"", offset)) != std::string::npos) {
    offset += 8;
    const size_t start = offset;
    while (offset < json.size() && json[offset] != '"') {
      if (json[offset] == '\\') ++offset;
      ++offset;
    }
    assert(offset < json.size());
    result.push_back(json.substr(start, offset - start));
    ++offset;
  }
  return result;
}
std::shared_ptr<Epub> paragraphBook(int count) {
  std::vector<std::string> pages;
  for (int page = 0; page < count; ++page) pages.push_back("page" + std::to_string(page));
  auto book = makeBook({pages});
  for (int page = 0; page < count; ++page) {
    pageLayout(book, 0, page, {line(pages[page], page * 100, page == 0)});
  }
  return book;
}
void paragraphCaptureTests(GfxRenderer& renderer, const ReaderRenderSpec& spec) {
  {
    fake::reset();
    auto book = paragraphBook(9);
    pageLayout(book, 0, 0, {line("unrelated-before", 0, true), line("start", 50, true)});
    pageLayout(book, 0, 1, {line("left-break", 100, true, true)});
    pageLayout(book, 0, 2, {line("left-edge", 200, true, true)});
    pageLayout(book, 0, 6, {line("right-edge", 600, true, true)});
    pageLayout(book, 0, 7, {line("right-break", 700, true, true)});
    pageLayout(book, 0, 8, {line("end", 800, false), line("unrelated-after", 850, true)});
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 4;
    assert(highlights::capture(book, current, 0, renderer, spec));
    assert(
        (pageTexts(exported(".json")) == std::vector<std::string>{"start", "left-break", "left-edge", "page3", "page4",
                                                                  "page5", "right-edge", "right-break", "end"}));
    assert(exported(".txt").find("unrelated") == std::string::npos);
    assert(current.currentPage == 4);
    assertOneBuilder(*book);
  }
  {
    fake::reset();
    auto book = makeBook({{"p0", "p1", "p2", "p3", "p4", "p5", "p6"}});
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 3;
    assert(highlights::capture(book, current, 0, renderer, spec));
    // Every plain stub page starts a paragraph; looking ahead must not add p6.
    assert((pageTexts(exported(".json")) == std::vector<std::string>{"p1", "p2", "p3", "p4", "p5"}));
    assert(current.currentPage == 3);
  }
  {
    fake::reset();
    auto book = paragraphBook(10);
    pageLayout(book, 0, 0, {line("unrelated-before", 0, true), line("left-start", 50, true)});
    pageLayout(book, 0, 1, {line("left-middle1", 100, false)});
    pageLayout(book, 0, 2, {line("left-middle2", 200, false)});
    pageLayout(book, 0, 3, {line("left-core", 300, false)});
    pageLayout(book, 0, 4, {line("core-one", 400, true)});
    pageLayout(book, 0, 5, {line("selected", 500, true)});
    pageLayout(book, 0, 6, {line("core-two", 600, true)});
    pageLayout(book, 0, 7, {line("right-start", 700, true)});
    pageLayout(book, 0, 8, {line("right-middle", 800, false)});
    pageLayout(book, 0, 9, {line("right-end", 900, false), line("unrelated-after", 950, true)});
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 5;
    assert(highlights::capture(book, current, 0, renderer, spec));
    assert((pageTexts(exported(".json")) == std::vector<std::string>{"left-start", "left-middle1", "left-middle2",
                                                                     "left-core", "core-one", "selected", "core-two",
                                                                     "right-start", "right-middle", "right-end"}));
    assert(exported(".txt").find("unrelated") == std::string::npos);
    assert(current.currentPage == 5);
    assertOneBuilder(*book);
  }
  {
    fake::reset();
    auto book = paragraphBook(8);
    pageLayout(book, 0, 2, {});
    pageLayout(book, 0, 6, {});
    pageLayout(book, 0, 7, {line("next-paragraph", 700, true)});
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 4;
    assert(highlights::capture(book, current, 0, renderer, spec));
    assert((pageTexts(exported(".json")) ==
            std::vector<std::string>{"page0", "page1", "", "page3", "page4", "page5", ""}));
    assert(current.currentPage == 4);
  }
  {
    fake::reset();
    auto book = paragraphBook(8);
    ElementSpec rtlStart{{{"start", {55, 60}, false, false}, {"he", {51, 53}, false, true}}, ""};
    pageLayout(book, 0, 0, {line("unrelated-before", 0, true), cap("T"), rtlStart});
    pageLayout(book, 0, 4, {line("old", 400, true), cap("N"), line("ew", 420, true)});
    ElementSpec rtlNewParagraph{{{"second", {530, 536}, false, false}, {"begin", {520, 525}, false, true}}, ""};
    pageLayout(book, 0, 5, {line("first", 500, true), rtlNewParagraph});
    pageLayout(book, 0, 6, {line("right-start", 600, true)});
    pageLayout(book, 0, 7, {line("right-end", 700, false), cap("X"), line("cluded", 800, true)});
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 4;
    assert(highlights::capture(book, current, 0, renderer, spec));
    const auto texts = pageTexts(exported(".json"));
    assert(texts.size() == 8);
    assert(texts.front() == "The start");
    assert(texts[4] == "old\\u000aNew");
    assert(texts[5] == "first\\u000abegin second");
    assert(texts.back() == "right-end");
    assert(exported(".txt").find("first\nbegin second") != std::string::npos);
    assert(exported(".txt").find("unrelated") == std::string::npos);
    assert(exported(".txt").find("Xcluded") == std::string::npos);
    assert(current.currentPage == 4);
  }
  {
    fake::reset();
    auto book = makeBook({{"previous-chapter"}, {"p0", "p1", "p2", "p3", "p4", "p5", "p6"}, {"next-chapter"}});
    for (int page = 0; page < 7; ++page)
      pageLayout(book, 1, page, {line("p" + std::to_string(page), page * 100, false)});
    Section current(book, 1, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 3;
    assert(highlights::capture(book, current, 1, renderer, spec));
    // Chapter edges end the paragraph even if no paragraph marker was emitted.
    assert((pageTexts(exported(".json")) == std::vector<std::string>{"p0", "p1", "p2", "p3", "p4", "p5", "p6"}));
    assert(current.currentPage == 3);
    assertOneBuilder(*book);
  }
  for (int count : {64, 65}) {
    fake::reset();
    auto book = paragraphBook(count);
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 32;
    assert(highlights::capture(book, current, 0, renderer, spec) == (count == 64));
    if (count == 64) {
      const auto texts = pageTexts(exported(".json"));
      assert(texts.size() == 64);
      assert(texts.front() == "page0" && texts.back() == "page63");
    } else {
      noExports();
    }
    assert(current.currentPage == 32);
    assertOneBuilder(*book);
  }
  for (int failedPage : {0, 6}) {
    fake::reset();
    auto book = paragraphBook(7);
    book->failedLoads.insert({0, failedPage});
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 3;
    assert(!highlights::capture(book, current, 0, renderer, spec));
    noExports();
    assert(current.currentPage == 3);
    assertOneBuilder(*book);
  }
  {
    fake::reset();
    auto book = paragraphBook(7);
    for (int page = 0; page < 7; ++page) {
      pageLayout(book, 0, page, {line(std::string(15000, 'x'), page * 20000, page == 0)});
    }
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 3;
    assert(!highlights::capture(book, current, 0, renderer, spec));
    noExports();
    assert(current.currentPage == 3);
  }
  {
    fake::reset();
    auto book = paragraphBook(7);
    pageLayout(book, 0, 0, {line(std::string(16385, 'x'), 0, true)});
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 3;
    assert(!highlights::capture(book, current, 0, renderer, spec));
    noExports();
    assert(current.currentPage == 3);
  }
  {
    fake::reset();
    auto book = paragraphBook(9);
    pageLayout(book, 0, 2, {line("left-edge", 200, true)});
    book->caches[0] = {7, true};
    book->failBuildAfterPages[0] = 6;
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 4;
    assert(!highlights::capture(book, current, 0, renderer, spec));
    noExports();
    assert(current.currentPage == 4);
    assertOneBuilder(*book);
  }
  {
    fake::reset();
    auto book = paragraphBook(7);
    pageLayout(book, 0, 0, {line("late-table-cell", 100, true), line("earlier-table-cell", 50, false)});
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 4;
    assert(!highlights::capture(book, current, 0, renderer, spec));
    noExports();
    assert(current.currentPage == 4);
  }
  for (bool reverseLeftEdge : {true, false}) {
    fake::reset();
    auto book = paragraphBook(7);
    if (reverseLeftEdge) {
      pageLayout(book, 0, 0, {line("later-source-before-page1", 200, true)});
    } else {
      pageLayout(book, 0, 6, {line("earlier-source-after-page5", 400, false)});
    }
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    current.currentPage = 3;
    assert(!highlights::capture(book, current, 0, renderer, spec));
    noExports();
    assert(current.currentPage == 3);
    assertOneBuilder(*book);
  }
}
int main() {
  GfxRenderer renderer;
  ReaderRenderSpec spec;
  {
    auto book = makeBook({{"before2", "before1"}, {"current"}, {}, {"after1", "after2"}});
    Section section(book, 1, renderer);
    assert(section.loadSectionFile(spec));
    assert(!section.isBuildComplete());
    fake::reset();
    assert(highlights::capture(book, section, 1, renderer, spec));
    const auto json = exported(".json");
    assert(json.find("before2") < json.find("before1"));
    assert(json.find("before1") < json.find("current\"}"));
    assert(json.find("current\"}") < json.find("after1"));
    assert(json.find("after1") < json.find("after2"));
    assert(json.find("A \\\"book\\\"") != std::string::npos);
    assert(json.find("\"current\":true") != std::string::npos);
    assert(section.currentPage == 0);
    assert(exported(".txt").find("[CAPTURED PAGE]") != std::string::npos);
    assert(exported(".tmp").empty());
    // Loading finalized caches does not signal buildComplete; neither tail should rebuild.
    assert((book->buildStarts == std::vector<int>{0, 0, 1, 0}));
    assertOneBuilder(*book);
  }

  for (int position : {0, 1, 2, 3, 4}) {
    fake::reset();
    auto book = makeBook({{"p0", "p1", "p2", "p3", "p4"}});
    Section one(book, 0, renderer);
    assert(one.loadSectionFile(spec));
    one.currentPage = position;
    assert(highlights::capture(book, one, 0, renderer, spec));
    auto text = exported(".txt");
    for (int p = 0; p < 5; ++p)
      assert((text.find("p" + std::to_string(p)) != std::string::npos) == (abs(p - position) <= 2));
    assert(one.currentPage == position);
    assert(book->buildStarts[0] == 0);
  }
  {
    fake::reset();
    auto book = makeBook({{"p0", "p1", "p2", "p3", "p4"}});
    book->caches[0] = {1, true};
    Section partial(book, 0, renderer);
    assert(partial.loadSectionFile(spec));
    assert(partial.isPartial());
    assert(highlights::capture(book, partial, 0, renderer, spec));
    assert(exported(".txt").find("p2") != std::string::npos);
    assert(book->buildStarts[0] == 1);
    assert(partial.isBuilding());
    assertOneBuilder(*book);
  }
  {
    fake::reset();
    auto book = makeBook({{"before0", "before1", "before2"}, {"p0", "p1", "p2", "p3", "p4", "p5"}});
    book->caches[0] = {};
    book->caches[1] = {};
    Section current(book, 1, renderer);
    assert(current.startBuild(spec));
    assert(current.buildSomeMore(1));
    book->events.clear();
    assert(highlights::capture(book, current, 1, renderer, spec));
    assert(exported(".txt").find("before1") != std::string::npos);
    assert(exported(".txt").find("p2") != std::string::npos);
    assert((book->buildStarts == std::vector<int>{1, 2}));
    assert(book->suspensions[1] == 2);
    const auto destroyed = std::find(book->events.begin(), book->events.end(), "destroy:0");
    const auto resumed = std::find(book->events.begin(), book->events.end(), "start:1");
    assert(destroyed != book->events.end() && resumed != book->events.end() && destroyed < resumed);
    assert(book->activeBuilders == 0);
    assert(current.currentPage == 0);
    assertOneBuilder(*book);
  }
  {
    fake::reset();
    auto book = makeBook({{"current"}, {"next0", "next1", "next2", "next3", "next4"}});
    book->caches[1] = {};
    Section current(book, 0, renderer);
    assert(current.loadSectionFile(spec));
    assert(highlights::capture(book, current, 0, renderer, spec));
    assert(exported(".txt").find("next1") != std::string::npos);
    assert(book->caches[1].partial && book->caches[1].pages >= 3 && book->caches[1].pages <= 4);
    // The initial two-page build is suspended while scanning the current chapter.
    // Boundary lookahead resumes it once, then export releases the shared parser.
    assert(book->buildStarts[1] == 2 && book->suspensions[1] == 2);
    Section reopened(book, 1, renderer);
    assert(reopened.loadSectionFile(spec));
    assert(reopened.loadPage(1));
    assert(book->activeBuilders == 0);
    assertOneBuilder(*book);
  }
  {
    fake::reset();
    auto book = makeBook({{"before"}, {}, {}, {"current"}, {}, {"after"}});
    book->caches[1] = {};
    book->caches[2] = {};
    book->caches[4] = {};
    Section current(book, 3, renderer);
    assert(current.loadSectionFile(spec));
    assert(highlights::capture(book, current, 3, renderer, spec));
    assert(exported(".txt").find("before") != std::string::npos);
    assert(exported(".txt").find("after") != std::string::npos);
    assert((book->buildStarts == std::vector<int>{0, 1, 1, 0, 1, 0}));
    assertOneBuilder(*book);
  }
  {
    fake::reset();
    auto book = makeBook({{"p0", "p1", "p2", "p3"}});
    book->caches[0] = {1, true};
    book->failBuild = true;
    Section partial(book, 0, renderer);
    assert(partial.loadSectionFile(spec));
    assert(!highlights::capture(book, partial, 0, renderer, spec));
    noExports();
    assertOneBuilder(*book);
  }
  {
    fake::reset();
    auto book = makeBook({{"p0", "p1", "p2", "p3"}});
    book->caches[0] = {};
    Section current(book, 0, renderer);
    assert(current.startBuild(spec));
    assert(current.buildSomeMore(2));
    current.currentPage = 1;
    book->failBuildStep = true;
    assert(!highlights::capture(book, current, 0, renderer, spec));
    noExports();
    assert(current.currentPage == 1);
    assert(current.pageCount == 0 && book->caches[0].pages == -1);
    assert(book->activeBuilders == 0);
    assertOneBuilder(*book);
  }

  auto book = makeBook({{"p0", "p1", "p2", "p3"}});
  Section partial(book, 0, renderer);
  assert(partial.loadSectionFile(spec));

  for (int n = 0; n < 2; ++n) {
    fake::reset();
    fake::failRename = n;
    assert(!highlights::capture(book, partial, 0, renderer, spec));
    noExports();
  }
  fake::reset();
  fake::shortWrite = 1;
  assert(!highlights::capture(book, partial, 0, renderer, spec));
  noExports();
  fake::reset();
  book->failLoad = true;
  assert(!highlights::capture(book, partial, 0, renderer, spec));
  noExports();
  book->failLoad = false;
  fake::reset();
  book->chapters = {{std::string(16385, 'x')}};
  book->cacheChapters();
  assert(partial.loadSectionFile(spec));
  assert(!highlights::capture(book, partial, 0, renderer, spec));
  noExports();
  paragraphCaptureTests(renderer, spec);
  std::cout
      << "Highlight capture: page windows, whole paragraphs, cache lifecycle, exclusive builders, failures passed\n";
}
