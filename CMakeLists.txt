project(diskBench)
cmake_minimum_required(VERSION 2.6.1)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

include(FindAPR)


set(DISKBENCH_VERSION 0.9.0)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

set (LIBS ${APR_LIBS} m )

include_directories(${PROJECT_SOURCE_DIR}/include ${APR_INCLUDE_PATH})

set(SRCS ${PROJECT_SOURCE_DIR}/src/diskBench.c ${PROJECT_SOURCE_DIR}/src/queue.c ${PROJECT_SOURCE_DIR}/src/sequential_workload.c ${PROJECT_SOURCE_DIR}/src/random_workload.c ${PROJECT_SOURCE_DIR}/src/mixed_workload.c)
set(HEADERS ${PROJECT_SOURCE_DIR}/include/diskBench.h)

if(WIN32)
  set(SRCS ${SRCS} ${PROJECT_SOURCE_DIR}/win32/win32.c)
else()
  set(SRCS ${SRCS} ${PROJECT_SOURCE_DIR}/linux/linux.c)
  set(LIBS ${LIBS} aio)
endif()

set (CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} ${APR_CPP_FLAGS}")
set (CMAKE_EXE_LINKER_FLAGS "${APR_LD_FLAGS}")

add_executable(diskBench ${SRCS} ${HEADERS})

target_link_libraries(diskBench ${LIBS})
