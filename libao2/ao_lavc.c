/*
 * audio encoding using libavformat
 * Copyright (C) 2011 Rudolf Polzer <divVerent@xonotic.org>
 * NOTE: this file is partially based on ao_pcm.c by Atmosfear
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>

#include <libavutil/common.h>
#include <libavutil/audioconvert.h>

#include "config.h"
#include "options.h"
#include "mpcommon.h"
#include "fmt-conversion.h"
#include "libaf/format.h"
#include "libaf/reorder_ch.h"
#include "talloc.h"
#include "audio_out.h"
#include "mp_msg.h"

#include "encode_lavc.h"

static const char *sample_padding_signed = "\x00\x00\x00\x00";
static const char *sample_padding_u8     = "\x80";
static const char *sample_padding_float  = "\x00\x00\x00\x00";

struct priv {
    uint8_t *buffer;
    size_t buffer_size;
    AVStream *stream;
    int pcmhack;
    int aframesize;
    int aframecount;
    int offset;
    int offset_left;
    int64_t savepts;
    int framecount;
    int64_t lastpts;
    int sample_size;
    const void *sample_padding;

    AVRational worst_time_base;
    int worst_time_base_is_stream;
};

// open & setup audio device
static int init(struct ao *ao, char *params)
{
    struct priv *ac = talloc_zero(ao, struct priv);
    const enum AVSampleFormat *sampleformat;
    AVCodec *codec;

    if (!encode_lavc_available(ao->encode_lavc_ctx)) {
        mp_msg(MSGT_ENCODE, MSGL_ERR,
               "ao-lavc: the option -o (output file) must be specified\n");
        return -1;
    }

    if (ac->stream) {
        mp_msg(MSGT_ENCODE, MSGL_ERR, "ao-lavc: rejecting reinitialization\n");
        return -1;
    }

    ac->stream = encode_lavc_alloc_stream(ao->encode_lavc_ctx,
                                          AVMEDIA_TYPE_AUDIO);

    if (!ac->stream) {
        mp_msg(MSGT_ENCODE, MSGL_ERR, "ao-lavc: could not get a new audio stream\n");
        return -1;
    }

    codec = encode_lavc_get_codec(ao->encode_lavc_ctx, ac->stream);

    // ac->stream->time_base.num = 1;
    // ac->stream->time_base.den = ao->samplerate;
    // doing this breaks mpeg2ts in ffmpeg
    // which doesn't properly force the time base to be 90000
    // furthermore, ffmpeg.c doesn't do this either and works

    ac->stream->codec->time_base.num = 1;
    ac->stream->codec->time_base.den = ao->samplerate;

    ac->stream->codec->sample_rate = ao->samplerate;
    ac->stream->codec->channels = ao->channels;

    ac->stream->codec->sample_fmt = AV_SAMPLE_FMT_NONE;

    {
        // first check if the selected format is somewhere in the list of
        // supported formats by the codec
        for (sampleformat = codec->sample_fmts;
             sampleformat && *sampleformat != AV_SAMPLE_FMT_NONE;
             ++sampleformat) {
            switch (*sampleformat) {
            case AV_SAMPLE_FMT_U8:
                if (ao->format == AF_FORMAT_U8)
                    goto out_search;
                break;
            case AV_SAMPLE_FMT_S16:
                if (ao->format == AF_FORMAT_S16_BE)
                    goto out_search;
                if (ao->format == AF_FORMAT_S16_LE)
                    goto out_search;
                break;
            case AV_SAMPLE_FMT_S32:
                if (ao->format == AF_FORMAT_S32_BE)
                    goto out_search;
                if (ao->format == AF_FORMAT_S32_LE)
                    goto out_search;
                break;
            case AV_SAMPLE_FMT_FLT:
                if (ao->format == AF_FORMAT_FLOAT_BE)
                    goto out_search;
                if (ao->format == AF_FORMAT_FLOAT_LE)
                    goto out_search;
                break;
            default:
                break;
            }
        }
out_search:
        ;
    }

    if (!sampleformat || *sampleformat == AV_SAMPLE_FMT_NONE) {
        // if the selected format is not supported, we have to pick the first
        // one we CAN support
        // note: not needing to select endianness here, as the switch() below
        // does that anyway for us
        for (sampleformat = codec->sample_fmts;
             sampleformat && *sampleformat != AV_SAMPLE_FMT_NONE;
             ++sampleformat) {
            switch (*sampleformat) {
            case AV_SAMPLE_FMT_U8:
                ao->format = AF_FORMAT_U8;
                goto out_takefirst;
            case AV_SAMPLE_FMT_S16:
                ao->format = AF_FORMAT_S16_NE;
                goto out_takefirst;
            case AV_SAMPLE_FMT_S32:
                ao->format = AF_FORMAT_S32_NE;
                goto out_takefirst;
            case AV_SAMPLE_FMT_FLT:
                ao->format = AF_FORMAT_FLOAT_NE;
                goto out_takefirst;
            default:
                break;
            }
        }
out_takefirst:
        ;
    }

    switch (ao->format) {
    // now that we have chosen a format, set up the fields for it, boldly
    // switching endianness if needed (mplayer code will convert for us
    // anyway, but ffmpeg always expects native endianness)
    case AF_FORMAT_U8:
        ac->stream->codec->sample_fmt = AV_SAMPLE_FMT_U8;
        ac->sample_size = 1;
        ac->sample_padding = sample_padding_u8;
        ao->format = AF_FORMAT_U8;
        break;
    default:
    case AF_FORMAT_S16_BE:
    case AF_FORMAT_S16_LE:
        ac->stream->codec->sample_fmt = AV_SAMPLE_FMT_S16;
        ac->sample_size = 2;
        ac->sample_padding = sample_padding_signed;
        ao->format = AF_FORMAT_S16_NE;
        break;
    case AF_FORMAT_S32_BE:
    case AF_FORMAT_S32_LE:
        ac->stream->codec->sample_fmt = AV_SAMPLE_FMT_S32;
        ac->sample_size = 4;
        ac->sample_padding = sample_padding_signed;
        ao->format = AF_FORMAT_S32_NE;
        break;
    case AF_FORMAT_FLOAT_BE:
    case AF_FORMAT_FLOAT_LE:
        ac->stream->codec->sample_fmt = AV_SAMPLE_FMT_FLT;
        ac->sample_size = 4;
        ac->sample_padding = sample_padding_float;
        ao->format = AF_FORMAT_FLOAT_NE;
        break;
    }

    ac->stream->codec->bits_per_raw_sample = ac->sample_size * 8;

    switch (ao->channels) {
    case 1:
        ac->stream->codec->channel_layout = AV_CH_LAYOUT_MONO;
        break;
    case 2:
        ac->stream->codec->channel_layout = AV_CH_LAYOUT_STEREO;
        break;
    /* someone please check if these are what mplayer normally assumes
       case 3:
        ac->stream->codec->channel_layout = AV_CH_LAYOUT_SURROUND;
        break;
       case 4:
        ac->stream->codec->channel_layout = AV_CH_LAYOUT_2_2;
        break;
     */
    case 5:
        ac->stream->codec->channel_layout = AV_CH_LAYOUT_5POINT0;
        break;
    case 6:
        ac->stream->codec->channel_layout = AV_CH_LAYOUT_5POINT1;
        break;
    case 8:
        ac->stream->codec->channel_layout = AV_CH_LAYOUT_7POINT1;
        break;
    default:
        mp_msg(MSGT_ENCODE, MSGL_ERR,
               "ao-lavc: unknown channel layout; hoping for the best\n");
        break;
    }

    if (encode_lavc_open_codec(ao->encode_lavc_ctx, ac->stream) < 0)
        return -1;

    ac->pcmhack = 0;
    if (ac->stream->codec->frame_size <= 1)
        ac->pcmhack = av_get_bits_per_sample(ac->stream->codec->codec_id) / 8;

    if (ac->pcmhack) {
        ac->aframesize = 16384; // "enough"
        ac->buffer_size = ac->aframesize * ac->pcmhack * ao->channels * 2 + 200;
    } else {
        ac->aframesize = ac->stream->codec->frame_size;
        ac->buffer_size = ac->aframesize * ac->sample_size * ao->channels * 2 +
                          200;
    }
    if (ac->buffer_size < FF_MIN_BUFFER_SIZE)
        ac->buffer_size = FF_MIN_BUFFER_SIZE;
    ac->buffer = talloc_size(ac, ac->buffer_size);

    // enough frames for at least 0.25 seconds
    ac->framecount = ceil(ao->samplerate * 0.25 / ac->aframesize);
    // but at least one!
    ac->framecount = FFMAX(ac->framecount, 1);

    ac->savepts = MP_NOPTS_VALUE;
    ac->lastpts = MP_NOPTS_VALUE;
    ac->offset = ac->stream->codec->sample_rate *
                 encode_lavc_getoffset(ao->encode_lavc_ctx, ac->stream);
    ac->offset_left = ac->offset;

    //fill_ao_data:
    ao->outburst = ac->aframesize * ac->sample_size * ao->channels *
                   ac->framecount;
    ao->buffersize = ao->outburst * 2;
    ao->bps = ao->channels * ao->samplerate * ac->sample_size;
    ao->untimed = true;
    ao->priv = ac;

    return 0;
}

