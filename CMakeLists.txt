cmake_minimum_required(VERSION 3.20)
project(NetFlow C)

set(CMAKE_C_STANDARD 11)

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSSH2 REQUIRED libssh2)

add_executable(netflow WIN32
    src/main.c
    src/window.c
    src/terminal.c
    src/ssh.c
    src/serial.c
    src/session.c
    src/tabs.c
    src/ui.c
    src/resource.rc
)

target_include_directories(netflow PRIVATE
    ${LIBSSH2_INCLUDE_DIRS}
    src
)

target_link_libraries(netflow PRIVATE
    ${LIBSSH2_LIBRARIES}
    ws2_32
    comctl32
    gdi32
    user32
    comdlg32
)

target_link_directories(netflow PRIVATE
    ${LIBSSH2_LIBRARY_DIRS}
)
