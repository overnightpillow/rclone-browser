find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_standard_project_setup()

set(UI
  main_window.ui
  remote_widget.ui
  transfer_dialog.ui
  export_dialog.ui
  progress_dialog.ui
  job_widget.ui
  mount_widget.ui
  stream_widget.ui
  preferences_dialog.ui
)

set(HEADERS
  main_window.h
  remote_widget.h
  transfer_dialog.h
  export_dialog.h
  progress_dialog.h
  job_widget.h
  mount_widget.h
  stream_widget.h
  preferences_dialog.h
  icon_cache.h
  list_of_job_options.h
  item_model.h
  pch.h
  utils.h
  job_options.h
)

set(SOURCES
  main.cpp
  main_window.cpp
  remote_widget.cpp
  transfer_dialog.cpp
  export_dialog.cpp
  progress_dialog.cpp
  job_widget.cpp
  mount_widget.cpp
  stream_widget.cpp
  preferences_dialog.cpp
  icon_cache.cpp
  item_model.cpp
  utils.cpp
  job_options.cpp
  list_of_job_options.cpp
)

if(WIN32)
  list(APPEND SOURCES resources.rc)
elseif(APPLE)
  list(APPEND HEADERS osx_helper.h)
  list(APPEND SOURCES osx_helper.mm)
endif()

qt_add_executable(rclone-browser
  WIN32 MACOSX_BUNDLE
  ${SOURCES} ${HEADERS} ${UI} resources.qrc
)

# AUTOUIC/AUTOMOC/AUTORCC replace the hand-rolled qt5_wrap_ui/qt5_wrap_cpp/
# qt5_add_resources calls; qt_standard_project_setup enables them.
target_include_directories(rclone-browser PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_compile_definitions(rclone-browser PRIVATE
  RCLONE_BROWSER_VERSION="${PROJECT_VERSION}"
)

target_precompile_headers(rclone-browser PRIVATE pch.h)

target_link_libraries(rclone-browser PRIVATE
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
)

if(WIN32)
  # SHGetFileInfoW, used by icon_cache.cpp for per-extension shell icons.
  target_link_libraries(rclone-browser PRIVATE shell32)
elseif(APPLE)
  find_library(COCOA_LIB Cocoa REQUIRED)
  # UniformTypeIdentifiers backs the iconForContentType: lookup in osx_helper.mm.
  find_library(UTTYPE_LIB UniformTypeIdentifiers REQUIRED)
  target_link_libraries(rclone-browser PRIVATE ${COCOA_LIB} ${UTTYPE_LIB})
  set_source_files_properties(icon.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(rclone-browser PRIVATE icon.icns)
  set_target_properties(rclone-browser PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist")
else()
  install(TARGETS rclone-browser RUNTIME DESTINATION bin)

  set(ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/../assets")
  install(FILES "${ASSETS}/rclone-browser.svg"
    DESTINATION "share/icons/hicolor/scalable/apps" RENAME "rclone-browser.svg")
  foreach(SIZE 32 64 128 256 512)
    install(FILES "${ASSETS}/rclone-browser-${SIZE}x${SIZE}.png"
      DESTINATION "share/icons/hicolor/${SIZE}x${SIZE}/apps"
      RENAME "rclone-browser.png")
  endforeach()
  install(FILES "${ASSETS}/rclone-browser.desktop" DESTINATION "share/applications")
endif()
