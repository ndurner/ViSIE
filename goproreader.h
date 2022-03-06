#ifndef GOPROREADER_H
#define GOPROREADER_H

#include <exiv2/exif.hpp>
#include <QByteArray>
#include "gpmf-parser/GPMF_parser.h"

class GoproReader
{
public:
    GoproReader() = delete;
    GoproReader(uint32_t timeStamp, Exiv2::ExifData *exifData);

    void extract(QByteArray &data);

private:
    GPMF_stream begin;
    uint32_t timeStamp;
    Exiv2::ExifData *exifData;

    bool gpsFix();
    bool handleGPS();
    void handleDOP();
    void handleGPSTime();
    void handleDeviceName();
};

#endif // GOPROREADER_H
