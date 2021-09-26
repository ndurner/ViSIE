#include "mediareader.h"

#include <QDebug>
#include <QDateTime>
#include <exiv2/error.hpp>

MediaReader::MediaReader(AVIOContext *ctx, Exiv2::ExifData *exifData, int targetTrackID, double timeStamp) :
    ctx(ctx), md(exifData), targetTrackID(targetTrackID), timeStamp(timeStamp)
{
}

void MediaReader::extract()
{
    if (avio_seek(ctx, 0, AVSEEK_FORCE) != 0)
        return;

    decend(ctx, avio_size(ctx));

    return;
}

void MediaReader::decend(AVIOContext *ctx, int64_t rangeEnd)
{
    while (true) {
        size_t atomSize = avio_rb32(ctx);
        auto fourCC = avio_rb32(ctx);

        if (fourCC == 0)
            break;

        if (atomSize == 0) {
            atomSize = avio_size(ctx);
        }
        else if (atomSize == 1) {
            atomSize = avio_rb64(ctx);
            atomSize -= (sizeof(atomSize) + 2 * sizeof(fourCC));
        }
        else {
            atomSize -= (2 * sizeof(fourCC));
        }

        char fc[5];
        fc[0] = (fourCC >> 24) & 255;
        fc[1] = (fourCC >> 16) & 255;
        fc[2] = (fourCC >> 8) & 255;
        fc[3] = (fourCC >> 0) & 255;
        fc[4] = 0;

        auto basePos = avio_tell(ctx);
        qDebug() << basePos << QString::fromLatin1(fc, 4) << atomSize;

        switch(fourCC) {
            case 'moov':
            case 'trak':
                decend(ctx, basePos + atomSize);
                break;
            case 'udta':
                qInfo() << "udta";
                handle_udta(ctx, basePos + atomSize);
                break;
            case 'tkhd':
                handle_tkhd(ctx, basePos + atomSize);
                break;
        }

        auto rc = avio_seek(ctx, basePos + atomSize, SEEK_SET);
        if (rc <= 0) {
            qDebug() << "seek: " << rc;
            break;
        }

        if (avio_tell(ctx) >= rangeEnd || avio_feof(ctx))
            break;
    }
}

void MediaReader::handle_udta(AVIOContext *ctx, int64_t rangeEnd)
{
    while(true) {
        auto itm_size = avio_rb32(ctx);
        auto itm_type = avio_rb32(ctx);

        auto basePos = avio_tell(ctx);

        if (itm_type == static_cast<uint32_t>('\xa9xyz')) {
            // GPS

            auto str_size = avio_rb16(ctx);
            avio_skip(ctx, 2); // language

            unsigned char *gps = new unsigned char[str_size];
            avio_read(ctx, gps, str_size);
            auto gpsStr = QString::fromLatin1(QByteArray::fromRawData((const char *) gps, str_size));
            if (gpsStr.endsWith("/"))
                gpsStr.chop(1);
            auto sep = gpsStr.lastIndexOf('-');
            if (sep == -1)
                sep = gpsStr.lastIndexOf('+');

            (*md)["Exif.GPSInfo.GPSLatitudeRef"] = gpsStr.startsWith("-") ? "S" : "N";
            (*md)["Exif.GPSInfo.GPSLongitudeRef"] = gpsStr[sep] == '+' ? "E" : "W";

            for (auto comp: {std::make_pair("Exif.GPSInfo.GPSLatitude", gpsStr.left(sep).toDouble()),
                            std::make_pair("Exif.GPSInfo.GPSLongitude", gpsStr.mid(sep).toDouble())})
            {
                const auto decDeg = comp.second;
                const auto deg = floor(decDeg);
                const auto min = floor((decDeg - deg) * 60.0);
                const auto sec = floor((decDeg - deg - min / 60.0) * 3600.0);

                std::unique_ptr<Exiv2::URationalValue> rv(new Exiv2::URationalValue);
                rv->value_.assign({std::make_pair(deg, 1), std::make_pair(min, 1), std::make_pair(sec, 1)});
                md->add(Exiv2::ExifKey(comp.first), rv.get());
            }
        }

        avio_seek(ctx, basePos + itm_size, SEEK_SET);
        if (avio_tell(ctx) >= rangeEnd)
            break;
    }
}

void MediaReader::handle_tkhd(AVIOContext *ctx, int64_t rangeEnd)
{
    auto pos = avio_tell(ctx);
    if (pos + 4 > rangeEnd)
        return;

    // read Tracker Header
    auto ver = avio_r8(ctx);
    avio_skip(ctx, 3); // flags

    uint64_t creat, mod;
    if (ver == 0) {
        if (pos + 4 + 2 * 4 + 4 > rangeEnd)
            return;

        creat = avio_rb32(ctx);
        mod = avio_rb32(ctx);
    }
    else if (ver == 1) {
        if (pos + 4 + 2 * 8 + 4 > rangeEnd)
            return;

        creat = avio_rb64(ctx);
        mod = avio_rb64(ctx);
    }
    else {
        return;
    }
    auto track = avio_rb32(ctx);

    if (track != targetTrackID)
        return;

    // add time stamp inside the video to values read
    double ofs;
    uint32_t frac = modf(timeStamp, &ofs) * 100;
    if (creat == mod) {
        creat += ofs;
        mod += ofs;
    }
    else
        creat += ofs;

    // add Exif fields
    (*md)["Exif.Image.DateTime"] = QDateTime::fromString("01011904", "ddMMyyyy").addSecs(mod)
            .toString("yyyy:MM:dd hh:mm:ss").toStdString();
    (*md)["Exif.Image.DateTimeOriginal"] = QDateTime::fromString("01011904", "ddMMyyyy").addSecs(creat)
            .toString("yyyy:MM:dd hh:mm:ss").toStdString();
    (*md)["Exif.Photo.DateTimeOriginal"] = (*md)["Exif.Image.DateTimeOriginal"];
    (*md)["Exif.Photo.SubSecTime"] = QString("%1").arg(frac).toStdString();
    (*md)["Exif.Photo.SubSecTimeOriginal"] = (*md)["Exif.Photo.SubSecTime"];
}

static_assert('ftyp' == 1718909296);
