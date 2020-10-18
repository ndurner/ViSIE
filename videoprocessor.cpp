#include "videoprocessor.h"


VideoProcessor::VideoProcessor(QObject *parent) : QObject(parent), ctx(nullptr)
{

}

VideoProcessor::~VideoProcessor()
{
    cleanup();
}

void VideoProcessor::loadVideo(QString fn)
{
    cleanup();
    ctx = avformat_alloc_context();

    try {
        // open file
        if (avformat_open_input(&ctx, fn.toLocal8Bit(), NULL, NULL) != 0)
            throw QString("Cannot open file");

        // access video stream
        if (avformat_find_stream_info(ctx, NULL) < 0)
            throw QString("Cannot read video stream info");

        videoStrm = av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoStrm < 0) {
            if (videoStrm == AVERROR_STREAM_NOT_FOUND)
                throw QString("Video stream not found");
            else if (videoStrm == AVERROR_DECODER_NOT_FOUND)
                throw QString("Decoder not found");
            else
                throw QString("Video stream not found: unknown error");
        }

        // report number of frames
        emit frameCount(ctx->streams[videoStrm]->nb_frames);

        // success!
        emit loadSuccess();
    }
    catch (QString msg) {
        emit loadError(msg);
        cleanup();
    }
}

void VideoProcessor::cleanup()
{
    if (ctx)
        avformat_free_context(ctx);
}
