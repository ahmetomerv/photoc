cmake_minimum_required(VERSION 3.21)

project(photoc VERSION 0.1.0 LANGUAGES C)

function(photoc_configure_c_target target)
    set_target_properties("${target}" PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    if(MSVC)
        target_compile_options("${target}" PRIVATE /W4 /WX)
    elseif(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

add_library(photoc_fs STATIC src/core/fs.c)
photoc_configure_c_target(photoc_fs)
target_include_directories(photoc_fs PUBLIC include)

add_executable(photoc
    src/main.c
    src/core/cli.c
    src/core/parse.c
    src/commands/stub.c
)
photoc_configure_c_target(photoc)
target_link_libraries(photoc PRIVATE photoc_fs)

configure_file(
    include/photoc/version.h.in
    generated/photoc/version.h
    @ONLY
)

target_include_directories(photoc PRIVATE
    include
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

include(CTest)

if(BUILD_TESTING)
    add_executable(test_fs tests/test_fs.c)
    photoc_configure_c_target(test_fs)
    target_link_libraries(test_fs PRIVATE photoc_fs)
    target_compile_definitions(test_fs PRIVATE
        PHOTOC_TEST_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/sample.txt"
    )
    add_test(NAME filesystem COMMAND test_fs)

    function(add_cli_test name case)
        set(extra_args)
        if(ARGC GREATER 2)
            list(APPEND extra_args "-DARG=${ARGV2}")
        endif()
        add_test(
            NAME "${name}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DCASE=${case}"
                "-DEXPECT_VERSION=${PROJECT_VERSION}"
                ${extra_args}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.cmake"
        )
    endfunction()

    foreach(case IN ITEMS help short_help version short_version no_args unknown
                          unknown_with_option unknown_option_before
                          unknown_option_after unknown_short_option
                          help_version_conflict verbose_quiet_conflict
                          version_with_command end_of_options command_arguments)
        add_cli_test("cli_${case}" "${case}")
    endforeach()

    foreach(command IN ITEMS compress exif duplicates stats rename sort focus scrub)
        add_cli_test("command_${command}" stub "${command}")
        add_cli_test("command_${command}_help" command_help "${command}")
    endforeach()

    foreach(option IN ITEMS -v --verbose -q --quiet --json)
        string(REGEX REPLACE "^-+" "" test_suffix "${option}")
        add_cli_test("option_${test_suffix}" option_routing "${option}")
    endforeach()
    add_cli_test(option_after_command option_after_routing -v)
endif()
