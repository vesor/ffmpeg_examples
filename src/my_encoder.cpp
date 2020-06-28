
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

#include <map>
#include <string>
#include <vector>

#define SCALE_FLAGS SWS_BICUBIC

#define DATA_FRAME_SIZE 128

// a wrapper around a single output AVStream
struct OutputStream {
    std::string topic;

    AVCodec *codec = 0;

    AVStream *st = 0;
    AVCodecContext *enc = 0;

    AVFrame *frame = 0;
    AVFrame *tmp_frame = 0;

    struct SwsContext *sws_ctx = 0;
    struct SwrContext *swr_ctx = 0;
};

std::map<std::string, OutputStream> topics_info;
AVOutputFormat *fmt;
AVFormatContext *oc;
    

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("pts:%ld pts_time:%f dts:%ld dts_time:%f duration:%ld duration_time:%f stream_index:%d\n",
           pkt->pts, pkt->pts * time_base->num / (double)time_base->den,
           pkt->dts, pkt->dts * time_base->num / (double)time_base->den,
           pkt->duration, pkt->duration * time_base->num / (double)time_base->den,
           pkt->stream_index);
}

static void log_metadata(const AVDictionary* metadata)
{
    AVDictionaryEntry *tag = NULL;
    printf("log metadata: ");
    while ((tag = av_dict_get(metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
        printf("%s=%s ", tag->key, tag->value);
    printf("\n");
}

static int write_frame(AVFormatContext *fmt_ctx, AVCodecContext *c,
                       AVStream *st, AVFrame *frame)
{
    int ret;

    if (frame) {
        // test metadata
        // seems metadata is not supported for all formats: http://www.ffmpeg-archive.org/Per-frame-metadata-example-td4688736.html
        // av_dict_set(&frame->metadata, "meta_pts", std::to_string(frame->pts).c_str(), 0);
        
        // also opaque data is not encoded: https://stackoverflow.com/questions/39853810/opaque-pointer-in-ffmpeg-avframe

        // Maybe we can try SEI in h264.

        AVFrameSideData* sd = av_frame_new_side_data(frame, AV_FRAME_DATA_GOP_TIMECODE,
                                    sizeof(int64_t));
        if (!sd)
            return AVERROR(ENOMEM);
    } else {
        // frame == NULL is flush request

        printf("Flush codec.\n");
    }

    if (c->codec_id != AV_CODEC_ID_NONE)
    {
        // send the frame to the encoder
        ret = avcodec_send_frame(c, frame);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            fprintf(stderr, "Error sending a frame to the encoder: %s\n", errbuf);
            exit(1);
        }

        bool frame_wrote = false;
        while (ret >= 0) {
            AVPacket pkt = { 0 };

            ret = avcodec_receive_packet(c, &pkt);
            if (ret == AVERROR(EAGAIN)) {
                // printf("Codec need more input. \n");
                break;
            } else if (ret == AVERROR_EOF) {
                fprintf(stderr, "Codec eof. \n");
                break;
            } else if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                fprintf(stderr, "Error encoding a frame: %s\n", errbuf);
                exit(1);
            }

            // static uint64_t privateId = 0;
            // // Allocate dictionary and add appropriate key/value record
            // AVDictionary * frameDict = NULL;
            // av_dict_set(&frameDict, "private_id", std::to_string(privateId++).c_str(), 0);
            // // Pack dictionary to be able to use it as a side data in AVPacket
            // int frameDictSize = 0;
            // uint8_t *frameDictData = av_packet_pack_dictionary(frameDict, &frameDictSize);
            // // Free dictionary not used any more
            // av_dict_free(&frameDict);
            // // Add side_data to AVPacket which will be decoded
            // av_packet_add_side_data(&pkt, AVPacketSideDataType::AV_PKT_DATA_STRINGS_METADATA, frameDictData, frameDictSize);

            /* rescale output packet timestamp values from codec to stream timebase */
            av_packet_rescale_ts(&pkt, c->time_base, st->time_base);
            pkt.stream_index = st->index;
            if (pkt.pts == 0) {
                AVDictionary* metadata;
                metadata = st->metadata;
                // metadata = fmt_ctx->metadata;
                av_dict_set(&metadata, "pts_start_time", "2020-06-26", 0);
                char char_arr [101];
                snprintf(char_arr, 100, "topic_%d", st->index);
                av_dict_set(&metadata, "stream", char_arr, 0);

                log_metadata(metadata);
            }
            /* Write the compressed frame to the media file. */
            log_packet(fmt_ctx, &pkt);
            ret = av_interleaved_write_frame(fmt_ctx, &pkt);
            frame_wrote = true;

            av_packet_unref(&pkt);
            if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                fprintf(stderr, "Error while writing output packet: %s\n", errbuf);
                exit(1);
            }
        }

        if (frame && !frame_wrote) {
            fprintf(stderr, "Warning: frame buffered. Check if zerolatency is implemented by codec.\n");
            // exit(1);
        }
    } else {

        if (frame) {
            AVPacket pkt = {0};
            av_new_packet(&pkt, DATA_FRAME_SIZE);
            // pkt.buf = frame->buf[0];
            // av_buffer_ref(pkt.buf);
            // pkt.data = pkt.buf->data;
            // pkt.size = DATA_FRAME_SIZE;
            pkt.pts = frame->pts;
            pkt.dts = frame->pts;
            log_packet(fmt_ctx, &pkt);
            ret = av_interleaved_write_frame(fmt_ctx, &pkt);
        } else {
            ret = AVERROR_EOF;
        }
    }

    return ret == AVERROR_EOF ? 1 : 0;
}

