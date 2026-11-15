For the Canon PowerShot G12. Download the zip for your camera's firmware:

| Firmware | Card files | Status |
| --- | --- | --- |
| 1.00G | `zrno-{version}-g12-100g.zip` | Tested on a camera |
| 1.00C | `zrno-{version}-g12-100c-untested.zip` | Untested |
| 1.00E | `zrno-{version}-g12-100e-untested.zip` | Untested |
| 1.00F | `zrno-{version}-g12-100f-untested.zip` | Untested |

**Check the firmware first:** put an empty file named `ver.req` on a card, start the camera in playback, hold FUNC.SET and press DISP. The screen shows the version, for example GM1.00G.

The untested builds are made from CHDK's own G12 port for each firmware, but Zrno has only run on a camera with 1.00G. Use them at your own risk. If you try one, please open an issue and say how it went.

**Install on a Mac:** unzip the zip for your firmware, put the SD card in the Mac and double-click `install.command`. Then lock the card and put it in the camera. `INSTALL.TXT` in the zip has the details, and the steps by hand for Windows and Linux.

Each `-chdk-source.zip` is the source of the zip of the same name: CHDK release-1_6 r6355 with Zrno integrated. The `SHA256SUMS` files have the hashes of both.
