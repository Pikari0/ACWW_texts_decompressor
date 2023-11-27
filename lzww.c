/*----------------------------------------------------------------------------*/
/*--  lzww.c - LZSS decoding for Animal Crossing: Wild World                --*/
/*--  Copyright (C) 2023 CUE                                                --*/
/*--                                                                        --*/
/*--  This program is free software: you can redistribute it and/or modify  --*/
/*--  it under the terms of the GNU General Public License as published by  --*/
/*--  the Free Software Foundation, either version 3 of the License, or     --*/
/*--  (at your option) any later version.                                   --*/
/*--                                                                        --*/
/*--  This program is distributed in the hope that it will be useful,       --*/
/*--  but WITHOUT ANY WARRANTY; without even the implied warranty of        --*/
/*--  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          --*/
/*--  GNU General Public License for more details.                          --*/
/*--                                                                        --*/
/*--  You should have received a copy of the GNU General Public License     --*/
/*--  along with this program. If not, see <http://www.gnu.org/licenses/>.  --*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

/*----------------------------------------------------------------------------*/
#define CMD_DECODE    0x00       // decode
#define CMD_CODE_10   0x10       // LZSS magic number
#define CMD_CODE_4C   0x4C       // LZSS magic number

#define LZS_NORMAL    0x00       // normal mode, (0)
#define LZS_FAST      0x80       // fast mode, (1 << 7)
#define LZS_BEST      0x40       // best mode, (1 << 6)

#define LZS_WRAM      0x00       // VRAM not compatible (LZS_WRAM | LZS_NORMAL)
#define LZS_VRAM      0x01       // VRAM compatible (LZS_VRAM | LZS_NORMAL)
#define LZS_WFAST     0x80       // LZS_WRAM fast (LZS_WRAM | LZS_FAST)
#define LZS_VFAST     0x81       // LZS_VRAM fast (LZS_VRAM | LZS_FAST)
#define LZS_WBEST     0x40       // LZS_WRAM best (LZS_WRAM | LZS_BEST)
#define LZS_VBEST     0x41       // LZS_VRAM best (LZS_VRAM | LZS_BEST)

#define LZS_SHIFT     1          // bits to shift
#define LZS_MASK      0x80       // bits to check:
                                 // ((((1 << LZS_SHIFT) - 1) << (8 - LZS_SHIFT)

#define LZS_THRESHOLD 2          // max number of bytes to not encode
#define LZS_N         0x1000     // max offset (1 << 12)
#define LZS_F         0x12       // max coded ((1 << 4) + LZS_THRESHOLD)
#define LZS_NIL       LZS_N      // index for root of binary search trees

#define RAW_MINIM     0x00000000 // empty file, 0 bytes
#define RAW_MAXIM     0x00FFFFFF // 3-bytes length, 16MB - 1

#define LZS_MINIM     0x00000004 // header only (empty RAW file)
#define LZS_MAXIM     0x01400000 // 0x01200003, padded to 20MB:
                                 // * header, 4
                                 // * length, RAW_MAXIM
                                 // * flags, (RAW_MAXIM + 7) / 8
                                 // 4 + 0x00FFFFFF + 0x00200000 + padding

/*----------------------------------------------------------------------------*/
unsigned char ring[LZS_N + LZS_F - 1];
int           dad[LZS_N + 1], lson[LZS_N + 1], rson[LZS_N + 1 + 256];
int           pos_ring, len_ring, lzs_vram;

/*----------------------------------------------------------------------------*/
#define BREAK(text) { printf(text); return; }
#define EXIT(text)  { printf(text); exit(-1); }

/*----------------------------------------------------------------------------*/
void  Usage(void);

char *Load(char *filename, int *length, int min, int max);
void  Save(char *filename, char *buffer, int length);
char *Memory(int length, int size);

void  LZS_Decode(char *filename);

/*----------------------------------------------------------------------------*/
int main(int argc, char **argv) {
  int cmd, mode;
  int arg;

  if (argc < 2) Usage();
  if      (!strcasecmp(argv[1], "-d"))   { cmd = CMD_DECODE; }
  else                                  EXIT("Command not supported\n");
  if (argc < 3) EXIT("Filename not specified\n");

  switch (cmd) {
    case CMD_DECODE:
      for (arg = 2; arg < argc; arg++) LZS_Decode(argv[arg]);
      break;
    default:
      break;
  }

  return(0);
}

/*----------------------------------------------------------------------------*/
void Usage(void) {
  EXIT(
    "Usage: LZSS command filename [filename [...]]\n"
    "\n"
    "command:\n"
    "  -d ..... decode 'filename'\n"
    "\n"
    "* multiple filenames and wildcards are permitted\n"
    "* the original file is overwritten with the new file\n"
  );
}

