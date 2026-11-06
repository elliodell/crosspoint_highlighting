# Verification

## Host checks

Run from the repository root:

```sh
sh test/highlights/run.sh
sh test/readwise_sync/run.sh
python3 test/test_power_highlight.py
python3 test/test_pending_highlight.py
python3 test/test_clipping_selection_text.py
python3 test/test_clipping_highlight_planes.py
PATH="$PWD/.venv/bin:$PATH" ./bin/clang-format-fix -g
pio run -e x4pro
```

The capture harness compiles the actual capture implementation with fake page layouts and storage. It checks book boundaries, multi-chapter windows (including empty chapters), partial pagination, exclusive chapter-builder ownership, reuse of completed caches, export labels, escaping, unchanged position, write/rename failures, and rejection of oversized pages. The pending-capture harness checks delayed Bluetooth shutdown and reading-position recovery after failure. These checks do not emulate the ESP32, actual EPUB layout, power cuts, or physical button timing.

## X4 Pro checks

Use a small EPUB with easily identifiable pages. Keep a backup of the SD card.

1. Capture in the middle of a chapter: both files contain exactly current−2 through current+2, in order, and the visible page does not change.
2. Capture at a chapter boundary and the first/last book page. Neighboring chapters are included; nonexistent pages are omitted.
3. Change font size and orientation. Repeat in portrait, inverted portrait, landscape clockwise, and landscape counterclockwise. Exports must follow the new pagination.
4. Open a new, uncached long chapter with embedded CSS and capture immediately near its beginning. Check neighboring chapters and continue reading: formatting and pagination must stay consistent. Repeat with a Bluetooth page turner connected, including a slow disconnect. A reported failure must leave you at the original text and must not publish a partial JSON capture.
5. Check quotation marks, accented/non-Latin text, bidirectional text, paragraph boundaries, drop caps, discretionary/literal hyphens, and image-only pages. Images are not OCR'd.
6. Test power with frontlight double-click on and off, and with each short-power setting. Single press saves, double-click toggles light only when enabled, hold sleeps. Wake, screenshot, menu navigation, and a canceled delayed click must not save unintended captures. Send a Bluetooth turn immediately after tapping power: the capture must retain the tapped page. Open and close the control center while a capture is waiting: that pending capture must be canceled.
7. Save offline, restart, then join Wi-Fi. With Readwise configured, verify one excerpt per capture; reconnect and interrupt an upload to check retries. Readwise setup includes further credential/error checks.
8. Enter/exit USB Drive with pending captures. Confirm the host owns storage exclusively and exports remain valid.
9. Check repeated captures and Wi-Fi/reader transitions for freezes or leaked memory. Record internal and PSRAM free space and largest blocks separately. Confirm battery behavior and frontlight remain normal.

This repository has no automated physical-device test and contains no Readwise or Almanac account credentials. Build/host results do not establish live-account delivery or device memory behavior.
