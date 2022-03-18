#ifndef MEDIAREADER_H
#define MEDIAREADER_H

#include <string>
#include <tuple>
#include <list>
#include <QByteArray>
#include <QString>
#include <exiv2/exif.hpp>
extern "C" {
#include <libavformat/avio.h>
}

#include "colorparams.h"

class MediaReader
{
public:
    using MetadataKV = std::tuple<std::string, std::string>;
    using Metadata = std::list<MetadataKV>;
    using TrackSample = struct {uint64_t offset; uint64_t size; uint32_t duration;};

    MediaReader(AVIOContext *ctx, Exiv2::ExifData *exifData, int targetTrackID, double timeStamp);
    void extract();
    const ColorParams color() {return colorParams;};

private:
    AVIOContext *ctx;
    Exiv2::ExifData *md;
    uint32_t targetTrackID;
    double timeStamp;
    ColorParams colorParams;
    bool readingGoProMeta;
    std::vector<TrackSample> metaTrackSamples;

    static QString fourCCStr(int fourCC);
    void decend(AVIOContext *ctx, int64_t rangeEnd);
    void handle_udta(AVIOContext *ctx, int64_t rangeEnd);
    void handle_tkhd(AVIOContext *ctx, int64_t rangeEnd);
    void handle_stsd(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd);
    void handle_avc1(AVIOContext *ctx, int64_t atomSize);
    void handle_colr(AVIOContext *ctx, int64_t rangeEnd);
    void handle_hdlr(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd);
    void handle_stco(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd);
    void handle_stsz(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd);
    void handle_mdia(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd);
    void handle_stts(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd);
};

#endif // MEDIAREADER_H
