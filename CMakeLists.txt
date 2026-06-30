cmake_minimum_required(VERSION 3.22)
project(sipB)

set(CMAKE_CXX_STANDARD 17)

include_directories(/usr/local/include/eXosip2)
include_directories(/usr/local/include/osipparser2)
include_directories(/usr/local/include/osip2)

add_subdirectory(3rd/pugixml)
add_subdirectory(3rd/ZLToolKit)

add_subdirectory(src)


add_executable(sipB
    main.cpp
)

target_link_libraries(sipB
        PRIVATE
    /usr/local/lib/libeXosip2.so
    /usr/local/lib/libosip2.so
    /usr/local/lib/libosipparser2.so
        src
)
