#include "videoprocessor.h"

#include <QDebug>
#include <QFile>
#include <memory>
#include <libheif/heif.h>
#include <exiv2/exif.hpp>

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

    // prepare color profile
    std::unique_ptr<heif_color_profile_nclx> cp(new heif_color_profile_nclx);

    // prepare color settings
    heif_colorspace cs;
    heif_chroma chroma;
    heif_channel channels[3];
    int widths[3], heights[3], depths[3];
    int n_channels;
    setHeifColor(cs, chroma, cp.get(), n_channels, channels, depths, widths, heights);

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
    auto perr = heif_image_set_nclx_color_profile(img.get(), cp.get());
    if (perr.code != heif_error_Ok) {
        qCritical() << "setting color profile failed:" << perr.message;
        return;
    }

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

    perr = addMeta(hCtx.get(), imgH.get());
    Q_ASSERT_X(perr.code == 0, "", perr.message);

    err = heif_context_write_to_file(hCtx.get(), "/tmp/visie.heic");
    if (err.code != heif_error_Ok) {
        qCritical() << "error writing image:" << err.message;
        return;
    }
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
        auto ratio = std::max(wRatio, hRatio);

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
}

heif_error VideoProcessor::addMeta(heif_context *ctx, heif_image_handle *hndl)
{
    Exiv2::ExifData exif;

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

        exif["Exif.Image.Orientation"] = orient;
    }

    Exiv2::ExifParser p;
    Exiv2::Blob blob;
    uint8_t pre[] = {'E','x','i','f', 0, 0};
    uint8_t post[] = {00, 01, 00, 00};

    blob.insert(blob.begin(), pre, pre + sizeof(pre));
    p.encode(blob, Exiv2::ByteOrder::bigEndian, exif);
    blob.insert(blob.end(), post, post + sizeof(post));

    return heif_context_add_exif_metadata(ctx, hndl, blob.data(), blob.size());
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

