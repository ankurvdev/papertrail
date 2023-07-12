#!/bin/bash
scriptdir=$(realpath $(dirname $0))
imagefile=${scriptdir}/fedora-armhfp-33.raw
mount=${scriptdir}/sysroot_mount
if [ ! -d "$1" ]; then echo "Cannot find directory $1"; exit -1; fi
if [[ ! $(mount | grep ${mount}) ]]; then
    echo "Mounting ${imagefile} -> ${mount}"
    sudo mount -o loop,offset=595591168 ${imagefile} ${mount}
fi
mkdir -p ${scriptdir}/build
export ARM_SYSROOT=${mount}
export VCPKG_C_COMPILER_ARM_LINUX=/usr/bin/arm-linux-gnu-gcc
export VCPKG_CXX_COMPILER_ARM_LINUX=/usr/bin/arm-linux-gnu-g++
export VCPKG_TOOLCHAIN_ARM_LINUX=${scriptdir}/toolchain.cmake
cmake -DCMAKE_TOOLCHAIN_FILE=${scriptdir}/toolchain.cmake -B ${scriptdir}/build $1
cmake --build ${scriptdir}/build