static AVCodec* select_video_codec(const char** selected_codec_name)
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
static void add_stream(OutputStream *ost, const AVMediaType media_type)
{
    AVCodecContext *c;
    
    /* find the encoder */
    // *codec = avcodec_find_encoder(codec_id);

    const char* video_codec_name = NULL;
    if (media_type == AVMEDIA_TYPE_VIDEO) {
        ost->codec = select_video_codec(&video_codec_name);
        if (!ost->codec) {
            fprintf(stderr, "Could not find encoder\n");
            exit(1);
        }
        printf("Select video codec: %s\n", video_codec_name);
    } else if (media_type == AVMEDIA_TYPE_DATA) {
        ost->codec = avcodec_find_encoder(AV_CODEC_ID_NONE); // AV_CODEC_ID_WRAPPED_AVFRAME AV_CODEC_ID_BIN_DATA
        printf("Select data codec: %p\n", ost->codec);
    } else {
        fprintf(stderr, "AVMediaType not supported\n");
        exit(1);
    }

    ost->st = avformat_new_stream(oc, ost->codec);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    ost->st->id = oc->nb_streams-1;
    c = avcodec_alloc_context3(ost->codec);
    if (!c) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }
    ost->enc = c;

    if (ost->codec) {
        c->codec_id = ost->codec->id;
        
        // no need, done in avcodec_parameters_from_context
        // ost->st->codecpar->codec_id = ost->codec->id;
        // printf("Stream codecpar:%i\n", ost->st->codecpar->codec_id);
        
    } else {
        c->codec_id = AV_CODEC_ID_NONE;
        c->codec_type = AVMEDIA_TYPE_DATA;
    }

    
    if (media_type == AVMEDIA_TYPE_VIDEO) {
        
        if (strstr(video_codec_name, "nvenc"))
        { // for nvenc

            // I don't know why vbr + cq not working for nvenc
            // av_opt_set(c->priv_data, "rc", "vbr_hq", 0);
            // av_opt_set(c->priv_data, "cq", "1", 0);

            av_opt_set(c->priv_data, "preset", "fast", 0);
            // av_opt_set(c->priv_data, "zerolatency", "1", 0);
            av_opt_set(c->priv_data, "rc", "constqp", 0);
            av_opt_set(c->priv_data, "qp", "30", 0);
        } else {
            // c->global_quality = 10;
            // c->compression_level = 12;
            // av_opt_set(c->priv_data, "preset", "slow", 0);
            // c->bit_rate = 400000; // don't set bit rate if we use crf
            
            av_opt_set(c->priv_data, "preset", "fast", 0);
            // av_opt_set(c->priv_data, "tune", "zerolatency", 0);
            // av_opt_set(c->priv_data, "qp", "30", 0);
            av_opt_set(c->priv_data, "crf", "20", 0);
        }

        /* Resolution must be a multiple of two. */
        c->width    = 1280;
        c->height   = 720;

        c->gop_size      = 8; /* emit one intra frame every 8 frames at most */
        c->pix_fmt       = AV_PIX_FMT_YUV420P;
        c->max_b_frames = 0; //disable B frames
        
        
    } else if (media_type == AVMEDIA_TYPE_DATA) {
        
    }

    
    /* timebase: This is the fundamental unit of time (in seconds) in terms
        * of which frame timestamps are represented. For fixed-fps content,
        * timebase should be 1/framerate and timestamp increments should be
        * identical to 1. */
    c->framerate = (AVRational){0, 1}; //set as unknown
    c->time_base = (AVRational){1, 1000}; // set time unit as 1e-3 second
    ost->st->time_base = c->time_base;

    
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

static AVFrame *alloc_data_frame(int size)
{
    AVFrame *frame;
    int ret;

    frame = av_frame_alloc();
    if (!frame)
        return NULL;

    /* allocate the buffers for the frame data */
    frame->buf[0] = av_buffer_alloc(size);
    frame->data[0] = frame->buf[0]->data;

    return frame;
}

