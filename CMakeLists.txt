cmake_minimum_required(VERSION 3.16)
project(dcas_policy_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(dcas_policy_engine
    src/step_b.cpp
    src/step_c.cpp
    src/policy_runtime.cpp
)

target_include_directories(dcas_policy_engine PUBLIC include)

add_subdirectory(../rt-control-ipc rt-control-ipc)

add_executable(dcas_policy_runner src/main.cpp)
target_link_libraries(dcas_policy_runner PRIVATE dcas_policy_engine)

add_executable(dcas_rt_bridge src/dcas_rt_bridge.cpp)
target_link_libraries(dcas_rt_bridge PRIVATE dcas_policy_engine rt_control_ipc)
target_link_libraries(dcas_rt_bridge PRIVATE rt)

enable_testing()
add_executable(dcas_policy_tests tests/test_policy.cpp)
target_link_libraries(dcas_policy_tests PRIVATE dcas_policy_engine)
add_test(NAME dcas_policy_tests COMMAND dcas_policy_tests)
