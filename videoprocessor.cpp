#include "videoprocessor.h"

#include <QDebug>
#include <memory>

VideoProcessor::VideoProcessor(QObject *parent) : QObject(parent)
{
    ctx = nullptr;
    cnvCtx = nullptr;
    width = height = 0;
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
    std::unique_ptr<AVFrame, std::function<void(AVFrame *)>> frame(av_frame_alloc(), [] (AVFrame *f) {
        av_frame_free(&f);
    });

    const auto frm = frame.get();

    if (av_seek_frame(ctx, videoStrm, pts, 0) < 0) {
        qWarning() << "cannot seek to frame" << pts;
        return;
    }

    do {
        if (av_read_frame(ctx, packet.get()) < 0) {
            qWarning() << "error reading frame";
            return;
        }

        if (packet->stream_index == videoStrm) {
            avcodec_send_packet(codecCtx, packet.get());
            rcErr = avcodec_receive_frame(codecCtx, frm);
        }
    } while (rcErr == AVERROR(EAGAIN));

    auto fmt = static_cast<AVPixelFormat>(frm->format);
    if (!cnvCtx) {
        cnvCtx = sws_getContext(frm->width, frm->height, fmt, width, height,
                               AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR,
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

void VideoProcessor::cleanup()
{
    if (ctx)
        avformat_free_context(ctx);
    if (codecCtx)
        avcodec_free_context(&codecCtx);
    if (cnvCtx)
        sws_freeContext(cnvCtx);
}
