> The original service description below applies to Almanac. Enabling the optional
> Books mirror also sends session metadata to your Readwise account; see the fork’s
> READWISE.md for its setup and privacy behavior.

# Almanac + Readwise Books

This downstream edition can also mirror reading sessions to Readwise Books.
It requires CrossPoint Highlighting 2.0.0-highlights.5 or later and a one-time
USB setup. See https://github.com/elliodell/almanac-readwise/tree/feat/readwise-books-sync
for installation, privacy, and recovery instructions. Existing Almanac setup
continues to work on ordinary firmware, which ignores the optional mirror.

Reading stats on the official CrossPoint firmware. No other firmware, no account.

Sends each reading session to your stats server and shows your stats under Home > Plugins > Almanac + Readwise Books. Nothing to do while reading.

Almanac is in beta and early access: things may change or break along the way. Privacy and terms: www.almanac.im/privacy

## Setup

1. Enable Settings > Connectivity > Plugins. No restart is needed.
2. Connect to your Wi-Fi once, so the reader remembers it.
3. Open Home > Plugins > Almanac + Readwise Books and choose Sign in. Scan the code with your phone and confirm it. The browser you confirm it in stays signed in to your stats.

## How it works

A session starts when you open a book and ends when you leave it or put the reader to sleep. Only forward page turns on pages shown for at least 2 seconds count, and a session needs at least 10 seconds of reading.

The reader sends finished sessions whenever it connects to Wi-Fi:

- When you put it to sleep. If Wi-Fi is off, it joins the last network it used, sends and sleeps. This adds a few seconds to going to sleep.
- When you open Home > Plugins > Almanac + Readwise Books. The reader connects first, sends, then shows your stats, so they are up to date. Leave the book before you open it.
- When it connects from the Wi-Fi menu, or File Transfer joins your network.

## When sessions wait

- Battery below 20% or the last network out of range at sleep: sessions wait for the next connection. Opening the plugin also works with any other saved network.
- Up to 4 sessions are sent per connection; the rest follow on the next one.
- The reader keeps about 12 unsent sessions. One more and it starts over, losing the unsent ones, so connect now and then when reading away from home.
- Until you sign in, sessions wait on the reader.