static void fill_with_padding(void *buf, int cnt, int sz, const void *padding)
{
    int i;
    if (sz == 1) {
        memset(buf, cnt, *(char *)padding);
        return;
    }
    for (i = 0; i < cnt; ++i)
        memcpy((char *) buf + i * sz, padding, sz);
}

// close audio device
static int encode(struct ao *ao, int ptsvalid, double apts, void *data);
static void uninit(struct ao *ao, bool cut_audio)
{
    struct priv *ac = ao->priv;
    if (ac->buffer) {
        double pts = ao->pts + ac->offset / (double) ao->samplerate;
        if (ao->buffer.len > 0) {
            void *paddingbuf = talloc_size(ao,
                    ac->aframesize * ao->channels * ac->sample_size);
            memcpy(paddingbuf, ao->buffer.start, ao->buffer.len);
            fill_with_padding((char *) paddingbuf + ao->buffer.len,
                              (ac->aframesize * ao->channels * ac->sample_size
                               - ao->buffer.len) / ac->sample_size,
                              ac->sample_size, ac->sample_padding);
            encode(ao, ao->pts != MP_NOPTS_VALUE, pts, paddingbuf);
            pts += ac->aframesize / (double) ao->samplerate;
            talloc_free(paddingbuf);
            ao->buffer.len = 0;
        }
        while (encode(ao, true, pts, NULL) > 0) ;
    }

    ao->priv = NULL;
}

