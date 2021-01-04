#include "videoprocessor.h"

#include <QDebug>
#include <memory>
#include <libheif/heif.h>

#include "scopedresource.h"

VideoProcessor::VideoProcessor(QObject *parent) : QObject(parent)
{
    ctx = nullptr;
    cnvCtx = nullptr;
    codecCtx = nullptr;
    width = height = 0;

    frmBuf[0].frm = nullptr;
    frmBuf[1].frm = nullptr;
    frmBuf[0].other = &frmBuf[1];
    frmBuf[1].other = &frmBuf[0];
}

VideoProcessor::~VideoProcessor()
{
    cleanup();
}

void VideoProcessor::setDimensions(int width, int height)
{
    if (width != this->width || height != this->height) {
        sws_freeContext(cnvCtx);
        cnvCtx = nullptr;

        av_frame_free(&frmBuf[0].frm);
        av_frame_free(&frmBuf[1].frm);

        this->width = width;
        this->height = height;
    }
}

void VideoProcessor::loadVideo(QString fn)
{
    cleanup();
    ctx = avformat_alloc_context();

    try {
        AVCodec *videoCodec;

        // open file
        if (avformat_open_input(&ctx, fn.toLocal8Bit(), NULL, NULL) != 0)
            throw QString("Cannot open file");

        // access video stream
        if (avformat_find_stream_info(ctx, NULL) < 0)
            throw QString("Cannot read video stream info");

        videoStrm = av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);
        if (videoStrm < 0) {
            if (videoStrm == AVERROR_STREAM_NOT_FOUND)
                throw QString("Video stream not found");
            else if (videoStrm == AVERROR_DECODER_NOT_FOUND)
                throw QString("Decoder not found");
            else
                throw QString("Video stream not found: unknown error");
        }

        // report number of frames
        emit streamLength(ctx->streams[videoStrm]->duration);

        // establish codec
        codecCtx = avcodec_alloc_context3(videoCodec);
        if (!codecCtx)
            throw QString("cannot create codec context");

        avcodec_parameters_to_context(codecCtx, ctx->streams[videoStrm]->codecpar);

        if (avcodec_open2(codecCtx, videoCodec, nullptr) < 0)
            throw QString("cannot open codec {0}").arg(videoCodec->long_name);

        frmBuf[0].frm = av_frame_alloc();
        frmBuf[1].frm = av_frame_alloc();

        // success!
        emit loadSuccess();
    }
    catch (QString msg) {
        qCritical() << msg;
        emit loadError(msg);
        cleanup();
    }
}

void VideoProcessor::present(uint64_t pts)
{
    if (!codecCtx) {
        qCritical() << "video not loaded";
        return;
    }

    int rcErr = AVERROR(EAGAIN);

    std::unique_ptr<AVPacket, std::function<void(AVPacket *)>> packet(av_packet_alloc(), [] (AVPacket *p) {
        av_packet_free(&p);
    });

    // seek to keyframe
    if (av_seek_frame(ctx, videoStrm, pts, AVSEEK_FLAG_BACKWARD) < 0) {
        qWarning() << "cannot seek to frame" << pts;
        return;
    }
    avcodec_flush_buffers(codecCtx);

    // work towards target (intra)frame
    curFrm = &frmBuf[0];
    bool found = false;
    while (av_read_frame(ctx, packet.get()) == 0 && !found) {
        if (packet->stream_index == videoStrm) {
            avcodec_send_packet(codecCtx, packet.get());

            while (avcodec_receive_frame(codecCtx, curFrm->frm) == 0 && !found) {
                const auto curPts = curFrm->frm->pts;
                if (curPts > pts) {
                    // overshot, use last frame
                    curFrm = curFrm->other;
                    found = true;
                    break;
                }
                else if (curPts == pts) {
                    // target hit
                    found = true;
                    break;
                }
                else {
                    // need to decode more
                    curFrm = curFrm->other;
                }
            }
        }
    }

    const auto frm = curFrm->frm;
    if (frm->format == -1) {
        return;
    }

    // create RGB image for presentation
    auto fmt = static_cast<AVPixelFormat>(frm->format);
    if (!cnvCtx) {
        cnvCtx = sws_getContext(frm->width, frm->height, fmt, width, height,
                               AV_PIX_FMT_RGB24, SWS_POINT,
                               nullptr, nullptr, nullptr);
        Q_ASSERT(cnvCtx);
    }

    const uint8_t bytesPerPixel = 24 / 8;
    const int dstStride[1] = {bytesPerPixel * int(width)};
    uint8_t *outBuf = new uint8_t[width * height * bytesPerPixel];

    auto actualHeight = sws_scale(cnvCtx, frm->data, frm->linesize, 0,
                                  frm->height, &outBuf, dstStride);

    emit imgReady(QImage(outBuf, width, actualHeight, *dstStride, QImage::Format_RGB888,
                         [](void *buf) {
                            delete [] static_cast<uint8_t *>(buf);
                         },
                        outBuf));
}

