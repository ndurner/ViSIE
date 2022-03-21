cmake_minimum_required(VERSION 3.5)

project(visie)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)
find_package(libheif 1.12.0 CONFIG REQUIRED)
find_package(exiv2 CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(openjpeg CONFIG REQUIRED)
add_subdirectory(exiv2wrapper)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    scopedresource.cpp
    videoprocessor.cpp
    scopedresource.h
    videoprocessor.h
    colorparams.h
    mediareader.cpp
    mediareader.h
    goproreader.h goproreader.cpp
    gpmf-parser/GPMF_parser.c
    exiv2wrapper/exiv2wrapper.h
    filewriter.h filewriter.cpp
    heifwriter.h heifwriter.cpp
    jp2writer.h jp2writer.cpp
    res.qrc
)

qt_add_executable(visie
    ${PROJECT_SOURCES}
)

target_link_libraries(visie PRIVATE heif)
target_link_libraries(visie PRIVATE exiv2wrapper)
target_link_libraries(visie PRIVATE openjp2)
target_include_directories(visie PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(visie PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(visie PRIVATE ${FFMPEG_LIBRARIES})

target_link_libraries(visie PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
