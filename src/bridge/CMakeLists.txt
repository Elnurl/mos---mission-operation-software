add_library(mos_bridge STATIC
  ICfsBridge.h
  StubCfsBridge.cpp
  StubCfsBridge.h
  SimTmGenerator.cpp
  SimTmGenerator.h
)

target_include_directories(mos_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mos_bridge PUBLIC Qt6::Core)
