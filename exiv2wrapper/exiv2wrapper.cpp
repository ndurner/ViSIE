#include "exiv2wrapper.h"

#include <exiv2/exif.hpp>
#include <exiv2/image.hpp>

class ExifDataImpl
{
public:
    ExifDataImpl() : data(new Exiv2::ExifData())
    {
    }

    std::unique_ptr<Exiv2::ExifData> data;
};

class ExifImageImpl
{
public:
    ExifImageImpl(const std::string &path) : img(Exiv2::ImageFactory::open(path, false))
    {
    }

    Exiv2::Image::AutoPtr img;
};

ExifData::ExifData() : d_ptr(new ExifDataImpl())
{
}

ExifData::~ExifData()
{
    delete d_ptr;
}

void ExifData::add(const std::string &key, uint16_t val)
{
    (*d_ptr->data)[key] = val;
}

void ExifData::add(const std::string &key, const std::string &val)
{
    (*d_ptr->data)[key] = val;
}

void ExifData::add(const std::string &key, std::pair<unsigned int, unsigned int> val1,
                   std::pair<unsigned int, unsigned int> val2, std::pair<unsigned int, unsigned int> val3)
{
    std::unique_ptr<Exiv2::URationalValue> rv(new Exiv2::URationalValue);
    rv->value_.assign({val1, val2, val3});
    d_ptr->data->add(Exiv2::ExifKey(key), rv.get());
}

void ExifData::add(const std::string &key, unsigned long val1, unsigned long val2)
{
    Exiv2::URational v(val1, val2);
    (*d_ptr->data)[key] = v;
}

ExifImage::ExifImage(const std::string &path) : d_ptr(new ExifImageImpl(path))
{
}

ExifImage::~ExifImage()
{
    delete d_ptr;
}

void ExifImage::setExifData(const ExifData &data)
{
    d_ptr->img->setExifData(*data.d_ptr->data.get());
}

void ExifImage::setIccProfile(const uint8_t *data, const size_t len)
{
    Exiv2::DataBuf buf(data, len);
    d_ptr->img->setIccProfile(buf, false);
}

void ExifImage::writeMetadata()
{
    d_ptr->img->writeMetadata();
}

void ExifSerializer::serialize(const ExifData &exif, std::vector<uint8_t> &blob)
{
    // build output
    Exiv2::ExifParser p;
    uint8_t pre[] = {'E','x','i','f', 0, 0};
    uint8_t post[] = {00, 01, 00, 00};

    blob.insert(blob.begin(), pre, pre + sizeof(pre));
    p.encode(blob, Exiv2::ByteOrder::bigEndian, *exif.d_ptr->data);
    blob.insert(blob.end(), post, post + sizeof(post));
}
