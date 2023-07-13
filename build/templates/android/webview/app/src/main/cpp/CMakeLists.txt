cmake_minimum_required(VERSION 3.4.1)
include(GenerateExportHeader)
set(Python3_EXECUTABLE "zzzsPythonBinzzze" CACHE PATH "")
set(Quick_DIR "zzzsCMakeBuildToolsDirzzze/cmake" CACHE PATH "")

find_package(Quick REQUIRED)

project(zzzsApplication.BinaryNamezzze)

EnableStrictCompilation()

add_subdirectory(zzzsCMakeSourceDirzzze zzzsApplication.BinaryNamezzze)

set(applib zzzsApplication.BinaryNamezzze)
if (NOT TARGET ${applib})
    message(FATAL_ERROR "Cannot find Target ${applib}")
endif()

add_library(native_glue SHARED glue.cpp)
generate_export_header(native_glue)
target_link_libraries(native_glue PRIVATE zzzsApplication.BinaryNamezzze)
target_include_directories(native_glue PRIVATE "${PROJECT_BINARY_DIR}" "${QuickCMake_DIR}/../include")
