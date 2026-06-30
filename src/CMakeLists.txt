
file(GLOB SRC_LIST ${CMAKE_CURRENT_SOURCE_DIR}/*/*.cpp)
add_library(src ${SRC_LIST})

target_include_directories(src
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(src
        PUBLIC
    /usr/local/lib/libeXosip2.so
    /usr/local/lib/libosip2.so
    /usr/local/lib/libosipparser2.so
        PUBLIC ZLToolKit
        PUBLIC pugixml
)
