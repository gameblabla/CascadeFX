#!/bin/sh
./convimg background2.png bg.bin bg.pal
./convimg textures.png textures.bin textures.pal
./convimg title.png title.bin title.pal

# PC-FX samples texture indices as bytes. Keep the PNG's source byte order in
# a signed-byte array so V810 can use LD.B without runtime endian selection or
# unsigned-load zero extension.
./convimg textures.png textures_pcfx.bin textures.pal no_swap
python3 tools/texture_to_c.py textures_pcfx.bin src/textures_pcfx.h

bin2c -w bg.bin src/bg.h bg_game
bin2c -w title.bin src/title.h bg_title
bin2c -w textures.bin src/textures.h texture
bin2c -w title.pal src/gamepal.h gamepal
