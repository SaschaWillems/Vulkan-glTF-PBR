cmake_minimum_required(VERSION 3.4.1 FATAL_ERROR)

set(SRC_DIR ../../src)
set(BASE_DIR ../../base)
set(EXTERNAL_DIR ../../external)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -DVK_USE_PLATFORM_ANDROID_KHR -DVK_NO_PROTOTYPES")

file(GLOB BASE_SRC "${SRC_DIR}/main.cpp" "${EXTERNAL_DIR}/imgui/*.cpp")

add_library(
    native-lib SHARED
    ${BASE_SRC}

    ${BASE_DIR}/VulkanAndroid.cpp
    ${BASE_DIR}/VulkanExampleBase.cpp
    ${BASE_DIR}/VulkanTexture.hpp
    ${EXTERNAL_DIR}/imgui/imgui_draw.cpp
)

add_library(native-app-glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")

include_directories(${BASE_DIR})
include_directories(${EXTERNAL_DIR})
include_directories(${EXTERNAL_DIR}/glm)
include_directories(${EXTERNAL_DIR}/gli)
include_directories(${EXTERNAL_DIR}/tinygltf)
include_directories(${ANDROID_NDK}/sources/android/native_app_glue)

set(KTX_DIR ${EXTERNAL_DIR}/ktx)
set(KTX_SOURCES
	${KTX_DIR}/lib/texture.c
	${KTX_DIR}/lib/hashlist.c
	${KTX_DIR}/lib/checkheader.c
	${KTX_DIR}/lib/swap.c
	${KTX_DIR}/lib/memstream.c
	${KTX_DIR}/lib/filestream.c
)
set(KTX_INCLUDE
	${KTX_DIR}/include
	${KTX_DIR}/lib
	${KTX_DIR}/other_include
)

add_library(libktx ${KTX_SOURCES})
target_include_directories(libktx PUBLIC ${KTX_INCLUDE})
set_property(TARGET libktx PROPERTY FOLDER "external")

target_link_libraries(
    native-lib
    native-app-glue
    android
    log
    libktx
)
