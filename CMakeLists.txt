cmake_minimum_required(VERSION 3.20)
project(madServerPowerManager VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

add_library(madspm_tuya STATIC src/tuya_native.cpp src/crypto.cpp)
target_compile_definitions(madspm_tuya PRIVATE MADSPM_TUYA_LIBRARY=1)
target_include_directories(madspm_tuya PUBLIC include PRIVATE src)
target_compile_features(madspm_tuya PUBLIC cxx_std_20)

add_library(madspm_core STATIC
    src/types.cpp
    src/config.cpp
    src/nut_client.cpp
    src/state_machine.cpp
    src/state_store.cpp
    src/proxmox.cpp
    src/manager.cpp
    src/http_api.cpp
)
target_include_directories(madspm_core PUBLIC include)
target_compile_features(madspm_core PUBLIC cxx_std_20)
target_link_libraries(madspm_core PUBLIC madspm_tuya pthread)

add_executable(mad-server-power-manager src/main.cpp)
target_link_libraries(mad-server-power-manager PRIVATE madspm_core)

add_executable(plugctl src/tuya_native.cpp src/crypto.cpp)
target_include_directories(plugctl PRIVATE src include)
target_compile_features(plugctl PRIVATE cxx_std_20)

foreach(target madspm_tuya madspm_core mad-server-power-manager plugctl)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        $<$<CONFIG:Release>:-D_FORTIFY_SOURCE=2>
    )
endforeach()

if(BUILD_TESTING)
    add_executable(madspm-tests tests/test_state_machine.cpp)
    target_link_libraries(madspm-tests PRIVATE madspm_core)
    add_test(NAME state-machine COMMAND madspm-tests)
endif()

install(TARGETS mad-server-power-manager plugctl
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
