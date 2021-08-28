QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    scopedresource.cpp \
    videoprocessor.cpp \
    mediareader.cpp

HEADERS += \
    mainwindow.h \
    scopedresource.h \
    videoprocessor.h \
    mediareader.h

FORMS += \
    mainwindow.ui

INCLUDEPATH += \
    /usr/local/include

LIBS += \
    -L/usr/local/lib \
    -lavformat \
    -lavcodec \
    -lswscale \
    -lheif \
    -lexiv2

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
