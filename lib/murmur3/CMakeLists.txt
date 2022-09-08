add_library(murmur3 STATIC murmur3.c)

target_include_directories(
  murmur3 PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR})
