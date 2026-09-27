cmake_minimum_required(VERSION 3.21)

project(photoc VERSION 0.1.0 LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEXIF REQUIRED IMPORTED_TARGET libexif)
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)

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
    src/core/duplicates.c
    src/core/fs.c
    src/core/hash.c
    src/core/image.c
    src/core/filename_template.c
    src/core/jpeg.c
    src/core/jpeg_write.c
    src/core/json.c
    src/core/metadata.c
    src/core/photo.c
    src/core/quality_search.c
    src/core/scan.c
    src/core/session.c
    src/core/stats.c
    src/core/timestamp.c
)
photoc_configure_c_target(photoc_core)
target_include_directories(photoc_core PUBLIC include)
target_link_libraries(photoc_core PUBLIC PkgConfig::LIBEXIF PkgConfig::TURBOJPEG)

add_executable(photoc
    src/main.c
    src/core/cli.c
    src/core/parse.c
    src/commands/compress.c
    src/commands/duplicates.c
    src/commands/exif.c
    src/commands/rename.c
    src/commands/scrub.c
    src/commands/sort.c
    src/commands/stats.c
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

    add_executable(test_hash tests/test_hash.c)
    photoc_configure_c_target(test_hash)
    target_link_libraries(test_hash PRIVATE photoc_core)
    add_test(NAME file_hashing COMMAND test_hash)

    add_executable(test_photo tests/test_photo.c)
    photoc_configure_c_target(test_photo)
    target_link_libraries(test_photo PRIVATE photoc_core)
    add_test(NAME photo_model COMMAND test_photo)

    add_executable(test_session tests/test_session.c)
    photoc_configure_c_target(test_session)
    target_link_libraries(test_session PRIVATE photoc_core)
    add_test(NAME photo_sessions COMMAND test_session)

    add_executable(test_parse_gap tests/test_parse_gap.c src/core/parse.c)
    photoc_configure_c_target(test_parse_gap)
    target_include_directories(test_parse_gap PRIVATE include)
    add_test(NAME gap_parser COMMAND test_parse_gap)

    add_executable(test_quality_search tests/test_quality_search.c)
    photoc_configure_c_target(test_quality_search)
    target_link_libraries(test_quality_search PRIVATE photoc_core)
    add_test(NAME quality_search COMMAND test_quality_search)

    add_executable(test_filename_template tests/test_filename_template.c)
    photoc_configure_c_target(test_filename_template)
    target_link_libraries(test_filename_template PRIVATE photoc_core)
    add_test(NAME filename_template COMMAND test_filename_template)

    add_executable(test_scan tests/test_scan.c)
    photoc_configure_c_target(test_scan)
    target_link_libraries(test_scan PRIVATE photoc_core)
    target_compile_definitions(test_scan PRIVATE
        PHOTOC_SCAN_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
    )
    add_test(NAME photo_scanner COMMAND test_scan)

    add_executable(test_stats tests/test_stats.c)
    photoc_configure_c_target(test_stats)
    target_link_libraries(test_stats PRIVATE photoc_core)
    add_test(NAME stats_aggregation COMMAND test_stats)

    add_executable(test_metadata tests/test_metadata.c)
    photoc_configure_c_target(test_metadata)
    target_link_libraries(test_metadata PRIVATE photoc_core)
    target_compile_definitions(test_metadata PRIVATE
        PHOTOC_METADATA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
    )
    add_test(NAME jpeg_metadata COMMAND test_metadata)

    add_executable(test_jpeg_write tests/test_jpeg_write.c)
    photoc_configure_c_target(test_jpeg_write)
    target_link_libraries(test_jpeg_write PRIVATE photoc_core)
    target_compile_definitions(test_jpeg_write PRIVATE
        PHOTOC_METADATA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
    )
    add_test(NAME jpeg_safe_write COMMAND test_jpeg_write)

    add_executable(test_image tests/test_image.c)
    photoc_configure_c_target(test_image)
    target_link_libraries(test_image PRIVATE photoc_core)
    target_compile_definitions(test_image PRIVATE
        PHOTOC_IMAGE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
    )
    add_test(NAME jpeg_image_roundtrip COMMAND test_image)

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

    foreach(command IN ITEMS focus)
        add_cli_test("command_${command}" stub "${command}")
        add_cli_test("command_${command}_help" command_help "${command}")
    endforeach()
    add_cli_test(command_exif_help command_help exif)
    add_cli_test(command_duplicates_help command_help duplicates)
    add_cli_test(command_stats_help command_help stats)
    add_cli_test(command_rename_help command_help rename)
    add_cli_test(command_sort_help command_help sort)
    add_cli_test(command_scrub_help command_help scrub)
    add_cli_test(command_compress_help command_help compress)

    foreach(case IN ITEMS default quality quality_one quality_hundred quality_effect
                          no_exif extension collision invalid
                          unsupported missing directory no_argument extra_argument
                          quality_missing quality_zero quality_high quality_negative
                          quality_decimal quality_text quality_duplicate json
                          output_dir recursive_file output_dir_missing
                          output_dir_duplicate output_dir_conflict)
        add_test(
            NAME "compress_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_compress_cli.cmake"
        )
    endforeach()

    foreach(case IN ITEMS flat recursive output_dir output_dir_external
                          output_dir_same collision invalid)
        add_test(
            NAME "compress_directory_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_compress_directory.cmake"
        )
    endforeach()

    foreach(case IN ITEMS reachable reachable_exif unreachable max_fits
                          collision quality_conflict min_without_target
                          invalid_target invalid_min missing_target
                          duplicate_target directory)
        add_test(
            NAME "compress_target_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_compress_target.cmake"
        )
    endforeach()

    foreach(case IN ITEMS duplicates same_size_nonduplicates no_duplicates
                          multiple_groups
                          nested missing no_argument extra_argument
                          unknown_option)
        add_test(
            NAME "duplicates_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_duplicates_cli.cmake"
        )
    endforeach()

    foreach(case IN ITEMS duplicates no_duplicates empty multiple_groups
                          recursive escaped_paths before_command missing)
        add_test(
            NAME "duplicates_json_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_duplicates_json.cmake"
        )
    endforeach()

    foreach(case IN ITEMS flat recursive missing_date collision duplicate
                          case_duplicate parent_conflict invalid_jpeg unchanged
                          empty missing_directory missing_by missing_value
                          duplicate_by unknown_by json unknown_option)
        add_test(
            NAME "sort_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_sort_cli.cmake"
        )
    endforeach()

    foreach(case IN ITEMS default gap_30m gap_2h recursive missing_date
                          collision duplicate parent_conflict unchanged
                          invalid_jpeg empty gap_zero gap_with_date gap_missing
                          gap_duplicate invalid_gap_no_unit
                          invalid_gap_uppercase invalid_gap_decimal
                          invalid_gap_overflow)
        add_test(
            NAME "sort_session_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_sort_session.cmake"
        )
    endforeach()

    foreach(case IN ITEMS date_success date_recursive session_success
                          session_recursive unchanged empty collision duplicate
                          missing_date invalid_jpeg parent_file parent_symlink
                          directory_failure runtime_rollback)
        add_test(
            NAME "sort_apply_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_sort_apply.cmake"
        )
    endforeach()

    foreach(case IN ITEMS flat recursive missing_date missing_camera collision
                          file_collision duplicate case_duplicate
                          separate_directories invalid_jpeg unchanged empty
                          format_before_directory unknown_placeholder
                          malformed_template missing_format missing_value
                          duplicate_format missing_directory json unknown_option)
        add_test(
            NAME "rename_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_rename_cli.cmake"
        )
    endforeach()

    foreach(case IN ITEMS success recursive unchanged empty collision duplicate
                          missing_date invalid_jpeg runtime_rollback)
        add_test(
            NAME "rename_apply_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_rename_apply.cmake"
        )
    endforeach()

    foreach(case IN ITEMS flat recursive empty single missing_exif missing no_argument
                          extra_argument unknown_option)
        add_test(
            NAME "stats_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_stats_cli.cmake"
        )
    endforeach()

    foreach(case IN ITEMS flat recursive empty missing_exif escaped_directory
                          before_command missing)
        add_test(
            NAME "stats_json_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_stats_json.cmake"
        )
    endforeach()

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

    foreach(case IN ITEMS single directory directory_failure recursive no_gps invalid
                          collision unsupported missing_gps missing_path
                          recursive_file json unknown_option
                          in_place_single in_place_directory
                          in_place_directory_failure in_place_recursive
                          in_place_no_gps in_place_invalid
                          in_place_hardlink in_place_symlink)
        add_test(
            NAME "scrub_${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPHOTOC=$<TARGET_FILE:photoc>"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
                "-DCASE=${case}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_scrub_cli.cmake"
        )
    endforeach()

    foreach(option IN ITEMS -v --verbose -q --quiet --json)
        string(REGEX REPLACE "^-+" "" test_suffix "${option}")
        add_cli_test("option_${test_suffix}" option_routing "${option}")
    endforeach()
    add_cli_test(option_after_command option_after_routing -v)
endif()
