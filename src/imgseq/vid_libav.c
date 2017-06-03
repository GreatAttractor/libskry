/*
libskry - astronomical image stacking
Copyright (C) 2016 Filip Szczerek <ga.software@yahoo.com>

This file is part of libskry.

Libskry is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Libskry is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with libskry.  If not, see <http://www.gnu.org/licenses/>.

File description:
    Video support via libav
*/

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <skry/image.h>
#include <skry/imgseq.h>

#include "imgseq_internal.h"
#include "../utils/logging.h"
#include "video.h"


struct libav_data
{
    char *file_name;

    enum SKRY_pixel_format pix_fmt;

    AVFormatContext *fmt_ctx;
    AVCodecContext *video_dec_ctx;
    int video_stream_idx;

    // Buffer for decoded frame
    uint8_t *video_dst_data[4];
    int video_dst_line_size[4];

    AVFrame *frame;
    AVPacket packet;
};

static
void vid_libav_deactivate_img_seq(SKRY_ImgSequence *img_seq)
{
    // Currently does nothing; the video file is kept open until a call to libav_free()
}

static
void vid_libav_free(SKRY_ImgSequence *img_seq)
{
    if (img_seq)
    {
        if (img_seq->data)
        {
            struct libav_data *data = (struct libav_data *)img_seq->data;
            free(data->file_name);
            av_free(data->video_dst_data[0]);
            av_frame_free(&data->frame);
            avcodec_free_context(&data->video_dec_ctx);
            avformat_close_input(&data->fmt_ctx);

            free(img_seq->data);
        }
        free(img_seq);
    }
}

static
enum SKRY_result vid_libav_get_curr_img_metadata(
    const SKRY_ImgSequence *img_seq,
    unsigned *width, unsigned *height,
    enum SKRY_pixel_format *pix_fmt)
{
    const struct libav_data *data = img_seq->data;
    if (width)
        *width = data->video_dec_ctx->width;
    if (height)
        *height = data->video_dec_ctx->height;
    if (pix_fmt)
        *pix_fmt = data->pix_fmt;

    return SKRY_SUCCESS;
}

/** Decodes frame contents into data->video_dst_data;
    returns number of bytes used or <0 on error. */
static
int decode_packet(struct libav_data *data, int *got_frame, enum SKRY_result *result)
{
    int decoded = data->packet.size;

    *got_frame = 0;

    if (data->packet.stream_index == data->video_stream_idx)
    {
        if (avcodec_decode_video2(data->video_dec_ctx, data->frame, got_frame, &data->packet) < 0)
        {
            if (result)
                *result = SKRY_LIBAV_DECODING_ERROR;
            return -1;
        }

        if (*got_frame)
        {
            // TODO: react to a changed frame width or height

            av_image_copy(data->video_dst_data, data->video_dst_line_size,
                          (const uint8_t **)data->frame->data,
                          data->frame->linesize,
                          data->video_dec_ctx->pix_fmt,
                          data->video_dec_ctx->width,
                          data->video_dec_ctx->height);
        }
    }

    if (*got_frame)
        av_frame_unref(data->frame);

    if (result)
        *result = SKRY_SUCCESS;

    return decoded;
}

static
SKRY_Image *vid_libav_get_img_by_index(const SKRY_ImgSequence *img_seq,
                                       size_t index, enum SKRY_result *result)
{
    struct libav_data *data = img_seq->data;
    if (avformat_seek_file(data->fmt_ctx, data->video_stream_idx,
                           index, index, index,
                           AVSEEK_FLAG_FRAME | AVSEEK_FLAG_ANY) < 0)
    {
        if (result)
            *result = SKRY_FILE_IO_ERROR;
        return NULL;
    }

    data->packet.data = NULL;
    data->packet.size = 0;

    if (av_read_frame(data->fmt_ctx, &data->packet) < 0)
    {
        if (result)
            *result = SKRY_FILE_IO_ERROR;
        return NULL;
    }

    int got_frame;
    AVPacket orig_pkt = data->packet;

    do
    {
        int ret;
        if ((ret = decode_packet(data, &got_frame, result)) < 0)
        {
            av_packet_unref(&orig_pkt);
            if (result)
                *result = SKRY_LIBAV_DECODING_ERROR;
            return NULL;
        }

        data->packet.data += ret;
        data->packet.size -= ret;
    } while (data->packet.size > 0);

    SKRY_Image *img = NULL;

    if (got_frame)
    {
        img = SKRY_new_image(data->video_dec_ctx->width,
                             data->video_dec_ctx->height,
                             data->pix_fmt, NULL, 0);

        if (!img)
        {
            if (result)
                *result = SKRY_OUT_OF_MEMORY;
            av_packet_unref(&orig_pkt);
            return NULL;
        }

        const size_t dest_BPP = SKRY_get_bytes_per_pixel(img);
        uint8_t *dest = SKRY_get_line(img, 0);
        const ptrdiff_t step = SKRY_get_line_stride_in_bytes(img);

        const uint8_t *src = data->video_dst_data[0];

        for (int line = 0; line < data->video_dec_ctx->height; line++)
        {
            memcpy(dest, src, data->video_dec_ctx->width * dest_BPP);
            dest += step;
            src += data->video_dst_line_size[0];
        }
    }

    av_packet_unref(&orig_pkt);

    return img;
}

