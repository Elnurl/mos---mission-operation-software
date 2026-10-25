add_executable(mos-desktop
  main.cpp
  MainWindow.cpp
  MainWindow.h
)

target_link_libraries(mos-desktop
  PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    mos_core
)

install(TARGETS mos-desktop RUNTIME DESTINATION bin)
