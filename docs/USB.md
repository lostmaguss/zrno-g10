# USB tools

With the G12 plugged into a Mac and running Zrno, `tools/g12_usb.py` reads
logs, copies files, grabs the screen, runs Lua and test-boots a build. The
card stays in the camera. It drives CHDK's PTP extension through
[chdkptp](https://app.assembla.com/spaces/chdkptp/documents) (setup below).

USB can't switch the camera on, and the G12 doesn't charge over USB: an
ACK-DC50 mains adapter helps for long sessions. Canon's firmware switches to
the gallery whenever USB is connected; `rec` goes back to shooting.

## Commands

Run from the repository root, for example `python3 tools/g12_usb.py status`.

| Command | What it does |
| --- | --- |
| `status` | Model, CHDK build, mode, battery (mV), free space (KiB) |
| `ls [DIR]` | List a card folder, `A/CHDK/FILM` by default |
| `logs [DEST]` | Download every `A/CHDK/FILM/*.TXT` to `build/usb-logs/<time>/` |
| `get SRC... DEST` | Download files or folders |
| `push SRC... DIR` | Upload files to a card folder, e.g. `push build/x.flt A/CHDK/MODULES` |
| `screen [OUT]` | Save the screen with its overlays as a PNG in `build/usb-screens/` |
| `lua CODE` | Run Lua on the camera and print what it returns, e.g. `lua 'return peek(0x19a4)'` |
| `rec` / `play` | Switch to shooting mode or the gallery |
| `reboot [FILE]` | Restart CHDK, optionally booting `FILE` instead of the card's `DISKBOOT.BIN` |
| `raw CMD...` | Pass a command to chdkptp, e.g. `raw 'rmem ADDR COUNT -f=FILE'` to copy memory, or `raw 'clock -sync'` to set the camera's clock |

Paths on the camera start with `A/`, the card's data partition.

**Try a build without changing the card's boot files:** rename CHDK's
`main.bin` (the core image, not `DISKBOOT.BIN`), `push` it to `A/` and run
`reboot A/FILE.BIN`. A power cycle returns to the installed build.

Shots taken with Lua `shoot()` count as scripted, so Zrno doesn't process
them; use the shutter button.

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `No G12 found over USB` | The camera is off, CHDK isn't running, or the cable is out. |
| Found, then the connection drops | macOS's `ptpcamerad` grabs the camera; the script stops it and retries. |
| `screen` fails on `field 'subst'` | chdkptp r1528 crashes on `lvdumpimg -nosubst`; don't use that option. |
| Odd behaviour when plugging in | CHDK's *USB remote* setting is on; turn it off in CHDK's menu. |

## Setting up chdkptp on a Mac

The script runs `~/dev/chdkptp/chdkptp.sh`, or `$CHDKPTP`. It is tested with
chdkptp 1.0rc1 r1528 (protocol 2.9).

1. Download `chdkptp-src-1.0rc1-r1528.zip` from the
   [chdkptp files page](https://app.assembla.com/spaces/chdkptp/documents)
   and unzip it to `~/dev/chdkptp`.
2. Build Lua 5.3 into it (Homebrew no longer has 5.3):

   ```bash
   curl -LO https://www.lua.org/ftp/lua-5.3.6.tar.gz
   tar xzf lua-5.3.6.tar.gz && cd lua-5.3.6
   make macosx && make install INSTALL_TOP=$HOME/dev/chdkptp/lua53
   ```

3. `brew install libusb libusb-compat readline`
4. In `~/dev/chdkptp`, create `config.mk` for a command-line build, then run
   `make`:

   ```
   USE_LUA_VER=53
   IUP_SUPPORT=0
   CD_PLUS_SUPPORT=0
   GTK_SUPPORT=0
   LUASIGNAL_SUPPORT=0
   READLINE_SUPPORT=1
   READLINE_LIB_DIR=/opt/homebrew/opt/readline/lib
   READLINE_INCLUDE_DIR=/opt/homebrew/opt/readline/include
   LUA_INCLUDE_DIR=$(HOME)/dev/chdkptp/lua53/include
   LUA_LIB_DIR=$(HOME)/dev/chdkptp/lua53/lib
   LIBUSB_INCLUDE_DIR=/opt/homebrew/include
   LIBUSB_LIB_DIR=/opt/homebrew/lib
   ```

5. Create an executable `~/dev/chdkptp/chdkptp.sh`:

   ```sh
   #!/bin/sh
   CHDKPTP_DIR="$HOME/dev/chdkptp"
   export LUA_PATH="$CHDKPTP_DIR/lua/?.lua;;"
   export LUA_CPATH="$CHDKPTP_DIR/?.so;;"
   exec "$CHDKPTP_DIR/chdkptp" "$@"
   ```

6. `~/dev/chdkptp/chdkptp.sh -e"version"` should print `host:2.9 cam:not connected`.
