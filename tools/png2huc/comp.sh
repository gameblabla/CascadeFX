#!/bin/sh
./png2huc background256.png bg.bin bg.pal
./png2huc textures.png textures.bin textures.pal
./png2huc title.png title.bin title.pal

bin2c -w bg.bin src/bg.h bg_game
bin2c -w title.bin src/title.h bg_title
bin2c -w textures.bin src/textures.h texture
bin2c -w title.pal src/gamepal.h gamepal