void VideoProcessor::setHeifColor(heif_colorspace &space, heif_chroma &chroma, heif_color_profile_nclx *cp, int &n_channels, heif_channel channels[], int depths[], int widths[], int heights[])
{
    const heif_color_primaries prim[] = {
        heif_color_primaries_unspecified,               // AVCOL_PRI_RESERVED0
        heif_color_primaries_ITU_R_BT_709_5,            // AVCOL_PRI_BT709
        heif_color_primaries_unspecified,               // AVCOL_PRI_UNSPECIFIED
        heif_color_primaries_unspecified,               // AVCOL_PRI_RESERVED
        heif_color_primaries_ITU_R_BT_470_6_System_B_G, // AVCOL_PRI_BT470BG
        heif_color_primaries_SMPTE_240M,                // AVCOL_PRI_SMPTE170M
        heif_color_primaries_SMPTE_240M,                // AVCOL_PRI_SMPTE240M
        heif_color_primaries_generic_film,              // AVCOL_PRI_FILM
        heif_color_primaries_ITU_R_BT_2020_2_and_2100_0, // AVCOL_PRI_BT2020
        heif_color_primaries_SMPTE_ST_428_1,            // AVCOL_PRI_SMPTE428
        heif_color_primaries_SMPTE_ST_428_1,            // AVCOL_PRI_SMPTEST428_1
        heif_color_primaries_SMPTE_RP_431_2,            // AVCOL_PRI_SMPTE431
        heif_color_primaries_SMPTE_EG_432_1,            // AVCOL_PRI_SMPTE432
        heif_color_primaries_EBU_Tech_3213_E,           // AVCOL_PRI_EBU3213
        heif_color_primaries_EBU_Tech_3213_E            // AVCOL_PRI_JEDEC_P22
    };

    const heif_matrix_coefficients coeff[] {
        heif_matrix_coefficients_RGB_GBR,
        heif_matrix_coefficients_ITU_R_BT_709_5,
        heif_matrix_coefficients_unspecified,
        heif_matrix_coefficients_unspecified,
        heif_matrix_coefficients_US_FCC_T47,
        heif_matrix_coefficients_ITU_R_BT_470_6_System_B_G,
        heif_matrix_coefficients_SMPTE_240M,
        heif_matrix_coefficients_SMPTE_240M,
        heif_matrix_coefficients_YCgCo,
        heif_matrix_coefficients_YCgCo,
        heif_matrix_coefficients_chromaticity_derived_non_constant_luminance,
        heif_matrix_coefficients_ITU_R_BT_2020_2_constant_luminance,
        heif_matrix_coefficients_SMPTE_ST_2085,
        heif_matrix_coefficients_chromaticity_derived_non_constant_luminance,
        heif_matrix_coefficients_chromaticity_derived_constant_luminance,
        heif_matrix_coefficients_ICtCp
    };

    const heif_transfer_characteristics trans[] = {
        heif_transfer_characteristic_unspecified,               // AVCOL_TRC_RESERVED0
        heif_transfer_characteristic_ITU_R_BT_709_5,
        heif_transfer_characteristic_unspecified,               // AVCOL_TRC_UNSPECIFIED
        heif_transfer_characteristic_unspecified,               // AVCOL_TRC_RESERVED
        heif_transfer_characteristic_ITU_R_BT_470_6_System_M,   // AVCOL_TRC_GAMMA22
        heif_transfer_characteristic_ITU_R_BT_470_6_System_B_G, // AVCOL_TRC_GAMMA28
        heif_transfer_characteristic_ITU_R_BT_601_6,            // AVCOL_TRC_SMPTE170M
        heif_transfer_characteristic_ITU_R_BT_601_6,            // AVCOL_TRC_SMPTE240M
        heif_transfer_characteristic_linear,
        heif_transfer_characteristic_logarithmic_100,
        heif_transfer_characteristic_logarithmic_100_sqrt10,
        heif_transfer_characteristic_IEC_61966_2_4,
        heif_transfer_characteristic_ITU_R_BT_1361,
        heif_transfer_characteristic_IEC_61966_2_1,
        heif_transfer_characteristic_ITU_R_BT_2020_2_10bit,
        heif_transfer_characteristic_ITU_R_BT_2020_2_12bit,
        heif_transfer_characteristic_unspecified,               // AVCOL_TRC_SMPTE2084
        heif_transfer_characteristic_unspecified,               // AVCOL_TRC_SMPTEST2084
        heif_transfer_characteristic_SMPTE_ST_428_1,
        heif_transfer_characteristic_SMPTE_ST_428_1,
        heif_transfer_characteristic_unspecified,               // AVCOL_TRC_ARIB_STD_B67
    };

    auto frm = curFrm->frm;

    cp->color_primaries = prim[frm->color_primaries];
    cp->matrix_coefficients = coeff[frm->colorspace];

    if (frm->color_range == AVCOL_RANGE_UNSPECIFIED) {
        switch(frm->format) {
            case AV_PIX_FMT_YUVJ420P:
            case AV_PIX_FMT_YUVJ422P:
            case AV_PIX_FMT_YUVJ444P:
                cp->full_range_flag = true;
                break;
            case AV_PIX_FMT_YUV420P:
            case AV_PIX_FMT_YUV422P:
            case AV_PIX_FMT_YUV444P:
            case AV_PIX_FMT_YUV410P:
            case AV_PIX_FMT_YUV411P:
                cp->full_range_flag = false;
                break;
            default:
                qWarning() << "cannot determine full range flag";
                cp->full_range_flag = false;
        }
    }
    else
        cp->full_range_flag = frm->color_range == AVCOL_RANGE_JPEG;

    cp->transfer_characteristics = trans[frm->color_trc];
    if (cp->transfer_characteristics == heif_transfer_characteristic_unspecified)
        qWarning() << "unspecified color transfer characteristics for" << frm->color_trc;

    switch(frm->format) {
        case AV_PIX_FMT_YUVJ420P:
        case AV_PIX_FMT_YUV420P:
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
            qCritical() << "unexpected pixel format" << curFrm->frm->format;
            n_channels = 0;
            space = heif_colorspace_undefined;
            chroma = heif_chroma_undefined;
            break;
    }
}
