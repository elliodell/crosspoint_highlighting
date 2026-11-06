"""Exercise the production reader capture queue with delayed radio and storage failures."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
header = (root / 'src/activities/reader/EpubReaderActivity.h').read_text()
manager_source = (root / 'src/activities/ActivityManager.cpp').read_text()


def block_at(marker, document=source):
    """Extract one complete production function/block, preserving its return paths."""
    start = document.index(marker)
    opening = document.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (document[end] == '{') - (document[end] == '}')
        end += 1
    return document[start:end]


supports = block_at('bool EpubReaderActivity::supportsPowerHighlight() const')
queue = block_at('void EpubReaderActivity::capturePowerHighlight()')
pending = block_at('if (powerHighlightPending) {')
input_over_page = block_at('bool inputOverPage() const override', header)
remote_methods = '\n'.join(block_at(marker, manager_source) for marker in (
    'bool ActivityManager::remoteClick(',
    'bool ActivityManager::remoteShortcut(',
    'bool ActivityManager::remoteTurn(',
))
wait_constant = re.search(r'static constexpr uint32_t BUILD_ROOM_WAIT_MS = \d+;', header).group()

harness = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include "activities/reader/ChapterBuildGate.h"
#include "activities/reader/RemoteTurnGate.h"

unsigned long now = 100;
unsigned long millis() { return now; }
bool lockAvailable = true;
int lockDepth = 0;
struct RenderLock {
  enum class Mode { Try };
  bool owned;
  RenderLock() : owned(true) { ++lockDepth; }
  explicit RenderLock(Mode) : owned(lockAvailable) { if (owned) ++lockDepth; }
  ~RenderLock() { if (owned) --lockDepth; }
  bool ownsLock() const { return owned; }
  static bool peek() { return lockDepth > 0 || !lockAvailable; }
};
struct BoardConfig { static bool isX4Pro() { return true; } };
enum class StrId { STR_INDEXING, STR_CLIPPING_SAVED, STR_CLIPPING_FAILED };
#define tr(id) StrId::id
#define LOG_ERR(...) do {} while (false)
struct Renderer {};
struct ReaderRenderSpec { int width; int height; };
struct Settings {
  ReaderRenderSpec readerRenderSpec(int width, int height) { return {width, height}; }
} SETTINGS;
struct Gui {
  int popups = 0;
  void drawPopup(Renderer&, StrId text) {
    assert(lockDepth == 1 && text == StrId::STR_INDEXING);
    ++popups;
  }
} GUI;
struct Epub {};
struct Section {
  int currentPage = 7;
  int pageCount = 42;
  std::optional<uint32_t> offset = 12345;
  std::optional<uint32_t> getVisibleTextOffsetForPage(int page) const {
    assert(page == currentPage);
    return offset;
  }
};
namespace bleturner {
Status radio{};
BuildRelease releaseResult = BuildRelease::NotHeld;
unsigned long releaseDelayMs = 0;
int releaseCalls = 0;
Status status() { return radio; }
BuildRelease beforeChapterBuild() {
  assert(lockDepth == 1);
  ++releaseCalls;
  now += releaseDelayMs;
  return releaseResult;
}
}
namespace highlights {
int calls = 0;
bool success = true;
bool capture(const std::shared_ptr<Epub>& epub, Section& section, int spine,
             Renderer&, ReaderRenderSpec spec) {
  assert(lockDepth == 1 && epub && spine == 3);
  assert(section.currentPage == 7 && section.pageCount == 42);
  assert(spec.width == 600 && spec.height == 800);
  ++calls;
  if (!success) {
    // A failed Section build can destroy its previous cache and position.
    section.currentPage = 0;
    section.pageCount = 0;
    section.offset.reset();
  }
  return success;
}
}
struct ReaderActivity {
  std::atomic<bool> pageRendered{true};
  bool overPage = false;
  virtual ~ReaderActivity() = default;
  virtual bool inputOverPage() const { return overPage; }
  virtual bool pageTurn(bool forward) = 0;
  virtual bool skipPages(int amount) = 0;
  virtual bool remoteShortcut(bool readerMenu) = 0;
};
enum class RemoteButton { Next, Previous };
struct MappedInput {
  bool pending = false;
  bool hasRemoteClick() const { return pending; }
  bool injectRemoteClick(RemoteButton) { pending = true; return true; }
};
struct ActivityManager {
  std::shared_ptr<ReaderActivity> currentActivity;
  MappedInput mappedInput;
  std::atomic<bool> requestedUpdate{false};
  uint32_t screenVisit = 1;
  uint32_t visit() const { return screenVisit; }
  bool isForegroundReader() const { return currentActivity != nullptr; }
  bool isRemoteReaderContext() const { return currentActivity != nullptr; }
  void requestUpdate() { requestedUpdate = true; }
  bool remoteClick(RemoteButton button);
  bool remoteTurn(bool forward, bool chapter);
  bool remoteShortcut(bool readerMenu);
} activityManager;
struct EpubReaderActivity : ReaderActivity {
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  std::atomic<bool> radioReleasedForBuild{false};
  bool powerHighlightPending = false;
  unsigned long powerHighlightStarted = 0;
  uint32_t powerHighlightVisit = 0;
  bool automaticPageTurnActive = true;
  int pendingManualTurn = 1;
  int currentSpineIndex = 3;
  int buildViewportWidth = 600;
  int buildViewportHeight = 800;
  enum class Overlay { None, Toolbar };
  Overlay overlay = Overlay::None;
  Renderer renderer;
  int cachedSpineIndex = -1;
  int cachedChapterTotalPageCount = -1;
  std::optional<uint32_t> cachedVisibleTextOffset;
  int nextPageNumber = -1;
  StrId clippingMessage = StrId::STR_CLIPPING_SAVED;
  bool showClippingMessage = false;
  unsigned long clippingMessageTime = 0;
  int updates = 0;
  int normalIterations = 0;
  int pageTurns = 0;
  int chapterTurns = 0;
  int shortcuts = 0;
  bool pageTurn(bool forward) override {
    ++pageTurns;
    section->currentPage += forward ? 1 : -1;
    return true;
  }
  bool skipPages(int amount) override {
    ++chapterTurns;
    currentSpineIndex += amount;
    return true;
  }
  bool remoteShortcut(bool) override { ++shortcuts; overPage = true; return true; }
  void requestUpdate() { ++updates; }
  bool supportsPowerHighlight() const;
  void capturePowerHighlight();
  void loop();
'''
harness += input_over_page + '\n' + wait_constant + '\n};\n' + supports + '\n' + queue
harness += '\n' + remote_methods
harness += '\nvoid EpubReaderActivity::loop() {\n' + pending + '\n++normalIterations;\n}\n'
harness += r'''
void reset() {
  assert(lockDepth == 0);
  now = 100;
  lockAvailable = true;
  GUI.popups = 0;
  highlights::calls = 0;
  highlights::success = true;
  bleturner::radio = {};
  bleturner::releaseResult = bleturner::BuildRelease::NotHeld;
  bleturner::releaseDelayMs = 0;
  bleturner::releaseCalls = 0;
  activityManager.currentActivity.reset();
  activityManager.mappedInput = {};
  activityManager.requestedUpdate = false;
  activityManager.screenVisit = 1;
}
int main() {
  // The first stop can consume three seconds, but the pending block then returns
  // so a later main-loop radio tick can finish teardown before capture starts.
  reset();
  {
    EpubReaderActivity reader;
    bleturner::radio.stopping = true;
    bleturner::releaseResult = bleturner::BuildRelease::StillUp;
    bleturner::releaseDelayMs = 3000;
    reader.capturePowerHighlight();
    assert(reader.powerHighlightPending && !reader.automaticPageTurnActive);
    assert(reader.pendingManualTurn == 0 && GUI.popups == 1);
    assert(!reader.supportsPowerHighlight());
    now = 200;
    reader.capturePowerHighlight();
    assert(reader.powerHighlightStarted == 100 && GUI.popups == 1);
    reader.loop();
    assert(now == 3200 && reader.powerHighlightPending && reader.radioReleasedForBuild.load());
    assert(highlights::calls == 0 && reader.normalIterations == 0 && reader.updates == 0);
    assert(bleturner::releaseCalls == 1 && lockDepth == 0);
    now = 4100;
    reader.loop();
    assert(reader.powerHighlightPending && bleturner::releaseCalls == 1);
    // This is the state change made by the next independent Bluetooth tick.
    now = 4200;
    bleturner::radio.stopping = false;
    reader.loop();
    assert(!reader.powerHighlightPending && highlights::calls == 1);
    assert(reader.section && reader.section->currentPage == 7 && reader.section->pageCount == 42);
    assert(reader.showClippingMessage && reader.clippingMessage == StrId::STR_CLIPPING_SAVED);
    assert(reader.clippingMessageTime == now && reader.updates == 1);
    reader.loop();
    assert(highlights::calls == 1 && reader.normalIterations == 1);
  }
  // A radio that never releases produces a failure without touching the book.
  reset();
  {
    EpubReaderActivity reader;
    bleturner::radio.stopping = true;
    bleturner::releaseResult = bleturner::BuildRelease::StillUp;
    reader.capturePowerHighlight();
    now += EpubReaderActivity::BUILD_ROOM_WAIT_MS + 2999;
    reader.loop();
    assert(reader.powerHighlightPending && highlights::calls == 0);
    ++now;
    reader.loop();
    assert(!reader.powerHighlightPending && highlights::calls == 0);
    assert(reader.showClippingMessage && reader.clippingMessage == StrId::STR_CLIPPING_FAILED);
    assert(reader.updates == 1 && reader.section && reader.section->currentPage == 7);
  }
  // Failure after capture has invalidated the Section restores the old bookmark
  // fields and discards the broken object so normal rendering can reopen it.
  reset();
  {
    EpubReaderActivity reader;
    highlights::success = false;
    reader.capturePowerHighlight();
    reader.loop();
    assert(highlights::calls == 1 && !reader.powerHighlightPending && !reader.section);
    assert(reader.cachedSpineIndex == 3 && reader.cachedChapterTotalPageCount == 42);
    assert(reader.cachedVisibleTextOffset == 12345 && reader.nextPageNumber == 7);
    assert(reader.showClippingMessage && reader.clippingMessage == StrId::STR_CLIPPING_FAILED);
    assert(reader.updates == 1 && reader.clippingMessageTime == now);
  }
  // If the render task owns the lock, neither radio state nor capture is touched.
  reset();
  {
    EpubReaderActivity reader;
    reader.capturePowerHighlight();
    lockAvailable = false;
    reader.loop();
    assert(reader.powerHighlightPending && bleturner::releaseCalls == 0);
    assert(highlights::calls == 0 && reader.updates == 0 && reader.normalIterations == 0);
    lockAvailable = true;
    reader.loop();
    assert(!reader.powerHighlightPending && highlights::calls == 1 && reader.updates == 1);
  }
  // A start still in flight must settle before it can be asked to release heap.
  reset();
  {
    EpubReaderActivity reader;
    bleturner::radio.starting = true;
    reader.capturePowerHighlight();
    reader.loop();
    assert(reader.powerHighlightPending && bleturner::releaseCalls == 0 && highlights::calls == 0);
    bleturner::radio.starting = false;
    reader.loop();
    assert(!reader.powerHighlightPending && highlights::calls == 1);
  }
  // A remote dispatch runs before the reader loop. While capture is queued,
  // the actual manager must not route direct turns or shortcuts into the book.
  reset();
  {
    auto reader = std::make_shared<EpubReaderActivity>();
    activityManager.currentActivity = reader;
    reader->capturePowerHighlight();
    assert(reader->inputOverPage() && reader->powerHighlightVisit == activityManager.visit());
    assert(!activityManager.remoteShortcut(true));
    assert(!activityManager.remoteShortcut(false));
    assert(!activityManager.remoteTurn(true, true));
    assert(!activityManager.remoteTurn(false, true));
    // Ordinary remote keys may be buffered for the active input owner, but
    // must not turn the page before this pending capture has run.
    activityManager.remoteTurn(true, false);
    activityManager.remoteTurn(false, false);
    assert(reader->pageTurns == 0 && reader->chapterTurns == 0 && reader->shortcuts == 0);
    assert(reader->section->currentPage == 7 && reader->currentSpineIndex == 3);
    assert(!activityManager.requestedUpdate.load());
    reader->loop();
    assert(highlights::calls == 1 && !reader->powerHighlightPending);
    assert(reader->section->currentPage == 7 && reader->currentSpineIndex == 3);
    assert(reader->clippingMessage == StrId::STR_CLIPPING_SAVED);
  }
  // Leave and return to the reader before its pending block: the new visit
  // cancels the old capture even when the same reader object is retained.
  reset();
  {
    EpubReaderActivity reader;
    reader.capturePowerHighlight();
    const auto queuedVisit = reader.powerHighlightVisit;
    ++activityManager.screenVisit;
    reader.loop();
    assert(queuedVisit != activityManager.visit());
    assert(!reader.powerHighlightPending && highlights::calls == 0 && bleturner::releaseCalls == 0);
    assert(reader.updates == 1 && !reader.showClippingMessage && reader.normalIterations == 0);
    assert(reader.section && reader.section->currentPage == 7 && reader.currentSpineIndex == 3);
  }
  assert(lockDepth == 0);
  puts("Pending power highlight checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix='crosspoint-pending-highlight-') as directory:
    cpp = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    cpp.write_text(harness)
    subprocess.run([
        'c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
        '-I' + str(root / 'src'),
        '-I' + str(root / 'lib/BlePageTurner/include'),
        '-I' + str(root / 'freeink-sdk/libs/network/BleKeyboardHost/include'),
        str(cpp), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
