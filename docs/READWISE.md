# Automatic Readwise Reader uploads

This fork uploads each saved five-page highlight as a separate **excerpt document in your Reader Inbox**, tagged `crosspoint-highlight`. It includes the current page and up to two pages on each side, the book title and author, and a label on the selected page. At the beginning or end of a book there can be fewer pages. These are searchable Reader documents, not native yellow highlights attached to an existing Reader copy of the whole book.

Saving a highlight always writes the local export first. Uploads use an existing Wi-Fi connection; saving a highlight does not turn on the radio. Your originals remain in `/Highlights` on the SD card after upload.

## One-time setup

1. Sign in to [Readwise and copy your access token](https://readwise.io/access_token). Use an account with Reader access. Keep the token private.
2. Connect the reader by USB and open **USB Drive** on the device, or put its SD card into your computer.
3. Show hidden files, open the `.crosspoint` folder, and create a `readwise` folder inside it. On macOS, Command–Shift–Period toggles hidden files.
4. Create a plain text file named `config.json` in that folder with this content, replacing only the placeholder:

   ```json
   {
     "enabled": true,
     "token": "PASTE_YOUR_READWISE_ACCESS_TOKEN_HERE"
   }
   ```

   The full path on the SD card must be `/.crosspoint/readwise/config.json`, with no extra `.txt` suffix. Use normal straight quotation marks. Do not put the file in this public repository or paste the token into an issue or chat.
5. Safely eject the drive and leave USB Drive mode. Restart if requested by the device.
6. Save a highlight while reading, then use **File transfer → Join network** to connect to an internet-enabled Wi-Fi network. Keep that screen open while a backlog uploads. A network that requires a captive-portal browser sign-in will not work.
7. Open [Reader](https://read.readwise.io/) and search for the tag `crosspoint-highlight` or the book title.

The device's date and time must be correct: secure connections verify certificate validity. Joining Wi-Fi uses CrossPoint's normal clock synchronization. If uploads fail after the clock was reset, correct the device time and reconnect.

## Retries, privacy, and recovery

- Only complete `.json` captures are uploaded. Temporary captures and `.txt` exports are ignored.
- Uploads are serialized at existing network idle points. A successful upload is followed by at least a three-second interval. Failures back off from 30 seconds to 15 minutes; numeric server `Retry-After` delays are respected, up to 24 hours. No upload task stays alive during reading or USB Drive mode.
- Network or authentication failures leave captures on the SD card for retry. Replace an expired token in `config.json` and reconnect. A 401 or 403 pauses attempts for at least 15 minutes; restarting clears the in-memory timer.
- Every capture has one stable synthetic source URL. Reader's document-save API returns the existing document for a repeated URL, so an interrupted acknowledgement can safely be retried. The `.invalid` source link is an identifier, not a website containing your book; the excerpt HTML is sent directly to Readwise.
- A successful acknowledgement is written under `/.crosspoint/readwise/sent/<capture-id>`. Do not delete that folder during routine use. If you change Readwise accounts, remove the `sent` folder to upload your local captures to the new account as well. Removing it does not delete anything from either Reader account.
- The token is stored as plain text on your SD card so it can be configured without a separate account screen. The firmware blocks its folder from the web file manager and plugin file endpoints. Anyone with physical access to the SD card or USB Drive can read it; revoke the token from Readwise if it is exposed.
- Requests use verified HTTPS to `readwise.io` and never forward credentials through redirects. Logs contain upload status and capture IDs, not tokens, book text, or server response bodies. Local exports are never deleted by sync.
- To disable syncing, set `"enabled": false` or remove `config.json`. Local saving and export continue to work.

## Maintainer notes and verification

The implementation follows the official [Reader API](https://readwise.io/reader_api), using `POST /api/v3/save/`, a stable URL, supplied HTML, `should_clean_html: false`, and Token authentication. Saving excerpt documents avoids the native-highlight endpoint's exact-passage matching and its undocumented retry deduplication behavior.

The firmware includes [ISRG Root X1 from Let's Encrypt](https://letsencrypt.org/certificates/). Readwise's public certificate chain was checked on November 5, 2026 and chained through YR1 and Root YR to ISRG Root X1. A future change to a different root CA requires updating the bundled root and rebuilding; there is no insecure fallback. The build applies a narrow, checked patch to the pinned FreeInk SDK that enables hostname verification and checks CA loading on its authenticated TLS path. The need for the explicit hostname call is documented in [wolfSSL's certificate guide](https://www.wolfssl.com/documentation/manuals/wolfssl/chapter07.html). Existing upstream insecure callers retain their behavior.

Memory is allocated only for one cold-path upload: at most a 96 KiB source JSON buffer, its zero-copy parse metadata, an exactly sized escaped-HTML request (capped at 580 KiB), a 1 KiB configuration buffer, a 1 KiB response buffer, and the HTTP/TLS client's working memory. The source buffer is released before the TLS handshake. Available heap and largest-block checks defer work when memory is tight; actual peak internal RAM, PSRAM, and stack use need verification on hardware. No second framebuffer or persistent worker is added.

Before using this on your reading library, test with a short non-sensitive book:

1. Save a highlight offline; confirm both local exports and their five-page content.
2. Join Wi-Fi and check Reader for one correctly titled excerpt with all saved text.
3. Reconnect and restart; confirm there is no duplicate.
4. Interrupt Wi-Fi during an upload; reconnect and confirm eventual delivery with the local originals intact.
5. Test an invalid token; confirm no captures disappear. Restore a valid token and confirm the queue drains after the delay or a restart.
6. Enter USB Drive while uploads are pending; confirm no filesystem activity runs while the host owns the SD card.
7. Check a large capture, several queued captures, book-edge captures, non-Latin text, quotation marks, ampersands, and angle brackets.

Live account delivery, timeout behavior, repeated-transition memory use, and device responsiveness must be verified on the X4 Pro. A successful firmware build alone does not establish those results.
