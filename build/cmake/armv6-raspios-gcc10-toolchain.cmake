if (NOT DEFINED ENV{ARM_SYSROOT})
message(FATAL_ERROR "Please defined ARM_SYSROOT to the path of the arm image")
endif()

set(CMAKE_SYSROOT "$ENV{ARM_SYSROOT}")

SET(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv6)
SET(CMAKE_CROSSCOMPILING TRUE)

SET(CMAKE_C_COMPILER "arm-linux-gnueabihf-gcc")
SET(CMAKE_CXX_COMPILER "arm-linux-gnueabihf-g++")

if (DEFINED ENV{VCPKG_C_COMPILER_ARM_LINUX})
    SET(CMAKE_C_COMPILER "$ENV{VCPKG_C_COMPILER_ARM_LINUX}")
endif()
if (DEFINED ENV{VCPKG_CXX_COMPILER_ARM_LINUX})
    SET(CMAKE_CXX_COMPILER "$ENV{VCPKG_CXX_COMPILER_ARM_LINUX}")
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})

#set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "${CMAKE_SYSROOT}/usr/include/arm-linux-gnueabihf")
#set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
#    "${CMAKE_SYSROOT}/usr/include/c++/10.2.0"
#    "${CMAKE_SYSROOT}/usr/include/c++/10.2.0/arm-linux-gnueabihf"
#    ${CMAKE_C_STANDARD_INCLUDE_DIRECTORIES}
#   )

# Default C compiler flags
set(CMAKE_C_FLAGS "-march=armv6 -I${CMAKE_SYSROOT}/usr/include/arm-linux-gnueabihf")
set(CMAKE_C_FLAGS_DEBUG_INIT "-g3 -Og -Wall -pedantic -DDEBUG")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG_INIT}")
set(CMAKE_C_FLAGS_RELEASE_INIT "-O3 -Wall")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE_INIT}")
set(CMAKE_C_FLAGS_MINSIZEREL_INIT "-Os -Wall")
set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL_INIT}")
set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT  "-O2 -g -Wall")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_ASM_FLAGS_RELWITHDEBINFO_INIT}")
# Default C++ compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -I${CMAKE_SYSROOT}/usr/include/c++/10.2.0 -I${CMAKE_SYSROOT}/usr/include/c++/10.2.0/arm-linux-gnueabihf")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "-g3 -Og -Wall -pedantic -DDEBUG ")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG_INIT}")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "-O3 -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE_INIT}")
set(CMAKE_CXX_FLAGS_MINSIZEREL_INIT "-Os -Wall")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL_INIT}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT  "-O2 -g -Wall")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_ASM_FLAGS_RELWITHDEBINFO_INIT}")

set(CMAKE_FIND_ROOT_PATH ${BINUTILS_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
set(CMAKE_HAVE_THREADS_LIBRARY 1)
set(CMAKE_THREAD_LIBS_INIT "-lpthread")
set(CMAKE_HAVE_THREADS_LIBRARY 1)
set(CMAKE_USE_WIN32_THREADS_INIT 0)
set(CMAKE_USE_PTHREADS_INIT 1)

# Raspberry pi
# Initial set up
# sudo rmdir usr/lib/pkgconfig
# sudo ln -s arm-linux-gnueabihf/pkgconfig usr/lib/pkgconfig
# sudo mv home/pi/native-pi-gcc-10.2.0-0/ gcc10
# sudo ln -s ../../../gcc10/include/c++/10.2.0/ usr/include/c++/10.2.0
# find links libm

link_directories(${CMAKE_SYSROOT}/usr/lib/gcc/arm-linux-gnueabihf/10.2.0)
link_directories(${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf)
link_directories(${CMAKE_SYSROOT}/usr/lib)

link_libraries(gcc_s)
link_libraries(m)
link_libraries(rt)
link_libraries(pcre)
link_libraries(pthread)

set(CMAKE_EXE_LINKER_FLAGS "-static-libstdc++ -static-libgcc")
set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} ${CMAKE_SYSROOT})
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig:${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf/pkgconfig")

message(WARNING "Using Custom Toolchain for armv6-raspios-gcc10")
