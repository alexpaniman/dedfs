add_library(hash-table STATIC precompiled-hash-tables.cpp)

target_include_directories(
  hash-table PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(hash-table PUBLIC linked-list macro-utils)