// return: how many bytes can be played without blocking
static int get_space(struct ao *ao)
{
    return ao->outburst;
}

// must get exactly ac->aframesize amount of data
static int encode(struct ao *ao, int ptsvalid, double apts, void *data)
{
    AVFrame *frame;
    AVPacket packet;
    struct priv *ac = ao->priv;
    struct encode_lavc_context *ectx = ao->encode_lavc_ctx;
    double realapts = ac->aframecount * (double) ac->aframesize /
                      ao->samplerate;
    int status, gotpacket;

    ac->aframecount++;
    if (data && (ao->channels == 5 || ao->channels == 6 || ao->channels == 8)) {
        reorder_channel_nch(data, AF_CHANNEL_LAYOUT_MPLAYER_DEFAULT,
                            AF_CHANNEL_LAYOUT_LAVC_DEFAULT,
                            ao->channels,
                            ac->aframesize * ao->channels, ac->sample_size);
    }

    if (data && ptsvalid)
        ectx->audio_pts_offset = realapts - apts;

    av_init_packet(&packet);
    packet.data = ac->buffer;
    packet.size = ac->buffer_size;
    if(data)
    {
        frame = avcodec_alloc_frame();
        frame->nb_samples = ac->aframesize;
        if(avcodec_fill_audio_frame(frame, ao->channels, ac->stream->codec->sample_fmt, data, ac->aframesize * ao->channels * ac->sample_size, 1))
        {
            mp_msg(MSGT_ENCODE, MSGL_ERR, "ao-lavc: error filling\n");
            return -1;
        }

        if (ao->encode_lavc_ctx->options->rawts) {
            // raw audio pts
            frame->pts = floor(apts * ac->stream->codec->time_base.den / ac->stream->codec->time_base.num + 0.5);
        } else if (ectx->options->copyts) {
            // real audio pts
            frame->pts = floor((apts + ectx->discontinuity_pts_offset) * ac->stream->codec->time_base.den / ac->stream->codec->time_base.num + 0.5);
        } else {
            // audio playback time
            frame->pts = floor(realapts * ac->stream->codec->time_base.den / ac->stream->codec->time_base.num + 0.5);
        }

        int64_t frame_pts = av_rescale_q(frame->pts, ac->stream->codec->time_base, ac->worst_time_base);
        if (ac->lastpts != MP_NOPTS_VALUE && frame_pts <= ac->lastpts) {
            // this indicates broken video
            // (video pts failing to increase fast enough to match audio)
            mp_msg(MSGT_ENCODE, MSGL_WARN, "ao-lavc: audio frame pts went backwards "
                    "(%d <- %d), autofixed\n", (int)frame->pts,
                    (int)ac->lastpts);
            frame_pts = ac->lastpts + 1;
            frame->pts = av_rescale_q(frame_pts, ac->worst_time_base, ac->stream->codec->time_base);
        }
        ac->lastpts = frame_pts;

        frame->quality = ac->stream->codec->global_quality;
        status = avcodec_encode_audio2(ac->stream->codec, &packet, frame, &gotpacket);

        if (!status) {
            if (ac->savepts == MP_NOPTS_VALUE)
                ac->savepts = frame->pts;
        }

	av_free(frame);
    }
    else
    {
        status = avcodec_encode_audio2(ac->stream->codec, &packet, NULL, &gotpacket);
    }

    if(status)
    {
        mp_msg(MSGT_ENCODE, MSGL_ERR, "ao-lavc: error encoding\n");
        return -1;
    }

    if(!gotpacket)
        return 0;

    mp_msg(MSGT_ENCODE, MSGL_DBG2,
           "ao-lavc: got pts %f (playback time: %f); out size: %d\n",
           apts, realapts, packet.size);

    encode_lavc_write_stats(ao->encode_lavc_ctx, ac->stream);

    // Do we need this at all? Better be safe than sorry...
    if (packet.pts == AV_NOPTS_VALUE) {
        mp_msg(MSGT_ENCODE, MSGL_WARN, "ao-lavc: encoder lost pts, why?\n");
        if (ac->savepts != MP_NOPTS_VALUE)
            packet.pts = ac->savepts;
    }

    if (packet.pts != AV_NOPTS_VALUE)
        packet.pts = av_rescale_q(packet.pts, ac->stream->codec->time_base,
                ac->stream->time_base);

    if (packet.dts != AV_NOPTS_VALUE)
        packet.dts = av_rescale_q(packet.dts, ac->stream->codec->time_base,
                ac->stream->time_base);

    if(packet.duration > 0)
        packet.duration = av_rescale_q(packet.duration, ac->stream->codec->time_base,
                ac->stream->time_base);

    ac->savepts = MP_NOPTS_VALUE;

    if (encode_lavc_write_frame(ao->encode_lavc_ctx, &packet) < 0) {
        mp_msg(MSGT_ENCODE, MSGL_ERR, "ao-lavc: error writing at %f %f/%f\n",
               realapts, (double) ac->stream->time_base.num,
               (double) ac->stream->time_base.den);
        return -1;
    }

    return packet.size;
}

