# 6.4 is the floor because that is what Ubuntu 24.04 LTS ships and CI builds
# against. Stating it here turns "too old" into a configure-time message rather
# than a compile error inside some translation unit.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network)

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
  restic_model.h
  restic_widget.h
  restic_repo_dialog.h
  pch.h
  utils.h
  job_options.h
  restic.h
  formatting.h
  parsing.h
  theme.h
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
  formatting.cpp
  parsing.cpp
  theme.cpp
  restic.cpp
  restic_model.cpp
  restic_widget.cpp
  restic_repo_dialog.cpp
)

if(WIN32)
  list(APPEND SOURCES resources.rc)
elseif(APPLE)
  list(APPEND HEADERS osx_helper.h)
  list(APPEND SOURCES osx_helper.mm)
endif()

# Everything except main(), exported so the tests link the same translation
# units the application ships rather than a re-listed subset that can drift.
set(RRM_SOURCES ${SOURCES})
list(REMOVE_ITEM RRM_SOURCES main.cpp)
set(RRM_SOURCES ${RRM_SOURCES} PARENT_SCOPE)
set(RRM_HEADERS ${HEADERS} PARENT_SCOPE)
set(RRM_UI ${UI} PARENT_SCOPE)
set(RRM_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)

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
  # Upstream's Info.plist declared no CFBundleIdentifier at all. macOS keys
  # TCC permission grants (Files & Folders, Full Disk Access) off the bundle
  # identifier, and notarization requires one, so this string must stay stable
  # across releases: changing it makes every user re-grant access.
  set_target_properties(rclone-browser PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.overnightpillow.rclone-browser"
    MACOSX_BUNDLE_BUNDLE_NAME "rclone-browser"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
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
