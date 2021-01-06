#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QDebug>
#include <QStandardPaths>
#include <QGraphicsPixmapItem>
#include <QKeyEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    installEventFilter(this);
    connect(&proc, &VideoProcessor::loadSuccess, this, &MainWindow::videoLoaded);
    connect(&proc, &VideoProcessor::loadError, this, &MainWindow::loadFailed);
    connect(&proc, &VideoProcessor::streamLength, this, &MainWindow::setFrames);
    connect(&proc, &VideoProcessor::imgReady, this, &MainWindow::showImg);
    connect(ui->frameSlider, &QSlider::valueChanged, &proc, &VideoProcessor::present);
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::on_actionOpen_triggered()
{
    statusBar()->clearMessage();

    // determine video folder
    auto stdFolders = QStandardPaths::standardLocations(QStandardPaths::StandardLocation::MoviesLocation);

    // file open dialog
    auto fn = QFileDialog::getOpenFileName(this,
        tr("Open Movie"), stdFolders.at(0), tr("Movie Files (*.mpg *.mpeg *.mov *.mp4)"));

    // load file
    if (! fn.isEmpty()) {
        proc.setDimensions(ui->graphicsView->width(), ui->graphicsView->height());
        proc.loadVideo(fn);
    }

}

void MainWindow::videoLoaded()
{
    statusBar()->showMessage("Video loaded");

    proc.present(0);
}

void MainWindow::loadFailed(QString msg)
{
    statusBar()->showMessage("Loading video failed: " + msg);
    resetUI();
}

void MainWindow::setFrames(int64_t count)
{
    ui->frameSlider->setMaximum(count);
    ui->frameSlider->setEnabled(true);
}

void MainWindow::showImg(QImage img)
{
    const auto view = ui->graphicsView;
    auto scene = ui->graphicsView->scene();
    if (!scene) {
        scene = new QGraphicsScene(view->viewport()->rect(), this);
        view->setScene(scene);
    }

    scene->clear();
    scene->addPixmap(QPixmap::fromImage(img));
}

void MainWindow::resetUI()
{
    ui->frameSlider->setEnabled(false);
}

void MainWindow::resizeEvent(QResizeEvent *)
{
    qDebug() << __FUNCTION__;

    auto scene = ui->graphicsView->scene();
    if (scene) {
        auto rect = ui->graphicsView->viewport()->rect();
        scene->setSceneRect(rect);
        proc.setDimensions(rect.width(), rect.height());
        proc.present(ui->frameSlider->value());
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
        auto ev = reinterpret_cast<QKeyEvent *>(event);

        if (ev->key() == Qt::Key_Left || ev->key() == Qt::Key_Right) {
            auto pts = proc.presentPrevNext(ev->key() == Qt::Key_Left);
            ui->frameSlider->setValue(pts);
        }
    }

    return false;
}

void MainWindow::on_actionSave_triggered()
{
    proc.saveFrame();
}
