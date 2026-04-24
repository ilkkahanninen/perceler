/*
 * capture.c - Offline-render video + audio sink.
 *
 * See capture.h for the file formats. The RAW is a plain stream of
 * palette+pixels frames with a 4-byte "PRCL" header. The WAV uses the
 * standard 44-byte PCM header; RIFF size and data chunk size are unknown
 * at open time, so we write placeholders and patch them in shutdown.
 */

#include "capture.h"

#include "audio.h" /* AUDIO_RATE */
#include "vga.h"

#include <conio.h>
#include <stdio.h>
#include <string.h>

#define VGA_DAC_READ 0x3C7
#define VGA_DAC_DATA 0x3C9

static FILE *g_dat = NULL;
static FILE *g_wav = NULL;
static int g_enabled = 0;

static void write_le32(unsigned char *p, unsigned long v)
{
  p[0] = (unsigned char)(v & 0xFF);
  p[1] = (unsigned char)((v >> 8) & 0xFF);
  p[2] = (unsigned char)((v >> 16) & 0xFF);
  p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static void wav_write_header(FILE *f)
{
  unsigned char h[44];
  unsigned long byte_rate = (unsigned long)AUDIO_RATE * 4UL; /* stereo × 2 bytes */

  memcpy(h, "RIFF", 4);
  write_le32(h + 4, 0);          /* file_size - 8, patched on close */
  memcpy(h + 8, "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  write_le32(h + 16, 16);        /* fmt chunk payload size */
  h[20] = 1; h[21] = 0;          /* PCM */
  h[22] = 2; h[23] = 0;          /* stereo */
  write_le32(h + 24, AUDIO_RATE);
  write_le32(h + 28, byte_rate);
  h[32] = 4; h[33] = 0;          /* block align = channels × sample bytes */
  h[34] = 16; h[35] = 0;         /* bits per sample */
  memcpy(h + 36, "data", 4);
  write_le32(h + 40, 0);         /* data size, patched on close */

  fwrite(h, 1, 44, f);
}

static void wav_patch_sizes(FILE *f)
{
  long file_size;
  long data_size;
  unsigned char buf[4];

  if (fseek(f, 0, SEEK_END) != 0)
    return;
  file_size = ftell(f);
  if (file_size < 44)
    return;
  data_size = file_size - 44;

  write_le32(buf, (unsigned long)(file_size - 8));
  fseek(f, 4, SEEK_SET);
  fwrite(buf, 1, 4, f);

  write_le32(buf, (unsigned long)data_size);
  fseek(f, 40, SEEK_SET);
  fwrite(buf, 1, 4, f);
}

int capture_init(const char *prefix)
{
  char path[128];

  if (!prefix || !prefix[0])
    return 0;

  sprintf(path, "%s.RAW", prefix);
  g_dat = fopen(path, "wb");
  if (!g_dat)
  {
    printf("capture: could not open %s for writing\n", path);
    return 0;
  }
  fwrite("PRCL", 1, 4, g_dat);

  sprintf(path, "%s.WAV", prefix);
  g_wav = fopen(path, "wb");
  if (!g_wav)
  {
    printf("capture: could not open %s for writing\n", path);
    fclose(g_dat);
    g_dat = NULL;
    return 0;
  }
  wav_write_header(g_wav);

  g_enabled = 1;
  return 1;
}

int capture_enabled(void)
{
  return g_enabled;
}

void capture_frame(const unsigned char *backbuffer)
{
  unsigned char pal[768];
  int i;

  if (!g_enabled || !g_dat)
    return;

  /* Read the current VGA DAC state. outp(DAC_READ, 0) resets the
   * read index; successive inp(DAC_DATA) returns auto-incrementing
   * R,G,B triples (values 0-63). */
  outp(VGA_DAC_READ, 0);
  for (i = 0; i < 768; i++)
    pal[i] = (unsigned char)inp(VGA_DAC_DATA);

  fwrite(pal, 1, 768, g_dat);
  fwrite(backbuffer, 1, VGA_SIZE, g_dat);
}

void capture_audio(const short *stereo_samples, int samples)
{
  if (!g_enabled || !g_wav || samples <= 0)
    return;
  fwrite(stereo_samples, (unsigned)(samples * 4), 1, g_wav);
}

void capture_shutdown(void)
{
  if (g_dat)
  {
    fclose(g_dat);
    g_dat = NULL;
  }
  if (g_wav)
  {
    wav_patch_sizes(g_wav);
    fclose(g_wav);
    g_wav = NULL;
  }
  g_enabled = 0;
}
