#include "goproreader.h"

#include <QString>
#include <QDebug>
#include <QtEndian>
#include <memory>
#include <QDateTime>

GoproReader::GoproReader(uint32_t timeStamp, Exiv2::ExifData *exifData) :
     begin(GPMF_stream()), timeStamp(timeStamp), exifData(exifData)
{
}

QString fourCCStr(int fourCC)
{
    char fc[5];
    fc[0] = (fourCC >> 24) & 255;
    fc[1] = (fourCC >> 16) & 255;
    fc[2] = (fourCC >> 8) & 255;
    fc[3] = (fourCC >> 0) & 255;
    fc[4] = 0;

    return QString::fromLatin1(fc, 4);
}

bool GoproReader::gpsFix()
{
    GPMF_stream strm;
    bool ret;

    GPMF_CopyState(&begin, &strm);
    // 'FSPG' = 'GPSF' in GoPro byte order
    if (GPMF_FindNext(&strm, 'FSPG', GPMF_RECURSE_LEVELS) == GPMF_OK) {
        unsigned long fixType;
        GPMF_ScaledData(&strm, &fixType, sizeof(fixType), 0, 1, GPMF_TYPE_UNSIGNED_LONG);
        if (fixType == 0)
            qWarning() << "no GPS fix";

        ret = fixType != 0;
    }
    else
        ret = false;

    return ret;
}

bool GoproReader::handleGPS()
{
    GPMF_stream strm;

    GPMF_CopyState(&begin, &strm);
    // '5SPG' = 'GPS5' in GoPro byte order
    if (GPMF_FindNext(&strm, '5SPG', GPMF_RECURSE_LEVELS) == GPMF_OK) {
        GPMF_stream prev;
        GPMF_CopyState(&strm, &prev);

        // get scaling info, 'LACS' = 'SCAL' in GoPro byte order
        GPMF_FindPrev(&prev, 'LACS', GPMF_CURRENT_LEVEL);
        struct {
            long lat, lon, alt, speed2, speed3;
        } scal;
        Q_UNUSED(scal.speed3)
        static_assert(sizeof(scal) == 5 * sizeof(long));
        GPMF_ScaledData(&prev, &scal, sizeof(scal), 0, 4, GPMF_TYPE_SIGNED_LONG);

        // get closest GPS sample
        auto gpsSamples = GPMF_Repeat(&strm);
        auto sample = floor(((double) timeStamp) / (1000.0 / gpsSamples));
        if (sample > gpsSamples)
            sample = gpsSamples;

        // get GPS data
        struct {
            long lat, lon, alt, speed2, speed3;
        } gps;
        Q_UNUSED(gps.speed3)
        static_assert(sizeof(gps) == 5 * sizeof(long));
        GPMF_FormattedData(&strm, &gps, sizeof(gps), sample, 1);

        // GPS lat/lon reference
        if (gps.lon < 0) {
            (*exifData)["Exif.GPSInfo.GPSLatitudeRef"] = "S";
            gps.lon *= -1;
        }
        else
            (*exifData)["Exif.GPSInfo.GPSLatitudeRef"] = "N";

        if (gps.lat < 0) {
            (*exifData)["Exif.GPSInfo.GPSLongitudeRef"] = "W";
            gps.lat *= -1;
        }
        else
            (*exifData)["Exif.GPSInfo.GPSLongitudeRef"] = "E";

        // GPS lat/lon
        for (auto comp: {std::make_pair("Exif.GPSInfo.GPSLatitude", ((float) gps.lat) / scal.lat),
                        std::make_pair("Exif.GPSInfo.GPSLongitude", ((float) gps.lon) / scal.lon)})
        {
            const auto decDeg = comp.second;
            const auto deg = floor(decDeg);
            const auto min = floor((decDeg - deg) * 60.0);
            const auto sec = floor((decDeg - deg - min / 60.0) * 3600.0);

            std::unique_ptr<Exiv2::URationalValue> rv(new Exiv2::URationalValue);
            rv->value_.assign({std::make_pair(deg, 1), std::make_pair(min, 1), std::make_pair(sec, 1)});
            exifData->add(Exiv2::ExifKey(comp.first), rv.get());
        }

        // GPS altitude
        if (gps.alt < 0) {
            (*exifData)["Exif.GPSInfo.GPSAltitudeRef"] = 1; // below sea level
            gps.alt *= -1;
        }
        else
            (*exifData)["Exif.GPSInfo.GPSAltitudeRef"] = 0; // above sea level

        Exiv2::URational alt(gps.alt, scal.alt);
        (*exifData)["Exif.GPSInfo.GPSAltitude"] = alt;

        // GPS speed
        if (gps.speed2 > 0) {
            // convert m/s to km/h.
            // mulitply by 60 * 60, 1000 is shortened to 36, 10
            Exiv2::URational speed(gps.speed2 * 36, scal.speed2 * 10);
            (*exifData)["Exif.GPSInfo.GPSSpeed"] = speed;
            (*exifData)["Exif.GPSInfo.GPSSpeedRef"] = "K";
        }

        return true;
    }
    else
        return false;
}

