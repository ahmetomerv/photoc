if(NOT DEFINED PHOTOC OR NOT DEFINED FIXTURE_DIR)
    message(FATAL_ERROR "PHOTOC and FIXTURE_DIR are required")
endif()

function(expect_failure command_args category)
    execute_process(
        COMMAND "${PHOTOC}" ${command_args}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT result EQUAL 1 OR NOT stdout STREQUAL "" OR
       NOT stderr MATCHES "${category}")
        message(FATAL_ERROR "Expected ${category} from ${command_args}\nexit ${result}\nstdout: ${stdout}\nstderr: ${stderr}")
    endif()
endfunction()

expect_failure("exif;${FIXTURE_DIR}/invalid.jpg" "image decode error")
expect_failure("exif;${FIXTURE_DIR}/unsupported.png" "unsupported file")
expect_failure("exif;${FIXTURE_DIR}/missing.jpg" "file I/O error")
expect_failure("compress;${FIXTURE_DIR}/invalid.jpg" "image decode error")

set(root "${CMAKE_CURRENT_BINARY_DIR}/photoc-error-collision")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}")
file(COPY_FILE "${FIXTURE_DIR}/with_exif.jpg" "${root}/photo.jpg")
execute_process(COMMAND "${PHOTOC}" compress "${root}/photo.jpg"
    RESULT_VARIABLE first_exit)
if(NOT first_exit EQUAL 0)
    message(FATAL_ERROR "Setup compression failed: ${first_exit}")
endif()
expect_failure("compress;${root}/photo.jpg" "collision")
file(REMOVE_RECURSE "${root}")

execute_process(
    COMMAND "${PHOTOC}" compress --quality fast
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT result EQUAL 2 OR NOT stdout STREQUAL "" OR
   stderr MATCHES "file I/O error|image decode error|collision|internal error|unsupported file|metadata error")
    message(FATAL_ERROR "Usage error was classified as an operational failure\nexit ${result}\n${stderr}")
endif()
