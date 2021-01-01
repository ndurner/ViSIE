#ifndef VIDEOPROCESSOR_H
#define VIDEOPROCESSOR_H

#include <QObject>
#include <QImage>

extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class VideoProcessor : public QObject
{
    Q_OBJECT
public:
    explicit VideoProcessor(QObject *parent = nullptr);
    virtual ~VideoProcessor();

    void setDimensions(int width, int height);
    void loadVideo(QString fn);

signals:
    void loadSuccess();
    void loadError(QString msg);
    void streamLength(int64_t count);
    void imgReady(QImage img);

public slots:
    void present(uint64_t pts);

protected:
    int width, height;
    AVFormatContext* ctx;
    int videoStrm;
    AVCodecContext *codecCtx;
    SwsContext *cnvCtx;

    void cleanup();
};

#endif // VIDEOPROCESSOR_H
