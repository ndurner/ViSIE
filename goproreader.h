#ifndef GOPROREADER_H
#define GOPROREADER_H

#include "exiv2wrapper/exiv2wrapper.h"
#include <QByteArray>
#include "gpmf-parser/GPMF_parser.h"

class GoproReader
{
public:
    GoproReader() = delete;
    GoproReader(uint32_t timeStamp, ExifData *exifData);

    void extract(QByteArray &data);

private:
    GPMF_stream begin;
    uint32_t timeStamp;
    ExifData *exifData;

    bool gpsFix();
    bool handleGPS();
    void handleDOP();
    void handleGPSTime();
    void handleDeviceName();
};

#endif // GOPROREADER_H
