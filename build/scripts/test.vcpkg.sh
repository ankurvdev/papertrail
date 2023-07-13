#!/bin/bash
scriptdir=$(realpath $(dirname $0))
rootdir=$1
if [ "${rootdir}" == "" ]; then read -p "Root directory to use:" rootdir; fi
env python3 ${scriptdir}/../Vcpkg.py --test --root=${rootdir}