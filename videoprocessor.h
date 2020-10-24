#ifndef VIDEOPROCESSOR_H
#define VIDEOPROCESSOR_H

#include <QObject>

extern "C" {
#include <libavformat/avformat.h>
}

class VideoProcessor : public QObject
{
    Q_OBJECT
public:
    explicit VideoProcessor(QObject *parent = nullptr);
    virtual ~VideoProcessor();

    void loadVideo(QString fn);

signals:
    void loadSuccess();
    void loadError(QString msg);
    void streamLength(int64_t count);

protected:
    AVFormatContext* ctx;
    int videoStrm;

    void cleanup();
};

#endif // VIDEOPROCESSOR_H
