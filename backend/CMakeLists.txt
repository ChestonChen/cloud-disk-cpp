cmake_minimum_required(VERSION 3.16)

project(cloud_disk_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(cloud-disk
    src/main.cpp
    src/services/AuthService.cpp
    src/services/FileService.cpp
    src/services/MetadataStore.cpp
    src/utils/Hash.cpp
    src/utils/HttpServer.cpp
    src/utils/Json.cpp
    src/utils/Path.cpp
)

target_include_directories(cloud-disk PRIVATE src)

if (MSVC)
    target_compile_options(cloud-disk PRIVATE /W4)
else()
    target_compile_options(cloud-disk PRIVATE -Wall -Wextra -Wpedantic)
endif()

