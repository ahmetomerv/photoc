cmake_minimum_required(VERSION 3.21)

include(cmake/Version.cmake)
project(photoc VERSION "${PHOTOC_VERSION}" LANGUAGES C)

include(GNUInstallDirs)

option(PHOTOC_SANITIZERS
    "Build with AddressSanitizer and UndefinedBehaviorSanitizer"
    OFF)
option(PHOTOC_THREAD_SANITIZER "Build with ThreadSanitizer" OFF)
if(PHOTOC_SANITIZERS AND PHOTOC_THREAD_SANITIZER)
    message(FATAL_ERROR "ThreadSanitizer cannot be combined with AddressSanitizer")
endif()
if(PHOTOC_THREAD_SANITIZER)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        message(FATAL_ERROR "PHOTOC_THREAD_SANITIZER requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()
if(PHOTOC_SANITIZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        message(FATAL_ERROR "PHOTOC_SANITIZERS requires GCC or Clang")
    endif()
    add_compile_options(
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=undefined
    )
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LIBEXIF REQUIRED IMPORTED_TARGET libexif)
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)
pkg_check_modules(LIBJPEG REQUIRED IMPORTED_TARGET libjpeg)

function(photoc_configure_c_target target)
    set_target_properties("${target}" PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )

    if(MSVC)
        target_compile_options("${target}" PRIVATE /W4 /WX)
    elseif(CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        target_compile_options("${target}" PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wformat-security
            -Wshadow
            -Wimplicit-fallthrough
            -Wmissing-prototypes
            -Wstrict-prototypes
            -Wundef
            -Wvla
            -Wcast-align
            -Wpointer-arith
            -Wswitch-enum
            -Wnull-dereference
            -Wwrite-strings
        )
    endif()
endfunction()

option(PHOTOC_CLANG_TIDY "Run clang-tidy while compiling" OFF)
if(PHOTOC_CLANG_TIDY)
    find_program(PHOTOC_CLANG_TIDY_PROGRAM NAMES clang-tidy REQUIRED)
    set(CMAKE_C_CLANG_TIDY
        "${PHOTOC_CLANG_TIDY_PROGRAM}"
        "--warnings-as-errors=*"
    )
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(photoc_core STATIC
    src/core/arw.c
    src/core/duplicates.c
    src/core/error.c
    src/core/fs.c
    src/core/format.c
    src/core/hash.c
    src/core/image.c
    src/core/image_analysis.c
    src/core/filename_template.c
    src/core/jpeg.c
    src/core/jpeg_check.c
    src/core/jpeg_metadata.c
    src/core/jpeg_write.c
    src/core/json.c
    src/core/metadata.c
    src/core/output.c
    src/core/parse.c
    src/core/photo.c
    src/core/query.c
    src/core/quality_search.c
    src/core/scan.c
    src/core/session.c
    src/core/sharpness.c
    src/core/stats.c
    src/core/timestamp.c
    src/core/tiff.c
    src/core/thread_pool.c
)
photoc_configure_c_target(photoc_core)
target_include_directories(photoc_core PUBLIC include)
target_link_libraries(photoc_core PUBLIC PkgConfig::LIBEXIF PkgConfig::TURBOJPEG PkgConfig::LIBJPEG Threads::Threads)

add_executable(photoc
    src/main.c
    src/core/cli.c
    src/commands/check.c
    src/commands/compress.c
    src/commands/duplicates.c
    src/commands/exif.c
    src/commands/focus.c
    src/commands/query.c
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

install(TARGETS photoc RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES man/photoc.1 DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY licenses DESTINATION "${CMAKE_INSTALL_DOCDIR}")

include(CTest)

option(PHOTOC_BENCHMARKS "Build optional concurrency benchmarks" OFF)
if(PHOTOC_BENCHMARKS)
    add_executable(benchmark_concurrency benchmarks/concurrency.c)
    photoc_configure_c_target(benchmark_concurrency)
    target_link_libraries(benchmark_concurrency PRIVATE photoc_core)
endif()

if(BUILD_TESTING)
    if(UNIX)
        add_test(NAME installer_shell_syntax
            COMMAND /bin/sh -n "${CMAKE_CURRENT_SOURCE_DIR}/scripts/install.sh")
        add_test(NAME uninstaller_shell_syntax
            COMMAND /bin/sh -n "${CMAKE_CURRENT_SOURCE_DIR}/scripts/uninstall.sh")
        find_package(Python3 QUIET COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME output_failure
                COMMAND "${CMAKE_COMMAND}" -E env "PHOTOC_OUTPUT_TEST_BINARY=$<TARGET_FILE:photoc>"
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_output_failure.py")
            add_test(NAME verbosity_cli
                COMMAND "${CMAKE_COMMAND}" -E env "PHOTOC_VERBOSITY_TEST_BINARY=$<TARGET_FILE:photoc>"
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_verbosity.py")
            set_tests_properties(verbosity_cli PROPERTIES TIMEOUT 60)
            add_test(NAME compress_metadata_cli
                COMMAND "${CMAKE_COMMAND}" -E env "PHOTOC_COMPRESS_METADATA_BINARY=$<TARGET_FILE:photoc>"
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_compress_metadata.py")
            set_tests_properties(compress_metadata_cli PROPERTIES TIMEOUT 60)
            add_test(NAME check_cli
                COMMAND "${CMAKE_COMMAND}" -E env "PHOTOC_CHECK_TEST_BINARY=$<TARGET_FILE:photoc>"
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_check_cli.py")
            set_tests_properties(check_cli PROPERTIES TIMEOUT 60)
            add_test(NAME query_cli
                COMMAND "${CMAKE_COMMAND}" -E env "PHOTOC_QUERY_TEST_BINARY=$<TARGET_FILE:photoc>"
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_query_cli.py")
            set_tests_properties(query_cli PROPERTIES TIMEOUT 60)
            add_test(NAME arw_cli
                COMMAND "${CMAKE_COMMAND}" -E env "PHOTOC_ARW_TEST_BINARY=$<TARGET_FILE:photoc>"
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_arw_cli.py")
            set_tests_properties(arw_cli PROPERTIES TIMEOUT 60)
            add_test(NAME stats_extended_cli
                COMMAND "${CMAKE_COMMAND}" -E env "PHOTOC_STATS_TEST_BINARY=$<TARGET_FILE:photoc>"
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_stats_extended.py")
            set_tests_properties(stats_extended_cli PROPERTIES TIMEOUT 60)
            add_test(NAME focus_cli
                COMMAND "${CMAKE_COMMAND}" -E env "PHOTOC_FOCUS_TEST_BINARY=$<TARGET_FILE:photoc>"
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_focus_cli.py")
            set_tests_properties(focus_cli PROPERTIES TIMEOUT 60)
            add_test(NAME release_installer
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_installer.py")
            add_test(NAME release_uninstaller
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_uninstaller.py")
            add_test(NAME release_packaging
                COMMAND "${CMAKE_COMMAND}" -E env "PHOTOC_RELEASE_TEST_BINARY=$<TARGET_FILE:photoc>"
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_release_packaging.py")
            add_test(NAME versioning
                COMMAND "${CMAKE_COMMAND}" -E env
                    "PHOTOC_VERSION_TEST_BINARY=$<TARGET_FILE:photoc>"
                    "PHOTOC_VERSION_TEST_CMAKE=${CMAKE_COMMAND}"
                    "PHOTOC_VERSION_TEST_COMPILER=${CMAKE_C_COMPILER}"
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_versioning.py")
            set_tests_properties(release_installer release_uninstaller release_packaging versioning
                PROPERTIES TIMEOUT 60)
        else()
            message(STATUS "Python 3 not found; release script and versioning tests are not registered")
        endif()
        add_test(
            NAME installation
            COMMAND "${CMAKE_COMMAND}"
                "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
                "-DMAN_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/man/photoc.1"
                "-DBINDIR=${CMAKE_INSTALL_BINDIR}"
                "-DMANDIR=${CMAKE_INSTALL_MANDIR}"
                "-DDOCDIR=${CMAKE_INSTALL_DOCDIR}"
                "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DCONFIG=$<CONFIG>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_install.cmake"
        )
    endif()
    find_program(PHOTOC_MANDOC_EXECUTABLE NAMES mandoc)
    if(PHOTOC_MANDOC_EXECUTABLE)
        add_test(NAME man_page_lint
            COMMAND "${PHOTOC_MANDOC_EXECUTABLE}" -T lint -W warning
                "${CMAKE_CURRENT_SOURCE_DIR}/man/photoc.1")
    endif()

    # Completions use native shells; none is required to build or run photoc.
    foreach(shell IN ITEMS bash zsh fish)
        find_program(PHOTOC_${shell}_EXECUTABLE NAMES ${shell})
        if(PHOTOC_${shell}_EXECUTABLE)
            if(shell STREQUAL "bash")
                set(completion_file "completions/bash/photoc")
                set(completion_shell_flags --noprofile --norc)
            elseif(shell STREQUAL "zsh")
                set(completion_file "completions/zsh/_photoc")
                set(completion_shell_flags -f)
            else()
                set(completion_file "completions/fish/photoc.fish")
                set(completion_shell_flags --no-config)
            endif()
            add_test(
                NAME "completion_${shell}"
                COMMAND "${PHOTOC_${shell}_EXECUTABLE}"
                    ${completion_shell_flags}
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_completion_${shell}.${shell}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/${completion_file}"
                    "${CMAKE_CURRENT_BINARY_DIR}"
            )
            set_tests_properties("completion_${shell}" PROPERTIES TIMEOUT 20)
        else()
            message(STATUS "${shell} not found; its optional completion test is not registered")
        endif()
    endforeach()

    add_executable(test_thread_pool tests/test_thread_pool.c)
    photoc_configure_c_target(test_thread_pool)
    target_link_libraries(test_thread_pool PRIVATE photoc_core)
    add_test(NAME thread_pool COMMAND test_thread_pool)

    add_executable(test_rename_rollback tests/test_rename_rollback.c)
    photoc_configure_c_target(test_rename_rollback)
    target_link_libraries(test_rename_rollback PRIVATE photoc_core)
    add_test(NAME rename_rollback_safety COMMAND test_rename_rollback)

    add_executable(test_concurrency tests/test_concurrency.c)
    photoc_configure_c_target(test_concurrency)
    target_link_libraries(test_concurrency PRIVATE photoc_core)
    target_compile_definitions(test_concurrency PRIVATE
        PHOTOC_CONCURRENCY_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
    )
    add_test(NAME concurrent_scanning COMMAND test_concurrency)

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

    add_executable(test_arw tests/test_arw.c)
    photoc_configure_c_target(test_arw)
    target_link_libraries(test_arw PRIVATE photoc_core)
    target_compile_definitions(test_arw PRIVATE
        PHOTOC_ARW_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/arw")
    add_test(NAME arw_metadata COMMAND test_arw)

    add_executable(test_query tests/test_query.c)
    photoc_configure_c_target(test_query)
    target_link_libraries(test_query PRIVATE photoc_core)
    add_test(NAME query_predicates COMMAND test_query)

    add_executable(test_session tests/test_session.c)
    photoc_configure_c_target(test_session)
    target_link_libraries(test_session PRIVATE photoc_core)
    add_test(NAME photo_sessions COMMAND test_session)

    add_executable(test_parse_gap tests/test_parse_gap.c src/core/parse.c)
    photoc_configure_c_target(test_parse_gap)
    target_include_directories(test_parse_gap PRIVATE include)
    add_test(NAME gap_parser COMMAND test_parse_gap)

    add_executable(test_parse_threshold tests/test_parse_threshold.c src/core/parse.c)
    photoc_configure_c_target(test_parse_threshold)
    target_include_directories(test_parse_threshold PRIVATE include)
    add_test(NAME threshold_parser COMMAND test_parse_threshold)

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

    add_executable(test_jpeg_metadata tests/test_jpeg_metadata.c)
    photoc_configure_c_target(test_jpeg_metadata)
    target_link_libraries(test_jpeg_metadata PRIVATE photoc_core)
    target_compile_definitions(test_jpeg_metadata PRIVATE
        PHOTOC_SEGMENT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg_metadata")
    add_test(NAME jpeg_metadata_segments COMMAND test_jpeg_metadata)
    set_tests_properties(jpeg_metadata_segments PROPERTIES TIMEOUT 60)

    add_executable(test_jpeg_check tests/test_jpeg_check.c)
    photoc_configure_c_target(test_jpeg_check)
    target_link_libraries(test_jpeg_check PRIVATE photoc_core)
    add_test(NAME jpeg_integrity COMMAND test_jpeg_check)
    set_tests_properties(jpeg_integrity PROPERTIES TIMEOUT 60)

    add_executable(test_jpeg_temp_failure tests/test_jpeg_temp_failure.c)
    photoc_configure_c_target(test_jpeg_temp_failure)
    target_link_libraries(test_jpeg_temp_failure PRIVATE photoc_core)
    target_compile_definitions(test_jpeg_temp_failure PRIVATE
        PHOTOC_METADATA_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
    )
    add_test(NAME jpeg_temp_failure_safety COMMAND test_jpeg_temp_failure)

    add_executable(test_image tests/test_image.c)
    photoc_configure_c_target(test_image)
    target_link_libraries(test_image PRIVATE photoc_core)
    target_compile_definitions(test_image PRIVATE
        PHOTOC_IMAGE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
    )
    add_test(NAME jpeg_image_roundtrip COMMAND test_image)

    add_executable(test_image_analysis tests/test_image_analysis.c)
    photoc_configure_c_target(test_image_analysis)
    target_link_libraries(test_image_analysis PRIVATE photoc_core)
    add_test(NAME image_analysis COMMAND test_image_analysis)

    add_executable(test_sharpness tests/test_sharpness.c)
    photoc_configure_c_target(test_sharpness)
    target_link_libraries(test_sharpness PRIVATE photoc_core)
    target_compile_definitions(test_sharpness PRIVATE
        PHOTOC_SHARPNESS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
    )
    add_test(NAME jpeg_sharpness COMMAND test_sharpness)

    add_executable(test_exif_link tests/test_exif_link.c)
    photoc_configure_c_target(test_exif_link)
    target_link_libraries(test_exif_link PRIVATE photoc_core)
    add_test(NAME exif_library_link COMMAND test_exif_link)

    add_executable(test_json tests/test_json.c)
    photoc_configure_c_target(test_json)
    target_link_libraries(test_json PRIVATE photoc_core)
    add_test(NAME json_writer COMMAND test_json)

    add_executable(test_error tests/test_error.c)
    photoc_configure_c_target(test_error)
    target_link_libraries(test_error PRIVATE photoc_core)
    add_test(NAME error_model COMMAND test_error)

    add_executable(test_output tests/test_output.c)
    photoc_configure_c_target(test_output)
    target_link_libraries(test_output PRIVATE photoc_core)
    add_test(NAME output_policy COMMAND test_output)

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
                          verbose_quiet_after version_with_command
                          end_of_options end_of_options_command
                          help_before_command dash_argument focus_json
                          command_arguments)
        add_cli_test("cli_${case}" "${case}")
    endforeach()

    foreach(command IN ITEMS focus)
        add_cli_test("command_${command}" focus_usage "${command}")
        add_cli_test("command_${command}_help" command_help "${command}")
    endforeach()
    add_cli_test(command_exif_help command_help exif)
    add_cli_test(command_duplicates_help command_help duplicates)
    add_cli_test(command_stats_help command_help stats)
    add_cli_test(command_rename_help command_help rename)
    add_cli_test(command_sort_help command_help sort)
    add_cli_test(command_scrub_help command_help scrub)
    add_cli_test(command_compress_help command_help compress)
    add_cli_test(command_check_help command_help check)
    add_cli_test(command_query_help command_help query)

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
                          output_dir_same collision invalid skipped_inputs)
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

    foreach(case IN ITEMS duplicates same_size_nonduplicates
                          large_unique_same_size no_duplicates
                          multiple_groups
                          nested missing file unreadable no_argument
                          extra_argument unknown_option)
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
                          symlink empty missing_directory missing_by missing_value
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
                          separate_directories invalid_jpeg unchanged symlink
                          unsafe_name empty
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
                          directory no_argument extra_argument unknown_option)
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
                          collision unsupported missing_gps missing_path symlink
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

    add_test(
        NAME cli_consistency
        COMMAND "${CMAKE_COMMAND}"
            "-DPHOTOC=$<TARGET_FILE:photoc>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli_consistency.cmake"
    )

    add_test(
        NAME error_reporting
        COMMAND "${CMAKE_COMMAND}"
            "-DPHOTOC=$<TARGET_FILE:photoc>"
            "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jpeg"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_errors.cmake"
    )
endif()
