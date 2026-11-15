#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Zrno installer for macOS. Double-click it in Finder: it prepares an SD card
# for the Canon PowerShot G12 (firmware 1.00G) and copies Zrno onto it.
#
# The G12 starts CHDK only from a FAT16 partition marked bootable, and keeps
# photos on the card's first FAT32 partition, so a card gets CHDK's
# two-partition layout:
#   1. ZRNO_BOOT, 32 MB, FAT16, with "BOOTDISK" at byte 0x40 of its boot
#      sector: DISKBOOT.BIN
#   2. ZRNO, the rest of the card, FAT32: DISKBOOT.BIN, the CHDK folder
#      and the photos
# A card that already has this layout can be updated instead of erased: its
# photos and settings stay.
#
# Writing the boot mark is the one step that needs an administrator
# password (sudo), because it writes a sector of the card directly.
#
# For testing only: ZRNO_TEST_DISK=diskN picks that disk without asking,
# and is refused unless it is a disk image (hdiutil attach).

set -u
export LC_ALL=C
HERE=$(cd "$(dirname "$0")" && pwd)
BOOT_LABEL=ZRNO_BOOT
DATA_LABEL=ZRNO
TMP=$(mktemp -d -t zrno)
trap 'rm -rf "$TMP"' EXIT

say() { printf '%s\n' "$*"; }
finish() {
    if [ -z "${ZRNO_TEST_DISK:-}" ]; then
        say ''
        read -r -p 'Press Return to close this window. ' _ </dev/tty
    fi
    exit "$1"
}
die() { say ''; say "Stopped: $*"; finish 1; }

# One value from a property list in a file: plist_get FILE KEY.PATH
# Prints nothing when the key is missing: some macOS versions print plutil's
# "Could not extract value" error on stdout, which must not pass for a value.
plist_get() {
    local v
    v=$(plutil -extract "$2" raw -o - "$1" 2>/dev/null) || return 1
    printf '%s\n' "$v"
}

# Disk facts, from diskutil's property lists.
disk_info() { diskutil info -plist "$1" >"$TMP/$1.plist" 2>/dev/null; }
disk_get() { plist_get "$TMP/$1.plist" "$2"; }

# with_limit SECONDS COMMAND...: runs it, giving up after SECONDS.
with_limit() {
    local s=$1 pid n=0
    shift
    "$@" &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        if [ $n -ge $((s * 5)) ]; then
            kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; return 124
        fi
        sleep 0.2; n=$((n + 1))
    done
    wait "$pid"
}

# card_facts DISK sets SIZE, PROTO, REMOVABLE and WRITABLE for a whole disk.
# "diskutil info" on a whole SD card can hang for minutes (a Mac's built-in
# card slot does), so the facts come from the card's first partition, which
# reports the same bus, removable and writable facts, and the size from
# "diskutil list". Only a disk with no partition is asked directly.
card_facts() {
    local src
    SIZE='' PROTO='' REMOVABLE='' WRITABLE=''
    diskutil list -plist "$1" >"$TMP/one.plist" 2>/dev/null || return 1
    SIZE=$(plist_get "$TMP/one.plist" AllDisksAndPartitions.0.Size)
    src=$(plist_get "$TMP/one.plist" AllDisksAndPartitions.0.Partitions.0.DeviceIdentifier)
    if [ -n "$src" ]; then
        disk_info "$src" || return 1
    else
        src=$1
        with_limit 15 disk_info "$1" || return 1
    fi
    PROTO=$(disk_get "$src" BusProtocol)
    REMOVABLE=$(disk_get "$src" RemovableMedia)
    WRITABLE=$(disk_get "$src" WritableMedia)
}
gb() { awk -v b="$1" 'BEGIN { printf "%.1f GB", b / 1e9 }'; }

say 'Zrno installer for the Canon PowerShot G12 (firmware 1.00G)'
say '==========================================================='
say ''

# 1. The files to install, checked before anything touches the card.
[ "$(uname -s)" = Darwin ] || die 'this installer is for macOS.'
cd "$HERE" || die "cannot open $HERE"
[ -f CARD.SHA256 ] && [ -f DISKBOOT.BIN ] && [ -d CHDK ] ||
    die 'DISKBOOT.BIN, the CHDK folder or CARD.SHA256 is missing next to this installer. Unzip the whole release and run it from there.'
shasum -a 256 -c -s CARD.SHA256 ||
    die 'some of the files next to this installer are damaged (they differ from CARD.SHA256). Download the release again.'
VERSION=$(cat VERSION 2>/dev/null || echo '')
say "Zrno ${VERSION:-(unknown version)}: $(wc -l <CARD.SHA256 | tr -d ' ') files checked."

