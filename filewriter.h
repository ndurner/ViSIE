#ifndef FILEWRITER_H
#define FILEWRITER_H

#include <future>
#include <QString>
#include "exiv2wrapper/exiv2wrapper.h"
#include "colorparams.h"
extern "C" {
#include <libavformat/avformat.h>
}

class FileWriter
{
public:
    virtual bool save(AVFrame *frm, QString fileName, std::future<void> &metaDataReady, QString &iccFileName,
                      ColorParams &colr, ExifData &exifData) = 0;
    virtual ~FileWriter() {};
};

#endif // FILEWRITER_H
