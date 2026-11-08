# Reading time with Almanac

This feature branch includes **Almanac + Readwise Books 0.4.0-readwise.1**, based on the existing CrossPoint
plugin that shows reading time, streaks and book progress on the reader and
at [almanac.im](https://almanac.im). It uses the reading-session support already
present in the CrossPoint 2.0.0 base. The Almanac service itself needs no extra firmware patch. The optional
[Readwise Books mirror](READWISE-BOOKS.md) requires this branch’s native adapter.

Almanac is a beta service maintained by BART!. Its upstream author reports
testing on the Xteink X3. This bundle uses the same plugin interface on the
**Xteink X4 Pro**. QR sign-in, the stats screen and reading-time updates were verified on that
model. See the [recorded device checks](VERIFY.md) for the tested firmware
and remaining limits.

## Set up on an Xteink X4 Pro

1. Install this fork's **X4 Pro** firmware using the [repository instructions](../README.md).
2. Make the SD card available to your computer using the reader's USB Drive
   mode or an SD-card reader. Copy the repository's
   [`sd-card/plugins/almanac`](../sd-card/plugins/almanac) directory to
   `/plugins/almanac` on the SD card. For example, the device manifest must end
   up at `/plugins/almanac/device.json`. Keep the included `LICENSE` and
   `SOURCE.md` with the plugin.
3. Safely eject the drive and restart the reader after leaving USB Drive mode
   or reinserting the card. Enable **Settings → Connectivity → Plugins**.
   Restart once more if the plugin was installed while the reader was running;
   a restart ensures its reading-session subscription is loaded.
4. Connect the reader to your Wi-Fi once so it remembers the network and gets
   a trustworthy clock. Reading sessions need a valid clock to be recorded.
5. Open **Home → Plugins → Almanac + Readwise Books → Sign in**. Scan the QR code
   with your phone and confirm the displayed code. If the reader says **Not signed
   in yet**, tap that message (away from the back arrow) to start sign-in. Keep
   the sign-in screen open until the reader shows your stats. No email/password
   account is required. The phone browser used for confirmation retains access
   to your stats.
6. Read normally. After reading, leave the book and open **Almanac + Readwise Books** to upload pending sessions and see your totals. Putting the reader
   to sleep also attempts an upload on the last connected Wi-Fi network.

If you prefer Plugin Hub, you can instead enable plugins and install
**Almanac: Reading Stats** from **Home → Plugins → Plugin Hub**. That installs
the version currently offered by the Hub, which may differ from the pinned
0.4.0 copy in this repository. Avoid installing duplicate Almanac folders under
different plugin roots.

When replacing an existing installation, preserve `token.json` and
`events.jsonl`. Copy only the supplied plugin files over the old ones, then
restart. Do not copy your device's token or pending reading history back into
this public repository.

## What counts as reading time

Time is measured between rendered pages and credited on a successful ordinary
forward page turn. A page must have been displayed for at least **2 seconds**;
each credited dwell is capped at **30 minutes**. Backward turns, chapter skips,
failed turns and quick paging do not count. A session needs at least
**10 seconds** of counted reading and a trustworthy start time.

From highlights.6, X4 Pro sessions use a fresh RTC UTC reading rather than the
loan-expiry clock. This prevents an old future clock floor from dating new
sessions ahead of real time. Active seconds still use elapsed page dwell.
An unreadable or unset RTC provides no new session timestamp.

A session is flushed when you leave a book or put the reader to sleep. The
time spent on the final displayed page is not credited until a qualifying
forward turn, so this is an estimate of active reading rather than a stopwatch
for the entire time a book is open. Saving a highlight does not itself add
reading time. Sessions are supported for EPUB, TXT and XTC reading; text
highlight availability is a separate feature of this fork.

## Wi-Fi and offline limits

- Reading can happen offline. Viewing Almanac's statistics and sending queued
  sessions require its online service.
- At sleep, the reader can join the last connected network and upload pending
  sessions. This adds a few seconds to sleep and uses additional battery. The
  firmware limits the Wi-Fi join attempt to 10 seconds.
- If battery is below **20%**, the network is unavailable, or upload fails,
  sessions wait for a later connection. Opening Almanac or starting File
  Transfer in Join Network mode also triggers delivery.
- The upstream outbox is **4 KB**, roughly **12 sessions**, depending on book
  path length. When that limit is exceeded, the firmware discards the previous
  unsent queue. Connect regularly; this is not a durable offline reading log.
- At most **four queued events** are delivered per drain. If there is a backlog,
  further connections may be needed before all totals appear.
- Very long book paths can exceed the firmware's **512-byte event-line limit**
  and prevent recording. The plugin author recommends paths shorter than about
  **235 bytes**. Shorter, shallow book folders are the safer choice.

## Privacy and disabling

The bundled manifest sends the book's SD-card path (including filename), a
document hash, session identifier, timestamps, reading duration and progress
to **almanac.im** over HTTPS. It does not send book text or the highlights
created by this fork. The service also receives ordinary connection metadata.
Read the author's [privacy policy and terms](https://almanac.im/privacy) before
pairing the reader. Anonymous pairing still gives the service your reading
history.

The plugin keeps a bearer token in `/plugins/almanac/token.json` and pending
sessions in `/plugins/almanac/events.jsonl` on the SD card. Keep both private.
Sessions can queue before pairing; install the plugin when you are ready to
set it up.

To stop collecting and sending Almanac sessions, uninstall Almanac from
**Home → Plugins**, or disable **Settings → Connectivity → Plugins** to stop
all plugins. You can also remove `/plugins/almanac` with the reader powered off
and then restart it. Removing the plugin does not delete history already held
by the online service; consult the service for that.

## Check it works

After setup, open a book and read several pages, leaving each visible for at
least five seconds before an ordinary forward turn. Accumulate more than ten
seconds, leave the book, and open Almanac while online. Confirm that the book
and reading time appear. Repeat with Wi-Fi unavailable, then reconnect and
check that the pending session arrives. Offline delivery, outbox limits and
battery impact still need separate checks on the X4 Pro.

## Source and maintenance

The bundle is based on upstream **v0.4.0**, commit
`360a4cd29a46115f0db9cb26a07f3c2b16e9e76e`, under the MIT license.
[Source and file hashes](../sd-card/plugins/almanac/SOURCE.md) are included.

Almanac’s URLs and authentication stay in the SD-card plugin. Its normal uploads use
its existing generic [plugin events](plugin-events.md), and the optional Books mirror uses the adapter documented above. No new background task is created. The author controls the online
service; future service changes may require updating the plugin through Plugin
Hub or copying a newer release to the SD card. No hardware measurements of its
battery or timing impact are included in this fork.
