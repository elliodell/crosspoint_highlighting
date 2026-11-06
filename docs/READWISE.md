# Automatic Readwise highlight uploads

This fork sends saved captures to **Readwise Highlights → Browse → Library**, grouped by book title and author. New passages become real highlights; overlapping captures send only their new source paragraphs. Saving locally does not turn on Wi-Fi; uploads use an existing connection. Your `.json` and `.txt` originals remain in `/Highlights` on the SD card.

## Long captures and identifying parts

Readwise accepts at most 8191 characters per highlight. The firmware conservatively limits each part to **8191 UTF-8 bytes**, ending at a paragraph or whitespace boundary where possible, otherwise at a complete Unicode character. Non-Latin text can therefore split before 8191 characters. New captures preserve the source paragraphs: screen page boundaries disappear, real paragraphs use a blank line, and explicit line breaks inside a paragraph remain. New text is sent in reading order without truncation.

Every highlight has a **note**, separate from its passage, with this format:

```text
crosspoint:0123456789abcdef0123456789abcdef 2/3
merge:0123456789abcdef0123456789abcdef
Chapter title — page 15 (selected page)
#crosspoint-highlight
```

The 32-character ID belongs to one capture, not to the book. A different capture from the same book gets a different ID. `2/3` identifies this particular part and gives the total number of parts. A single uploaded piece uses `1/1`. To combine parts of that upload, match the complete capture ID and put them in part-number order. Parts contain only that capture's new text; use the original local export to reconstruct its complete window. IDs and part labels are never inserted into the highlight text.

## Overlapping captures

For example, if one capture contains paragraphs A–C and the next contains B–E, the first uploads A–C and the second uploads only D–E. A fully covered capture makes no new highlight. New text on opposite sides of an already saved passage becomes separate numbered pieces, so omitted text does not falsely join them.

Every note also has a `merge:<id>` line. An extending capture inherits the earlier group's ID. A capture bridging two groups lists both IDs on its new pieces. Match these lines to find related highlights. Older notes are not edited, and a fully covered bridge has no new remote note; this is a grouping aid, not a remote merged-document editor.

Matching requires identical complete source paragraphs in the same chapter and book title/author, using acknowledged captures retained on the SD card. Different pagination can still match. Small textual differences are retained. Changes to the book's chapter structure may prevent matching. The reader does not scan your existing Readwise library for unrelated imports.

Readwise also deduplicates identical title, author, passage text, and source URL. This can merge repeated captures or identical split parts and replace their notes. Direct sync cannot guarantee that all capture/part IDs survive those exact-text duplicates. Local exports preserve every full capture. Distinct text is split into distinct pieces.

Page numbers are local to the captured chapter and depend on the reader's layout; they are not printed-book page numbers. The note retains the selected chapter/page. Readwise book titles are limited here to 511 UTF-8 bytes and authors to 1024, ending at complete characters; full metadata remains in the local export.

New uploads from version `2.0.0-highlights.4` omit the source hyperlink, so the page number no longer opens a broken address. Capture/part and merge IDs remain in notes. The reader cannot open a local EPUB page from Readwise.

## One-time setup

