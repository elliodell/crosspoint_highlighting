# SD-card files

Copy the `plugins/almanac` folder to `/plugins/almanac` on the reader's SD card.
The result must contain `/plugins/almanac/device.json`, not
`/sd-card/plugins/almanac/device.json`.

Almanac supplies reading-time statistics through its online service. Copying
these files is separate from installing the firmware; complete the setup in
[Reading time with Almanac](../docs/ALMANAC.md). The bundled version is 0.4.0;
its license and pinned source are inside the plugin folder.

When updating an existing Almanac installation, keep its `token.json` and
`events.jsonl`; replace only the bundled plugin files. Do not share those
runtime files or add them to a public repository.
