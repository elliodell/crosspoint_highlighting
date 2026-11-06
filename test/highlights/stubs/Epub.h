#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
struct ReaderRenderSpec {};
class TextBlock {
 public:
  struct SourceRange {
    uint32_t start = UINT32_MAX, end = UINT32_MAX;
  };
  struct Word {
    std::string text;
    SourceRange range;
    bool hyphen = false;
    bool paragraph = false;
  };
  std::vector<Word> words;
  bool valid() const { return true; }
  uint16_t wordCount() const { return words.size(); }
  const char* wordText(int i) const { return words[i].text.c_str(); }
  SourceRange wordSourceRange(int i) const { return words[i].range; }
  bool wordHasDiscretionaryHyphen(int i) const { return words[i].hyphen; }
  bool wordStartsParagraph(int i) const { return words[i].paragraph; }
};
enum { TAG_PageLine, TAG_PageDropCap };
struct PageElement {
  virtual ~PageElement() = default;
  virtual int getTag() const = 0;
};
struct PageLine : PageElement {
  TextBlock block;
  int getTag() const override { return TAG_PageLine; }
  const TextBlock* getBlock() const { return &block; }
};
struct PageDropCap : PageElement {
  std::string text;
  int getTag() const override { return TAG_PageDropCap; }
  const char* getText() const { return text.c_str(); }
};
struct Page {
  std::vector<std::unique_ptr<PageElement>> elements;
};
class Epub {
 public:
  struct Toc {
    std::string title;
  };
  struct Cache {
    int pages = -1;
    bool partial = false;
  };
  std::vector<std::vector<std::string>> chapters;
  std::vector<Cache> caches;
  std::vector<int> buildStarts, suspensions;
  std::vector<std::string> events;
  const void* cssOwner = nullptr;
  int activeBuilders = 0, maxActiveBuilders = 0, cssViolations = 0;
  bool failBuild = false, failBuildStep = false, failLoad = false;
  void cacheChapters() {
    caches.clear();
    for (const auto& chapter : chapters) caches.push_back({static_cast<int>(chapter.size()), false});
    buildStarts.assign(chapters.size(), 0);
    suspensions.assign(chapters.size(), 0);
  }
  std::string getTitle() const { return "A \"book\""; }
  std::string getAuthor() const { return "Author"; }
  std::string getPath() const { return "/books/test.epub"; }
  int getSpineItemsCount() const { return chapters.size(); }
  int getTocIndexForSpineIndex(int i) const { return i; }
  Toc getTocItem(int i) const { return {"Chapter " + std::to_string(i + 1)}; }
};
