file(GLOB BASE_SRC "*.cpp" "../external/imgui/*.cpp")
file(GLOB BASE_HEADERS "*.hpp" "*.h")

add_library(base STATIC ${BASE_SRC} ${BASE_HEADERS})

if(WIN32)
    target_link_libraries(base ${Vulkan_LIBRARY} ${WINLIBS} ${DRACO_DECODER_LIBRARY})
 else(WIN32)
    target_link_libraries(base ${Vulkan_LIBRARY} ${XCB_LIBRARIES} ${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${DRACO_DECODER_LIBRARY})
endif(WIN32)