
#include "my_encoder.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS
#define __STDC_FORMAT_MACROS

extern "C"
{
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#define SCALE_FLAGS SWS_BICUBIC

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream *st;
    AVCodecContext *enc;

    AVFrame *frame;
    AVFrame *tmp_frame;

    float t, tincr, tincr2;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
} OutputStream;

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("pts:%ld pts_time:%f dts:%ld dts_time:%f duration:%ld duration_time:%f stream_index:%d\n",
           pkt->pts, pkt->pts * time_base->num / (double)time_base->den,
           pkt->dts, pkt->dts * time_base->num / (double)time_base->den,
           pkt->duration, pkt->duration * time_base->num / (double)time_base->den,
           pkt->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx, AVCodecContext *c,
                       AVStream *st, AVFrame *frame)
{
    int ret;

    // send the frame to the encoder
    ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        fprintf(stderr, "Error sending a frame to the encoder: %s\n", errbuf);
        exit(1);
    }

    while (ret >= 0) {
        AVPacket pkt = { 0 };

        ret = avcodec_receive_packet(c, &pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            fprintf(stderr, "Error encoding a frame: %s\n", errbuf);
            exit(1);
        }

        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(&pkt, c->time_base, st->time_base);
        pkt.stream_index = st->index;
        if (pkt.pts == 0) {
            av_dict_set(&fmt_ctx->metadata, "pts_start_time", "2020-06-26", 0);
            char char_arr [101];
            snprintf(char_arr, 100, "topic_%d", st->index);
            av_dict_set(&fmt_ctx->metadata, "stream", char_arr, 0);
        }
        /* Write the compressed frame to the media file. */
        log_packet(fmt_ctx, &pkt);
        ret = av_interleaved_write_frame(fmt_ctx, &pkt);

        av_packet_unref(&pkt);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            fprintf(stderr, "Error while writing output packet: %s\n", errbuf);
            exit(1);
        }
    }

    return ret == AVERROR_EOF ? 1 : 0;
}

static AVCodec* select_codec(const char** selected_codec_name)
{
    const char* codec_names[] = {
        "h264_nvenc", "nvenc_h264", "nvenc",
        // "h264_v4l2m2m", "h264_vaapi",
        "libx264"
    };

    AVCodec* codec = NULL;
    for (int i = 0; i < sizeof(codec_names)/sizeof(codec_names[0]); ++i) {
        const char* codec_name = codec_names[i];
        codec = avcodec_find_encoder_by_name(codec_name);
        if (!codec) {
            fprintf(stderr, "Codec '%s' not found\n", codec_name);
            exit(1);
        } else {
            *selected_codec_name = codec_name;
            break;
        }
    }

    return codec;
}

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
                       AVCodec **codec,
                       enum AVCodecID codec_id)
{
    AVCodecContext *c;
    
    /* find the encoder */
    // *codec = avcodec_find_encoder(codec_id);

    const char* selected_codec_name = NULL;
    *codec = select_codec(&selected_codec_name);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder\n");
        exit(1);
    }
    printf("Select codec: %s\n", selected_codec_name);

    ost->st = avformat_new_stream(oc, *codec);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    ost->st->id = oc->nb_streams-1;
    c = avcodec_alloc_context3(*codec);
    if (!c) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }
    ost->enc = c;

    c->codec_id = codec_id;

    if (strstr(selected_codec_name, "nvenc"))
    { // for nvenc

        // I don't know why vbr + cq not working for nvenc
        // av_opt_set(c->priv_data, "rc", "vbr_hq", 0);
        // av_opt_set(c->priv_data, "cq", "1", 0);

        av_opt_set(c->priv_data, "rc", "constqp", 0);
        av_opt_set(c->priv_data, "qp", "30", 0);
    } else {
        // c->global_quality = 10;
        // c->compression_level = 12;
        // av_opt_set(c->priv_data, "preset", "slow", 0);
        // c->bit_rate = 400000; // don't set bit rate if we use crf
        av_opt_set(c->priv_data, "crf", "20", 0);
    }
    

    /* Resolution must be a multiple of two. */
    c->width    = 1280;
    c->height   = 720;
    /* timebase: This is the fundamental unit of time (in seconds) in terms
        * of which frame timestamps are represented. For fixed-fps content,
        * timebase should be 1/framerate and timestamp increments should be
        * identical to 1. */
    c->framerate = (AVRational){0, 1}; //set as unknown
    c->time_base = (AVRational){1, 1000}; // set time unit as 1e-3 second
    ost->st->time_base = c->time_base;

    c->gop_size      = 8; /* emit one intra frame every 8 frames at most */
    c->pix_fmt       = AV_PIX_FMT_YUV420P;
    c->max_b_frames = 0; //disable B frames
    
    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }

    return picture;
}

static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *c = ost->enc;
    AVDictionary *opt = NULL;

    av_dict_copy(&opt, opt_arg, 0);

    // if(c->codec_id == AV_CODEC_ID_H264)
    {
        av_dict_set(&opt,"preset","fast",0);
        // av_dict_set(&opt,"tune","zerolatency",0);
        // av_dict_set(&opt,"profile","main",0);
    }

    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        fprintf(stderr, "Could not open video codec: %s\n", errbuf);
        exit(1);
    }

    /* allocate and init a re-usable frame */
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    ost->tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
        if (!ost->tmp_frame) {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}

