# Zrno in the browser

A page for browsing the shots on a Zrno card and trying the camera's looks
and effects on them. It reads the sensor backups the camera saves with each
processed shot (`A/FGRAW/Rnnnnnnn.RAW`), develops them, and runs the
camera's own C effect code (`../camera/film_*.c`, unchanged) compiled to
WebAssembly. Everything runs in the browser, and nothing goes to a server.

## Using it

*Connect camera* reads the card with the camera plugged in over USB and
running Zrno. It uses WebUSB and CHDK's PTP extension, the same commands
chdkptp uses, so it works in Chrome and Edge only. Every backup shows in a
film strip, matched to the JPEG the camera made from it (through
`CHDK/FILM/NATIVE.TXT`). The arrow keys step through the shots.

On a Mac, run `killall ptpcamerad` first, or the browser can't take the
camera. On Windows, Chrome only sees the camera after its driver is
replaced with WinUSB (with Zadig). After the first time, Chrome connects
without asking.

A backup downloads when its shot is opened. While it does, the frame shows
the shot's thumbnail, blurred, with the progress and speed. The last three
backups stay in memory. Thumbnails and dates come from the first 64 KB of
each camera JPEG, so the whole JPEG never crosses the cable.

Each shot opens with the look and effects the camera gave it, read from
`CHDK/FILM/STAGELOG.TXT`, including the camera's grain seed, so grain and
light leaks come out as they did on the camera. The stamp's mode and text
are not logged and stay as set here. *Back to this shot's look* undoes
changes. Shots re-developed on the camera before this version of the
firmware are logged without their number, so they open with the look of
their first develop.

A shot whose camera thumbnail can't be read shows a skeleton tile until it
is opened, then a small copy of the render.

### Looks

The Looks list is the camera's saved looks (`CHDK/FILM/LOOKS.TXT`), for
every engine. Clicking one applies it to the open photo. *New look* saves
the current engine and settings to the camera. Each look in the list can
be updated with the current settings, renamed or deleted. The camera holds
16 looks. A name is up to 20 characters and must fit the 92 px the camera's
menu gives it in its own font, which the page measures as you type.

Every change starts from the file as it is on the card, so looks saved on
the camera meanwhile stay. After writing the file the page leaves
`CHDK/FILM/LOOKSRLD.TXT`, and Zrno reads its looks again within a second.
Firmware older than this reads them at its next start.

### Engines

Film, ZrnoBoy, Print and Grade, with the camera's menu rows. ZrnoBoy and
Print run the camera's kernels on the developed frame. ZrnoBoy's Viewfinder
frame prints the exposure from the camera JPEG's Exif. Grade builds the
DIGIC colour table the camera installs, applied as Canon's develop applies
it. Grade shots make no backup on the camera, so here it grades backups
taken with the other engines. Sharpening and the halation colour are saved
with a look but not shown.

STAGELOG.TXT says only that a shot used ZrnoBoy or Print, not their
settings, so such a shot opens with the current settings.

### White balance

The camera saves its white balance record for each backup as
`FGRAW/Rnnnnnnn.SET`. A shot opens with that balance (As shot) and
Temperature and Tint adjust from there, warmer or cooler, greener or more
magenta. Changes re-develop the backup without loading it again. A backup
without a readable .SET starts from daylight. The levels are four 32-bit
words at offset 0x78d of the record, G R B G with G near 1024. That order
was checked on the camera's own files: a tungsten shot's blue level is
about 3.5 times its green, as its raw data needs.

### Saving and deleting

*Save to* picks this computer (a download, the default) or the camera's
card. On the card a render goes to `A/ZRNO`, named after the camera's JPEG
and the look (`IMG_0001_PORTRA.JPG`), with its Exif copied over. *Save all
shots* does every backup with the settings set at the time. Nothing is
written to DCIM or FGRAW.

*Delete shot* removes the backup, its .SET, the camera JPEG and a Canon RAW
of the same name from the card, after a confirmation that lists them.
Renders in ZRNO stay.

Dropping the card or a folder from it on the page still works, without the
camera. Chrome and Edge can then save and delete on the card in place.

Base colours differ from the camera's JPEG, since the backups are
developed here. The effects themselves are exact: the WebAssembly gives
byte-identical output to the camera code.

`usb.html` is a test bench for the connection. It lists card folders,
downloads files and logs the time and speed of each, switches between
shooting mode and the gallery, and runs Lua on the camera.

## Parts

- `src/fgraw.rs`: reads a backup, CHDK's raw buffer as it is (3744 × 2784
  12-bit samples, 15,634,944 bytes, no header).
- `src/develop.rs`: develops it as LibRaw does by default (black level,
  white balance, Malvar-He-Cutler demosaic, the G12's colour matrix, auto
  brightness, BT.709 curve), cropped to the 3648 × 2736 JPEG area.
- `src/pipeline.rs`: runs the camera's kernels in the camera's order.
- `src/engines.rs`: ZrnoBoy, Print and Grade, and the engines' menu labels
  from the kernels.
- `site/`: the page and its Web Worker.
- `site/ptp.js`: the USB connection. PTP framing over WebUSB plus CHDK's
  commands: run Lua, download and upload files, list folders.
- `site/usb.html`: the USB test page.

## On GitHub Pages

`.github/workflows/pages.yml` runs the tests, builds `dist` and publishes
it to GitHub Pages on every push to `main` that changes the page, the
camera code or the card's data files. It can also be run by hand from the
Actions tab. Pages serves the files over HTTPS, which WebUSB needs, and
everything still runs in the visitor's browser.

Pages has to be switched on once: in the repository's Settings, Pages,
set Source to GitHub Actions. The site is then at
`https://suzvuk.github.io/zrno/`. From a private repository this needs a
paid GitHub plan, and the site is still open to anyone with its address.

## Build and run

```bash
./build.sh
python3 -m http.server 8765 --directory dist
```

Needs `rustup target add wasm32-unknown-unknown` and a clang with the wasm32
target (Apple's clang has it). `cargo test --release` checks the backup
reader against CHDK's own pixel order.
