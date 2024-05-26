file(GLOB BASE_SRC "*.cpp" "../external/imgui/*.cpp")
file(GLOB BASE_HEADERS "*.hpp" "*.h")

# Basis Universal
set(BASISU_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../external/basisu)

set(BASISU_SOURCES
    ${BASISU_DIR}/transcoder/basisu_transcoder.cpp
    ${BASISU_DIR}/zstd/zstd.c)

add_library(base STATIC ${BASE_SRC} ${BASE_HEADERS} ${BASISU_SOURCES})

if(WIN32)
    target_link_libraries(base ${Vulkan_LIBRARY} ${WINLIBS} ${DRACO_DECODER_LIBRARY})
 else(WIN32)
    target_link_libraries(base ${Vulkan_LIBRARY} ${XCB_LIBRARIES} ${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${DRACO_DECODER_LIBRARY})
endif(WIN32)