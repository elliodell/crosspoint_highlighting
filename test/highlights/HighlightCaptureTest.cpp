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
    assert(book->caches[1].partial && book->caches[1].pages == 2);
    assert(book->buildStarts[1] == 1 && book->suspensions[1] == 1);
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
  std::cout
      << "Highlight capture: page windows, cache lifecycle, exclusive builders, empty chapters, failures passed\n";
}
