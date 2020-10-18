#ifndef VIDEOPROCESSOR_H
#define VIDEOPROCESSOR_H

#include <QObject>

class VideoProcessor : public QObject
{
    Q_OBJECT
public:
    explicit VideoProcessor(QObject *parent = nullptr);

    void loadVideo(QString fn);

signals:
    void loadSuccess();
};

#endif // VIDEOPROCESSOR_H
