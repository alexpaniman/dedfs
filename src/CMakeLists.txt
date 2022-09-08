add_executable(dedfs main.cpp)

set(CMAKE_CXX_FLAGS "-D_FILE_OFFSET_BITS=64")

# To find FUSE on system:
find_package(FUSE REQUIRED)

target_include_directories(dedfs
  PRIVATE ${FUSE_INCLUDE_DIR})

target_link_libraries(dedfs PRIVATE ${FUSE_LIBRARIES})
