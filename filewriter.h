#ifndef FILEWRITER_H
#define FILEWRITER_H

#include <future>
#include <QString>
#include <exiv2/exiv2.hpp>
#include "colorparams.h"
extern "C" {
#include <libavformat/avformat.h>
}

class FileWriter
{
public:
    virtual bool save(AVFrame *frm, QString fileName, std::future<void> &metaDataReady, QString &iccFileName,
                      ColorParams &colr, Exiv2::ExifData &exifData) = 0;
    virtual ~FileWriter() {};
};

#endif // FILEWRITER_H
