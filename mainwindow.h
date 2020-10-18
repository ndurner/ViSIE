#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "videoprocessor.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_actionOpen_triggered();
    void videoLoaded();

private:
    Ui::MainWindow *ui;
    VideoProcessor proc;
};
#endif // MAINWINDOW_H
