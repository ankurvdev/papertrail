set(script ${CMAKE_CURRENT_LIST_DIR}/../devel.py)
if(CPACK_EXTERNAL_ENABLE_STAGING)
   set(appdir "${CPACK_TEMPORARY_DIRECTORY}")
else()
   set(appdir "${CPACK_INSTALL_PREFIX}")
endif()

if(NOT EXISTS ${Python3_EXECUTABLE})
    find_package (Python3 REQUIRED COMPONENTS Interpreter)
endif()

execute_process(
   COMMAND "${Python3_EXECUTABLE}" "${script}" packager "${appdir}"
   COMMAND_ERROR_IS_FATAL ANY
)
 