static void open_stream(OutputStream *ost, AVDictionary *opt_arg, const AVMediaType media_type)
{
    int ret;
    AVCodecContext *c = ost->enc;
    AVDictionary *opt = NULL;

    av_dict_copy(&opt, opt_arg, 0);

    if (media_type == AVMEDIA_TYPE_VIDEO) {
        // av_dict_set(&opt,"preset","fast",0);
        // av_dict_set(&opt,"tune","zerolatency",0); // don't buffer frames
        // av_dict_set(&opt,"profile","main",0);
    }

    if (c->codec_id != AV_CODEC_ID_NONE) {
        /* open the codec */
        ret = avcodec_open2(c, ost->codec, &opt);
        av_dict_free(&opt);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            fprintf(stderr, "Could not open video codec: %s\n", errbuf);
            exit(1);
        }
    }

    if (media_type == AVMEDIA_TYPE_VIDEO) {
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
    } else if (media_type == AVMEDIA_TYPE_DATA) {
        ost->frame = alloc_data_frame(DATA_FRAME_SIZE);
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }

    {
        AVCodecParameters *par = ost->st->codecpar;
        printf("Codecpar: %i %i\n", par->codec_id, par->codec_type);
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
            pict->data[0][y * pict->linesize[0] + x] = (x + y + i * 3)/(1+stream_index);

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            pict->data[1][y * pict->linesize[1] + x] = (128 + y + i * 2);
            pict->data[2][y * pict->linesize[2] + x] = (64 + x + i * 5);
        }
    }
}

static AVFrame *get_video_frame(OutputStream *ost, int* frame_index)
{
    AVCodecContext *c = ost->enc;

    int stream_index = ost->st->index;

    if (stream_index == 0) {
        if (*frame_index > 25) return NULL;
    } else {
        if (*frame_index > 100) return NULL;
    }

    printf("video stream_index %i frame_index %i\n", stream_index, *frame_index);    
    
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

static AVFrame *get_data_frame(OutputStream *ost, int* frame_index)
{
    if (*frame_index > 25) return NULL;

    printf("data frame_index %i\n", *frame_index);   

    AVFrame *frame = ost->frame;
    
    uint8_t *q = frame->data[0];

    snprintf((char*)q, DATA_FRAME_SIZE, "data %i", *frame_index);

    int t_scale = 200;
    ost->frame->pts = *frame_index * t_scale;
    *frame_index += 1;

    return frame;
}

static AVFrame *get_frame(OutputStream *ost, int* frame_index, const AVMediaType media_type)
{
    if (media_type == AVMEDIA_TYPE_VIDEO) {
        return get_video_frame(ost, frame_index);
    } else if (media_type == AVMEDIA_TYPE_DATA) {
        return get_data_frame(ost, frame_index);
    } else {
        return NULL;
    }
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}

void add_topic(const std::string& topic, const AVMediaType media_type)
{
    OutputStream ost;
    ost.topic = topic;

    /* Add the video streams using the default format codecs
    * and initialize the codecs. */
    add_stream(&ost, media_type);

    /* Now that all the parameters are set, we can open the 
    * video codecs and allocate the necessary encode buffers. */
    open_stream(&ost, NULL, media_type);
    
    topics_info[topic] = ost;
}

void on_frame(const std::string& topic, AVFrame* frame, const AVMediaType media_type)
{
    const auto iter = topics_info.find(topic);

    // Do initialize for new topic
    if (iter == topics_info.end()) {
        fprintf(stderr, "Topic: %s not initialized\n", topic.c_str());
        exit(1);
    }

    auto& ost = topics_info[topic];
    int stream_eof = write_frame(oc, ost.enc, ost.st, frame);
    if (stream_eof) {
        fprintf(stderr, "Error eof stream");
        exit(1);
    }
}

int main(int argc, char **argv)
{
    const char *filename;
    int ret;
     
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

    add_topic("/camera0", AVMEDIA_TYPE_VIDEO);
    // add_topic("/camera1", AVMEDIA_TYPE_VIDEO);
    add_topic("/data0", AVMEDIA_TYPE_DATA);

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
    AVDictionary *opt = NULL;
    ret = avformat_write_header(oc, &opt);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        fprintf(stderr, "Error occurred when opening output file: %s\n", errbuf);
        return 1;
    }

    std::vector<int> s_frame_index(oc->nb_streams, 0);
    
    while (1) {
        /* select the stream to encode */
        bool has_frame = false;
        for (auto& pr : topics_info) {
            auto& ost = pr.second;
            int stream_index = ost.st->index;
            const AVMediaType codec_type = ost.st->codecpar->codec_type;
            AVFrame *frame = get_frame(&ost, &s_frame_index[stream_index], codec_type);
            if (frame) {
                on_frame(ost.topic, frame, codec_type);
                has_frame = true;
            }
        }

        if (!has_frame) break; //all streams end
    }

    // flush encoder
    for (auto& pr : topics_info) {
        auto& ost = pr.second;
        write_frame(oc, ost.enc, ost.st, NULL);
    }

    
    for (const auto& pr : topics_info) {
        log_metadata(pr.second.st->metadata);
    }

    for (int i = 0; i < oc->nb_streams; i++) {
        AVCodecParameters *par = oc->streams[i]->codecpar;
        printf("stream %i codecpar %i\n", i, par->codec_id);
    }


    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(oc);

    /* Close each codec. */
    for (auto& pr : topics_info) {
        close_stream(oc, &pr.second);
    }

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&oc->pb);

    /* free the stream */
    avformat_free_context(oc);

    return 0;
}