// plays 'len' bytes of 'data'
// it should round it down to outburst*n
// return: number of bytes played
static int play(struct ao *ao, void *data, int len, int flags)
{
    struct priv *ac = ao->priv;
    struct encode_lavc_context *ectx = ao->encode_lavc_ctx;
    int bufpos = 0;
    int64_t ptsoffset;
    void *paddingbuf = NULL;
    double nextpts;

    len /= ac->sample_size * ao->channels;

    if (!encode_lavc_start(ectx)) {
        mp_msg(MSGT_ENCODE, MSGL_WARN, "ao-lavc: NOTE: deferred initial audio frame (probably because video is not there yet)\n");
        return 0;
    }

    if (ac->worst_time_base.den == 0) {
        //if (ac->stream->codec->time_base.num / ac->stream->codec->time_base.den >= ac->stream->time_base.num / ac->stream->time_base.den)
        if (ac->stream->codec->time_base.num * (double) ac->stream->time_base.den >=
                ac->stream->time_base.num * (double) ac->stream->codec->time_base.den) {
            mp_msg(MSGT_ENCODE, MSGL_V, "ao-lavc: NOTE: using codec time base "
                   "(%d/%d) for pts adjustment; the stream base (%d/%d) is "
                   "not worse.\n", (int)ac->stream->codec->time_base.num,
                   (int)ac->stream->codec->time_base.den, (int)ac->stream->time_base.num,
                   (int)ac->stream->time_base.den);
            ac->worst_time_base = ac->stream->codec->time_base;
            ac->worst_time_base_is_stream = 0;
        } else {
            mp_msg(MSGT_ENCODE, MSGL_WARN, "ao-lavc: NOTE: not using codec time "
                   "base (%d/%d) for pts adjustment; the stream base (%d/%d) "
                   "is worse.\n", (int)ac->stream->codec->time_base.num,
                   (int)ac->stream->codec->time_base.den, (int)ac->stream->time_base.num,
                   (int)ac->stream->time_base.den);
            ac->worst_time_base = ac->stream->time_base;
            ac->worst_time_base_is_stream = 1;
        }

        // NOTE: we use the following "axiom" of av_rescale_q:
        // if time base A is worse than time base B, then
        //   av_rescale_q(av_rescale_q(x, A, B), B, A) == x
        // this can be proven as long as av_rescale_q rounds to nearest, which
        // it currently does

        // av_rescale_q(x, A, B) * B = "round x*A to nearest multiple of B"
        // and:
        //    av_rescale_q(av_rescale_q(x, A, B), B, A) * A
        // == "round av_rescale_q(x, A, B)*B to nearest multiple of A"
        // == "round 'round x*A to nearest multiple of B' to nearest multiple of A"
        //
        // assume this fails. Then there is a value of x*A, for which the
        // nearest multiple of B is outside the range [(x-0.5)*A, (x+0.5)*A[.
        // Absurd, as this range MUST contain at least one multiple of B.
    }

    ptsoffset = ac->offset;
    // this basically just edits ao->apts for syncing purposes

    if (ectx->options->copyts || ectx->options->rawts) {
        // we do not send time sync data to the video side,
        // but we always need the exact pts, even if zero
    } else {
        // here we must "simulate" the pts editing
        // 1. if we have to skip stuff, we skip it
        // 2. if we have to add samples, we add them
        // 3. we must still adjust ptsoffset appropriately for AV sync!
        // invariant:
        // if no partial skipping is done, the first frame gets ao->apts passed as pts!

        if (ac->offset_left < 0) {
            if (ac->offset_left <= -len) {
                // skip whole frame
                ac->offset_left += len;
                return len * ac->sample_size * ao->channels;
            } else {
                // skip part of this frame, buffer/encode the rest
                bufpos -= ac->offset_left;
                ptsoffset += ac->offset_left;
                ac->offset_left = 0;
            }
        } else if (ac->offset_left > 0) {
            // make a temporary buffer, filled with zeroes at the start
            // (don't worry, only happens once)

            paddingbuf = talloc_size(ac, ac->sample_size * ao->channels *
                                         (ac->offset_left + len));
            fill_with_padding(paddingbuf, ac->offset_left, ac->sample_size,
                              ac->sample_padding);
            data = (char *) paddingbuf + ac->sample_size * ao->channels *
                                         ac->offset_left;
            bufpos -= ac->offset_left; // yes, negative!
            ptsoffset += ac->offset_left;
            ac->offset_left = 0;

            // now adjust the bufpos so the final value of bufpos is positive!
            /*
              int cnt = (len - bufpos) / ac->aframesize;
              int finalbufpos = bufpos + cnt * ac->aframesize;
            */
            int finalbufpos = len - (len - bufpos) % ac->aframesize;
            if (finalbufpos < 0) {
                mp_msg(MSGT_ENCODE, MSGL_WARN, "ao-lavc: cannot attain the "
                       "exact requested audio sync; shifting by %d frames\n",
                       -finalbufpos);
                bufpos -= finalbufpos;
            }
        }
    }

    // fix the discontinuity pts offset
    if (ectx->discontinuity_pts_offset == MP_NOPTS_VALUE) {
        nextpts = ao->pts + ptsoffset / (double) ao->samplerate;
        ectx->discontinuity_pts_offset = ectx->next_in_pts - nextpts;
    }

    while (len - bufpos >= ac->aframesize) {
        encode(ao, ao->pts != MP_NOPTS_VALUE,
               ao->pts + (bufpos + ptsoffset) / (double) ao->samplerate +
               encode_lavc_getoffset(ectx, ac->stream),
               (char *) data + ac->sample_size * bufpos * ao->channels);
        bufpos += ac->aframesize;
    }

    talloc_free(paddingbuf);

    // set next allowed output pts value
    nextpts = ao->pts + ectx->discontinuity_pts_offset + (bufpos + ptsoffset) / (double) ao->samplerate;
    if (nextpts > ectx->next_in_pts)
        ectx->next_in_pts = nextpts;

    return bufpos * ac->sample_size * ao->channels;
}

const struct ao_driver audio_out_lavc = {
    .is_new = true,
    .info = &(const struct ao_info) {
        "audio encoding using libavcodec",
        "lavc",
        "Rudolf Polzer <divVerent@xonotic.org>",
        ""
    },
    .init      = init,
    .uninit    = uninit,
    .get_space = get_space,
    .play      = play,
};
