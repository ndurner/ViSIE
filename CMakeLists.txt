cmake_minimum_required(VERSION 3.5)

project(visie LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)
find_package(libheif CONFIG REQUIRED)
find_package(exiv2 CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    scopedresource.cpp
    videoprocessor.cpp
    scopedresource.h
    videoprocessor.h
)

qt_add_executable(visie
    ${PROJECT_SOURCES}
)


target_link_libraries(visie PRIVATE heif)
target_link_libraries(visie PRIVATE exiv2lib)
target_include_directories(visie PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(visie PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(visie PRIVATE ${FFMPEG_LIBRARIES})

target_link_libraries(visie PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)