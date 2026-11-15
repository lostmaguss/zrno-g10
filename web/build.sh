#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds zrno.wasm and assembles the browser tool in web/dist:
# index.html, worker.js, usb.html, ptp.js, zrno.wasm and luts.json (the 16 looks from card/CHDK/FILM/LUTS).
set -e
cd "$(dirname "$0")"
cargo build --release --target wasm32-unknown-unknown
target="${CARGO_TARGET_DIR:-target}"
rm -rf dist && mkdir -p dist
cp site/index.html site/worker.js site/usb.html site/ptp.js dist/
cp ../card/CHDK/FILM/PRINT.BIN dist/print.bin
cp "$target/wasm32-unknown-unknown/release/zrno_wasm.wasm" dist/zrno.wasm
# The card's looks as one JSON file of base64 (hosts serve .json everywhere).
python3 -c 'import base64,glob,json,os,sys; json.dump({os.path.basename(f): base64.b64encode(open(f,"rb").read()).decode() for f in sorted(glob.glob("../card/CHDK/FILM/LUTS/*.LUT"))}, open("dist/luts.json","w"))'
# The camera menu font's character widths, so look names can be kept to
# what fits the Look row (CHDK RBF header: charFirst at 88, charLast at 92,
# width table offset at 100).
python3 -c 'import json,struct; d=open("../card/CHDK/FILM/MENU.RBF","rb").read(); first,last=struct.unpack_from("<2i",d,88); at=struct.unpack_from("<i",d,100)[0]; json.dump({"first":first,"widths":list(d[at:at+last-first+1])}, open("dist/menufont.json","w"))'
ls -la dist
