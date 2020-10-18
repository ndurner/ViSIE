#include "videoprocessor.h"

VideoProcessor::VideoProcessor(QObject *parent) : QObject(parent)
{

}

void VideoProcessor::loadVideo(QString fn)
{
    emit loadSuccess();
}
