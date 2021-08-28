#include "mediareader.h"

#include <QDebug>

void MediaReader::extract(AVIOContext *ctx, Exiv2::ExifData *exifData)
{
    if (avio_seek(ctx, 0, AVSEEK_FORCE) != 0)
        return;

    decend(ctx, avio_size(ctx), exifData);

    return;
}

void MediaReader::decend(AVIOContext *ctx, int64_t rangeEnd, Exiv2::ExifData *md)
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
                decend(ctx, basePos + atomSize, md);
                break;
            case 'udta':
                qInfo() << "udta";
                handle_udta(ctx, basePos + atomSize, md);
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

void MediaReader::handle_udta(AVIOContext *ctx, int64_t rangeEnd, Exiv2::ExifData *md)
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

                Exiv2::URationalValue::UniquePtr rv(new Exiv2::URationalValue);
                rv->value_.assign({std::make_pair(deg, 1), std::make_pair(min, 1), std::make_pair(sec, 1)});
                md->add(Exiv2::ExifKey(comp.first), rv.get());
            }
        }

        avio_seek(ctx, basePos + itm_size, SEEK_SET);
        if (avio_tell(ctx) >= rangeEnd)
            break;
    }
}

static_assert('ftyp' == 1718909296);
