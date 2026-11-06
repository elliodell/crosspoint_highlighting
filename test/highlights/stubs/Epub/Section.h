#pragma once
#include <Epub.h>
#include <GfxRenderer.h>

#include <algorithm>

class Section {
  std::shared_ptr<Epub> epub;
  int spine;
  bool building = false, buildComplete = false, partial = false;
  int builtPages = 0, partialPages = 0;

  void releaseParser() {
    if (epub->cssOwner != this) ++epub->cssViolations;
    epub->cssOwner = nullptr;
    --epub->activeBuilders;
    building = false;
  }
  void abandonBuild() {
    releaseParser();
    buildComplete = false;
    partial = false;
    pageCount = builtPages = partialPages = 0;
    epub->caches[spine] = {};
  }

 public:
  int pageCount = 0, currentPage = 0;
  Section(const std::shared_ptr<Epub>& e, int s, GfxRenderer&) : epub(e), spine(s) {}
  ~Section() {
    suspendBuild();
    epub->events.push_back("destroy:" + std::to_string(spine));
  }
  bool loadSectionFile(const ReaderRenderSpec&) {
    const auto& cache = epub->caches[spine];
    if (cache.pages < 0) return false;
    pageCount = cache.pages;
    if (cache.partial) {
      partial = true;
      partialPages = pageCount;
    }
    // A loaded finalized cache leaves buildComplete false in the firmware.
    return true;
  }
  bool isBuildComplete() const { return buildComplete; }
  bool isBuilding() const { return building; }
  bool isPartial() const { return partial; }
  bool startBuild(const ReaderRenderSpec&) {
    if (building) return false;
    ++epub->buildStarts[spine];
    epub->events.push_back("start:" + std::to_string(spine));
    buildComplete = false;
    builtPages = 0;
    pageCount = partial ? partialPages : 0;
    if (epub->failBuild) return false;
    if (epub->cssOwner) ++epub->cssViolations;
    epub->cssOwner = this;
    building = true;
    ++epub->activeBuilders;
    epub->maxActiveBuilders = std::max(epub->maxActiveBuilders, epub->activeBuilders);
    return true;
  }
  bool buildSomeMore(int n) {
    if (!building) return false;
    const auto failAt = epub->failBuildAfterPages.find(spine);
    if (epub->failBuildStep || epub->cssOwner != this ||
        (failAt != epub->failBuildAfterPages.end() && builtPages >= failAt->second)) {
      abandonBuild();
      return false;
    }
    builtPages = std::min(builtPages + n, static_cast<int>(epub->chapters[spine].size()));
    pageCount = std::max(pageCount, builtPages);
    if (builtPages == static_cast<int>(epub->chapters[spine].size())) {
      epub->caches[spine] = {builtPages, false};
      partial = false;
      partialPages = 0;
      buildComplete = true;
      releaseParser();
    }
    return true;
  }
  void suspendBuild() {
    if (!building) return;
    ++epub->suspensions[spine];
    epub->events.push_back("suspend:" + std::to_string(spine));
    if (builtPages > 0 && (!partial || builtPages > partialPages)) {
      partial = true;
      partialPages = builtPages;
      epub->caches[spine] = {builtPages, true};
    }
    releaseParser();
    buildComplete = false;
    pageCount = partial ? partialPages : 0;
    builtPages = 0;
  }
  std::unique_ptr<Page> loadPage(int p) {
    const int onDisk = partial ? partialPages : (building ? 0 : pageCount);
    if (epub->failLoad || epub->failedLoads.contains({spine, p}) || p < 0 ||
        (p >= onDisk && (!building || p >= builtPages)))
      return nullptr;
    const auto factory = epub->pageFactories.find({spine, p});
    if (factory != epub->pageFactories.end()) return factory->second();
    auto page = std::make_unique<Page>();
    auto line = std::make_unique<PageLine>();
    line->block.words.push_back({epub->chapters[spine][p], {0, 4}, false, true});
    page->elements.push_back(std::move(line));
    return page;
  }
};