1. Sign in to [Readwise and copy your access token](https://readwise.io/access_token). Keep the token private.
2. Connect the reader by USB and open **USB Drive** on the device, or put its SD card into your computer.
3. Show hidden files, open `.crosspoint`, and create a `readwise` folder inside it. On macOS, Command–Shift–Period toggles hidden files.
4. Create a plain text file named `config.json` in that folder:

   ```json
   {
     "enabled": true,
     "token": "PASTE_YOUR_READWISE_ACCESS_TOKEN_HERE"
   }
   ```

   Replace the placeholder. The full SD-card path must be `/.crosspoint/readwise/config.json`, with no `.txt` suffix. Use straight quotation marks. Do not put this file in a public repository or paste the token into chat.
5. Safely eject the drive and leave USB Drive mode. The reader restarts.
6. Save a capture, then use **File transfer → Join network** to join internet-enabled Wi-Fi. Leave that screen open while pending captures upload. Networks requiring a browser sign-in will not work.
7. Open [Readwise's library](https://readwise.io/books) and find the book title. Its highlights contain the passages and their capture/part notes.

Secure connections require a valid device clock. Joining Wi-Fi uses CrossPoint's normal time synchronization. If the clock was reset, synchronize it and reconnect.

## Updating from Reader-document sync

Version `2.0.0-highlights.3` switches to the Readwise Highlights API. It does not create new Reader Inbox documents. The token remains compatible. New captures contain paragraph metadata; old exports are not rewritten.

Legacy Reader receipts in `/.crosspoint/readwise/sent` are preserved but do not count as delivery to Highlights. Existing captures are therefore uploaded to Highlights after the update. New receipts live under `/.crosspoint/readwise/highlights-v2/<account-hash>/`. Each `<capture-id>.request` contains a checksummed header and frozen request. A separate `<capture-id>.ack` stores the header checksum after success. The folder name is a hash of the configured token, not the token itself. Earlier Reader documents are retained; remove them in Reader yourself if desired.

Old captures lack reliable paragraph-boundary metadata. They are uploaded conservatively with their existing page breaks and do not participate in paragraph overlap matching. Recapture the passage with this version for automatic paragraph formatting, or repair an old export against its original EPUB before importing it. The firmware does not guess where old paragraphs began.

## Retries and privacy

- Only complete `.json` captures are uploaded. Temporary files and `.txt` exports are ignored.
- One capture's parts are sent together in one request. A complete HTTP 200 with a valid book acknowledgement is required before a separate matching acknowledgement is saved. Requests are never renamed or rewritten after publication; interrupted acknowledgements can retry without creating shared SD file entries. A fully covered capture records verified prior coverage without a network request. A duplicate response can contain no modified highlight IDs and still acknowledge the request.
- Retries send the exact frozen payload. Readwise's title/author/text/source deduplication handles repeat delivery; a `highlight_url` is not required. Capture/part IDs remain in notes and local receipts.
- Uploads run at existing network idle points. Success waits at least three seconds; failures back off from 30 seconds to 15 minutes. Numeric server `Retry-After` is respected up to 24 hours. Authentication failures wait at least 15 minutes; restarting clears the in-memory timer.
- Network, response, memory, and SD-write failures retain the local capture for retry. An empty or oversized acknowledgement is not marked sent. Keep both the original JSON captures and receipt folders: overlap checks need them. Missing or modified source files, corrupt receipts, or corrupt pending requests defer the affected sync instead of discarding uncertain text; keep a backup before repairing them. A different token gets its own receipt folder and re-queues captures automatically, including when rotating a token for the same account. Readwise's own deduplication still applies.
- No upload worker remains active while reading or in USB Drive mode. Plugin enablement is not required for Readwise sync.
- The token is plain text on the SD card. The firmware blocks its directory from web file management and plugin endpoints, but physical access to the card/USB Drive allows reading it.
- Requests use verified HTTPS to `readwise.io`; credentials are never forwarded through redirects. Logs omit tokens, passages, and server response bodies.
- Disable uploads by setting `"enabled": false` or removing `config.json`. Local capture and export continue working.

### Existing links from highlights.3

The update does not edit highlights already in your Readwise account. An upload prepared by highlights.3 also retries its original frozen request, including its old `.invalid` link; changing a request mid-retry could change delivery behavior. These old links do not open book pages. They can be cleared separately through the Readwise API by updating only the highlight's `url` field to an empty string. Keep the text and note intact, and retain the local requests and acknowledgements. Newly prepared uploads omit the link.

## Maintainer notes

The implementation uses the official [Readwise Highlights API](https://readwise.io/api_deets), `POST /api/v2/highlights/`. All parts share title, author, `category: books`, and `source_type: crosspoint_highlighting`; a capture-specific source URL is deliberately omitted so captures group under the book.

The existing ISRG Root X1 certificate and checked SDK hostname-verification patch are retained. Revalidate the certificate chain and patch when updating dependencies; there is no insecure fallback.

`HighlightCapture` writes `text_format: 2`. Each page has `text` plus a `joiner` before its first visible word: empty, a space, LF, or Unicode U+2029. U+2029 inside text also denotes a source paragraph; LF preserves an explicit in-paragraph break. Empty pages do not reset flow. TXT exports retain their page labels.

`ReadwisePlan` owns paragraph reconstruction, overlap matching and durable requests. `ReadwiseSync` owns configuration, network scheduling and HTTPS. Planning holds at most the current capture and one prior capture. Each has a source buffer up to 96 KiB, zero-copy JSON metadata, a canonical text buffer no larger than its source, and 1024 paragraph spans (16 KiB on ESP32). Individual source metadata strings and request headers are capped at 4 KiB; there are at most 32 merge groups, 128 upload parts and a 30-second budget for each pending/prior-capture scan. Exceeding a bound defers the capture without uploading a partial plan. Lookup cost grows with retained captures; there is no persistent paragraph index.

The exactly sized escaped request is capped at 580 KiB. The configuration buffer is 1 KiB and response buffer 16 KiB. All capture/planning buffers are released before TLS starts. A pending request is persisted before the first POST and takes priority until acknowledged, so retries after a restart cannot change text, groups, or part numbering. Each acknowledged capture retains its request and separate acknowledgement on SD; allow additional card space comparable to the uploaded text and metadata. The response parser retains only book IDs. Heap/largest-block guards defer uploads under memory pressure; peak internal RAM, PSRAM, and power use require device measurements. No background task or framebuffer is added.

## Verify on the device

1. Upload a short actual capture and confirm a highlight under its book in Readwise's library. Compare the full text, including outer paragraphs. Confirm the capture ID is in its note, not its passage.
2. Upload a long capture and another overlapping capture from the same book. Confirm the long capture's parts contain every new byte. The second uploads only new paragraphs, with its own capture ID and the shared merge ID. Check new text on both sides and a fully covered capture. Confirm all original text remains in the local exports.
3. Reconnect/restart and confirm no duplicate. Interrupt an upload, reconnect, and check eventual delivery with local originals retained.
4. Update a device containing legacy Reader receipts and confirm those captures also reach Highlights.
5. Check non-Latin characters and splits near paragraph/word boundaries. Test missing/invalid credentials and incomplete responses without deleting captures.
6. Check USB Drive transitions and repeated Wi-Fi connections for responsiveness and memory stability.

Host tests and a successful build do not establish device delivery, memory stability, or power behavior.
