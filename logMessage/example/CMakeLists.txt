cmake_minimum_required(VERSION 3.0)

PROJECT(LOGMSG)

set(CMAKE_CXX_STANDARD 11)

include_directories(../include ../src)
link_libraries(libpthread.so)
link_directories(../lib)
link_libraries(libzip.so)
set(LogFiles
	../src/rlog.cpp
	../src/stackMessage.cpp)
add_executable(LogMessage main.cpp ${LogFiles})


