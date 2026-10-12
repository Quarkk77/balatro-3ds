#!/bin/bash
# run_emu.sh - run the 3DS build in Azahar with debug hooks and tile the screenshots.
#   tools/3ds_test/run_emu.sh '<debug.txt contents, \n separated>' <seconds> <out.png>
# Env: NOTUT=1 starts from a save with the tutorial completed; NOASSETS=1 runs without
# Balatro's files (otherwise they are installed with src_install/install_balatro_c.sh).
# Wipes the test save folder in Azahar's sdmc first. debug.txt keys: see
# src_3ds/PORTING_NOTES.md ("Debug / test hooks"). Screenshots: shot_NN.png (top)
# and shot_NN_bottom.png in the sdmc folder; the tiled image shows top screens.
export LD_PRELOAD=
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
SD=~/.local/share/azahar-emu/sdmc/3ds/balatro_c
mkdir -p $SD; rm -rf $SD/shot_*.png $SD/log.txt $SD/1 $SD/2 $SD/3 $SD/*.jkr
# the game's own files, as the release installer puts them (NOASSETS=1: leave them out)
if [ -n "$NOASSETS" ]; then rm -rf $SD/assets
elif [ ! -f $SD/assets/balatro_c_assets.txt ]; then
    bash "$repo/src_install/install_balatro_c.sh" --sd "$(dirname "$(dirname "$SD")")" --no-3dsx -y >/dev/null || exit 1
fi
printf "%b" "$1" > $SD/debug.txt
[ -n "$NOTUT" ] && cp "$here/settings_notut.jkr" $SD/settings.jkr
azahar "$repo/build_3ds/balatro_c.3dsx" >/dev/null 2>&1 &
PID=$!
sleep "$2"
kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null
python3 - "$3" <<'PY'
import sys,glob
from PIL import Image
fs=sorted(f for f in glob.glob('/home/springaphex/.local/share/azahar-emu/sdmc/3ds/balatro_c/shot_*.png') if 'bottom' not in f)
if not fs: print('no shots'); sys.exit()
rows=(len(fs)+1)//2
o=Image.new('RGB',(800,240*rows))
for i,f in enumerate(fs): o.paste(Image.open(f),((i%2)*400,(i//2)*240))
o.save(sys.argv[1]); print(len(fs),'shots')
PY
tail -5 $SD/log.txt
