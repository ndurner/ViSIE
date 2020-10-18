#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QDebug>
#include <QStandardPaths>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    connect(&proc, &VideoProcessor::loadSuccess, this, &MainWindow::videoLoaded);
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
    if (! fn.isEmpty())
        proc.loadVideo(fn);

}

void MainWindow::videoLoaded()
{
    statusBar()->showMessage("Video loaded");
}
