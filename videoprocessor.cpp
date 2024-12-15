#include "videoprocessor.h"

#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <memory>
#include <future>
#include <libheif/heif.h>

#include "scopedresource.h"
#include "mediareader.h"
#include "heifwriter.h"

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
        const AVCodec *videoCodec;

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

        const AVCodec *subCodec = nullptr;
        subStrm = av_find_best_stream(ctx, AVMEDIA_TYPE_SUBTITLE, -1, -1, &subCodec, 0);
        if (subStrm > 0) {
            subCodecCtx = avcodec_alloc_context3(subCodec);
            avcodec_parameters_to_context(subCodecCtx, ctx->streams[subStrm]->codecpar);

            if (avcodec_open2(subCodecCtx, subCodec, nullptr) < 0)
                qDebug() << QString("cannot open sub codec {0}").arg(subCodec->long_name);
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

        // rotation
        auto rota = av_dict_get(this->ctx->streams[videoStrm]->metadata, "rotate", nullptr, 0);
        rotation = rota ? atoi(rota->value) : 0;

        // success!
        emit loadSuccess();
    }
    catch (QString msg) {
        qCritical() << msg;
        emit loadError(msg);
        cleanup();
    }
}

QString fcs(int fourCC)
{
    char fc[5];
    fc[0] = (fourCC >> 24) & 255;
    fc[1] = (fourCC >> 16) & 255;
    fc[2] = (fourCC >> 8) & 255;
    fc[3] = (fourCC >> 0) & 255;
    fc[4] = 0;

    return QString::fromLatin1(fc, 4);
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

    frmBuf[0].frm->pts = -1;
    frmBuf[1].frm->pts = -1;

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
                    // overshot, use last frame if available
                    if (curFrm->other->frm->pts != -1) {
                        curFrm = curFrm->other;
                    }
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
        else if (ctx->streams[packet->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            acquireSubtitle(packet.get());
        }
    }

    processCurrentFrame();
}

int VideoProcessor::presentPrevNext(bool prev)
{
    if (prev) {
        present(curFrm->frm->pts - 1);
    }
    else {
        std::unique_ptr<AVPacket, std::function<void(AVPacket *)>> packet(av_packet_alloc(), [] (AVPacket *p) {
            av_packet_free(&p);
        });

        curFrm = curFrm->other;
        bool found = false;

        // is the next frame already available in the decoder?
        if (avcodec_receive_frame(codecCtx, curFrm->frm) != 0) {
            // not available, load next packet
            while (av_read_frame(ctx, packet.get()) == 0) {
                if (packet->stream_index == videoStrm) {
                    avcodec_send_packet(codecCtx, packet.get());

                    if (avcodec_receive_frame(codecCtx, curFrm->frm) == 0) {
                        found = true;
                        break;
                    }
                }
                else if (packet->stream_index == subStrm) {
                    acquireSubtitle(packet.get());
                }
            }
        }
        else
            found = true;

        if (found) {
           processCurrentFrame();
        }
    }

    return curFrm->frm->pts;
}

void VideoProcessor::saveFrame()
{
    ExifData exifData;
    QString iccFileName;
    ColorParams colorParams;
    auto mdTask = std::async(std::launch::async, [&]() {
        extractMeta(exifData, iccFileName, colorParams);
    });

    // determine file name
    auto loca = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    loca += "/visie.heic";

    // make file name unique
    if (QFile::exists(loca)) {
        loca = loca.replace("/visie.heic", "/visie-%1.heic");
        qulonglong cntr = 0;
        while (true) {
            auto cand = QString(loca).arg(cntr, 3, 10, QChar('0'));
            if (!QFile::exists(cand)) {
                loca = cand;
                break;
            }
            cntr++;
        }
    }

    // check subs for DJI metadata
    QRegularExpression exp(".+F/([^,]+), SS ([^,]+), ISO ([^,]+), EV ([^,]+), DZOOM ([^,]+), "
                           "GPS \\(([^,]+), ([^,]+), ([^,]+)\\), D ([^,]+), H ([^,]+), H.S ([^,]+), "
                           "V.S ([^,]+) ");
    auto match = exp.match(subTitle);
    if (match.hasMatch()) {
        mdTask.wait();

        MediaReader::gps2Exif(&exifData, match.captured(7), match.captured(6));
        exifData.add("Exif.Image.ApertureValue", log2f(pow(match.captured(1).toFloat(), 2))); // unit: APEX
        exifData.add("Exif.Image.ShutterSpeedValue", log2f(1.0 / match.captured(2).toFloat())); // unit: APEX
        exifData.add("Exif.Photo.ISOSpeed", (uint16_t) match.captured(3).toUInt());
        exifData.add("Exif.Image.ExposureBiasValue", match.captured(4).toFloat());
        exifData.add("Exif.Photo.DigitalZoomRatio", match.captured(5).toFloat());

        auto speed = match.captured(12);
        if (speed.endsWith("m/s")) {
            exifData.add("Exif.GPSInfo.GPSSpeedRef", "K");
            exifData.add("Exif.GPSInfo.GPSSpeed", speed.toFloat() * 60.0f / 1000.0f);
        }
    }

    std::unique_ptr<FileWriter> writer(new HeifWriter);
    writer->save(curFrm->frm, loca, mdTask, iccFileName, colorParams, exifData);
}

