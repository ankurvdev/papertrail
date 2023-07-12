#!/bin/bash
scriptdir=$(realpath $(dirname $0))
imagefile=${scriptdir}/2020-12-02-raspios-buster-armhf-lite.img
mount=${scriptdir}/sysroot_mount
if [ ! -d "$1" ]; then echo "Cannot find directory $1"; exit -1; fi
if [[ ! $(mount | grep ${mount}) ]]; then
    echo "Mounting ${imagefile} -> ${mount}"
    sudo mount -o loop,offset=272629760 ${imagefile} ${mount}
    if [[ ! -e ${mount}/usr/lib/crt1.o ]]; then ln -s arm-linux-gnueabihf/crt1.o ${mount}/usr/lib/crt1.o; fi 
    if [[ ! -e ${mount}/usr/lib/crti.o ]]; then ln -s arm-linux-gnueabihf/crti.o ${mount}/usr/lib/crti.o; fi 
    if [[ ! -e ${mount}/usr/lib/crtn.o ]]; then ln -s arm-linux-gnueabihf/crtn.o ${mount}/usr/lib/crtn.o; fi 

fi
mkdir -p ${scriptdir}/build
export ARM_SYSROOT=${mount}
export VCPKG_C_COMPILER_ARM_LINUX=${scriptdir}/gcc/bin/arm-linux-gnueabihf-gcc
export VCPKG_CXX_COMPILER_ARM_LINUX=${scriptdir}/gcc/bin/arm-linux-gnueabihf-g++
export VCPKG_TOOLCHAIN_ARM_LINUX=${scriptdir}/toolchain.cmake
cmake -DCMAKE_TOOLCHAIN_FILE=${scriptdir}/toolchain.cmake -B ${scriptdir}/build -DCMAKE_BUILD_TYPE=Debug --config Debug $1
cmake --build ${scriptdir}/build --config Debug
