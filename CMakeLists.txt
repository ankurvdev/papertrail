cmake_minimum_required(VERSION 3.28)
include(FetchContent)
project(papertrail)
find_package(Quick REQUIRED)
QuickInit()
EnableStrictCompilation()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ProtoBuf REQUIRED)
find_package(Torch QUIET)
if (NOT Torch_FOUND)
    set(TORCH_VERSION 2.5.0)
    FetchContent_Declare(
          libtorch
          URL "https://download.pytorch.org/libtorch/cpu/libtorch-cxx11-abi-shared-with-deps-${TORCH_VERSION}%2Bcpu.zip"
    )
    FetchContent_MakeAvailable(libtorch)
    list(APPEND CMAKE_PREFIX_PATH "${libtorch_SOURCE_DIR}")
    find_package(Torch REQUIRED)
endif()
find_package(OpenCV 4 REQUIRED)
find_package(Threads REQUIRED)

add_executable(scanner
    cpp/scanner.cpp
    cpp/CRAFT.cpp
    cpp/CRNN.cpp
    cpp/TorchModel.cpp
)

target_include_directories(scanner PRIVATE
    ${TORCH_INCLUDE_DIRS}
)
target_link_libraries(scanner PRIVATE torch ${TORCH_LIBRARIES} ${OpenCV_LIBS} ${MKL})
