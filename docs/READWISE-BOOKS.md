# Direct Readwise Books sync

This feature branch extends BART!'s Almanac 0.4.0 plugin. Almanac keeps its own
sign-in, stats, and uploads. The optional mirror sends the same future reading
sessions directly from CrossPoint to **Readwise Books** over Wi-Fi. It does not
use Readwise Reader or the highlights API. No Mac or hosted bridge is needed
after setup, and no AI/MCP connection is required for daily syncing.

## Requirements

- Xteink X4 Pro running the companion CrossPoint Highlighting firmware
  **2.0.0-highlights.5-x4pro** or later with `readwise-books-v1` support.
- Readwise Books access and a [Readwise access token](https://readwise.io/access_token).
- Python 3.9+ on the setup computer (no extra Python packages).

Ordinary CrossPoint 2.0.0 ignores the mirror field. Almanac continues working,
but Books sync needs the native adapter. This is not a plugin-only installation
on stock firmware.

## Install

1. Back up the SD card. Install the companion **X4 Pro** application `.bin`
   through **Settings → System → SD Card Firmware Update**, then check About.
   Do not use the X3/X4 build or erase/flash the whole device.
2. Connect by USB and open **Home → File Transfer → USB Drive**. Wait for the
   drive to mount on the Mac.
3. From the [Almanac fork checkout](https://github.com/elliodell/almanac-readwise/tree/feat/readwise-books-sync) run:

   ```sh
   python3 scripts/setup_readwise.py --card "/Volumes/NO NAME" \
     --timezone America/New_York --create-missing
   ```

   Set your own IANA time zone. The helper reuses the private highlighting
   token already on the card, or asks for a token without displaying it.
   `--token-file /path/to/private.json` accepts a JSON file with a `token` field.
   Never pass a token in the command or commit that file.
4. The helper matches EPUB title and author to your existing Books library.
   `--create-missing` allows it to add unmatched titles/authors to that library;
   omit it to link existing books only. It uploads no book text or EPUB files.
   Ambiguous editions are left unlinked. A private `--map-file` can explicitly
   map an SD path such as `/books/example.epub` to its numeric Readwise Books ID.
5. It installs only the plugin program files, preserving Almanac's token and
   pending events. It writes a separate protected Books configuration and adds
   `readwise_books_id` to each linked book's `.meta.json`, preserving other keys.
   Original sidecars get a `.before-readwise` backup. Setup results remain in
   `/.crosspoint/readwise-books/setup-report.json` on the card.
6. Safely eject, leave USB Drive, and restart. Keep plugins enabled. Read normally,
   then put the reader to sleep or join Wi-Fi. Almanac's existing bounded sleep
   connection is reused; battery below 20% or unavailable Wi-Fi defers delivery.

Run the helper again after adding books. New unmapped sessions are retained and
can be linked by the helper; previously mapped sessions keep their original
destination. While unmapped records remain, sleep may still attempt Wi-Fi.

## What gets recorded

Book identity, session date/time, active reading seconds, and starting/ending
percentage are sent to Books. The EPUB, its text, and highlights are not sent by
this integration. Feed posting is explicitly disabled. Your Readwise account's
normal visibility rules still apply to library entries and statistics.

CrossPoint counts time on qualifying forward page turns, with its normal
minimum-duration and idle-time rules. Its session end is currently start plus
active reading time, not necessarily the wall-clock moment you left the book.
The adapter preserves that behavior. Stats/progress percentages are not a
promise of opening the other reader at the same EPUB location.

Almanac's old server history is not imported. Only future emitted sessions are
mirrored after setup. Readwise sessions are independent of highlight uploads.

## Reliability and recovery

The adapter writes every captured record to its own SD queue, independently of
Almanac's 4 KiB outbox. It stores the selected book and account binding at capture.
Requests, original events, and receipts remain in protected SD history. Storage
grows with reading history (roughly several filesystem clusters per session);
back it up and keep free space on the card. SD write failure can still lose a
new session and is reported in diagnostics.

At most one session is processed per drain, with a 60-second minimum interval.
The queue cursor is saved on SD so a blocked record cannot monopolize every
wake/sleep cycle.
HTTPS uses a cooperative 15-second deadline, 8-second transport timeouts, and
certificate/hostname verification. Blocking DNS/TCP operations can exceed the
cooperative deadline; actual sleep latency still needs device measurement. The adapter starts no network task and never turns Wi-Fi on while
reading. A large backlog needs multiple connections or online drain calls.

Readwise's API has no automatic duplicate detection. A verified attempt marker
is saved before POST; success creates a receipt. After an interrupted response,
the device only checks for an exact existing session. It never blindly repeats
that POST. Missing, ambiguous, damaged, or oversized reconciliation results
remain queued without blocking later sessions. This can leave a session pending
if power failed between writing its marker and sending its request.

Run these setup/status/recovery commands from that Almanac checkout, not from the CrossPoint checkout.
To inspect counts while USB Drive is connected:

```sh
python3 scripts/setup_readwise.py --card "/Volumes/NO NAME" --status
```

An uncertain session can be checked using its private queue filename:

```sh
python3 scripts/recover_session.py --card "/Volumes/NO NAME" --session SESSION_HASH
```

Only if you independently verify that it is absent, add
`--retry-confirmed-absent`; the tool rechecks the API and asks before releasing
one retry. A delayed server result could otherwise cause a duplicate. Never
delete attempt markers to force sync. Damaged records require investigation.

Queues are isolated by a hash of the access token. Changing or rotating the
token leaves old queues intact but stops their upload; resolve/back up pending
work before changing credentials. No credentials, EPUBs, or personal history
belong in this public repository.

To disable only the Books mirror, set `enabled` to `false` in its SD config.
Almanac continues. Removing or disabling the plugin stops both integrations.

## Source and checks

`firmware/ReadwiseBooks.cpp` and `.h` contain the adapter, copied into the
companion firmware under `src/integrations/`. `firmware/PluginEvents.patch`
contains the small event/subscription integration. The existing Readwise TLS
certificate and hostname-verification fix from CrossPoint Highlighting are
required. This is a maintained downstream feature, not an upstream PR.

```sh
# In the Almanac checkout:
python3 -m unittest discover -s test -v
# In the companion CrossPoint checkout:
sh test/readwise_books/run.sh
sh test/readwise_sync/run.sh
```

Host checks exercise conversion, account/book binding, offline queues, ambiguous
uploads, interrupted SD writes, and duplicate prevention. Firmware builds and
device validation are recorded separately in `VERIFY.md`. No synthetic test
reading sessions are uploaded to your account.

Protocol reference: [official Books API](https://readwise.io/books_api).
