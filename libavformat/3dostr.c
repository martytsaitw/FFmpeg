/*
 * 3DO STR demuxer
 * Copyright (c) 2015 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avformat.h"
#include "internal.h"

static int threedostr_probe(AVProbeData *p)
{
    if (memcmp(p->buf, "CTRL", 4) &&
        memcmp(p->buf, "SHDR", 4) &&
        memcmp(p->buf, "SNDS", 4))
        return 0;

    return AVPROBE_SCORE_MAX / 3 * 2;
}

static int threedostr_read_header(AVFormatContext *s)
{
    unsigned chunk, codec = 0, size, ctrl_size = -1, found_shdr = 0;
    AVStream *st;

    while (!avio_feof_xij(s->pb) && !found_shdr) {
        chunk = avio_rl32_xij(s->pb);
        size  = avio_rb32_xij(s->pb);

        if (size < 8)
            return AVERROR_INVALIDDATA;
        size -= 8;

        switch (chunk) {
        case MKTAG('C','T','R','L'):
            ctrl_size = size;
            break;
        case MKTAG('S','N','D','S'):
            if (size < 56)
                return AVERROR_INVALIDDATA;
            avio_skip_xij(s->pb, 8);
            if (avio_rl32_xij(s->pb) != MKTAG('S','H','D','R'))
                return AVERROR_INVALIDDATA;
            avio_skip_xij(s->pb, 24);

            st = avformat_new_stream_ijk(s, NULL);
            if (!st)
                return AVERROR(ENOMEM);

            st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
            st->codecpar->sample_rate = avio_rb32_xij(s->pb);
            st->codecpar->channels    = avio_rb32_xij(s->pb);
            if (st->codecpar->channels <= 0)
                return AVERROR_INVALIDDATA;
            codec                  = avio_rl32_xij(s->pb);
            avio_skip_xij(s->pb, 4);
            if (ctrl_size == 20 || ctrl_size == 3 || ctrl_size == -1)
                st->duration       = (avio_rb32_xij(s->pb) - 1) / st->codecpar->channels;
            else
                st->duration       = avio_rb32_xij(s->pb) * 16 / st->codecpar->channels;
            size -= 56;
            found_shdr = 1;
            break;
        case MKTAG('S','H','D','R'):
            if (size >  0x78) {
                avio_skip_xij(s->pb, 0x74);
                size -= 0x78;
                if (avio_rl32_xij(s->pb) == MKTAG('C','T','R','L') && size > 4) {
                    ctrl_size = avio_rb32_xij(s->pb);
                    size -= 4;
                }
            }
            break;
        default:
            av_log(s, AV_LOG_DEBUG, "skipping unknown chunk: %X\n", chunk);
            break;
        }

        avio_skip_xij(s->pb, size);
    }

    switch (codec) {
    case MKTAG('S','D','X','2'):
        st->codecpar->codec_id    = AV_CODEC_ID_SDX2_DPCM;
        st->codecpar->block_align = 1 * st->codecpar->channels;
        break;
    default:
        avpriv_request_sample(s, "codec %X", codec);
        return AVERROR_PATCHWELCOME;
    }

    avpriv_set_pts_info_ijk(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int threedostr_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    unsigned chunk, size, found_ssmp = 0;
    AVStream *st = s->streams[0];
    int64_t pos;
    int ret = 0;

    while (!found_ssmp) {
        if (avio_feof_xij(s->pb))
            return AVERROR_EOF;

        pos   = avio_tell(s->pb);
        chunk = avio_rl32_xij(s->pb);
        size  = avio_rb32_xij(s->pb);

        if (!size)
            continue;

        if (size < 8)
            return AVERROR_INVALIDDATA;
        size -= 8;

        switch (chunk) {
        case MKTAG('S','N','D','S'):
            if (size <= 16)
                return AVERROR_INVALIDDATA;
            avio_skip_xij(s->pb, 8);
            if (avio_rl32_xij(s->pb) != MKTAG('S','S','M','P'))
                return AVERROR_INVALIDDATA;
            avio_skip_xij(s->pb, 4);
            size -= 16;
            ret = av_get_packet_xij(s->pb, pkt, size);
            pkt->pos = pos;
            pkt->stream_index = 0;
            pkt->duration = size / st->codecpar->channels;
            size = 0;
            found_ssmp = 1;
            break;
        default:
            av_log(s, AV_LOG_DEBUG, "skipping unknown chunk: %X\n", chunk);
            break;
        }

        avio_skip_xij(s->pb, size);
    }

    return ret;
}

AVInputFormat ff_threedostr_demuxer = {
    .name           = "3dostr",
    .long_name      = NULL_IF_CONFIG_SMALL("3DO STR"),
    .read_probe     = threedostr_probe,
    .read_header    = threedostr_read_header,
    .read_packet    = threedostr_read_packet,
    .extensions     = "str",
    .flags          = AVFMT_GENERIC_INDEX,
};