void GoproReader::handleDOP()
{
    GPMF_stream strm;

    GPMF_CopyState(&begin, &strm);
    // 'PSPG' = GPSP in GoPro byte order
    if (GPMF_FindNext(&strm, 'PSPG', GPMF_RECURSE_LEVELS) == GPMF_OK) {
        GPMF_stream prev;
        GPMF_CopyState(&strm, &prev);

        // get scaling info
        unsigned long scal;
        GPMF_FindPrev(&prev, 'LACS', GPMF_CURRENT_LEVEL);
        GPMF_ScaledData(&prev, &scal, sizeof(scal), 0, 1, GPMF_TYPE_SIGNED_LONG);

        // get DOP
        unsigned short dop;
        GPMF_FormattedData(&strm, &dop, sizeof(dop), 0, 1);
        Exiv2::URational gpsDOP(dop, scal);
        (*exifData)["Exif.GPSInfo.GPSDOP"] = gpsDOP;
    }
}

void GoproReader::handleGPSTime()
{
    GPMF_stream strm;

    GPMF_CopyState(&begin, &strm);
    if (GPMF_FindNext(&strm, 'USPG', GPMF_RECURSE_LEVELS) == GPMF_OK) {
        auto bufsize = GPMF_FormattedDataSize(&strm);
        auto repeats = GPMF_Repeat(&strm);
        QByteArray buf(bufsize, Qt::Uninitialized);
        GPMF_FormattedData(&strm, buf.data(), buf.capacity(), 0, repeats);
        auto dateStr = QString::fromLatin1(buf);
        auto date = QDateTime::fromString(dateStr, "yyMMddhhmmss.zzz");

        if (date.isValid()) {
            (*exifData)["Exif.GPSInfo.GPSDateStamp"] = date.toString("yyyy:MM:dd").toLatin1().data();
            (*exifData)["Exif.GPSInfo.GPSTimeStamp"] = date.toString("hh:mm:ss").toLatin1().data();
        }
    }
}

void GoproReader::handleDeviceName()
{
    // 'MNVD' = 'DVNM' in GoPro byte order
    if (GPMF_FindNext(&begin, 'MNVD', GPMF_RECURSE_LEVELS) == GPMF_OK) {
        auto bufsize = GPMF_FormattedDataSize(&begin);
        auto repeats = GPMF_Repeat(&begin);
        QByteArray buf(bufsize, Qt::Uninitialized);
        GPMF_FormattedData(&begin, buf.data(), buf.capacity(), 0, repeats);
        auto model = QString::fromLatin1(buf).toStdString();

        (*exifData)["Exif.Image.Make"] = "GoPro";
        (*exifData)["Exif.Image.Model"] = model;
    }
}

void GoproReader::extract(QByteArray &data)
{
    if (GPMF_Init(&begin, reinterpret_cast<uint32_t *>(data.data()), data.size()) != GPMF_OK)
        return;

    // GPS
    if (gpsFix()) {
        if (handleGPS())
            (*exifData)["Exif.GPSInfo.GPSProcessingMethod"] = "GPS";

        handleDOP();
        handleGPSTime();
    }

    // device name
    handleDeviceName();
}
