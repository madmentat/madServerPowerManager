cmake_minimum_required(VERSION 3.20)
project(madServerPowerManager VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

add_library(madspm_tuya STATIC src/tuya_native.cpp src/crypto.cpp)
target_include_directories(madspm_tuya PUBLIC include PRIVATE src)
target_compile_features(madspm_tuya PUBLIC cxx_std_20)

add_library(madspm_core STATIC
    src/types.cpp
    src/config.cpp
    src/nut_client.cpp
    src/state_machine.cpp
    src/state_store.cpp
    src/server_client.cpp
    src/manager.cpp
    src/http_api.cpp
)
target_include_directories(madspm_core PUBLIC include)
target_compile_features(madspm_core PUBLIC cxx_std_20)
target_link_libraries(madspm_core PUBLIC madspm_tuya pthread)

add_executable(mad-server-power-manager src/main.cpp)
target_link_libraries(mad-server-power-manager PRIVATE madspm_core)

add_executable(plugctl src/plugctl.cpp)
target_link_libraries(plugctl PRIVATE madspm_tuya)

foreach(target madspm_tuya madspm_core mad-server-power-manager plugctl)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        $<$<CONFIG:Release>:-D_FORTIFY_SOURCE=2>
    )
endforeach()

if(BUILD_TESTING)
    foreach(test_name state_machine state_store config nut server_client http_api manager)
        add_executable(madspm-test-${test_name} tests/test_${test_name}.cpp)
        target_link_libraries(madspm-test-${test_name} PRIVATE madspm_core)
        add_test(NAME ${test_name} COMMAND madspm-test-${test_name})
    endforeach()
endif()

install(TARGETS mad-server-power-manager plugctl
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
