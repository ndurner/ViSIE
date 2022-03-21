#ifndef HEIFWRITER_H
#define HEIFWRITER_H

#include "filewriter.h"
extern "C" {
#include <libheif/heif.h>
}

class HeifWriter : public FileWriter
{
public:
    bool save(AVFrame *frm, QString fileName, std::future<void> &metaDataReady, QString &iccFileName,
              ColorParams &colr, ExifData &exifData);

protected:
    void setHeifColor(AVFrame *frm, heif_colorspace &space, heif_chroma &chroma, int &n_channels,
                      heif_channel channels[], int depths[], int widths[], int heights[]);
    void setColorProfile(heif_color_profile_nclx *cp, ColorParams &colorParams);
    heif_error addMeta(heif_context *ctx, heif_image_handle *hndl, const ExifData &exif);
};

#endif // HEIFWRITER_H
