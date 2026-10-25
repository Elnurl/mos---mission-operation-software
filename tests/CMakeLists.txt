add_executable(mos_tests
  test_mos.cpp
)

target_link_libraries(mos_tests
  PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Sql
    mos_persist
    mos_core
    mos_bridge
    mos_adapters
)

add_test(NAME mos_tests COMMAND mos_tests)
