add_library(mos_core STATIC
  Models.h
  AssetRegistry.cpp
  AssetRegistry.h
  Session.cpp
  Session.h
  TmPipeline.cpp
  TmPipeline.h
  TcPipeline.cpp
  TcPipeline.h
)

target_include_directories(mos_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/persist
    ${CMAKE_SOURCE_DIR}/src/adapters
    ${CMAKE_SOURCE_DIR}/src/bridge
)
target_link_libraries(mos_core
  PUBLIC
    Qt6::Core
    mos_persist
    mos_adapters
    mos_bridge
)