/* Prepare a dummy image. */
static void fill_yuv_image(AVFrame *pict, int stream_index, int frame_index,
                           int width, int height)
{
    int x, y, i;

    i = frame_index;

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            pict->data[0][y * pict->linesize[0] + x] = (x + y + i * 3)/(2 - stream_index);

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            pict->data[1][y * pict->linesize[1] + x] = (128 + y + i * 2)/(2 - stream_index);
            pict->data[2][y * pict->linesize[2] + x] = (64 + x + i * 5)/(2 - stream_index);
        }
    }
}

static AVFrame *get_video_frame(OutputStream *ost, int* frame_index)
{
    AVCodecContext *c = ost->enc;

    int stream_index = ost->st->index;

    if (stream_index == 0) {
        if (*frame_index > 100) return NULL;
    } else {
        if (*frame_index > 50) return NULL;
    }
    
    
    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally; make sure we do not overwrite it here */
    if (av_frame_make_writable(ost->frame) < 0)
        exit(1);

    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        /* as we only generate a YUV420P picture, we must convert it
         * to the codec pixel format if needed */
        if (!ost->sws_ctx) {
            ost->sws_ctx = sws_getContext(c->width, c->height,
                                          AV_PIX_FMT_YUV420P,
                                          c->width, c->height,
                                          c->pix_fmt,
                                          SCALE_FLAGS, NULL, NULL, NULL);
            if (!ost->sws_ctx) {
                fprintf(stderr,
                        "Could not initialize the conversion context\n");
                exit(1);
            }
        }
        fill_yuv_image(ost->tmp_frame, stream_index, *frame_index, c->width, c->height);
        sws_scale(ost->sws_ctx, (const uint8_t * const *) ost->tmp_frame->data,
                  ost->tmp_frame->linesize, 0, c->height, ost->frame->data,
                  ost->frame->linesize);
    } else {
        fill_yuv_image(ost->frame, stream_index, *frame_index, c->width, c->height);
    }

    int t_scale = 100;
    if (stream_index == 0) {
        t_scale = 200;
    }

    ost->frame->pts = *frame_index * t_scale;
    *frame_index += 1;

    return ost->frame;
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(AVFormatContext *oc, OutputStream *ost, AVFrame* frame)
{
    return write_frame(oc, ost->enc, ost->st, frame);

}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}

int main(int argc, char **argv)
{
    OutputStream video_st = { 0 }, video_st2 = {0};
    const char *filename;
    AVOutputFormat *fmt;
    AVFormatContext *oc;
    AVCodec *video_codec, *video_codec2;
    int ret;
    AVDictionary *opt = NULL;
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <output file>\n", argv[0]);
        exit(0);
    }

    av_register_all();

    filename = argv[1];

    /* allocate the output media context */
    // AVOutputFormat of_request;

    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc) {
        // printf("Could not deduce output format from file extension: using MPEG.\n");
        // avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
        printf("Could not deduce output format\n");
        exit(1);
    }

    oc->oformat->video_codec = AV_CODEC_ID_NONE; // clear the default codec id, we will alloc codec directly in later.
    fmt = oc->oformat;

    /* Add the video streams using the default format codecs
     * and initialize the codecs. */
    // if (fmt->video_codec != AV_CODEC_ID_NONE) 
    {
        printf("Guessed video codec: %s\n", avcodec_get_name(fmt->video_codec));
        add_stream(&video_st, oc, &video_codec, fmt->video_codec);
        add_stream(&video_st2, oc, &video_codec2, fmt->video_codec);
    }

    /* Now that all the parameters are set, we can open the 
     * video codecs and allocate the necessary encode buffers. */
    open_video(oc, video_codec, &video_st, opt);
    open_video(oc, video_codec2, &video_st2, opt);

    av_dump_format(oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            fprintf(stderr, "Could not open '%s': %s\n", filename, errbuf);
            return 1;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(oc, &opt);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        fprintf(stderr, "Error occurred when opening output file: %s\n", errbuf);
        return 1;
    }

    static int s_frame_index = 0;
    static int s_frame_index2 = 0;

    while (1) {
        /* select the stream to encode */

        AVFrame *frame = get_video_frame(&video_st, &s_frame_index);
        if (frame) {
            int video_eof = write_video_frame(oc, &video_st, frame);
            if (video_eof) {
                fprintf(stderr, "Error eof stream");
                exit(1);
            }
        } 
        
        AVFrame *frame2 = get_video_frame(&video_st2, &s_frame_index2);
        if (frame2) {
            int video_eof = write_video_frame(oc, &video_st2, frame2);
            if (video_eof) {
                fprintf(stderr, "Error eof stream2");
                exit(1);
            }
        }

        if (!frame && !frame2) break; //all streams end
    }

    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(oc);

    /* Close each codec. */
    close_stream(oc, &video_st);

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&oc->pb);

    /* free the stream */
    avformat_free_context(oc);

    return 0;
}
