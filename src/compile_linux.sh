#!/bin/sh
gcc -DPLATFORM="2" -I/usr/include/SDL -Isrc -Iinclude src/main.c src/common.c src/font_drawing_pc.c -lSDL -lSDL_image