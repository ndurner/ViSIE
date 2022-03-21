project(exiv2wrapper)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(exiv2wrapper STATIC exiv2wrapper.h exiv2wrapper.cpp)
target_link_libraries(exiv2wrapper PRIVATE exiv2lib)
