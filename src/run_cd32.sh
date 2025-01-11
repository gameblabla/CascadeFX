#!/bin/sh
rm -f cdda_cd32/Game/game
cp -p game cdda_cd32/Game/game
mkisofs -quiet -V POPCD32NTSC -copyright na -publisher na -o cdda.raw -relaxed-filenames -d -input-charset ASCII -output-charset ASCII -iso-level 3 -A "" -sysid CDTV cdda_cd32
python2 make_cd32_iso.py -t $PWD/CD32.TM cdda.raw
rm cdda.raw
fs-uae --amiga-model=CD32 --cdrom-drive-0="cdda.cue"
