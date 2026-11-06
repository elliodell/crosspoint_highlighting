# CrossPoint Highlighting — Xteink X4 Pro

A personal, public branch of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), based on the stable **2.0.0** release, with offline five-page captures extended to whole paragraphs, automatic Readwise Reader uploads, and the Almanac reading-time plugin.

**Experimental firmware for the X4 Pro.** Automated checks and a successful build do not replace testing on the device. Other CrossPoint devices remain in the upstream source, but this fork's power capture is enabled only on the X4 Pro.

## Download and install

Start with **[the X4 Pro installation guide](docs/INSTALL.md)**. Download the firmware and SD-card plugin bundle from [Releases](https://github.com/elliodell/crosspoint_highlighting/releases).

## Save a highlight

While a text page is open, **tap the power button**. The device saves that page, the two preceding pages, and the two following pages, in reading order. It crosses chapter boundaries and uses your current font, size, margins, and orientation. At the start/end of the book it saves only the pages that exist. Your reading position stays put.

If the first page starts partway through a paragraph, the capture extends backward to that paragraph's beginning. If the last page ends partway through a paragraph, it extends forward to that paragraph's end. The original five pages remain complete; extra pages include only the text needed to finish those outer paragraphs. A chapter boundary ends a paragraph, so paragraphs are never joined across chapters.

- A confirmation appears after the files are saved. A first capture near an uncached chapter can take longer because the reader must lay out nearby pages.
- Hold power for about half a second to sleep. The button that wakes the device does not also save a capture.
- With the X4 Pro frontlight double-click option enabled, a single capture waits half a second; double-click still toggles the light. Other input during that wait cancels the capture.
- Power retains the normal menu controls outside the reading page. Existing touch selection and ordinary CrossPoint clippings remain available separately.
- Images have no extracted text; this feature does not perform OCR. EPUB and CrossPoint's text-based readers use rendered text. Image-only XTC/XTCH books do not support power capture.

## Export and automatic upload

Each capture creates matching `.txt` and `.json` files in **`/Highlights` on the SD card**. The text file labels the captured page and each surrounding chapter/page, including partial outer pages. Copy these files via USB Drive, an SD-card reader, or CrossPoint's Wi-Fi file transfer page. Ordinary selection clippings still use `My Clippings.txt`; power captures use `/Highlights` so the excerpt is not subject to the ordinary clipping length limit.

**[Set up Readwise Reader sync](docs/READWISE.md)** with your own access token. When the reader joins internet-connected Wi-Fi, it attempts a pending upload automatically. Keep File transfer → Join network open to drain a backlog. Each capture becomes an excerpt document in Reader's Inbox tagged `crosspoint-highlight`, containing all saved pages. It is not a native highlight attached to a separately imported full book. Uploads never remove your local exports, and retrying the same capture reuses its unique URL.

## Reading time

**[Set up the bundled Almanac plugin](docs/ALMANAC.md)**. It uses CrossPoint's existing reading-session measurements to show reading time, streaks, and book progress. Almanac is optional and uses its own online service after sign-in. Highlight saving works without it.

## Boundaries and verification

Captures are bounded to 64 rendered pages, 16 KiB of extracted text per page, 96 KiB per JSON record, and a 30-second pagination/scan budget. If a complete paragraph cannot fit within those limits, an SD operation fails, or its boundaries cannot be determined reliably, the capture reports failure rather than silently cutting it off. The already-open book and earlier captures remain intact. Page numbers are chapter-local and depend on the selected layout.

See [verification and device test steps](docs/VERIFY.md). Readwise account delivery and physical button/display behavior must be checked on your X4 Pro; no device or account has been used to claim those checks passed.

## Build and maintain

The feature branch is `feat/power-highlights`; `main` contains the same released customization. Build with the pinned pioarduino toolchain and `pio run -e x4pro` as described in [INSTALL.md](docs/INSTALL.md). The GitHub workflow builds an X4 Pro artifact on pushes. Built-in update checks use this repository, so they will not silently replace your customization with upstream firmware.

Upstream source history, the FreeInk SDK submodule, and original licenses are retained. Almanac v0.4.0 is bundled unchanged with its MIT license and [source provenance](sd-card/plugins/almanac/SOURCE.md). The Readwise client applies a checked build-time SDK patch for authenticated HTTPS; maintainers must revalidate that patch and the pinned root certificate when updating dependencies.

[Original CrossPoint README](UPSTREAM_README.md) · [Upstream user guide](USER_GUIDE.md) · [License](LICENSE)