static
SKRY_Image *vid_libav_get_current_img(const SKRY_ImgSequence *img_seq, enum SKRY_result *result)
{
    return vid_libav_get_img_by_index(img_seq, SKRY_get_curr_img_idx(img_seq), result);
}

#define FAIL_ON_NULL(ptr)                         \
    if (!(ptr))                                   \
    {                                             \
        SKRY_free_img_sequence(img_seq);          \
        if (result) *result = SKRY_OUT_OF_MEMORY; \
        return 0;                                 \
    }

#define FAIL(error_code)                          \
    {                                             \
        SKRY_free_img_sequence(img_seq);          \
        if (result) *result = error_code;         \
        return 0;                                 \
    }

SKRY_ImgSequence *init_libav_video_file(const char *file_name,
                                  SKRY_ImagePool *img_pool, ///< May be null
                                  /// If not null, receives operation result
                                  enum SKRY_result *result)
{
    SKRY_ImgSequence *img_seq = malloc(sizeof(*img_seq));
    FAIL_ON_NULL(img_seq);

    *img_seq = (SKRY_ImgSequence) { 0 };
    img_seq->data = malloc(sizeof(struct libav_data));
    FAIL_ON_NULL(img_seq->data);

    img_seq->type = SKRY_IMG_SEQ_LIBAV_VIDEO;
    img_seq->free = vid_libav_free;
    img_seq->get_curr_img = vid_libav_get_current_img;
    img_seq->get_curr_img_metadata = vid_libav_get_curr_img_metadata;
    img_seq->get_img_by_index = vid_libav_get_img_by_index;
    img_seq->deactivate_img_seq = vid_libav_deactivate_img_seq;

    struct libav_data *data = img_seq->data;
    *data = (struct libav_data) { 0 };

    data->file_name = malloc(strlen(file_name) + 1);
    FAIL_ON_NULL(data->file_name);
    strcpy(data->file_name, file_name);

    if (avformat_open_input(&data->fmt_ctx, file_name, NULL, NULL) < 0)
        FAIL(SKRY_CANNOT_OPEN_FILE);

    if (avformat_find_stream_info(data->fmt_ctx, NULL) < 0)
        FAIL(SKRY_UNSUPPORTED_FILE_FORMAT);

    if ((data->video_stream_idx = av_find_best_stream(data->fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0)
        FAIL(SKRY_LIBAV_NO_VID_STREAM);

    AVStream *st = data->fmt_ctx->streams[data->video_stream_idx];
    AVCodec *decoder = avcodec_find_decoder(st->codecpar->codec_id);
    if (!decoder)
        FAIL(SKRY_LIBAV_UNSUPPORTED_FORMAT);

    data->video_dec_ctx = avcodec_alloc_context3(decoder);
    if (!data->video_dec_ctx)
        FAIL(SKRY_OUT_OF_MEMORY);

    if (avcodec_parameters_to_context(data->video_dec_ctx, st->codecpar) < 0)
        FAIL(SKRY_LIBAV_INTERNAL_ERROR);

    AVDictionary *opts = NULL;
    av_dict_set(&opts, "refcounted_frames", "1", 0);

    if (avcodec_open2(data->video_dec_ctx, decoder, &opts) < 0)
        FAIL(SKRY_LIBAV_UNSUPPORTED_FORMAT);

    if (av_image_alloc(data->video_dst_data, data->video_dst_line_size,
                       data->video_dec_ctx->width,
                       data->video_dec_ctx->height,
                       data->video_dec_ctx->pix_fmt,
                       4 // required for formats with palette
                       ) < 0)
    {
        FAIL(SKRY_OUT_OF_MEMORY);
    }

    data->frame = av_frame_alloc();
    if (!data->frame)
        FAIL(SKRY_OUT_OF_MEMORY);

    switch(data->video_dec_ctx->pix_fmt)
    {
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_PAL8:  // non-gray palettes are not supported
        data->pix_fmt = SKRY_PIX_MONO8; break;

    case AV_PIX_FMT_BGR24: data->pix_fmt = SKRY_PIX_BGR8; break;
    case AV_PIX_FMT_BGR0: data->pix_fmt = SKRY_PIX_BGRA8; break;

    case AV_PIX_FMT_RGB24: data->pix_fmt = SKRY_PIX_RGB8; break;

    default: FAIL(SKRY_UNSUPPORTED_PIXEL_FORMAT);
    }

    av_init_packet(&data->packet);

    img_seq->num_images = data->fmt_ctx->streams[data->video_stream_idx]->nb_frames;

    LOG_MSG(SKRY_LOG_LIBAV_VIDEO, "Video size: %dx%d, %zu frames, pixel format: %s",
            data->video_dec_ctx->width,
            data->video_dec_ctx->height,
            img_seq->num_images,
            av_get_pix_fmt_name(data->video_dec_ctx->pix_fmt));

    base_init(img_seq, img_pool);

    if (result) *result = SKRY_SUCCESS;

    return img_seq;
}