# 2. The card.
pick_disk() {
    local d i=0
    diskutil list -plist physical >"$TMP/list.plist" 2>/dev/null || die 'diskutil cannot list the disks.'
    CANDIDATES=''
    while d=$(plist_get "$TMP/list.plist" "WholeDisks.$i"); do
        i=$((i + 1))
        card_facts "$d" || continue
        # An SD card in a built-in slot or a USB reader. Never the Mac's own
        # disks, which are internal and not removable.
        case "$PROTO" in
            'Secure Digital') ;;
            USB) [ "$REMOVABLE" = true ] || continue ;;   # card readers; not USB drives
            *) continue ;;
        esac
        [ -n "$SIZE" ] && [ "$SIZE" -ge 1000000000 ] && [ "$SIZE" -le 2200000000000 ] || continue
        CANDIDATES="$CANDIDATES $d"
    done
}

describe() {
    local d=$1 k v vols=''
    for k in 1 2 3 4; do
        disk_info "${d}s$k" || break
        v=$(disk_get "${d}s$k" VolumeName)
        [ -n "$v" ] && vols="${vols:+$vols, }$v"
    done
    card_facts "$d"
    say "  $d   $(gb "$SIZE")   ${PROTO:-card}${vols:+   (now: $vols)}"
}

if [ -n "${ZRNO_TEST_DISK:-}" ]; then
    DISK=$ZRNO_TEST_DISK
    card_facts "$DISK" || die "no disk $DISK"
    [ "$PROTO" = 'Disk Image' ] || die 'ZRNO_TEST_DISK must be a disk image.'
else
    while :; do
        pick_disk
        set -- $CANDIDATES
        [ $# -gt 0 ] && break
        say 'No SD card found. Put the card in the Mac (card slot or USB reader),'
        read -r -p 'then press Return to look again, or type Q to quit: ' answer </dev/tty
        case "$answer" in [qQ]*) finish 1 ;; esac
    done
    if [ $# -eq 1 ]; then
        DISK=$1
        say ''; say 'SD card found:'; describe "$DISK"
    else
        say ''; say 'More than one card found:'
        for d in "$@"; do describe "$d"; done
        read -r -p 'Type the name of the card to use (for example disk4): ' DISK </dev/tty
        case " $* " in *" $DISK "*) ;; *) die "$DISK is not one of the cards listed." ;; esac
    fi
fi

card_facts "$DISK" || die "cannot read $DISK."
[ "$WRITABLE" = true ] ||
    die 'the card is locked. Take it out, slide its LOCK switch up (unlocked), put it back and run the installer again.'

# A card already set up for Zrno or CHDK: two partitions, FAT16 then FAT32,
# with DISKBOOT.BIN on the first.
layout_ok() {
    local p1=$1s1 p2=$1s2 m1
    disk_info "$p1" && disk_info "$p2" || return 1
    [ "$(disk_get "$p1" FilesystemUserVisibleName)" = 'MS-DOS (FAT16)' ] || [ "$(disk_get "$p1" FilesystemName)" = 'MS-DOS FAT16' ] || return 1
    case "$(disk_get "$p2" FilesystemName)$(disk_get "$p2" FilesystemUserVisibleName)" in *FAT32*) ;; *) return 1 ;; esac
    diskutil mount "$p1" >/dev/null 2>&1
    disk_info "$p1"; m1=$(disk_get "$p1" MountPoint)
    [ -n "$m1" ] && [ -f "$m1/DISKBOOT.BIN" ]
}

MODE=erase
if layout_ok "$DISK"; then
    say ''
    say 'This card is already set up for CHDK. You can:'
    say '  U  update it: copy Zrno onto it, keeping its photos and settings'
    say '  E  erase it and set it up again (everything on it is deleted)'
    if [ -n "${ZRNO_TEST_DISK:-}" ]; then answer=${ZRNO_TEST_MODE:-U}; else
        read -r -p 'Type U, E, or Q to quit: ' answer </dev/tty; fi
    case "$answer" in
        [uU]*) MODE=update ;;
        [eE]*) MODE=erase ;;
        *) finish 1 ;;
    esac
fi

if [ "$MODE" = erase ]; then
    say ''
    say "Everything on $DISK ($(gb "$SIZE")) will be deleted, photos included."
    if [ -z "${ZRNO_TEST_DISK:-}" ]; then
        read -r -p 'Type ERASE to go on: ' answer </dev/tty
        [ "$answer" = ERASE ] || die 'nothing was changed.'
    fi
    say ''; say 'Erasing and partitioning the card...'
    diskutil unmountDisk "$DISK" >/dev/null 2>&1
    diskutil partitionDisk "$DISK" MBR \
        'MS-DOS FAT16' "$BOOT_LABEL" 32M \
        'MS-DOS FAT32' "$DATA_LABEL" R >"$TMP/partition.log" 2>&1 ||
        { cat "$TMP/partition.log"; die 'diskutil could not partition the card.'; }
