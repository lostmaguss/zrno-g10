#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""G12 1.00G Zrno card updater, for development (a release installs with
installer/install.command). Dry run unless --apply; backs up every replaced file.

Data files (LUTs, fonts, tables) are listed in card-manifest.json. The firmware
(DISKBOOT.BIN and CHDK's modules) is built locally by tools/build_firmware.py,
which lists it in build/firmware/manifest.json."""
import argparse,hashlib,json,os,shutil,tempfile,time
from pathlib import Path
LAB=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def atomic(src,dst):
 dst.parent.mkdir(parents=True,exist_ok=True)
 fd,name=tempfile.mkstemp(prefix='FILM_',suffix='.TMP',dir=dst.parent)
 try:
  with os.fdopen(fd,'wb') as f:f.write(src.read_bytes());f.flush();os.fsync(f.fileno())
  os.replace(name,dst)
 finally:
  if Path(name).exists():Path(name).unlink()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--apply',action='store_true');p.add_argument('--diagnostic-only',action='store_true');p.add_argument('--restore',type=Path);p.add_argument('--luts-only',action='store_true',help='Update changed LUT data/catalogue only; preserve boot files, modules and settings')
 p.add_argument('--any-boot',action='store_true',help='Replace a DISKBOOT.BIN the installer does not recognise (it is backed up first)')
 p.add_argument('--enable-native',action='store_true',help='Enable native C2 film capture and Canon JPEG encoding')
 p.add_argument('--enable-stage-hook',action='store_true',help='Enable before/after encoding observations in the diagnostic task build')
 p.add_argument('--enable-lut-patch',action='store_true',help='Enable full-frame UYVY 3D LUT processing')
 a=p.parse_args()
 # A card set up by installer/install.command (ZRNO_BOOT, ZRNO) or by CHDK's tools (CHDK_BOOT, CANON_DATA).
 boot,data=next(((Path('/Volumes/'+b),Path('/Volumes/'+d)) for b,d in [('ZRNO_BOOT','ZRNO'),('CHDK_BOOT','CANON_DATA')] if Path('/Volumes/'+b).is_mount() and Path('/Volumes/'+d).is_mount()),(None,None))
 if not boot:raise SystemExit('Mount both card partitions: ZRNO_BOOT and ZRNO, or CHDK_BOOT and CANON_DATA.')
 if a.apply:
  for volume in [boot,data]:
   if os.statvfs(volume).f_flag & os.ST_RDONLY:
    raise SystemExit('Card is read-only. Eject, unlock its physical switch, and reinsert: '+str(volume))
 if a.restore:
  records=json.loads((a.restore/'INSTALL.json').read_text())
  for item in records:
   dst=Path(item['destination'])
   if not any(dst.is_relative_to(v) for v in [boot,data]):raise SystemExit('Unexpected restore path')
   print('Restore',dst)
   if a.apply:
    if item['backup']:atomic(a.restore/item['backup'],dst)
    elif dst.exists():dst.unlink()
  return
 if a.luts_only and (a.diagnostic_only or a.enable_native or a.enable_stage_hook or a.enable_lut_patch):
  raise SystemExit('--luts-only cannot be combined with diagnostic/enable flags')
 manifest=json.loads((LAB/'card-manifest.json').read_text())
 firmware=LAB/'build/firmware/manifest.json'
 if firmware.is_file():
  built=json.loads(firmware.read_text());manifest['files'].update(built['files'])
  manifest.setdefault('previous_boot_sha256',[]).extend(built.get('boot_history',[]))
 if 'DISKBOOT.BIN' not in manifest['files'] and not (a.luts_only or a.diagnostic_only):
  raise SystemExit('No firmware built yet: run tools/build_firmware.py first (docs/BUILDING.md).')
 if a.apply and not a.diagnostic_only and manifest.get('withdrawn_reason'):
  raise SystemExit('Build withdrawn: '+manifest['withdrawn_reason'])
 for name,h in manifest['files'].items():
  if sha(LAB/'card'/name)!=h:raise SystemExit('Package hash mismatch: '+name)
 allowed=[manifest['expected_stock_boot_sha256'],manifest['files'].get('DISKBOOT.BIN')]+manifest.get('previous_boot_sha256',[])
 for v in [boot,data]:
  if not a.any_boot and (not (v/'DISKBOOT.BIN').is_file() or sha(v/'DISKBOOT.BIN') not in allowed):
   raise SystemExit('Unexpected existing firmware on '+str(v)+' (add --any-boot to replace it; it is backed up first)')
 plan=[]
 names=list(manifest['files'])
 if a.luts_only:
  names=[n for n in names if n.startswith('CHDK/FILM/LUTS/') or n in ['CHDK/FILM/LOOK.LUT','CHDK/FILM/CREDITS.TXT','CHDK/FILM/LUTS.TXT']]
  # Publish the new catalogue only after all referenced LUTs are in place.
  names.sort(key=lambda n:(n.endswith('/LUTS.TXT'),n))
 for name in names:
  if a.luts_only and (data/name).is_file() and sha(data/name)==manifest['files'][name]:continue
  if a.diagnostic_only and name!='CHDK/SCRIPTS/FILMDIAG.LUA':continue
  src=LAB/'card'/name
  if name=='DISKBOOT.BIN':plan.append((src,boot/name))
  plan.append((src,data/name))
 if a.enable_native:
  if a.diagnostic_only:raise SystemExit('Cannot enable native with diagnostic-only')
  plan.append((LAB/'tools/markers/raw-enable-marker.txt',data/'CHDK/FILM/RAWGOLD.TXT'))
 if a.enable_stage_hook:
  if a.diagnostic_only:raise SystemExit('Cannot enable stage hook with diagnostic-only')
  plan.append((LAB/'tools/markers/stage-enable-marker.txt',data/'CHDK/FILM/STAGE.TXT'))
 if a.enable_lut_patch:
  if a.diagnostic_only:raise SystemExit('Cannot enable LUT patch with diagnostic-only')
  plan.append((LAB/'tools/markers/lut-patch-marker.txt',data/'CHDK/FILM/LUTPATCH.TXT'))
 if not a.diagnostic_only and not a.luts_only:
  for name in manifest.get('remove',[]):
   relative=Path(name)
   if relative.is_absolute() or '..' in relative.parts or name in manifest['files']:
    raise SystemExit('Invalid obsolete file path: '+name)
   plan.append((None,data/relative))
 for src,dst in plan:print(str(dst),str(src.stat().st_size)+' bytes' if src else 'remove obsolete file (backed up)')
 if not a.apply:print('Dry run: no card writes. Add --apply to install.');return
 if not plan:print('Already current; no card writes needed.');return
 backup=Path(tempfile.mkdtemp(prefix='card-install-'+time.strftime('%Y%m%d-%H%M%S')+'-',dir=LAB))
 records=[]
 # Complete backups before starting any writes.
 for i,(src,dst) in enumerate(plan):
  b=str(i)+'.original' if dst.exists() else None
  if b:shutil.copyfile(dst,backup/b)
  records.append({'destination':str(dst),'backup':b,'new_sha256':sha(src) if src else None})
 (backup/'INSTALL.json').write_text(json.dumps(records,indent=2)+'\n')
 config=data/'CHDK/CCHDK4.CFG';date_script=data/'CHDK/SCRIPTS/SKIPDATE.LUA'
 untouched={p:sha(p) for p in [config,date_script] if p.is_file()}   # a fresh card has neither
 for pref in (data/'CHDK/FILM').glob('LUTSEL?.CFG'):untouched[pref]=sha(pref)
 for pref in (data/'CHDK/FILM').glob('FILMSEL?.CFG'):untouched[pref]=sha(pref)
 for pref in (data/'CHDK/FILM').glob('FXSEL?.CFG'):untouched[pref]=sha(pref)
 for pref in (data/'CHDK/FILM').glob('LOOKS.TXT'):untouched[pref]=sha(pref)   # saved looks
 changed=[]
 try:
  for (src,dst),record in zip(plan,records):
   if src:atomic(src,dst)
   elif dst.exists():dst.unlink()
   changed.append(record)
   if (sha(dst)!=record['new_sha256']) if src else dst.exists():
    raise RuntimeError('Verification failed: '+str(dst))
  if any(sha(p)!=h for p,h in untouched.items()):raise RuntimeError('Existing configuration changed during install')
 except BaseException:
  rollback_errors=[]
  for record in reversed(changed):
   dst=Path(record['destination'])
   try:
    if record['backup']:atomic(backup/record['backup'],dst)
    elif dst.exists():dst.unlink()
   except OSError as error:rollback_errors.append(str(dst)+': '+str(error))
  if rollback_errors:print('Rollback incomplete; use backup',backup,rollback_errors)
  raise
 print('Installed and verified. Backup:',backup)
 print('Processing switched on (RAWGOLD.TXT):',(data/'CHDK/FILM/RAWGOLD.TXT').exists())
 print('Looks and effects switched on (LUTPATCH.TXT):',(data/'CHDK/FILM/LUTPATCH.TXT').exists())
 print('Existing settings and SKIPDATE.LUA are unchanged. Eject the card before camera use.')
if __name__=='__main__':main()
