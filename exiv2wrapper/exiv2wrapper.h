#ifndef EXIV2WRAPPER_H
#define EXIV2WRAPPER_H

#include <memory>
#include <string>
#include <vector>

class ExifData
{
public:
    ExifData();
    ExifData(const ExifData &) = delete;
    ~ExifData();
    void add(const std::string &key, uint16_t val);
    void add(const std::string &key, const std::string &val);
    void add(const std::string &key, std::pair<unsigned int, unsigned int> val1,
             std::pair<unsigned int, unsigned int> val2, std::pair<unsigned int, unsigned int> val3);
    void add(const std::string &key, unsigned long val1, unsigned long val2);
    void add(const std::string &key, float val);

private:
    class ExifDataImpl *d_ptr;

friend class ExifImage;
friend class ExifSerializer;
};

class ExifImage
{
public:
    ExifImage(const std::string &path);
    ExifImage() = delete;
    ExifImage(const ExifImage &) = delete;
    ~ExifImage();
    void setExifData(const ExifData &data);
    void setIccProfile(const uint8_t *data, const size_t len);
    void writeMetadata();

private:
    class ExifImageImpl *d_ptr;
};

class ExifSerializer
{
public:
    static void serialize(const ExifData &exif, std::vector<uint8_t> &buf);
};

#endif // EXIV2WRAPPER_H
