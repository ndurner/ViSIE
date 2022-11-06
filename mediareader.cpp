#include "mediareader.h"
#include "goproreader.h"

#include <QDebug>
#include <QDateTime>

MediaReader::MediaReader(AVIOContext *ctx, ExifData *exifData, int targetTrackID, double timeStamp) :
    ctx(ctx), md(exifData), targetTrackID(targetTrackID), timeStamp(timeStamp), colorParams({2, 2, 2} /* undef */)
{
}

void MediaReader::extract()
{
    if (avio_seek(ctx, 0, AVSEEK_FORCE) != 0)
        return;

    decend(ctx, avio_size(ctx));

    return;
}

void MediaReader::gps2Exif(ExifData *exifData, QString lat, QString lon)
{
    exifData->add("Exif.GPSInfo.GPSLatitudeRef", lat[0] == '-' ? "S" : "N");
    exifData->add("Exif.GPSInfo.GPSLongitudeRef", lon[0] == '-' ? "W" : "E");

    for (auto comp: {std::make_pair("Exif.GPSInfo.GPSLatitude", lat.toDouble()),
                    std::make_pair("Exif.GPSInfo.GPSLongitude", lon.toDouble())})
    {
        const auto decDeg = comp.second;
        const auto deg = floor(decDeg);
        const auto min = floor((decDeg - deg) * 60.0);
        const auto sec = floor((decDeg - deg - min / 60.0) * 3600.0);

        exifData->add(comp.first, std::make_pair(deg, 1), std::make_pair(min, 1), std::make_pair(sec, 1));
    }
}

