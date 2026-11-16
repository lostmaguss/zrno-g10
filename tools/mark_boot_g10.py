# SPDX-License-Identifier: GPL-2.0-or-later
# Magus (Windows): make an SD card's FAT16 boot partition (label ZRNO_BOOT)
# bootable for CHDK on the G10: write "BOOTDISK" at byte 0x40 of its boot
# sector, as CHDK's tools do. Run as administrator:
#     python mark_boot_g10.py E        (E = the drive letter of ZRNO_BOOT)
# Refuses unless the volume is FAT16, labelled ZRNO_BOOT and 20-40 MB. The
# original sector is saved next to this script first; every step is logged
# to mark_boot_g10.log.
# The original sector is saved next to this script before anything is written.
import ctypes, ctypes.wintypes as wt, os, sys, time, traceback

if len(sys.argv) != 2 or len(sys.argv[1].rstrip(':')) != 1:
    sys.exit('usage: python mark_boot_g10.py DRIVE_LETTER (the ZRNO_BOOT partition), as administrator')
DRIVE = sys.argv[1].rstrip(':').upper()
HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, 'mark_boot_g10.log')
BACKUP = os.path.join(HERE, 'bootsector-%s-backup.bin')
OFF, MARK = 0x40, b'BOOTDISK'   # FAT16: where the G10 looks (CHDK mark_filesystem_bootable)

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype = wt.HANDLE
k32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD, wt.HANDLE]
k32.DeviceIoControl.argtypes = [wt.HANDLE, wt.DWORD, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p, wt.DWORD,
                                ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
k32.ReadFile.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
k32.WriteFile.argtypes = [wt.HANDLE, ctypes.c_char_p, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
k32.SetFilePointer.argtypes = [wt.HANDLE, wt.LONG, ctypes.c_void_p, wt.DWORD]
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.FlushFileBuffers.argtypes = [wt.HANDLE]
GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
SHARE_RW, OPEN_EXISTING = 3, 3
FSCTL_LOCK_VOLUME, FSCTL_UNLOCK_VOLUME, FSCTL_DISMOUNT_VOLUME = 0x90018, 0x9001C, 0x90020
INVALID = wt.HANDLE(-1).value


def log(s):
    with open(LOG, 'a', encoding='utf-8') as f:
        f.write(time.strftime('%H:%M:%S ') + s + '\n')


def ioctl(h, code):
    n = wt.DWORD()
    return bool(k32.DeviceIoControl(h, code, None, 0, None, 0, ctypes.byref(n), None))


def read0(h):
    k32.SetFilePointer(h, 0, None, 0)
    buf = ctypes.create_string_buffer(512)
    n = wt.DWORD()
    if not k32.ReadFile(h, buf, 512, ctypes.byref(n), None) or n.value != 512:
        raise OSError(f'read failed, error {ctypes.get_last_error()}')
    return buf.raw


def main():
    log('---- start')
    root = DRIVE + ':\\'
    
    label = ctypes.create_unicode_buffer(64)
    fs = ctypes.create_unicode_buffer(64)
    k32.GetVolumeInformationW(root, label, 64, None, None, None, fs, 64)
    free, total = ctypes.c_ulonglong(), ctypes.c_ulonglong()
    k32.GetDiskFreeSpaceExW(root, None, ctypes.byref(total), ctypes.byref(free))
    log(f'label={label.value} fs={fs.value} size={total.value}')
    if label.value != 'ZRNO_BOOT' or fs.value != 'FAT' or not (20e6 < total.value < 40e6):
        raise SystemExit('not the expected boot partition (ZRNO_BOOT, FAT16, 32 MB)')

    h = k32.CreateFileW(r'\\.\%s:' % DRIVE, GENERIC_READ | GENERIC_WRITE, SHARE_RW, None, OPEN_EXISTING, 0, None)
    if h == INVALID:
        raise SystemExit(f'cannot open the volume, error {ctypes.get_last_error()} (run as administrator)')
    try:
        s = read0(h)
        log('sector 0x36: %r, 0x1FE: %s, 0x40: %s' % (s[0x36:0x3E], s[0x1FE:0x200].hex(), s[OFF:OFF + 8].hex()))
        if s[0x36:0x3E] != b'FAT16   ' or s[0x1FE:0x200] != b'\x55\xaa':
            raise SystemExit('sector 0 is not a FAT16 boot sector')
        if s[OFF:OFF + 8] == MARK:
            log('already bootable, nothing to do')
            print('ALREADY OK')
            return
        # 0x40 is in the FAT16 boot code, which the camera never runs; CHDK
        # and Zrno's installer write the mark there too.
        backup = BACKUP % DRIVE
        if not os.path.exists(backup):
            open(backup, 'wb').write(s)
            log('backup written: ' + backup)
        new = s[:OFF] + MARK + s[OFF + 8:]
        if not ioctl(h, FSCTL_LOCK_VOLUME):
            raise SystemExit(f'cannot lock the volume (close Explorer windows on D:), error {ctypes.get_last_error()}')
        try:
            k32.SetFilePointer(h, 0, None, 0)
            n = wt.DWORD()
            if not k32.WriteFile(h, new, 512, ctypes.byref(n), None) or n.value != 512:
                raise SystemExit(f'write failed, error {ctypes.get_last_error()}')
            k32.FlushFileBuffers(h)
            ioctl(h, FSCTL_DISMOUNT_VOLUME)
        finally:
            ioctl(h, FSCTL_UNLOCK_VOLUME)
    finally:
        k32.CloseHandle(h)

    h = k32.CreateFileW(r'\\.\%s:' % DRIVE, GENERIC_READ, SHARE_RW, None, OPEN_EXISTING, 0, None)
    s2 = read0(h)
    k32.CloseHandle(h)
    if s2 != new:
        raise SystemExit('verify failed: the sector does not hold the mark')
    log('verified: BOOTDISK at 0x40, rest of the sector unchanged')
    print('DONE: the card is bootable')


try:
    main()
except SystemExit as e:
    log(f'STOP: {e}')
    print(f'STOP: {e}')
except Exception:
    log('ERROR: ' + traceback.format_exc())
