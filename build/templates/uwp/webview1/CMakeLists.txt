cmake_minimum_required(VERSION 3.4.1)
include(GenerateExportHeader)
project(zzzsApplication.BinaryNamezzze)
set(CMAKE_CXX_STANDARD 20)

set(SKIP_FORMAT TRUE)
set(Quick_DIR "zzzsCMakeBuildToolsDirzzze/cmake" CACHE PATH "")
find_package(Quick REQUIRED)

set(Python3_EXECUTABLE "zzzsPythonBinzzze" CACHE PATH "")
add_subdirectory(zzzsCMakeSourceDirzzze zzzsApplication.BinaryNamezzze)
set(applib zzzsApplication.BinaryNamezzze)
if (NOT TARGET ${applib})
    message(FATAL_ERROR "Cannot find Target ${applib}")
endif()
file(GLOB AssetFiles "Assets/*")
set(ContentFiles     Package.appxmanifest)
add_executable(${applib}UWP WIN32 main.cpp
    ${AssetFiles}
    ${ContentFiles}
    AppxManifest.xml
    Package.appxmanifest
    zzzsApplication.BinaryNamezzze.pfx
)
set_property(SOURCE ${ContentFiles} PROPERTY VS_DEPLOYMENT_CONTENT 1)
set_property(SOURCE ${AssetFiles} PROPERTY VS_DEPLOYMENT_CONTENT 1)
set_property(SOURCE ${AssetFiles} PROPERTY VS_DEPLOYMENT_LOCATION "Assets")

target_link_libraries(${applib}UWP PRIVATE ${applib})
target_include_directories(${applib}UWP PRIVATE "${PROJECT_BINARY_DIR}")