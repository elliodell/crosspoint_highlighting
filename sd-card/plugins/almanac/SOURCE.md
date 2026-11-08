# Almanac source and license

This directory bundles **Almanac: Reading Stats 0.4.0** by BART! under the
[MIT license](LICENSE).

- Upstream: <https://github.com/bartholomej/almanac.crosspoint-plugin>
- Release: <https://github.com/bartholomej/almanac.crosspoint-plugin/releases/tag/v0.4.0>
- Pinned commit: `360a4cd29a46115f0db9cb26a07f3c2b16e9e76e`
- Source directory: `almanac.crosspoint-plugin/` at that commit.
- `LICENSE` comes from the root of the same upstream commit.

The downstream Readwise Books edition changes the manifest, device manifest,
and README. The license and DETAILS.md remain upstream. Its source lives at
https://github.com/elliodell/almanac-readwise/tree/feat/readwise-books-sync .
Git blob IDs below identify the original upstream files:

| File | Git blob SHA-1 |
| --- | --- |
| `manifest.json` | `77818f9f5854b61723cc79f4d7f9f67970b31650` |
| `device.json` | `9dc3f62f67b873b91237ac85c2873b6542e0ccc7` |
| `README.md` | `72cb97b7eec7a2ee5ca826d176bcf6189a930b20` |
| `DETAILS.md` | `0473599d129e72661eadc9d7ba9aa31307289812` |
| `LICENSE` | `41003bd4315698439f3ab942b6f254487fddfd32` |

The plugin uses the independent Almanac service at <https://almanac.im>.
Bundling its client does not bundle or self-host that service. Its availability
and server-side behavior are controlled by the Almanac author.

For this fork's installation instructions, see
[Reading time with Almanac](../../../docs/ALMANAC.md) in the repository.
