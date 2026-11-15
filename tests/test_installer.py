# SPDX-License-Identifier: GPL-2.0-or-later
"""Sandboxed install/removal/restore and rollback after a deletion failure."""
from pathlib import Path
import contextlib
import hashlib
import importlib.util
import io
import json
import sys
import tempfile
from unittest.mock import patch
lab=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('installer',lab/'tools/install_card.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)
def sha(data):return hashlib.sha256(data).hexdigest()
with tempfile.TemporaryDirectory() as directory:
 root=Path(directory);package=root/'package';boot=root/'boot';data=root/'data'
 for volume in (boot,data):volume.mkdir();(volume/'DISKBOOT.BIN').write_bytes(b'old boot')
 (package/'card').mkdir(parents=True);(package/'card/DISKBOOT.BIN').write_bytes(b'new boot')
 files={'CHDK/FILM/LUTS/GOLD17.LUT':b'warm'}
 for name,content in files.items():
  p=package/'card'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(content)
 original={'CHDK/MODULES/old.flt':b'legacy codec','CHDK/FILM/ENABLE.TXT':b'legacy flag',
 'CHDK/CCHDK4.CFG':b'user config','CHDK/SCRIPTS/SKIPDATE.LUA':b'date script',
 'CHDK/FILM/FXSEL0.CFG':b'saved bloom tuple','CHDK/FILM/FILMSEL0.CFG':b'saved look+effects','CHDK/FILM/LUTSEL0.CFG':b'legacy preferences'}
 for name,content in original.items():
  p=data/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(content)
 manifest={'files':{'DISKBOOT.BIN':sha(b'new boot'),**{n:sha(c) for n,c in files.items()}},
 'expected_stock_boot_sha256':sha(b'old boot'),'remove':['CHDK/MODULES/old.flt','CHDK/FILM/ENABLE.TXT']}
 (package/'card-manifest.json').write_text(json.dumps(manifest))
 def mapped(value):
  return {'/Volumes/CHDK_BOOT':boot,'/Volumes/CANON_DATA':data}.get(str(value),Path(value))
 def invoke(*args):
  with patch.object(installer,'LAB',package),patch.object(installer,'Path',mapped),patch.object(Path,'is_mount',lambda self:self in (boot,data)),patch.object(sys,'argv',['install_card.py',*args]),contextlib.redirect_stdout(io.StringIO()):
   installer.main()
 invoke();assert not list(package.glob('card-install-*')) and (boot/'DISKBOOT.BIN').read_bytes()==b'old boot'
 invoke('--apply')
 backup=next(package.glob('card-install-*'))
 records=json.loads((backup/'INSTALL.json').read_text())
 assert len(records)==5 and sum(r['new_sha256'] is None for r in records)==2
 assert (boot/'DISKBOOT.BIN').read_bytes()==b'new boot' and (data/'DISKBOOT.BIN').read_bytes()==b'new boot'
 assert all(not (data/n).exists() for n in manifest['remove'])
 assert all((data/n).read_bytes()==v for n,v in original.items() if n not in manifest['remove'])
 invoke('--apply','--restore',str(backup))
 assert all((data/n).read_bytes()==v for n,v in original.items())
 assert not (data/'CHDK/FILM/LUTS/GOLD17.LUT').exists()
 real_unlink=Path.unlink;failed=[False]
 def failing_unlink(path,*args,**kwargs):
  if path==data/'CHDK/FILM/ENABLE.TXT' and not failed[0]:
   failed[0]=True;raise OSError('injected obsolete-marker removal failure')
  return real_unlink(path,*args,**kwargs)
 with patch.object(Path,'unlink',failing_unlink):
  try:invoke('--apply')
  except OSError:pass
  else:raise AssertionError('expected injected failure')
 assert failed[0]
 assert all((v/'DISKBOOT.BIN').read_bytes()==b'old boot' for v in (boot,data))
 assert all((data/n).read_bytes()==v for n,v in original.items())
 assert not (data/'CHDK/FILM/LUTS/GOLD17.LUT').exists()
 # LUT-only install must leave firmware/modules/legacy flags in place.
 for name,content in {'CHDK/FILM/CREDITS.TXT':b'attribution','CHDK/FILM/LUTS.TXT':b'GOLD17.LUT|Warm 200\n'}.items():
  source=package/'card'/name;source.write_bytes(content);manifest['files'][name]=sha(content)
 (package/'card-manifest.json').write_text(json.dumps(manifest))
 before_boot={v:((v/'DISKBOOT.BIN').read_bytes(),(v/'DISKBOOT.BIN').stat().st_mtime_ns) for v in (boot,data)}
 atomic_real=installer.atomic;copied=[]
 def tracking_atomic(src,dst):copied.append(dst);return atomic_real(src,dst)
 with patch.object(installer,'atomic',tracking_atomic):invoke('--apply','--luts-only')
 assert len(copied)==3 and copied[-1]==data/'CHDK/FILM/LUTS.TXT'
 assert all((v/'DISKBOOT.BIN').read_bytes()==content and (v/'DISKBOOT.BIN').stat().st_mtime_ns==stamp for v,(content,stamp) in before_boot.items())
 assert all((data/n).read_bytes()==v for n,v in original.items())
 assert all((data/n).read_bytes()==(package/'card'/n).read_bytes() for n in manifest['files'] if n.startswith('CHDK/FILM/'))
 backup_count=len(list(package.glob('card-install-*')))
 with patch.object(installer,'atomic',tracking_atomic):invoke('--apply','--luts-only')
 assert len(copied)==3 and len(list(package.glob('card-install-*')))==backup_count
 # The firmware comes from build/firmware/manifest.json once it is built.
 del manifest['files']['DISKBOOT.BIN'];(package/'card-manifest.json').write_text(json.dumps(manifest))
 try:invoke('--apply')
 except SystemExit as error:assert 'build_firmware' in str(error)
 else:raise AssertionError('expected a missing-firmware stop')
 firmware=package/'build/firmware';firmware.mkdir(parents=True)
 (firmware/'manifest.json').write_text(json.dumps({'files':{'DISKBOOT.BIN':sha(b'new boot')},'boot_history':[]}))
 invoke('--apply')
 assert all((v/'DISKBOOT.BIN').read_bytes()==b'new boot' for v in (boot,data))
 # A card set up by installer/install.command has no CHDK config or date script yet.
 for name in ('CHDK/CCHDK4.CFG','CHDK/SCRIPTS/SKIPDATE.LUA'):(data/name).unlink()
 invoke('--apply')
 assert not (data/'CHDK/CCHDK4.CFG').exists()
print('Installer: dry run, full install, obsolete-file backup/removal, rollback/restore, LUT-only catalogue-last update, unchanged firmware/settings, no-op reinstall, built-firmware manifest and a fresh card without CHDK settings passed')
