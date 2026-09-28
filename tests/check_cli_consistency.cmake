if(NOT DEFINED PHOTOC)
    message(FATAL_ERROR "PHOTOC is required")
endif()

foreach(command IN ITEMS compress exif duplicates stats rename sort focus scrub)
    execute_process(
        COMMAND "${PHOTOC}" "${command}" --bogus
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    set(expected "photoc ${command}: unknown option '--bogus'\nTry 'photoc ${command} --help' for available options.\n")
    if(NOT "${result}" STREQUAL "2" OR NOT stdout STREQUAL "" OR
       NOT stderr STREQUAL "${expected}")
        message(FATAL_ERROR "Inconsistent unknown-option result for ${command}: exit ${result}\nstdout: ${stdout}\nstderr: ${stderr}")
    endif()

    execute_process(
        COMMAND "${PHOTOC}" "${command}" --help
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT "${result}" STREQUAL "0" OR NOT stderr STREQUAL "" OR
       NOT stdout MATCHES "^Usage: photoc \\[global options\\] ${command} ")
        message(FATAL_ERROR "Inconsistent help result for ${command}: exit ${result}\nstdout: ${stdout}\nstderr: ${stderr}")
    endif()
endforeach()

foreach(command IN ITEMS compress rename sort scrub)
    execute_process(
        COMMAND "${PHOTOC}" "${command}" --json
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    set(expected "photoc ${command}: --json is not supported\n")
    if(NOT "${result}" STREQUAL "2" OR NOT stdout STREQUAL "" OR
       NOT stderr STREQUAL "${expected}")
        message(FATAL_ERROR "Inconsistent JSON result for ${command}: exit ${result}\nstdout: ${stdout}\nstderr: ${stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${PHOTOC}" sort --by
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
set(expected "photoc sort: option '--by' requires a value\n")
if(NOT "${result}" STREQUAL "2" OR NOT stdout STREQUAL "" OR
   NOT stderr STREQUAL "${expected}")
    message(FATAL_ERROR "Inconsistent missing-value result: exit ${result}\nstdout: ${stdout}\nstderr: ${stderr}")
endif()

execute_process(
    COMMAND "${PHOTOC}" rename --format a --format b .
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
set(expected "photoc rename: option '--format' may be specified only once\n")
if(NOT "${result}" STREQUAL "2" OR NOT stdout STREQUAL "" OR
   NOT stderr STREQUAL "${expected}")
    message(FATAL_ERROR "Inconsistent duplicate-option result: exit ${result}\nstdout: ${stdout}\nstderr: ${stderr}")
endif()

execute_process(
    COMMAND "${PHOTOC}" --recursive stats "${CMAKE_CURRENT_BINARY_DIR}/missing-cli-consistency"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT "${result}" STREQUAL "1" OR NOT stdout STREQUAL "" OR
   NOT stderr MATCHES "^photoc stats: ")
    message(FATAL_ERROR "Option before command was rejected: exit ${result}\nstdout: ${stdout}\nstderr: ${stderr}")
endif()

execute_process(
    COMMAND "${PHOTOC}" --quality 0 compress .
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
set(expected "photoc compress: invalid quality '0'; use 1-100\n")
if(NOT "${result}" STREQUAL "2" OR NOT stdout STREQUAL "" OR
   NOT stderr STREQUAL "${expected}")
    message(FATAL_ERROR "Option before command was not accepted: exit ${result}\nstdout: ${stdout}\nstderr: ${stderr}")
endif()
