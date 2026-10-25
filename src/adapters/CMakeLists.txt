add_library(mos_adapters STATIC
  IProtocolAdapter.h
  CfsAdapter.cpp
  CfsAdapter.h
)

target_include_directories(mos_adapters
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/bridge
)
target_link_libraries(mos_adapters PUBLIC Qt6::Core mos_bridge)
