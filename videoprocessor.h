#ifndef VIDEOPROCESSOR_H
#define VIDEOPROCESSOR_H

#include <QObject>
#include <QImage>

extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libheif/heif.h>
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
    int presentPrevNext(bool prev);
    void saveFrame();

protected:
    int width, height;
    AVFormatContext* ctx;
    int videoStrm;
    AVCodecContext *codecCtx;
    SwsContext *cnvCtx;

    struct Frame {
        AVFrame *frm;
        Frame *other;
    } frmBuf[2];
    Frame *curFrm;

    void cleanup();
    void setHeifColor(heif_colorspace &space, heif_chroma &chroma, int &n_channels, heif_channel channels[], int depths[], int widths[], int heights[]);
    void processCurrentFrame();
};

#endif // VIDEOPROCESSOR_H