fi
BOOT_PART=${DISK}s1
DATA_PART=${DISK}s2

# 3. The boot mark: "BOOTDISK" at byte 0x40 of the FAT16 boot sector, where
# the G12 looks for it (CHDK's mark_filesystem_bootable does the same).
say ''; say 'Marking the card bootable...'
diskutil unmount "$BOOT_PART" >/dev/null 2>&1 || die "cannot unmount $BOOT_PART"
RAW=/dev/r$BOOT_PART
SUDO=''
if [ ! -w "$RAW" ] || [ ! -r "$RAW" ]; then
    SUDO=sudo
    say 'macOS asks for your password (the one you log in with) to write the boot mark.'
fi
$SUDO dd if="$RAW" of="$TMP/boot.bin" bs=512 count=1 2>/dev/null || die 'cannot read the boot sector.'
[ "$(dd if="$TMP/boot.bin" bs=1 skip=54 count=5 2>/dev/null)" = FAT16 ] &&
    [ "$(xxd -p -s 510 -l 2 "$TMP/boot.bin")" = 55aa ] ||
    die 'the first partition is not FAT16 as expected; nothing was written.'
{ head -c 64 "$TMP/boot.bin"; printf BOOTDISK; tail -c +73 "$TMP/boot.bin"; } >"$TMP/bootdisk.bin"
[ "$(wc -c <"$TMP/bootdisk.bin" | tr -d ' ')" = 512 ] || die 'internal error building the boot sector.'
if ! cmp -s "$TMP/boot.bin" "$TMP/bootdisk.bin"; then
    $SUDO dd if="$TMP/bootdisk.bin" of="$RAW" bs=512 count=1 conv=notrunc 2>/dev/null ||
        die 'cannot write the boot mark.'
fi
$SUDO dd if="$RAW" of="$TMP/check.bin" bs=512 count=1 2>/dev/null
cmp -s "$TMP/check.bin" "$TMP/bootdisk.bin" || die 'the boot mark did not stick.'
diskutil mount "$BOOT_PART" >/dev/null 2>&1 || die "cannot mount $BOOT_PART again."

# 4. The files: DISKBOOT.BIN on both partitions, the CHDK folder on the
# second. Only the files listed in CARD.SHA256, without macOS's extra
# attributes (which would land on the card as ._ files).
mount_point() { disk_info "$1"; disk_get "$1" MountPoint; }
BOOT=$(mount_point "$BOOT_PART")
DATA=$(mount_point "$DATA_PART")
[ -n "$BOOT" ] && [ -d "$BOOT" ] || { diskutil mount "$BOOT_PART" >/dev/null 2>&1; BOOT=$(mount_point "$BOOT_PART"); }
[ -n "$DATA" ] && [ -d "$DATA" ] || { diskutil mount "$DATA_PART" >/dev/null 2>&1; DATA=$(mount_point "$DATA_PART"); }
[ -n "$BOOT" ] && [ -n "$DATA" ] || die 'cannot find the card partitions mounted.'

say ''; say "Copying Zrno to the card..."
touch "$DATA/.metadata_never_index" "$BOOT/.metadata_never_index" 2>/dev/null
cp -X DISKBOOT.BIN "$BOOT/DISKBOOT.BIN" || die 'cannot copy DISKBOOT.BIN (if macOS asked whether Terminal may use removable volumes, allow it and run the installer again).'
while read -r hash name; do
    mkdir -p "$DATA/$(dirname "$name")" && cp -X "$name" "$DATA/$name" || die "cannot copy $name"
done <CARD.SHA256
sync

say 'Checking the copies...'
(cd "$DATA" && shasum -a 256 -c -s "$HERE/CARD.SHA256") || die 'a file on the card differs from the release. Run the installer again.'
grep ' DISKBOOT.BIN$' CARD.SHA256 | (cd "$BOOT" && shasum -a 256 -c -s) || die 'DISKBOOT.BIN on the boot partition differs. Run the installer again.'
find "$BOOT" "$DATA" -name '._*' -delete 2>/dev/null

diskutil eject "$DISK" >/dev/null 2>&1 || say "(The card could not be ejected: eject $DATA_LABEL in Finder before taking it out.)"

say ''
say 'Done. Zrno is on the card.'
say ''
say '  1. Take the card out.'
say '  2. Slide its LOCK switch down, to LOCK. The G12 starts Zrno only from'
say '     a locked card; the camera can still take photos onto it.'
say '  3. Put it in the camera and switch on. The S button opens the Zrno menu.'
say '     Processing is Off until you switch it on there.'
say ''
say 'An unlocked card starts the camera without Zrno, and Canon'"'"'s own firmware'
say "then sees only the small $BOOT_LABEL partition."
finish 0
