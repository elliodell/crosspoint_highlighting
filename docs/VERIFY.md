## highlights.6 reading-clock correction

New X4 Pro reading sessions use a fresh RTC UTC reading. The loan clock,
existing saved sessions and network/upload protocol are unchanged. The change
adds one RTC register read per completed page and no heap allocation or task.
A failed or unset RTC is not replaced with the loan clock. Boards without RTC
retain their previous fallback.

The production HAL/reading-clock host test passes, including a failed RTC boot
probe, unreadable/unset RTC, time zones, and a future loan-clock floor. Existing
loan-clock and Books/transport checks pass. Four independent review axes and
their targeted follow-ups are complete. Formatting and the final X4 Pro build
pass without compiler warnings/errors: static RAM 111,804 / 327,680 bytes and
flash 5,483,250 / 6,553,600 bytes. The application image is 5,488,512 bytes,
SHA-256 `9c3e818c975207df1f3d97e047a39d8dc9660ee1431552f3e4bc6fc09ddc0d0d`.

X4 Pro installation and direct Wi-Fi delivery are verified for .6. A new
session's book, UTC interval, active duration, progress and device fields matched
Readwise's API record. The saved receipt matched the exact request hash and API
session ID, and the acknowledged queue entry was removed. The user confirmed
the recorded duration was approximately correct. Reconnecting without further
reading retained the same API ID and exactly one matching session.
An older retained session also uploaded during sleep once its recorded end time
had passed; its API record matched the original frozen request without rewriting
the saved reading history.
The final SD check confirmed valid receipts for both sessions and an empty queue.

The Books app display, broader offline/restart recovery, heap/stack measurements
and battery impact remain unverified. Device test for another installation:
verify the .6 About version, read forward across at least ten active seconds,
connect Wi-Fi, then compare the new session and receipt with Readwise Books.
Repeat a connection without more reading to check for duplicate uploads.

## Readwise Books feature branch

Firmware 2.0.0-highlights.5 adds the optional Almanac session mirror. Host
conversion/fault tests, existing highlight regression tests, and setup tests
pass. Four review axes and targeted follow-ups are complete; the final X4 Pro
build passed without compiler warnings/errors (111,804 bytes static RAM and
5,483,058 bytes flash). The production HTTP retry-control test also passed
with AddressSanitizer/UndefinedBehaviorSanitizer. Installation was verified on
X4 Pro. The first Books session was retained on
SD but rejected for a future timestamp; the cause was the saved loan-clock
floor overriding the correct RTC. The .6 delivery checks are recorded above;
broader offline retry and heap/stack measurements remain pending. See
[the installation and recovery instructions](READWISE-BOOKS.md).

# Verification

## highlights.4 update checks

The Readwise host harness checks that newly prepared uploads omit `highlight_url` while retaining page numbers and capture/part notes, including long splits and separate captures. It also checks byte-for-byte retries, recovery of a pending highlights.3 request without rewriting it, and paragraph overlap against its acknowledgement.

