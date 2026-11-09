cmake_minimum_required(VERSION 3.21)

project(WakeDeck VERSION 1.0.0 LANGUAGES CXX)
set(WAKEDECK_VERSION_STRING "${PROJECT_VERSION}")  # the one place the version is written

# The release script sets CMAKE_OSX_DEPLOYMENT_TARGET to 13.0 and builds against
# Qt's official binaries, which run on macOS 13. A development build against
# Homebrew's Qt only runs on the macOS version Homebrew built Qt for.
if(CMAKE_OSX_DEPLOYMENT_TARGET)
  set(WAKEDECK_MIN_MACOS "${CMAKE_OSX_DEPLOYMENT_TARGET}")
else()
  set(WAKEDECK_MIN_MACOS "13.0")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT APPLE)
  message(FATAL_ERROR "WakeDeck builds on macOS only for now: the client launcher uses Launch Services and NSWorkspace.")
endif()
enable_language(OBJCXX)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -pedantic)

# 6.7 or later: QNetworkRequest::setTransferTimeout(std::chrono) and qt_standard_project_setup.
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Network Widgets Test)
qt_standard_project_setup()

# Core: the portable wake logic. Plain C++17, no Qt. This is what Milestone 1 shipped.
add_library(wakedeck_core STATIC
  src/core/MacAddress.h
  src/core/MacAddress.cpp
  src/core/MagicPacket.h
  src/core/MagicPacket.cpp
  src/core/WakePlan.h
  src/core/WakePlan.cpp
  src/core/UdpSender.h
  src/core/UdpSender.cpp
  src/core/WakeSender.h
  src/core/WakeSender.cpp)
target_include_directories(wakedeck_core PUBLIC src)

# Engine: readiness probes and the Wake & Connect session, on Qt Core and Qt Network.
add_library(wakedeck_engine STATIC
  src/engine/ProbeResult.h
  src/engine/ProbeResult.cpp
  src/engine/ServiceProbe.h
  src/engine/ServiceProbe.cpp
  src/engine/QtWaker.h
  src/engine/QtWaker.cpp
  src/engine/Explain.h
  src/engine/Explain.cpp
  src/engine/Session.h
  src/engine/Session.cpp
  src/engine/Profile.h
  src/engine/Profile.cpp
  src/engine/ProfileStore.h
  src/engine/ProfileStore.cpp
  src/engine/AppLocator.h
  src/engine/ClientLauncher.h
  src/engine/ClientLauncher.cpp
  src/mac/AppLocator.mm)
set_source_files_properties(src/mac/AppLocator.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(wakedeck_engine PUBLIC wakedeck_core Qt6::Core Qt6::Network "-framework AppKit")

add_executable(wakedeck-cli
  src/cli/main.cpp
  src/cli/Flags.h
  src/cli/Flags.cpp
  src/cli/SessionFlags.h
  src/cli/SessionFlags.cpp
  src/cli/ConnectCommand.h
  src/cli/ConnectCommand.cpp
  src/cli/ProfileCommand.h
  src/cli/ProfileCommand.cpp
  src/cli/LaunchCommand.h
  src/cli/LaunchCommand.cpp)
target_link_libraries(wakedeck-cli PRIVATE wakedeck_engine)
target_compile_definitions(wakedeck-cli PRIVATE WAKEDECK_VERSION="${WAKEDECK_VERSION_STRING}")

# The application: the window code is a library so the GUI tests can link it too.
add_library(wakedeck_ui STATIC
  src/app/MainWindow.h
  src/app/MainWindow.cpp
  src/app/ProfileEditorDialog.h
  src/app/ProfileEditorDialog.cpp)
target_include_directories(wakedeck_ui PUBLIC src)
target_link_libraries(wakedeck_ui PUBLIC wakedeck_engine Qt6::Widgets)

set(WAKEDECK_ICON ${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon/WakeDeck.icns)
set_source_files_properties(${WAKEDECK_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
qt_add_executable(WakeDeck MACOSX_BUNDLE src/app/main.cpp ${WAKEDECK_ICON})
qt_add_resources(WakeDeck "wakedeck_examples" PREFIX "/examples" BASE "examples" FILES examples/example-profile.json)
target_link_libraries(WakeDeck PRIVATE wakedeck_ui)
target_compile_definitions(WakeDeck PRIVATE WAKEDECK_VERSION="${WAKEDECK_VERSION_STRING}")
set_target_properties(WakeDeck PROPERTIES
  OUTPUT_NAME "WakeDeck"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
  MACOSX_BUNDLE_ICON_FILE "WakeDeck.icns"
  MACOSX_BUNDLE_BUNDLE_NAME "WakeDeck"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.MattB4906.WakeDeck"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_COPYRIGHT "MIT License. Uses the Qt framework (LGPL v3).")

enable_testing()

add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE wakedeck_core)
add_test(NAME test_core COMMAND test_core)

add_executable(test_loopback tests/test_loopback.cpp)
target_link_libraries(test_loopback PRIVATE wakedeck_core)
add_test(NAME test_loopback COMMAND test_loopback)

add_executable(test_engine tests/test_engine.cpp)
target_link_libraries(test_engine PRIVATE wakedeck_engine Qt6::Test)
add_test(NAME test_engine COMMAND test_engine)

add_executable(test_profiles tests/test_profiles.cpp)
target_link_libraries(test_profiles PRIVATE wakedeck_engine Qt6::Test)
target_compile_definitions(test_profiles PRIVATE WAKEDECK_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
add_test(NAME test_profiles COMMAND test_profiles)

add_executable(test_launcher tests/test_launcher.cpp)
target_link_libraries(test_launcher PRIVATE wakedeck_engine Qt6::Test)
add_test(NAME test_launcher COMMAND test_launcher)

add_executable(test_gui tests/test_gui.cpp)
qt_add_resources(test_gui "wakedeck_examples_test" PREFIX "/examples" BASE "examples" FILES examples/example-profile.json)
target_link_libraries(test_gui PRIVATE wakedeck_ui Qt6::Test)
add_test(NAME test_gui COMMAND test_gui)
set_tests_properties(test_gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_test(NAME cli_test
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.sh $<TARGET_FILE:wakedeck-cli>)
