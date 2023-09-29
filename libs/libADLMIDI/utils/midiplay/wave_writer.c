/* snes_spc 0.9.0. http://www.slack.net/~ant/ */

#include "wave_writer.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Copyright (C) 2003-2007 Shay Green. This module is free software; you
can redistribute it and/or modify it under the terms of the GNU Lesser
General Public License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version. This
module is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
details. You should have received a copy of the GNU Lesser General Public
License along with this module; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA */

enum { buf_size = 32768 * 2 };
enum { header_size = 0x2C };

typedef short sample_t;

struct Context
{
    unsigned char *m_buf;
    FILE *m_file;
    long  m_sample_count;
    long  m_sample_rate;
    long  m_buf_pos;
    int   m_chan_count;
};

static struct Context *g_wwContext = NULL;

static void exit_with_error(const char *str)
{
    fprintf(stderr, "WAVE Writer Error: %s\n", str);
    fflush(stderr);
}

int wave_open(long sample_rate, const char *filename)
{
    g_wwContext = ctx_wave_open(sample_rate, filename);
    return g_wwContext ? 0 : -1;
}

void wave_enable_stereo(void)
{
    ctx_wave_enable_stereo(g_wwContext);
}

void wave_write(short const *in, long remain)
{
    ctx_wave_write(g_wwContext, in, remain);
}

long wave_sample_count(void)
{
    return ctx_wave_sample_count(g_wwContext);
}

void wave_close(void)
{
    ctx_wave_close(g_wwContext);
}



static void set_le32(void *p, unsigned long n)
{
    ((unsigned char *) p) [0] = (unsigned char) n & (0xFF);
    ((unsigned char *) p) [1] = (unsigned char)(n >> 8) & (0xFF);
    ((unsigned char *) p) [2] = (unsigned char)(n >> 16) & (0xFF);
    ((unsigned char *) p) [3] = (unsigned char)(n >> 24) & (0xFF);
}

void *ctx_wave_open(long sample_rate, const char *filename)
{
    struct Context *ctx = (struct Context*)malloc(sizeof(struct Context));
    if(!ctx)
    {
        exit_with_error("Out of memory");
        return NULL;
    }

    ctx->m_sample_count = 0;
    ctx->m_sample_rate  = sample_rate;
    ctx->m_buf_pos      = header_size;
    ctx->m_chan_count   = 1;

    ctx->m_buf = (unsigned char *) malloc(buf_size);
    if(!ctx->m_buf)
    {
        exit_with_error("Out of memory");
        free(ctx);
        return NULL;
    }

#if !defined(_WIN32) || defined(__WATCOMC__)
    ctx->m_file = fopen(filename, "wb");
#else
    wchar_t widePath[MAX_PATH];
    int size = MultiByteToWideChar(CP_UTF8, 0, filename, strlen(filename), widePath, MAX_PATH);
    widePath[size] = '\0';
    ctx->m_file = _wfopen(widePath, L"wb");
#endif
    if(!ctx->m_file)
    {
        exit_with_error("Couldn't open WAVE file for writing");
        free(ctx);
        return NULL;
    }

    setvbuf(ctx->m_file, 0, _IOFBF, 32 * 1024L);
    return ctx;
}

void ctx_wave_enable_stereo(void *ctx)
{
    struct Context *wWriter = (struct Context *)ctx;
    wWriter->m_chan_count = 2;
}


static void flush_(struct Context *ctx)
{
    if(ctx->m_buf_pos && !fwrite(ctx->m_buf, (size_t)ctx->m_buf_pos, 1, ctx->m_file))
        exit_with_error("Couldn't write WAVE data");
    ctx->m_buf_pos = 0;
}

void ctx_wave_write(void *ctx, const short *in, long remain)
{
    struct Context *wWriter = (struct Context *)ctx;
    wWriter->m_sample_count += remain;
    while(remain)
    {
        if(wWriter->m_buf_pos >= buf_size)
            flush_(wWriter);
        {
            unsigned char *p = &wWriter->m_buf [wWriter->m_buf_pos];
            long n = (buf_size - (unsigned long)wWriter->m_buf_pos) / sizeof(sample_t);
            if(n > remain)
                n = remain;
            remain -= n;

            /* convert to LSB first format */
            while(n--)
            {
                int s = *in++;
                *p++ = (unsigned char) s & (0x00FF);
                *p++ = (unsigned char)(s >> 8) & (0x00FF);
            }

            wWriter->m_buf_pos = p - wWriter->m_buf;
            assert(wWriter->m_buf_pos <= buf_size);
        }
    }
}

long ctx_wave_sample_count(void *ctx)
{
    struct Context *wWriter = (struct Context *)ctx;
    return wWriter->m_sample_count;
}

void ctx_wave_close(void *ctx)
{
    struct Context *wWriter = (struct Context *)ctx;
    if(!wWriter)
        return;

    if(wWriter->m_file)
    {
        /* generate header */
        unsigned char h [header_size] =
        {
            'R', 'I', 'F', 'F',
            0, 0, 0, 0,     /* length of rest of file */
            'W', 'A', 'V', 'E',
            'f', 'm', 't', ' ',
            0x10, 0, 0, 0,  /* size of fmt chunk */
            1, 0,           /* uncompressed format */
            0, 0,           /* channel count */
            0, 0, 0, 0,     /* sample rate */
            0, 0, 0, 0,     /* bytes per second */
            0, 0,           /* bytes per sample frame */
            16, 0,          /* bits per sample */
            'd', 'a', 't', 'a',
            0, 0, 0, 0,     /* size of sample data */
            /* ... */       /* sample data */
        };
        long ds = wWriter->m_sample_count * (long)sizeof(sample_t);
        int frame_size = wWriter->m_chan_count * (long)sizeof(sample_t);

        set_le32(h + 0x04, header_size - 8 + ds);
        h [0x16] = (unsigned char)wWriter->m_chan_count;
        set_le32(h + 0x18, (unsigned long)wWriter->m_sample_rate);
        set_le32(h + 0x1C, (unsigned long)wWriter->m_sample_rate * (unsigned long)frame_size);
        h [0x20] = (unsigned char)frame_size;
        set_le32(h + 0x28, (unsigned long)ds);

        flush_(wWriter);

        /* write header */
        fseek(wWriter->m_file, 0, SEEK_SET);
        fwrite(h, header_size, 1, wWriter->m_file);
        fclose(wWriter->m_file);
        wWriter->m_file = 0;
        free(wWriter->m_buf);
        wWriter->m_buf = 0;
    }
    free(wWriter);
}
