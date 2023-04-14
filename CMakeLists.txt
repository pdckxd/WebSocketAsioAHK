cmake_minimum_required(VERSION 3.17)
project(WebSocketAsio)

set(CMAKE_CXX_STANDARD 17)

if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    #link_libraries(libboost_regex-vc142-mt-x32-1_73.lib)
    include_directories(C:/boost_1_80_0)
    link_directories(C:/boost_1_80_0/bin/${arch}/lib/)
#to static link runtime library into the dll
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -pthread -march=native")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -g0")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O2")
endif()

add_definitions(-DUNICODE -D_UNICODE)

if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
#    message("_AMD64_")
#    add_definitions( -D_AMD64_ )
    set(arch x64)
else()
#    message("_X86_")
#    add_definitions( -D_X86_ )
    set(arch x86)
endif()

add_library(WebSocketAsio-${arch} SHARED library.cpp library.h)