/*----------------------------------------------------------------------------*/
char *Load(char *filename, int *length, int min, int max) {
  FILE *fp;
  int   fs;
  char *fb;

  if ((fp = fopen(filename, "rb")) == NULL) EXIT("\nFile open error\n");
  fseek(fp, 0, SEEK_END);
  fs = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if ((fs < min) || (fs > max)) EXIT("\nFile size error\n");
  fb = Memory(fs + 3, sizeof(char));
  if (fread(fb, 1, fs, fp) != fs) EXIT("\nFile read error\n");
  if (fclose(fp) == EOF) EXIT("\nFile close error\n");

  *length = fs;

  return(fb);
}

/*----------------------------------------------------------------------------*/
void Save(char *filename, char *buffer, int length) {
  FILE *fp;

  if ((fp = fopen(filename, "ab")) == NULL) EXIT("\nFile create error\n");
  if (fwrite(buffer, 1, length, fp) != length) EXIT("\nFile write error\n");
  if (fclose(fp) == EOF) EXIT("\nFile close error\n");
}

/*----------------------------------------------------------------------------*/
void Empty(char *filename) {
  FILE *fp;

  if ((fp = fopen(filename, "wb")) == NULL) EXIT("\nFile create error\n");
  if (fclose(fp) == EOF) EXIT("\nFile close error\n");
}

/*----------------------------------------------------------------------------*/
char *Memory(int length, int size) {
  char *fb;

  fb = (char *) calloc(length, size);
  if (fb == NULL) EXIT("\nMemory error\n");

  return(fb);
}

/*----------------------------------------------------------------------------*/
void LZS_Decode(char *filename) {
  unsigned char *pak_buffer, *begin_buffer, *pak, *pak_end;
  unsigned int   pak_len, header, lz_header, len, pos;
  unsigned char  flags, mask;
  
  // buffer
  unsigned char *raw_buffer, *raw, *raw_end;
  unsigned int raw_len;
  
  //printf("- decoding '%s'", filename);

  pak_buffer = Load(filename, &pak_len, LZS_MINIM, LZS_MAXIM);

  header = *pak_buffer;
  if (header != CMD_CODE_4C) {
    printf("'%s'", filename);
    EXIT(", WARNING: file is not a Wild World text!\n");
  }

  begin_buffer = pak_buffer + 10;
  
  lz_header = *begin_buffer;
  int counter = 0;
  while(lz_header != CMD_CODE_10)
  {
    begin_buffer += 2;
    lz_header = *begin_buffer;
    counter++;
    if(counter > 20)
    {
      free(pak_buffer);
      printf("'%s'", filename);
      EXIT(", WARNING: file is not LZSS encoded!\n");
    }
  }
  
  Empty(filename);
  
  pak = begin_buffer;
  
  short ended = 0;
  unsigned int final_len = 0;
  while(!ended)
  {
    // Alloue la mémoire nécéssaire suivant la taille lue
    raw_len = *(unsigned int *)pak >> 8;
    raw_buffer = (unsigned char *) Memory(raw_len, sizeof(char));

    pak += 4;
    raw = raw_buffer;
    pak_end = pak_buffer + pak_len;
    raw_end = raw_buffer + raw_len;

    final_len += raw_len;
    
    mask = 0;

    while (raw < raw_end) {
      if (!(mask >>= LZS_SHIFT)) {
        if (pak == pak_end) break;
        flags = *pak++;
        //printf("%02x ", *pak);
        mask = LZS_MASK;
      }

      if (!(flags & mask)) {
        if (pak == pak_end) break;
        *raw++ = *pak++;
        //printf("%02x ", *pak);
      } else {
        if (pak + 1 >= pak_end) break;
        pos = *pak++;
        //printf("%02x ", *pak);
        pos = (pos << 8) | *pak++;
        //printf("%02x ", *pak);
        len = (pos >> 12) + LZS_THRESHOLD + 1;
        if (raw + len > raw_end) {
          printf("'%s'", filename);
          printf(", WARNING: wrong decoded length!");
          len = raw_end - raw;
        }
        pos = (pos & 0xFFF) + 1;
        while (len--) *raw++ = *(raw - pos);
      }
    }

    raw_len = raw - raw_buffer;
    
    if (raw != raw_end) {
      printf("'%s'", filename); 
      printf(", WARNING: unexpected end of encoded file!");
    }
      
    Save(filename, raw_buffer, raw_len);
    
    lz_header = *(pak);
    if(lz_header != CMD_CODE_10) {
      ended = 1;
    }
  }

  //printf(", decoded length: %d", final_len);
  
  lz_header = *(pak);
  if(lz_header == CMD_CODE_10) {
    printf("'%s'", filename);
    printf(", WARNING: there is more to decode!\n");
  }
  
  free(raw_buffer);
  free(pak_buffer);
}

/*----------------------------------------------------------------------------*/
/*--  EOF                                           Copyright (C) 2011 CUE  --*/
/*----------------------------------------------------------------------------*/
