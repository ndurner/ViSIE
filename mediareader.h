#ifndef MEDIAREADER_H
#define MEDIAREADER_H

#include <string>
#include <tuple>
#include <list>
#include <QByteArray>
#include <exiv2/exif.hpp>
extern "C" {
#include <libavformat/avio.h>
}

class MediaReader
{
public:
    using MetadataKV = std::tuple<std::string, std::string>;
    using Metadata = std::list<MetadataKV>;

    MediaReader(AVIOContext *ctx, Exiv2::ExifData *exifData, int targetTrackID, double timeStamp);
    void extract();

private:
    AVIOContext *ctx;
    Exiv2::ExifData *md;
    uint32_t targetTrackID;
    double timeStamp;

    void decend(AVIOContext *ctx, int64_t rangeEnd);
    void handle_udta(AVIOContext *ctx, int64_t rangeEnd);
    void handle_tkhd(AVIOContext *ctx, int64_t rangeEnd);
};

#endif // MEDIAREADER_H
