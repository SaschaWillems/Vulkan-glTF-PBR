cmake_minimum_required(VERSION 3.4.1 FATAL_ERROR)

set(SRC_DIR ../../src)
set(BASE_DIR ../../base)
set(EXTERNAL_DIR ../../external)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -DVK_USE_PLATFORM_ANDROID_KHR -DVK_NO_PROTOTYPES")

file(GLOB BASE_SRC "${SRC_DIR}/main.cpp" "${EXTERNAL_DIR}/imgui/*.cpp")

# Basis Universal
set(BASISU_DIR ${EXTERNAL_DIR}/basisu)

set(BASISU_SOURCES
        ${BASISU_DIR}/transcoder/basisu_transcoder.cpp
        ${BASISU_DIR}/zstd/zstd.c)

add_library(
    native-lib SHARED
    ${BASE_SRC}

    ${BASE_DIR}/VulkanAndroid.cpp
    ${BASE_DIR}/VulkanExampleBase.cpp
    ${BASE_DIR}/VulkanTexture.hpp
    ${BASE_DIR}/VulkanglTFModel.cpp
    ${EXTERNAL_DIR}/imgui/imgui_draw.cpp

    ${BASISU_SOURCES}
)

add_library(native-app-glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

include_directories(${BASE_DIR})
include_directories(${EXTERNAL_DIR})
include_directories(${EXTERNAL_DIR}/glm)
include_directories(${EXTERNAL_DIR}/gli)
include_directories(${EXTERNAL_DIR}/tinygltf)
include_directories(${EXTERNAL_DIR}/basisu/transcoder)
include_directories(${EXTERNAL_DIR}/basisu/zstd)
include_directories(${ANDROID_NDK}/sources/android/native_app_glue)

target_link_libraries(
    native-lib
    native-app-glue
    android
    log
)
