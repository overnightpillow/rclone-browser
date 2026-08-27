# Qt's imported targets are directory-scoped, so this directory needs its own
# find_package for every component it links, not just Test.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Test)

qt_standard_project_setup()

if(APPLE)
  find_library(COCOA_LIB Cocoa REQUIRED)
  find_library(UTTYPE_LIB UniformTypeIdentifiers REQUIRED)
endif()

# Tests compile the application's own translation units rather than a re-listed
# subset, so a source added to the app cannot silently fall out of test builds.
set(TEST_SOURCES "")
foreach(SRC ${RRM_SOURCES})
  list(APPEND TEST_SOURCES "${RRM_SRC_DIR}/${SRC}")
endforeach()

set(TEST_UI "")
foreach(FORM ${RRM_UI})
  list(APPEND TEST_UI "${RRM_SRC_DIR}/${FORM}")
endforeach()

function(rrm_add_test NAME)
  qt_add_executable(${NAME} ${NAME}.cpp ${TEST_SOURCES} ${TEST_UI})

  # qt_add_executable builds a GUI-subsystem binary on Windows, which has no
  # stdout when it is not launched from a console -- so every line QTest
  # printed was discarded and a failing test reported nothing at all. Tests are
  # console programs.
  set_property(TARGET ${NAME} PROPERTY WIN32_EXECUTABLE FALSE)

  target_include_directories(${NAME} PRIVATE
    ${RRM_SRC_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
  )

  target_compile_definitions(${NAME} PRIVATE
    RCLONE_BROWSER_VERSION="${PROJECT_VERSION}"
    # A process the tests can start on any platform. /bin/sh was the obvious
    # choice and is not available on Windows; cmake built these tests, so it is
    # certainly present.
    RRM_CMAKE_COMMAND="${CMAKE_COMMAND}"
  )

  target_link_libraries(${NAME} PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test
  )

  if(WIN32)
    target_link_libraries(${NAME} PRIVATE shell32)
  elseif(APPLE)
    target_link_libraries(${NAME} PRIVATE ${COCOA_LIB} ${UTTYPE_LIB})
  endif()

  add_test(NAME ${NAME} COMMAND ${NAME})

  # No display in CI, and none wanted locally either: widget tests must not
  # steal focus or open windows while they run.
  set_tests_properties(${NAME} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120
  )
endfunction()

rrm_add_test(test_formatting)
rrm_add_test(test_parsing)
rrm_add_test(test_tasks)
rrm_add_test(test_widgets)
