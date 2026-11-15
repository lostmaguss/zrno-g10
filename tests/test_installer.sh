#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# installer/install.command against a disk image (macOS only, no password):
# erases a blank 600 MB image, checks the partition table, the boot mark and
# every file, then updates it and checks a photo and a setting survive.
# Package the release first (tools/package_release.py).
set -eu
LAB=$(cd "$(dirname "$0")/.." && pwd)
ZIP=$(ls "$LAB"/build/release/zrno-*-g12-100g.zip | tail -1)
T=$(mktemp -d -t zrno-installer)
DEV=''
cleanup() { [ -n "$DEV" ] && hdiutil detach "$DEV" >/dev/null 2>&1; rm -rf "$T"; }
trap cleanup EXIT
fail() { echo "FAIL: $*"; exit 1; }

ditto -x -k "$ZIP" "$T/rel"
[ -x "$T/rel/install.command" ] || fail 'install.command is not executable in the zip'
hdiutil create -quiet -size 600m -layout NONE -type SPARSE -o "$T/card"
attach() { DEV=$(hdiutil attach "$@" "$T/card.sparseimage" 2>/dev/null | awk 'NR == 1 { sub("/dev/", "", $1); print $1 }'); }

attach -nomount
ZRNO_TEST_DISK=$DEV "$T/rel/install.command" >"$T/erase.log" || { cat "$T/erase.log"; fail 'erase run'; }
attach -nomount   # the installer ejects the card when it is done
mbr=$(dd if=/dev/r$DEV bs=512 count=1 2>/dev/null | xxd -p | tr -d '\n')
[ "${mbr:900:2}" = 06 ] || fail "partition 1 type ${mbr:900:2}, not 06 (FAT16)"
[ "${mbr:932:2}" = 0b ] || fail "partition 2 type ${mbr:932:2}, not 0b (FAT32)"
[ "$(dd if=/dev/r${DEV}s1 bs=512 count=1 2>/dev/null | dd bs=1 skip=64 count=8 2>/dev/null)" = BOOTDISK ] || fail 'no BOOTDISK mark'
diskutil mountDisk "$DEV" >/dev/null
mp() { diskutil info -plist "$1" | plutil -extract MountPoint raw -o - -; }
B=$(mp ${DEV}s1); D=$(mp ${DEV}s2)
(cd "$D" && shasum -a 256 -c -s "$T/rel/CARD.SHA256") || fail 'card files differ'
cmp -s "$B/DISKBOOT.BIN" "$T/rel/DISKBOOT.BIN" || fail 'DISKBOOT.BIN on the boot partition'
[ -z "$(find "$B" "$D" -name '._*')" ] || fail 'AppleDouble ._ files on the card'

mkdir -p "$D/DCIM/100CANON"; echo photo >"$D/DCIM/100CANON/IMG_0001.JPG"; echo on >"$D/CHDK/FILM/FILMON.TXT"
ZRNO_TEST_DISK=$DEV ZRNO_TEST_MODE=U "$T/rel/install.command" >"$T/update.log" || { cat "$T/update.log"; fail 'update run'; }
grep -q 'update it' "$T/update.log" || fail 'the set-up card was not offered an update'
attach
D=$(mp ${DEV}s2)
[ "$(cat "$D/DCIM/100CANON/IMG_0001.JPG")" = photo ] && [ "$(cat "$D/CHDK/FILM/FILMON.TXT")" = on ] ||
    fail 'the update lost a photo or a setting'
echo "installer: erase and update on a disk image passed ($(wc -l <"$T/rel/CARD.SHA256" | tr -d ' ') files)"
