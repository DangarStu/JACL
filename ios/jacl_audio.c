/* jacl_audio.c --- OGG Vorbis -> WAV decoding for the iOS app.
 *
 * Blorb sounds are Ogg Vorbis, which AVFoundation can't play. We decode them
 * to 16-bit PCM with the public-domain stb_vorbis and wrap that in a WAV
 * container so AVAudioPlayer (which does play WAV/PCM) can take it directly.
 *
 * stb_vorbis.c is #included here (one translation unit, no separate compile)
 * with file I/O disabled -- we only ever decode from memory.
 *
 * Compiled only into the iOS app target. See jacl_bridge.h.
 */

#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

static void write_le32(unsigned char *p, unsigned int v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

static void write_le16(unsigned char *p, unsigned int v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
}

void *jacl_ogg_to_wav(const void *ogg, int ogg_len, int *out_len)
{
    int channels = 0, rate = 0;
    short *pcm = NULL;
    int samples, data_bytes, wav_len, byte_rate, block_align;
    unsigned char *wav;

    *out_len = 0;
    if (!ogg || ogg_len <= 0)
        return NULL;

    samples = stb_vorbis_decode_memory((const unsigned char *) ogg, ogg_len,
                                       &channels, &rate, &pcm);
    if (samples <= 0 || !pcm || channels <= 0 || rate <= 0) {
        if (pcm) free(pcm);
        return NULL;
    }

    data_bytes = samples * channels * 2;          /* 16-bit samples */
    wav_len = 44 + data_bytes;
    wav = (unsigned char *) malloc(wav_len);
    if (!wav) { free(pcm); return NULL; }

    byte_rate = rate * channels * 2;
    block_align = channels * 2;

    memcpy(wav, "RIFF", 4);
    write_le32(wav + 4, 36 + data_bytes);
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    write_le32(wav + 16, 16);                     /* fmt chunk size */
    write_le16(wav + 20, 1);                      /* PCM */
    write_le16(wav + 22, channels);
    write_le32(wav + 24, rate);
    write_le32(wav + 28, byte_rate);
    write_le16(wav + 32, block_align);
    write_le16(wav + 34, 16);                     /* bits per sample */
    memcpy(wav + 36, "data", 4);
    write_le32(wav + 40, data_bytes);
    memcpy(wav + 44, pcm, data_bytes);

    free(pcm);
    *out_len = wav_len;
    return wav;
}
