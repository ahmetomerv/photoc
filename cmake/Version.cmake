# VERSION is the only manually maintained project version.
set(PHOTOC_VERSION_FILE "${CMAKE_CURRENT_LIST_DIR}/../VERSION")
file(READ "${PHOTOC_VERSION_FILE}" PHOTOC_VERSION)
string(STRIP "${PHOTOC_VERSION}" PHOTOC_VERSION)
if(NOT PHOTOC_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR
        "VERSION must contain MAJOR.MINOR.PATCH without leading zeroes")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PHOTOC_VERSION_FILE}")
