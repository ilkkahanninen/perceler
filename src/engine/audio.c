/*
 * audio.c - XM playback using libxmp-lite rendered into the SB16 DMA buffer.
 *
 * The SB16 driver fires an interrupt every SB16_HALF_SAMPLES stereo samples
 * and sets a flag.  audio_update() checks the flag and calls xmp_play_buffer()
 * to fill the inactive half of the DMA buffer with the next decoded PCM chunk.
 */

#include <stdlib.h>
#include <string.h>
#include "xmp.h"
#include "audio.h"
#include "sb16.h"
#include "data.h"

static xmp_context g_ctx    = NULL;
static int         g_loaded = 0;

/* Called by sb16_update() → fill one half-buffer with decoded PCM */
static void fill_pcm(short *buf, int samples)
{
    int bytes = samples * 2 * 2; /* stereo * 16-bit */
    if (g_loaded) {
        if (xmp_play_buffer(g_ctx, buf, bytes, 1) < 0) {
            /* module ended or error — silence */
            memset(buf, 0, (unsigned)bytes);
        }
    } else {
        memset(buf, 0, (unsigned)bytes);
    }
}

int audio_init(void)
{
    int err;

    g_ctx = xmp_create_context();
    if (!g_ctx) return -1;

    err = sb16_init(fill_pcm);
    if (err != SB16_OK) {
        xmp_free_context(g_ctx);
        g_ctx = NULL;
        return err;
    }
    return 0;
}

int audio_load(unsigned long offset, unsigned long length)
{
    void *buf;

    if (!g_ctx) return -1;

    if (g_loaded) {
        xmp_end_player(g_ctx);
        xmp_release_module(g_ctx);
        g_loaded = 0;
    }

    buf = data_read(offset, length);
    if (!buf) return -1;

    if (xmp_load_module_from_memory(g_ctx, buf, (long)length) != 0) {
        free(buf);
        return -1;
    }
    free(buf);

    if (xmp_start_player(g_ctx, SB16_RATE, 0) != 0) {
        xmp_release_module(g_ctx);
        return -1;
    }

    g_loaded = 1;
    return 0;
}

void audio_seek(unsigned long ms)
{
    if (g_loaded)
        xmp_seek_time(g_ctx, (int)ms);
}

void audio_update(void)
{
    sb16_update();
}

void audio_shutdown(void)
{
    sb16_shutdown();

    if (g_ctx) {
        if (g_loaded) {
            xmp_end_player(g_ctx);
            xmp_release_module(g_ctx);
            g_loaded = 0;
        }
        xmp_free_context(g_ctx);
        g_ctx = NULL;
    }
}