void VideoProcessor::saveFrame()
{
    ScopedResource<heif_context, int> hCtx(
        [](heif_context *&ctx, int &){
            ctx = heif_context_alloc();
        },
        [](heif_context *ctx, int) {
            heif_context_free(ctx);
        });
    ScopedResource<heif_encoder, heif_error> encPtr(
        [&](heif_encoder *&enc, heif_error &err) {
            err = heif_context_get_encoder_for_format(hCtx.get(), heif_compression_HEVC, &enc);
        },
        [](heif_encoder *enc, heif_error err) {
            if (err.code == heif_error_Ok)
                heif_encoder_release(enc);
        }
    );

    if (encPtr.error().code != heif_error_Ok) {
        qCritical() << "encoder creation failed:" << encPtr.error().message;
        return;
    }

    // set quality
    const auto enc = encPtr.get();
    auto err = heif_encoder_set_lossless(enc, 1);
    if (err.code != heif_error_Ok) {
        qWarning() << "cannot enable lossless processing";

        err = heif_encoder_set_lossy_quality(enc, 100);
        if (err.code != heif_error_Ok) {
            qCritical() << "cannot configure quality level to encoder";
            return;
        }
    }

    // prepare color settings
    heif_colorspace cs;
    heif_chroma chroma;
    heif_channel channels[3];
    int widths[3], heights[3], depths[3];
    int n_channels;
    setHeifColor(cs, chroma, n_channels, channels, depths, widths, heights);

    // image
    ScopedResource<heif_image, heif_error> img(
        [&] (heif_image *&img, heif_error &err) {
            err = heif_image_create(curFrm->frm->width, curFrm->frm->height, cs, chroma, &img);
        },
        [] (heif_image *img, const heif_error &err) {
            if (err.code != heif_error_Ok)
                heif_image_release(img);
        }
    );

    for (int i = 0; i < n_channels; i++) {
        int stride;
        heif_image_add_plane(img.get(), channels[i], widths[i], heights[i], depths[i]);
        auto planeData = heif_image_get_plane(img.get(), channels[i], &stride);
        for (int h = 0; h < heights[i]; h++) {
            memcpy(planeData + (stride * h), curFrm->frm->data[i] + (curFrm->frm->linesize[i] * h),
                   stride);
        }
    }

    // encode
    ScopedResource<heif_image_handle, heif_error> imgH(
        [&](heif_image_handle *&imgH, heif_error &err) {
            err = heif_context_encode_image(hCtx.get(), img.get(), encPtr.get(), nullptr, &imgH);
        },
        [](heif_image_handle *imgH, const heif_error &err) {
            if (err.code != heif_error_Ok)
                heif_image_handle_release(imgH);
        });
    if (imgH.error().code != heif_error_Ok) {
        qCritical() << "encoder error:" << imgH.error().message;
        return;
    }

    err = heif_context_write_to_file(hCtx.get(), "/tmp/visie.heic");
    if (err.code != heif_error_Ok) {
        qCritical() << "error writing image:" << err.message;
        return;
    }
}

void VideoProcessor::cleanup()
{
    if (ctx)
        avformat_free_context(ctx);
    if (codecCtx)
        avcodec_free_context(&codecCtx);
    if (cnvCtx)
        sws_freeContext(cnvCtx);

    av_frame_free(&frmBuf[0].frm);
    av_frame_free(&frmBuf[1].frm);
}

void VideoProcessor::setHeifColor(heif_colorspace &space, heif_chroma &chroma, int &n_channels, heif_channel channels[], int depths[], int widths[], int heights[])
{
    switch(curFrm->frm->format) {
        case AV_PIX_FMT_YUVJ420P:
            n_channels = 3;
            space = heif_colorspace_YCbCr;
            chroma = heif_chroma_420;
            channels[0] = heif_channel_Y;
            depths[0] = 8;
            widths[0] = curFrm->frm->width;
            heights[0] = curFrm->frm->height;
            channels[1] = heif_channel_Cb;
            depths[1] = 8;
            widths[1] = (curFrm->frm->width + 1) / 2;
            heights[1] = (curFrm->frm->height + 1) / 2;
            channels[2] = heif_channel_Cr;
            depths[2] = depths[1];
            widths[2] = widths[1];
            heights[2] = heights[1];
            break;
        default:
            n_channels = 0;
            space = heif_colorspace_undefined;
            chroma = heif_chroma_undefined;
            break;
    }
}
