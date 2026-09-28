# DESTDIR keeps all installation writes inside the test staging tree, even
# when the configured install directories are absolute.
set(root "${BUILD_DIR}/install-test")
set(stage "${root}/stage with spaces")
set(prefix "/photoc-test")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${stage}")

set(install_command "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}")
if(NOT CONFIG STREQUAL "")
    list(APPEND install_command --config "${CONFIG}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "DESTDIR=${stage}" ${install_command}
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Staged installation failed: ${install_output}${install_error}")
endif()

foreach(directory IN ITEMS BINDIR MANDIR DOCDIR)
    if(IS_ABSOLUTE "${${directory}}")
        set(installed_${directory} "${stage}${${directory}}")
    else()
        set(installed_${directory} "${stage}${prefix}/${${directory}}")
    endif()
endforeach()
foreach(name IN ITEMS LICENSE THIRD_PARTY_NOTICES.md licenses/libexif/COPYING
                      licenses/libjpeg-turbo/README.ijg
                      licenses/libjpeg-turbo/LICENSE-3.2.0.md
                      licenses/libjpeg-turbo/LICENSE-2.1.2.md)
    if(NOT EXISTS "${installed_DOCDIR}/${name}")
        message(FATAL_ERROR "Missing installed license notice: ${name}")
    endif()
    file(SHA256 "${SOURCE_DIR}/${name}" source_hash)
    file(SHA256 "${installed_DOCDIR}/${name}" installed_hash)
    if(NOT source_hash STREQUAL installed_hash)
        message(FATAL_ERROR "Installed license notice differs: ${name}")
    endif()
endforeach()
set(binary "${installed_BINDIR}/photoc")
set(man_page "${installed_MANDIR}/man1/photoc.1")
if(NOT EXISTS "${binary}" OR NOT EXISTS "${man_page}")
    message(FATAL_ERROR "Expected installed executable and man page: ${install_output}")
endif()
file(SHA256 "${MAN_SOURCE}" source_hash)
file(SHA256 "${man_page}" installed_hash)
if(NOT source_hash STREQUAL installed_hash)
    message(FATAL_ERROR "Installed man page differs from its source")
endif()
execute_process(COMMAND "${binary}" --version
    RESULT_VARIABLE version_result OUTPUT_VARIABLE version_output ERROR_VARIABLE version_error)
if(NOT version_result EQUAL 0 OR NOT version_output MATCHES "^photoc [0-9]" OR
   NOT version_error STREQUAL "")
    message(FATAL_ERROR "Installed executable failed: ${version_output}${version_error}")
endif()
file(REMOVE_RECURSE "${root}")
