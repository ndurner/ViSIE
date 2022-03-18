#include "heifwriter.h"
#include "scopedresource.h"

#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <libheif/heif.h>

bool HeifWriter::save(AVFrame *frm, QString fileName, std::future<void> &metaDataReady, QString &iccFileName,
                      ColorParams &colr, Exiv2::ExifData &exifData)
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
        return false;
    }

    // set quality
    const auto enc = encPtr.get();
    auto err = heif_encoder_set_lossless(enc, 1);
    if (err.code != heif_error_Ok) {
        qWarning() << "cannot enable lossless processing";

        err = heif_encoder_set_lossy_quality(enc, 100);
        if (err.code != heif_error_Ok) {
            qCritical() << "cannot configure quality level to encoder";
            return false;
        }
    }

    // prepare color settings
    heif_colorspace cs;
    heif_chroma chroma;
    heif_channel channels[3];
    int widths[3], heights[3], depths[3];
    int n_channels;
    setHeifColor(frm, cs, chroma, n_channels, channels, depths, widths, heights);

    std::unique_ptr<heif_color_profile_nclx> cp(new heif_color_profile_nclx);
    setColorProfile(cp.get(), colr);

    // image
    ScopedResource<heif_image, heif_error> img(
        [&] (heif_image *&img, heif_error &err) {
            err = heif_image_create(frm->width, frm->height, cs, chroma, &img);
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
            memcpy(planeData + (stride * h), frm->data[i] + (frm->linesize[i] * h),
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
        return false;
    }

    metaDataReady.wait();

    auto perr = heif_image_set_nclx_color_profile(img.get(), cp.get());
    if (perr.code != heif_error_Ok) {
        qCritical() << "setting color profile failed:" << perr.message;
        return false;
    }

    if (!iccFileName.isEmpty()) {
        qDebug() << "embedding color profile" << iccFileName;

        QFile iccFile(iccFileName);
        iccFile.open(QIODevice::ReadOnly);
        auto icc = iccFile.readAll();
        heif_image_set_raw_color_profile(img.get(), "prof", icc.constData(), icc.size());
    }
    else
        qWarning() << "no color profile embedded for trc" << frm->color_trc;

    perr = addMeta(hCtx.get(), imgH.get(), exifData);
    Q_ASSERT_X(perr.code == 0, "", perr.message);

    // write HEIC
    err = heif_context_write_to_file(hCtx.get(), fileName.toLocal8Bit().constData());
    if (err.code != heif_error_Ok) {
        qCritical() << "error writing image:" << err.message;
        return false;
    }
    else {
        qInfo() << "written to: " << fileName;
    }

    return true;
}

void HeifWriter::setHeifColor(AVFrame *frm, heif_colorspace &space, heif_chroma &chroma, int &n_channels,
                              heif_channel channels[], int depths[], int widths[], int heights[])
{
    switch(frm->format) {
        case AV_PIX_FMT_YUVJ420P:
        case AV_PIX_FMT_YUV420P:
            n_channels = 3;
            space = heif_colorspace_YCbCr;
            chroma = heif_chroma_420;
            channels[0] = heif_channel_Y;
            depths[0] = 8;
            widths[0] = frm->width;
            heights[0] = frm->height;
            channels[1] = heif_channel_Cb;
            depths[1] = 8;
            widths[1] = (frm->width + 1) / 2;
            heights[1] = (frm->height + 1) / 2;
            channels[2] = heif_channel_Cr;
            depths[2] = depths[1];
            widths[2] = widths[1];
            heights[2] = heights[1];
            break;
        default:
            qCritical() << "unexpected pixel format" << frm->format;
            n_channels = 0;
            space = heif_colorspace_undefined;
            chroma = heif_chroma_undefined;
            break;
    }
}

void HeifWriter::setColorProfile(heif_color_profile_nclx *cp, ColorParams &color)
{
    heif_color_primaries prim[] = {
        heif_color_primaries_unspecified,
        heif_color_primaries_ITU_R_BT_709_5,
        heif_color_primaries_unspecified,
        heif_color_primaries_unspecified,
        heif_color_primaries_unspecified,
        heif_color_primaries_ITU_R_BT_601_6,
        heif_color_primaries_ITU_R_BT_601_6,
        heif_color_primaries_unspecified,
        heif_color_primaries_unspecified,
        heif_color_primaries_ITU_R_BT_2020_2_and_2100_0,
        heif_color_primaries_unspecified,
        heif_color_primaries_SMPTE_RP_431_2,
        heif_color_primaries_SMPTE_EG_432_1
    };
    if (color.primaries >= 0 && color.primaries < sizeof(prim) / sizeof(heif_color_primaries))
        cp->color_primaries = prim[color.primaries];
    else
        cp->color_primaries = prim[0];

    // transfer function
    switch (color.transfer)
    {
        case 1:
            cp->transfer_characteristics = heif_transfer_characteristic_ITU_R_BT_709_5;
            break;
        case 7:
            cp->transfer_characteristics = heif_transfer_characteristic_SMPTE_240M;
            break;
        case 17:
            cp->transfer_characteristics = heif_transfer_characteristic_SMPTE_ST_428_1;
            break;
        default:
            cp->transfer_characteristics = heif_transfer_characteristic_unspecified;
            break;
    }

    // matrix
    switch (color.matrix)
    {
        case 1:
            cp->matrix_coefficients = heif_matrix_coefficients_ITU_R_BT_709_5;
            break;
        case 6:
            cp->matrix_coefficients = heif_matrix_coefficients_ITU_R_BT_470_6_System_B_G;
            break;
        case 7:
            cp->matrix_coefficients = heif_matrix_coefficients_SMPTE_240M;
            break;
        case 9:
            cp->matrix_coefficients = heif_matrix_coefficients_ITU_R_BT_2020_2_non_constant_luminance;
            break;
        default:
            cp->matrix_coefficients = heif_matrix_coefficients_unspecified;
            break;
    }

    cp->full_range_flag = true;
}

heif_error HeifWriter::addMeta(heif_context *ctx, heif_image_handle *hndl, const Exiv2::ExifData &exif)
{
    // build output
    Exiv2::ExifParser p;
    Exiv2::Blob blob;
    uint8_t pre[] = {'E','x','i','f', 0, 0};
    uint8_t post[] = {00, 01, 00, 00};

    blob.insert(blob.begin(), pre, pre + sizeof(pre));
    p.encode(blob, Exiv2::ByteOrder::bigEndian, exif);
    blob.insert(blob.end(), post, post + sizeof(post));

    return heif_context_add_exif_metadata(ctx, hndl, blob.data(), blob.size());
}
