/*
 * Copyright (C) 2006-2011, Mark Grebe
 * Copyright (C) 2018-2026, Jens Guenther
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include "error.h"
#include "trs_state_save.h"

static const char stateFileBanner[] = "SDLTRS State Save File";
static int const stateFileBannerLen = sizeof(stateFileBanner) - 1;
static unsigned const stateVersionNumber = 15;

int trs_state_save(const char *filename)
{
  FILE *file = fopen(filename, "wb");

  if (file) {
    trs_save_uint8(file, (Uint8 *)stateFileBanner, stateFileBannerLen);
    trs_save_uint32(file, &stateVersionNumber);
    trs_screen_save(file);
    trs_cassette_save(file);
    trs_clone_save(file);
    trs_cp500_save(file);
    trs_disk_save(file);
    trs_hard_save(file);
    trs_omti_save(file);
    trs_stringy_save(file);
    trs_interrupt_save(file);
    trs_io_save(file);
    trs_mem_save(file);
    trs_keyboard_save(file);
    trs_uart_save(file);
    trs_z80_save(file);
    trs_imp_exp_save(file);

    fclose(file);
    return 0;
  }

  file_error("save State: '%s'", filename);
  return -1;
}

int trs_state_load(const char *filename)
{
  FILE *file = fopen(filename, "rb");

  if (file) {
    char banner[80];
    unsigned int version;

    trs_load_uint8(file, (Uint8 *)banner, stateFileBannerLen);

    if (strncmp(banner, stateFileBanner, stateFileBannerLen)) {
      error("failed to get State Banner from '%s'", filename);
      fclose(file);
      return -1;
    }

    trs_load_uint32(file, &version);

    if (version != stateVersionNumber) {
      error("unsupported version %d of State file '%s'", version, filename);
      fclose(file);
      return -1;
    }

    trs_screen_load(file);
    trs_cassette_load(file);
    trs_clone_load(file);
    trs_cp500_load(file);
    trs_disk_load(file);
    trs_hard_load(file);
    trs_omti_load(file);
    trs_stringy_load(file);
    trs_interrupt_load(file);
    trs_io_load(file);
    trs_mem_load(file);
    trs_keyboard_load(file);
    trs_uart_load(file);
    trs_z80_load(file);
    trs_imp_exp_load(file);

    fclose(file);
    return 0;
  }

  file_error("load State: '%s'", filename);
  return -1;
}

void trs_save_uint8(FILE *file, const Uint8 *buffer, int count)
{
  fwrite(buffer, 1, count, file);
}

void trs_load_uint8(FILE *file, Uint8 *buffer, int count)
{
  fread(buffer, 1, count, file);
}

void trs_save_uint16(FILE *file, const Uint16 *buffer)
{
  Uint16 temp = *buffer;
  Uint8  byte = temp & 0xFF;

  fwrite(&byte, 1, 1, file);
  byte = temp >> 8;
  fwrite(&byte, 1, 1, file);
}

void trs_load_uint16(FILE *file, Uint16 *buffer)
{
  Uint8 byte[2] = { 0 };

  fread(&byte, 1, 2, file);
  *buffer++ = (byte[1] << 8) | byte[0];
}

void trs_save_uint32(FILE *file, const Uint32 *buffer)
{
  unsigned int temp = *buffer;
  Uint8 byte = temp & 0xFF;

  fwrite(&byte, 1, 1, file);

  temp >>= 8;
  byte = temp & 0xFF;
  fwrite(&byte, 1, 1, file);

  temp >>= 8;
  byte = temp & 0xFF;
  fwrite(&byte, 1, 1, file);

  temp >>= 8;
  byte = temp & 0xFF;
  fwrite(&byte, 1, 1, file);
}

void trs_load_uint32(FILE *file, Uint32 *buffer)
{
  Uint8 byte[4] = { 0 };

  fread(&byte, 1, 4, file);
  *buffer++ = (byte[3] << 24) | (byte[2] << 16) | (byte[1] << 8) | byte[0];
}

void trs_save_uint64(FILE *file, const Uint64 *buffer)
{
  int i;
  Uint64 temp = *buffer;

  for (i = 0; i < 8; i++) {
    Uint8 byte = temp & 0xFF;

    fwrite(&byte, 1, 1, file);
    temp = temp >> 8;
  }
}

void trs_load_uint64(FILE *file, Uint64 *buffer)
{
  int i;
  Uint64 temp = 0;
  Uint8 byte[8] = { 0 };

  fread(&byte, 1, 8, file);

  for (i = 7; i >= 0; i--) {
    temp |= byte[i];
    if (i)
      temp = temp << 8;
  }

  *buffer++ = temp;
}

void trs_save_int(FILE *file, const int *buffer, int count)
{
  int i;

  for (i = 0; i < count; i++) {
    unsigned int unum;
    Uint8 sign = 0;
    Uint8 byte;
    int num = *buffer++;

    if (num < 0) {
      sign = 0x80;
      num = -num;
    }

    unum = num;
    byte = unum & 0xFF;
    fwrite(&byte, 1, 1, file);

    unum = unum >> 8;
    byte = unum & 0xFF;
    fwrite(&byte, 1, 1, file);

    unum = unum >> 8;
    byte = unum & 0xFF;
    fwrite(&byte, 1, 1, file);

    unum = unum >> 8;
    byte = (unum & 0x7F) | sign;
    fwrite(&byte, 1, 1, file);
  }
}

void trs_load_int(FILE *file, int *buffer, int count)
{
  int i;

  for (i = 0; i < count; i++) {
    Uint8 byte[4], sign;
    int temp;

    fread(&byte, 1, 4, file);

    sign = byte[3] & 0x80;
    byte[3] &= 0x7f;
    temp = (byte[3] << 24) | (byte[2] << 16) | (byte[1] << 8) | byte[0];
    if (sign)
      temp = -temp;

    *buffer++ = temp;
  }
}

void trs_save_float(FILE *file, const float *buffer)
{
  char float_buff[21];

  snprintf(float_buff, 21, "%20f", *buffer);
  trs_save_uint8(file, (Uint8 *)float_buff, 20);
}

void trs_load_float(FILE *file, float *buffer)
{
  char float_buff[21];

  trs_load_uint8(file, (Uint8 *)float_buff, 20);
  sscanf(float_buff, "%f", buffer);
}

void trs_save_filename(FILE *file, const char *filename)
{
  Uint16 length = strlen(filename);

  trs_save_uint16(file, &length);
  trs_save_uint8(file, (Uint8 *)filename, length);
}

void trs_load_filename(FILE *file, char *filename)
{
  Uint16 length;

  trs_load_uint16(file, &length);
  trs_load_uint8(file, (Uint8 *)filename, length);
  filename[length] = 0;
}
