cmake_minimum_required(VERSION 3.21)

project(photoc VERSION 0.1.0 LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEXIF REQUIRED IMPORTED_TARGET libexif)

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

add_library(photoc_core STATIC
    src/core/fs.c
    src/core/json.c
    src/core/metadata.c
    src/core/photo.c
    src/core/scan.c
)
photoc_configure_c_target(photoc_core)
target_include_directories(photoc_core PUBLIC include)
target_link_libraries(photoc_core PUBLIC PkgConfig::LIBEXIF)

add_executable(photoc
    src/main.c
    src/core/cli.c
    src/core/parse.c
    src/commands/exif.c
    src/commands/stub.c
)
photoc_configure_c_target(photoc)
target_link_libraries(photoc PRIVATE photoc_core)

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
    target_link_libraries(test_fs PRIVATE photoc_core)
    target_compile_definitions(test_fs PRIVATE
        PHOTOC_TEST_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/fs/sample.txt"
    )
    add_test(NAME filesystem COMMAND test_fs)

    add_executable(test_photo tests/test_photo.c)
    photoc_configure_c_target(test_photo)
    target_link_libraries(test_photo PRIVATE photoc_core)
    add_test(NAME photo_model COMMAND test_photo)

    add_executable(test_scan tests/test_scan.c)
    photoc_configure_c_target(test_scan)
    target_link_libraries(test_scan PRIVATE photoc_core)
    target_compile_definitions(test_scan PRIVATE
        PHOTOC_SCAN_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
    )
    add_test(NAME photo_scanner COMMAND test_scan)

    add_executable(test_metadata tests/test_metadata.c)
    photoc_configure_c_target(test_metadata)
    target_link_libraries(test_metadata PRIVATE photoc_core)
    target_compile_definitions(test_metadata PRIVATE
        PHOTOC_METADATA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
    )
    add_test(NAME jpeg_metadata COMMAND test_metadata)

    add_executable(test_exif_link tests/test_exif_link.c)
    photoc_configure_c_target(test_exif_link)
    target_link_libraries(test_exif_link PRIVATE photoc_core)
    add_test(NAME exif_library_link COMMAND test_exif_link)

    add_executable(test_json tests/test_json.c)
    photoc_configure_c_target(test_json)
    target_link_libraries(test_json PRIVATE photoc_core)
    add_test(NAME json_writer COMMAND test_json)

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

    foreach(command IN ITEMS compress duplicates stats rename sort focus scrub)
        add_cli_test("command_${command}" stub "${command}")
        add_cli_test("command_${command}_help" command_help "${command}")
    endforeach()
    add_cli_test(command_exif_help command_help exif)

    foreach(case IN ITEMS with_exif with_gps no_exif invalid unsupported missing
                          no_argument extra_argument unknown_option)
        add_test(
            NAME "exif_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exif_cli.cmake"
        )
    endforeach()

    foreach(case IN ITEMS with_exif with_gps no_exif escaped_path
                          before_command invalid unsupported)
        add_test(
            NAME "exif_json_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exif_json.cmake"
        )
    endforeach()

    foreach(option IN ITEMS -v --verbose -q --quiet --json)
        string(REGEX REPLACE "^-+" "" test_suffix "${option}")
        add_cli_test("option_${test_suffix}" option_routing "${option}")
    endforeach()
    add_cli_test(option_after_command option_after_routing -v)
endif()
