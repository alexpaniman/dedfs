add_executable(dedfs main.cpp)

set(CMAKE_CXX_FLAGS "-D_FILE_OFFSET_BITS=64")

# To find FUSE on system:
find_package(FUSE REQUIRED)

target_include_directories(dedfs
  PUBLIC ${FUSE_INCLUDE_DIR})

target_link_libraries(dedfs PUBLIC hash-table murmur3 ${FUSE_LIBRARIES})
install(TARGETS dedfs DESTINATION bin)
