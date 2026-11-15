#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Download the CHDK source Zrno builds on: release-1_6, SVN r6355.

Only the G12 port and the shared code are fetched (other cameras' platform and
loader folders and the docs are skipped). CHDK's Subversion server takes the
public guest login. Each file is checked against tools/chdk-r6355.sha256, so
the tree is exactly the one Zrno was developed and tested against.

Usage: python3 tools/fetch_chdk.py [DEST] (default build/chdk-r6355)
If the download stops, run it again: it resumes from DEST.partial.
"""
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED
from pathlib import Path
from urllib.parse import unquote
from urllib.request import Request, urlopen
import base64, hashlib, re, sys, time

BASE = 'https://subversion.assembla.com/svn/chdk/!svn/bc/6355/branches/release-1_6/'
AUTH = 'Basic ' + base64.b64encode(b'guest:guest').decode()
LAB = Path(__file__).resolve().parents[1]
KEEP = {'platform/': ('platform/g12/', 'platform/generic/'), 'loader/': ('loader/g12/', 'loader/generic/')}


def wanted(path):
    if path.startswith(('doc/', '.')):
        return False
    for top, keep in KEEP.items():
        if path.startswith(top) and path != top:
            rest = path[len(top):]
            # Files directly in platform/ or loader/ are shared; folders are cameras.
            return path.startswith(keep) or ('/' not in rest.rstrip('/') and not path.endswith('/'))
    return True


def fetch(url):
    # The server refuses connections for a while when asked too fast: back off.
    for attempt in range(8):
        try:
            return urlopen(Request(url, headers={'Authorization': AUTH}), timeout=60).read()
        except Exception:
            if attempt == 7:
                raise
            time.sleep(min(60, 2 ** attempt))


def main():
    dest = Path(sys.argv[1]) if len(sys.argv) > 1 else LAB / 'build/chdk-r6355'
    if dest.exists():
        sys.exit(f'{dest} already exists; remove it to fetch again.')
    tmp = dest.with_name(dest.name + '.partial')   # kept on failure; a rerun resumes it

    def get(path):
        if path and not wanted(path):
            return []
        if path and not path.endswith('/') and (tmp / path).is_file():
            return []
        data = fetch(BASE + path)
        if not path or path.endswith('/'):
            (tmp / path).mkdir(parents=True, exist_ok=True)
            links = re.findall(r'href="([^"]+)"', data.decode('utf-8', 'replace'))
            return [path + unquote(x) for x in links if x != '../' and not x.startswith(('/', 'http'))]
        (tmp / path).parent.mkdir(parents=True, exist_ok=True)
        part = tmp / (path + '.part')
        part.write_bytes(data)
        part.replace(tmp / path)   # whole files only, so a resume can trust them
        return []

    with ThreadPoolExecutor(max_workers=3) as pool:
        pending, done_count = {pool.submit(get, '')}, 0
        while pending:
            done, pending = wait(pending, return_when=FIRST_COMPLETED)
            for f in done:
                pending |= {pool.submit(get, p) for p in f.result()}
                done_count += 1
                if done_count % 100 == 0:
                    print(done_count, 'entries', flush=True)

    expected = {}
    for line in (LAB / 'tools/chdk-r6355.sha256').read_text().splitlines():
        digest, name = line.split('  ', 1)
        expected[name] = digest
    got = {p.relative_to(tmp).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
           for p in tmp.rglob('*') if p.is_file()}
    bad = sorted(n for n in expected if got.get(n) != expected[n])
    if bad:
        sys.exit(f'{len(bad)} files missing or different from r6355, e.g. {bad[:5]}; kept in {tmp}')
    tmp.rename(dest)
    print(f'CHDK release-1_6 r6355: {len(got)} files in {dest}')


if __name__ == '__main__':
    main()
