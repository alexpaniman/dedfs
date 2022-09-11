file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/null.cpp "")

add_library(linked-list STATIC
  ${CMAKE_CURRENT_BINARY_DIR}/null.cpp)

target_include_directories(
  linked-list SYSTEM INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(linked-list PUBLIC trace)