void VideoProcessor::processCurrentFrame()
{
    const auto frm = curFrm->frm;
    if (frm->format == -1) {
        return;
    }

    // create RGB image for presentation
    auto fmt = static_cast<AVPixelFormat>(frm->format);
    if (!cnvCtx) {
        int srcFrmWidth, srcFrmHeight;

        if (rotation == 0 || rotation == 180) {
            srcFrmWidth = frm->width;
            srcFrmHeight = frm->height;
        }
        else {
            srcFrmWidth = frm->height;
            srcFrmHeight = frm->width;
        }

        auto wRatio = double(srcFrmWidth) / width;
        auto hRatio = double(srcFrmHeight) / height;
        auto ratio = qMax(wRatio, hRatio);

        width = frm->width / ratio;
        height = frm->height / ratio;

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

    auto qImg = QImage(outBuf, width, actualHeight, *dstStride, QImage::Format_RGB888,
                         [](void *buf) {
                            delete [] static_cast<uint8_t *>(buf);
                         },
                        outBuf);

    // apply rotation
    if (rotation) {
        QTransform trans;

        trans = trans.translate(qImg.width() / 2, qImg.height() / 2).rotate(rotation);
        qImg = qImg.transformed(trans);
    }

    imgReady(qImg);

    // --
    qDebug() << "prim:" << frm->color_primaries << "spc" << frm->colorspace << "trns" << frm->color_trc;
    //--
}

void VideoProcessor::acquireSubtitle(AVPacket *pkt)
{
    AVSubtitle sub;
    int gotSub;

    auto len = avcodec_decode_subtitle2(subCodecCtx, &sub, &gotSub, pkt);
    if (len > 0 && gotSub && sub.num_rects > 0) {
        subTitle = sub.rects[0]->ass;
        avsubtitle_free(&sub);
    }
}

void VideoProcessor::extractMeta(ExifData &exif, QString &iccFileName,
                                 ColorParams &color)
{
    // rotation
    auto rota = av_dict_get(this->ctx->streams[videoStrm]->metadata, "rotate", nullptr, 0);
    if (rota) {
        uint16_t orient = 1;

        switch (atoi(rota->value)) {
            case 90:
                orient = 6;
                break;
            case 180:
                orient = 3;
                break;
            case 270:
                orient = 8;
                break;
            default:
                orient = 1;
        }

        exif.add("Exif.Image.Orientation", orient);
    }

    // BMFF content
    auto timeBase = &this->ctx->streams[videoStrm]->time_base;
    auto timeStamp = double(curFrm->frm->best_effort_timestamp * timeBase->num) / timeBase->den;
    auto rd = new MediaReader(this->ctx->pb, &exif, this->ctx->streams[videoStrm]->id, timeStamp);
    rd->extract();
    color = rd->color();

    // base color profile selection based on primaries, https://forum.doom9.org/showthread.php?t=168424
    switch (color.primaries)
    {
        case 1:
            iccFileName = ":/icc/ITU-R_BT709.icc";
            break;
        case 9:
            iccFileName = ":/icc/ITU-R_BT2020.icc";
            break;
        default:
            // libavformat may be wrong (OnePlus) but use it as a fallback
            switch (curFrm->frm->color_trc) {
                case AVCOL_TRC_BT709:
                    iccFileName = ":/icc/ITU-R_BT709.icc";
                    break;
                case AVCOL_TRC_BT2020_10:
                    iccFileName = ":/icc/ITU-R_BT2020.icc";
                    break;
                default:
                    ;
            }
    }

    delete rd;
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
