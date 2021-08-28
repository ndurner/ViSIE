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

    static void extract(AVIOContext *ctx, Exiv2::ExifData *exifData);

private:
    MediaReader() = delete;

    static void decend(AVIOContext *ctx, int64_t rangeEnd, Exiv2::ExifData *md);
    static void handle_udta(AVIOContext *ctx, int64_t rangeEnd, Exiv2::ExifData *md);
};

#endif // MEDIAREADER_H
