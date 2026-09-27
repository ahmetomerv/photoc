cmake_minimum_required(VERSION 3.21)

project(photoc VERSION 0.1.0 LANGUAGES C)

add_executable(photoc
    src/main.c
    src/core/cli.c
    src/commands/stub.c
)

set_target_properties(photoc PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

configure_file(
    include/photoc/version.h.in
    generated/photoc/version.h
    @ONLY
)

target_include_directories(photoc PRIVATE
    include
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

if(MSVC)
    target_compile_options(photoc PRIVATE /W4 /WX)
elseif(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    target_compile_options(photoc PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(CTest)

if(BUILD_TESTING)
    foreach(case IN ITEMS help version no_args unknown)
        add_test(
            NAME cli_${case}
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DCASE=${case}"
                "-DEXPECT_VERSION=${PROJECT_VERSION}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.cmake"
        )
    endforeach()

    foreach(command IN ITEMS compress exif duplicates stats rename sort focus scrub)
        add_test(
            NAME command_${command}
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DCASE=stub"
                "-DARG=${command}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.cmake"
        )
    endforeach()
endif()
