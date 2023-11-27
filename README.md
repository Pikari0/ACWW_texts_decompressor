# ACWW_texts_decompressor

A tool to decompress texts extracted from an Animal Crossing Wild World ROM.

Compilation : `gcc lzww.c -o lzww`

Usage : `./lzww -d text.bmg` /!\ it will overwrite the file

Also there is a script to decompress all the files recursively from a root folder : `./recurse_lzww.sh [folder]`

This tool is heavily inspired by theses ones made by PeterLemon: https://github.com/PeterLemon/Nintendo_DS_Compressors.
I forked lzss.c, thank you PerterLemon for your LZ77 decompression implementation.