QString MediaReader::fourCCStr(int fourCC)
{
    char fc[5];
    fc[0] = (fourCC >> 24) & 255;
    fc[1] = (fourCC >> 16) & 255;
    fc[2] = (fourCC >> 8) & 255;
    fc[3] = (fourCC >> 0) & 255;
    fc[4] = 0;

    return QString::fromLatin1(fc, 4);
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
            case 'minf':
            case 'stbl':
                decend(ctx, basePos + atomSize);
                break;
            case 'udta':
                qInfo() << "udta";
                handle_udta(ctx, basePos + atomSize);
                break;
            case 'tkhd':
                handle_tkhd(ctx, basePos + atomSize);
                break;
            case 'stsd':
                handle_stsd(ctx, basePos, basePos + atomSize);
                break;
            case 'colr':
                handle_colr(ctx, basePos + atomSize);
                break;
            case 'hdlr':
                handle_hdlr(ctx, basePos, basePos + atomSize);
                break;
            case 'stco':
                handle_stco(ctx, basePos, basePos + atomSize);
                break;
            case 'stsz':
                handle_stsz(ctx, basePos, basePos + atomSize);
                break;
            case 'stts':
                handle_stts(ctx, basePos, basePos + atomSize);
                break;
            case 'mdia':
                handle_mdia(ctx, basePos, basePos + atomSize);
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
        auto basePos = avio_tell(ctx);

        auto itm_size = avio_rb32(ctx);
        auto itm_type = static_cast<uint32_t>(avio_rb32(ctx));

        if (itm_type == 0xA978797A /* xyz */) {
            // GPS

            auto str_size = avio_rb16(ctx);
            avio_skip(ctx, 2); // language

            std::string gps(str_size + 1, '\0');
            avio_read(ctx, (unsigned char *) gps.data(), str_size);
            auto gpsStr = QString::fromLatin1(gps.data(), gps.find('\0'));
            if (gpsStr.endsWith("/"))
                gpsStr.chop(1);
            auto sep = gpsStr.lastIndexOf('-');
            if (sep == -1)
                sep = gpsStr.lastIndexOf('+');

            gps2Exif(md, gpsStr.left(sep), gpsStr.mid(sep));
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
    const auto origDate = QDateTime::fromString("01011904", "ddMMyyyy").addSecs(creat)
            .toString("yyyy:MM:dd hh:mm:ss").toStdString();
    const auto subSecTime = QString("%1").arg(frac).toStdString();
    md->add("Exif.Image.DateTime", QDateTime::fromString("01011904", "ddMMyyyy").addSecs(mod)
            .toString("yyyy:MM:dd hh:mm:ss").toStdString());
    md->add("Exif.Image.DateTimeOriginal", origDate);
    md->add("Exif.Photo.DateTimeOriginal", origDate);
    md->add("Exif.Photo.SubSecTime", subSecTime);
    md->add("Exif.Photo.SubSecTimeOriginal", subSecTime);
}

void MediaReader::handle_stsd(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd)
{
    // stsd reference:
    //  https://titanwolf.org/Network/Articles/Article?AID=b97d2313-9919-4a62-b4c5-d29d53b1bf71
    //  https://img-blog.csdn.net/20170205180429644

    if (rangeBase + 8 > rangeEnd)
        return;

    auto ver = avio_r8(ctx);
    if (ver != 0)
        return;

    avio_skip(ctx, 3); // flags

    auto entries = avio_rb32(ctx);
    for (decltype(entries) entry = 0; entry < entries; entry++) {
        auto size = avio_rb32(ctx);
        auto fmt = avio_rb32(ctx);

        if (fmt == 'avc1') {
            handle_avc1(ctx, size);
        }
    }
}

void MediaReader::handle_avc1(AVIOContext *ctx, int64_t atomSize)
{
    auto pos = avio_tell(ctx);
    if (atomSize <= 78)
        return;

    avio_skip(ctx, 78); // TODO: check entry count https://img-blog.csdn.net/20170205180429644
    decend(ctx, pos - 8 + atomSize);
}

void MediaReader::handle_colr(AVIOContext *ctx, int64_t rangeEnd)
{
    // colr spec: https://developer.apple.com/library/archive/documentation/QuickTime/QTFF/QTFFChap3/qtff3.html#//apple_ref/doc/uid/TP40000939-CH205-125526

    auto pos = avio_tell(ctx);
    if (pos + 10 > rangeEnd)
        return;

    auto paramType = avio_rb32(ctx);
    if (!(paramType == 'nclc' || paramType == 'nclx'))
        return;

    colorParams.primaries = avio_rb16(ctx);
    colorParams.transfer = avio_rb16(ctx);
    colorParams.matrix = avio_rb16(ctx);
}

void MediaReader::handle_hdlr(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd)
{
    if (rangeBase + 25 > rangeEnd)
        return;

    avio_skip(ctx, 4);
    auto comp = avio_rb32(ctx);
    if (comp != 'mhlr')
        return;

    auto sub = avio_rb32(ctx);
    if (sub != 'meta')
        return;

    avio_skip(ctx, 13);
    auto len = rangeEnd - rangeBase - 25;
    QByteArray name(len, Qt::Uninitialized);
    avio_read(ctx, reinterpret_cast<unsigned char *>(name.data()), len);
    readingGoProMeta = QString::fromLatin1(name) == "GoPro MET  ";
}

void MediaReader::handle_stco(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd)
{
    if (rangeBase + 8 > rangeEnd)
        return;

    if (readingGoProMeta) {
        avio_skip(ctx, 4);
        auto entries = avio_rb32(ctx);

        metaTrackSamples.resize(entries);

        for (auto &sample: metaTrackSamples)
            sample.offset = avio_rb32(ctx);
    }
}

void MediaReader::handle_stsz(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd)
{
    if (rangeBase + 8 > rangeEnd)
        return;

    if (readingGoProMeta) {
        avio_skip(ctx, 8);
        auto entries = avio_rb32(ctx);

        metaTrackSamples.resize(entries);

        for (auto &sample: metaTrackSamples)
            sample.size = avio_rb32(ctx);
    }
}

void MediaReader::handle_stts(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd)
{
    if (rangeBase + 8 > rangeEnd)
        return;

    if (readingGoProMeta) {
        avio_skip(ctx, 4);
        auto entries = avio_rb32(ctx);

        decltype(entries) sample = 0;
        for (decltype(entries) entry = 0; entry < entries; entry++)
        {
            auto sampleCount = avio_rb32(ctx);
            auto sampleDuration = avio_rb32(ctx);

            for (decltype(sampleCount) cntr = 0; cntr < sampleCount; cntr++) {
                if (sample + 1 > metaTrackSamples.size())
                    metaTrackSamples.resize(sample + 1);

                metaTrackSamples[sample++].duration = sampleDuration;
            }
        }
    }
}

void MediaReader::handle_mdia(AVIOContext *ctx, int64_t rangeBase, int64_t rangeEnd)
{
    readingGoProMeta = false;
    metaTrackSamples.clear();

    decend(ctx, rangeEnd);

    if (!readingGoProMeta)
        return;

    uint64_t ts = timeStamp * 1000;

    auto target = metaTrackSamples.end();
    uint64_t trackTimestmp = 0;
    for (auto sample = metaTrackSamples.begin(); sample != metaTrackSamples.end(); sample++) {
        if (ts >= trackTimestmp && ts <= trackTimestmp + sample->duration) {
            target = sample;
            break;
        }
        trackTimestmp += sample->duration;
    }

    decltype(ts) timeOffset;

    if (target == metaTrackSamples.end() && metaTrackSamples.size() > 0) {
        target = metaTrackSamples.end() - 1;
        timeOffset = 999; // end of last sample
    }
    else
        timeOffset = ts - trackTimestmp;

    if (target != metaTrackSamples.end()) {
        QByteArray buf(target->size, Qt::Uninitialized);
        avio_seek(ctx, target->offset, SEEK_SET);
        avio_read(ctx, reinterpret_cast<unsigned char*>(buf.data()), target->size);

        GoproReader gr(timeOffset, md);
        gr.extract(buf);
    }
}

static_assert('ftyp' == 1718909296);
