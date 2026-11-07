cmake_minimum_required(VERSION 3.21)

project(WakeDeck VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -pedantic)

# 6.7 or later: QNetworkRequest::setTransferTimeout(std::chrono) and qt_standard_project_setup.
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Network Test)
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
  src/engine/ProfileStore.cpp)
target_link_libraries(wakedeck_engine PUBLIC wakedeck_core Qt6::Core Qt6::Network)

add_executable(wakedeck-cli
  src/cli/main.cpp
  src/cli/Flags.h
  src/cli/Flags.cpp
  src/cli/SessionFlags.h
  src/cli/SessionFlags.cpp
  src/cli/ConnectCommand.h
  src/cli/ConnectCommand.cpp
  src/cli/ProfileCommand.h
  src/cli/ProfileCommand.cpp)
target_link_libraries(wakedeck-cli PRIVATE wakedeck_engine)

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

add_test(NAME cli_test
  COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.sh $<TARGET_FILE:wakedeck-cli>)