On the X4 Pro, install `2.0.0-highlights.4-x4pro`, capture a new passage, and sync over Wi-Fi. In Readwise, verify that its page number has no broken source link, the passage and note remain correct, and reconnecting does not create another copy. Existing or previously prepared uploads may retain the old link; see [Readwise setup](READWISE.md#existing-links-from-highlights3). This release still needs that device check; the recorded hardware results below apply to highlights.3.

## Recorded X4 Pro checks

Version `2.0.0-highlights.3-x4pro` was installed through the SD-card updater and its displayed version verified.

- Library backup and preservation of books, settings and original TXT exports were verified.
- Fresh adjacent-page captures matched uninterrupted intervals of original EPUB paragraphs, including outer paragraph boundaries, a chapter boundary and an explicit line break in a heading.
- Direct device Wi-Fi uploads preserved all unique paragraphs without repeating overlapping text. Capture/part IDs and shared merge IDs appeared in notes only.
- Almanac 0.4.0 sign-in, stats display and reading-time updates were verified.
- Upload receipts were read back and their header, source, account, payload and acknowledgement hashes verified. Existing Readwise highlights were retained. Legacy migration checks used exports repaired against their source EPUB; fresh capture checks used the installed firmware.

These checks cover the tested passages and layout. They do not establish every EPUB structure or orientation, long split uploads on hardware, physical power-loss recovery, restart/reconnect retries, memory stability, battery behavior, or offline Almanac delivery. Those checks remain outstanding. Private passages, credentials and device backups are excluded from this repository.

## Host checks

Run from the repository root:

```sh
sh test/highlights/run.sh
sh test/readwise_sync/run.sh
python3 test/test_power_highlight.py
python3 test/test_pending_highlight.py
python3 test/test_clipping_selection_text.py
python3 test/test_clipping_highlight_planes.py
cmake -S test -B build/host-tests
cmake --build build/host-tests --target ChapterHtmlSlimParserTest
build/host-tests/chapter_html_slim_parser/ChapterHtmlSlimParserTest
PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix -g
pio run -e x4pro
```

On non-macOS hosts the Readwise harness also requires OpenSSL development headers and libcrypto.

The capture harness compiles the actual capture implementation with fake page layouts and storage. It checks whole-paragraph expansion and trimming at both ends, drop caps, right-to-left paragraph markers, blank pages, chapter boundaries, and the 64-page and text limits. It also checks multi-chapter windows (including empty chapters), partial pagination, exclusive chapter-builder ownership, reuse of completed caches, export labels, escaping, unchanged position, and write/rename failures. The actual EPUB parser harness checks paragraph markers through hard line breaks and cache serialization. The pending-capture harness checks delayed Bluetooth shutdown and reading-position recovery after failure. The Readwise harness checks that captures with 6 and 64 pages upload every page in order and that 65-page records are rejected. It also checks page joiners, source paragraphs versus explicit line breaks, pagination-independent overlap, new text on either side, bridges between groups, account isolation, stable split retries after a restart, and corrupted pending files. These checks do not emulate the ESP32, power cuts, or physical button timing.

The CMake checks require CMake, a C++20 compiler, Python, and the Expat and zlib development libraries. Their first configuration downloads GoogleTest.

## X4 Pro checks

Use a small EPUB with easily identifiable pages. Keep a backup of the SD card.

1. Capture in the middle of a chapter: both files contain all of current−2 through current+2, in order, and the visible page does not change. If either outer edge splits a paragraph, only that paragraph's missing text is added. Check short paragraphs that need part of one extra page and long paragraphs spanning several extra pages on both sides. A paragraph already ending at the page edge must not pull in the next paragraph.
2. Capture at a chapter boundary and the first/last book page. Neighboring chapters are included in the original window; nonexistent pages are omitted. Outer paragraph extensions stop at chapter boundaries without joining the following chapter's first paragraph.
3. Change font size and orientation. Repeat in portrait, inverted portrait, landscape clockwise, and landscape counterclockwise. Exports must follow the new pagination.
4. Open a new, uncached long chapter with embedded CSS and capture immediately near its beginning. Check neighboring chapters and continue reading: formatting and pagination must stay consistent. Repeat with a Bluetooth page turner connected, including a slow disconnect. A reported failure must leave you at the original text and must not publish a partial JSON capture.
5. Check quotation marks, accented/non-Latin text, bidirectional text, paragraph boundaries, drop caps, discretionary/literal hyphens, and image-only pages. Images are not OCR'd. Compare the first and last exported words with the original paragraph boundaries. Complex column layouts with ambiguous source order must fail safely rather than silently cut a paragraph. Check a paragraph that exceeds the page, text, or time limit: failure must leave no new JSON record to upload.
6. Test power with frontlight double-click on and off, and with each short-power setting. Single press saves, double-click toggles light only when enabled, hold sleeps. Wake, screenshot, menu navigation, and a canceled delayed click must not save unintended captures. Send a Bluetooth turn immediately after tapping power: the capture must retain the tapped page. Open and close the control center while a capture is waiting: that pending capture must be canceled.
7. Save offline, restart, then join Wi-Fi. With Readwise configured, verify actual highlights under the book in Readwise Browse → Library. A short capture creates one highlight; a long capture creates ordered parts with the same capture ID and distinct part numbers in their notes. Check two overlapping captures keep only new paragraphs and link their notes with a shared merge ID; their upload capture IDs remain distinct. A fully covered capture creates no new highlight. Compare all text, including the outer paragraphs. Reconnect and interrupt an upload to check retries and duplicate prevention. A capture with an old Reader receipt must also upload to Highlights. Readwise setup includes further credential/error checks.
8. Enter/exit USB Drive with pending captures. Confirm the host owns storage exclusively and exports remain valid.
9. Check repeated captures and Wi-Fi/reader transitions for freezes or leaked memory. Record internal and PSRAM free space and largest blocks separately. Confirm battery behavior and frontlight remain normal.
10. Use paragraphs with HTML `<br>` breaks and preformatted newlines at both outer page edges. The capture must reach the real paragraph boundaries and preserve those internal line breaks. Repeat with TXT/Markdown books.
11. Before updating, save one book with completed page caches and another with a partially built long chapter. After updating, reopen both and capture: old caches must rebuild safely, reading position and settings must remain available, and existing exports must remain unchanged. Reopen again to check that the new caches are reused.

This repository has no automated physical-device test and contains no Readwise or Almanac account credentials. Build/host results do not establish live-account delivery or device memory behavior.
