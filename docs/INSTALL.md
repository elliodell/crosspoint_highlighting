# Install on an Xteink X4 Pro

These instructions are for **X4 Pro (ESP32-S3)**. Do not use the X3/X4 ESP32-C3 binary.

## Browser installation

1. Back up the SD card, including the hidden `.crosspoint` folder, and charge the reader. Keep that backup outside the public source repository.
2. Download `crosspoint-2.0.0-highlights.3-x4pro.bin` and `crosspoint-highlighting-sd-card.zip` from [this repository's Releases](https://github.com/elliodell/crosspoint_highlighting/releases). The release is experimental until you complete the device checks below.
3. Use a desktop browser with Web Serial support, such as Chrome or Edge. Connect the X4 Pro with a USB data cable.
4. Open [CrossPoint's flash tools](https://crosspointreader.com/#flash-tools), select **X4 Pro**, choose **Custom .bin**, and select the downloaded firmware file. Follow the flasher's connection and completion prompts. Do not disconnect during flashing.
5. Restart the reader. The version should contain `2.0.0-highlights.3` and `x4pro`.
6. Extract the SD-card ZIP. Copy its `plugins` folder to the SD-card root (merge with an existing `plugins` folder). The result must be `/plugins/almanac/manifest.json`, not `/sd-card/plugins/...`.
7. Safely eject the card/USB Drive, then restart. Follow [Almanac setup](ALMANAC.md) and [Readwise setup](READWISE.md) if desired.
8. Open a short EPUB, tap power once, and check `/Highlights` for matching text and JSON exports. Follow the [device checks](VERIFY.md) before relying on it for important captures.

The `.bin` is an application image. For a completely erased device or incompatible partition layout, first install official CrossPoint 2.0.0 for X4 Pro using the normal flasher, then install this custom image. Do not erase the device or write the application binary at address zero.

## Update from an existing CrossPoint installation

CrossPoint also supports installing a firmware file from the SD card through its firmware update flow. Use this fork's X4 Pro binary only; follow the prompts in Settings → System. Browser installation above is the most direct path if your current version's menu differs.

Version `2.0.0-highlights.3` uploads actual highlights to Readwise Browse → Library. The existing token configuration still works. Previously captured text is retained and uploaded even if an earlier version already sent it as a Reader document. Long captures become numbered parts sharing their own capture ID. New captures preserve paragraphs and upload only new text when they overlap earlier acknowledged captures. Older exports lack that metadata and retain their existing formatting; see the migration notes in [Readwise setup](READWISE.md). Earlier Reader documents are retained; this update does not delete anything from your account.

Whole-paragraph capture from version `2.0.0-highlights.2` remains in place; existing exports are not rewritten.

This update rebuilds older completed and partially built page caches as chapters are opened. The first opening or capture can take longer while those pages are laid out again. Books, saved exports, settings, and reading progress are retained; there is no need to delete the hidden `.crosspoint` folder.

Built-in internet update checks point to this fork. Experimental prereleases are not returned by GitHub's “latest stable release” endpoint, so install experimental builds from the release page. To return to official CrossPoint, use the [official flasher](https://crosspointreader.com/#flash-tools) and choose the normal X4 Pro release. Your exported files remain on the SD card.

## Build from source

Use Python 3.13 and the repository-pinned **pioarduino Core 6.1.19**. These are the same base dependencies as upstream 2.0.0.

```sh
git clone --recursive https://github.com/elliodell/crosspoint_highlighting.git
cd crosspoint_highlighting
python3 -m venv .venv
. .venv/bin/activate
python -m pip install 'https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip'
python -m pip install 'littlefs-python>=0.16.0' 'fatfs-ng>=0.1.14' 'pyyaml>=6.0.2' \
  'rich-click>=1.8.6' 'zopfli>=0.2.2' 'intelhex>=2.3.0' 'rich>=14.0.0' \
  'cryptography>=45.0.3' 'certifi>=2025.8.3' 'ecdsa>=0.19.1' 'bitstring>=4.3.1' \
  'reedsolo>=1.5.3,<1.8' 'esp-idf-size>=2.0.0' 'esp-coredump>=1.14.0'
pio run -e x4pro
```

The output is `.pio/build/x4pro/firmware.bin`. The build downloads several large compiler/framework packages on its first run. It also applies the checked TLS patch to the pinned SDK working copy; that SDK modification is generated and must not be committed as a different submodule revision.

For a developer installation over USB:

```sh
pio run -e x4pro --target upload
```

Let PlatformIO select the device, or add `--upload-port YOUR_PORT` using the actual port listed for your X4 Pro. Do not copy another person's port value.
