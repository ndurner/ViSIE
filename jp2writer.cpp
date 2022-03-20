#include "jp2writer.h"

#include <QDebug>
#include <QFile>
#include <vector>
#include <execution>
#include "scopedresource.h"
#include <openjpeg.h>
#include <exiv2/image.hpp>

bool Jp2Writer::save(AVFrame *frm, QString fileName, std::future<void> &metaDataReady, QString &iccFileName,
                     ColorParams &colr, Exiv2::ExifData &exifData)
{
    std::vector<opj_image_cmptparm_t> cmptparm;
    opj_cparameters_t encParams;
    OPJ_COLOR_SPACE cSpace;
    int compts;

    if (frm->format == AV_PIX_FMT_YUVJ420P || frm->format == AV_PIX_FMT_YUV420P) {
        opj_image_cmptparm_t comp {
            .dx = 0, .dy = 0,
            .w = OPJ_UINT32(frm->width), .h = OPJ_UINT32(frm->height),
            .x0 = 0, .y0 = 0,
            .prec = 8,
            .bpp = 8,
            .sgnd = 0
        };
        cmptparm.push_back(comp); // Y

        comp.w = frm->linesize[1];
        comp.h = (frm->height + 1) / 2;
        cmptparm.push_back(comp); // Cb
        cmptparm.push_back(comp); // Cr

        cSpace = OPJ_CLRSPC_SYCC;
        compts = 3;
    }
    else {
        qCritical() << "unsupported frame format" << frm->format;
        return false;
    }

    ScopedResource<opj_image, bool> img(
        [&] (opj_image *&img, bool &err) {
            img = opj_image_create(cmptparm.size(), cmptparm.data(), cSpace);
            err = img == nullptr;
        },
        [](opj_image *img, const bool &err) {
            if (!err)
                opj_image_destroy(img);
        });

    auto jp2 = img.get();
    if (jp2 == nullptr) {
        qCritical() << "error creating JP2 image";
        return false;
    }

    jp2->x0 = jp2->y0 = 0;
    jp2->x1 = frm->width;
    jp2->y1 = frm->height;

    ScopedResource<opj_codec_t, bool> codec(
        [&] (opj_codec_t *&codec, bool &err) {
            codec = opj_create_compress(OPJ_CODEC_JP2);
            err = codec == nullptr;
        },
        [](opj_codec_t *codec, const bool &err) {
            if (!err)
                opj_destroy_codec(codec);
        });
    if (codec.error()) {
        qCritical() << "error creating JP2 codec";
        return false;
    }

    opj_set_default_encoder_parameters(&encParams);
    if (!opj_setup_encoder(codec.get(), &encParams, jp2)) {
        qCritical() << "error setting up JP2 encoder";
        return false;
    }
    opj_codec_set_threads(codec.get(), opj_get_num_cpus());

    {
        ScopedResource<opj_stream_t, bool> strm(
            [&] (opj_stream_t *&strm, bool &err) {
                strm = opj_stream_create_default_file_stream(fileName.toLocal8Bit(), false);
                err = strm == nullptr;
            },
            [](opj_codec_t *strm, const bool &err) {
                if (!err)
                    opj_stream_destroy(strm);
            });
        if (strm.error()) {
            qCritical() << "error creating JP2 output stream";
            return false;
        }

        for (int i = 0; i < compts; i++) {
            std::copy(std::execution::par_unseq,
                      frm->data[i], frm->data[i] + frm->linesize[i] * jp2->comps[i].h + 1, jp2->comps[i].data);
        }

        if (!opj_start_compress(codec.get(), jp2, strm.get()) ||
                !opj_encode(codec.get(), strm.get()) ||
                !opj_end_compress(codec.get(), strm.get())) {
            qCritical() << "error running JP2 encoder";
            return false;
        }
    }

    // metadata
    auto exiv2 = Exiv2::ImageFactory::open(fileName.toStdString(), false);
    metaDataReady.wait();
    exiv2->setExifData(exifData);
    if (!iccFileName.isEmpty()) {
        QFile file(iccFileName);
        file.open(QIODevice::ReadOnly);
        auto ba = file.readAll();
        Exiv2::DataBuf buf(reinterpret_cast<Exiv2::byte *>(ba.data()), ba.length());
        exiv2->setIccProfile(buf, false);
    }
    exiv2->writeMetadata();

    return true;
}
