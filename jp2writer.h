#ifndef JP2WRITER_H
#define JP2WRITER_H

#include "filewriter.h"

class Jp2Writer : public FileWriter
{
public:
    virtual bool save(AVFrame *frm, QString fileName, std::future<void> &metaDataReady, QString &iccFileName,
                      ColorParams &colr, ExifData &exifData);
};

#endif // JP2WRITER_H
