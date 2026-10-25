add_library(mos_persist STATIC
  Database.cpp
  Database.h
  Schema.cpp
  Schema.h
)

target_include_directories(mos_persist PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mos_persist PUBLIC Qt6::Core Qt6::Sql)
