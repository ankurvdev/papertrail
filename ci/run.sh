#!/bin/bash
set -e
set -o pipefail
set -x

scriptdir=$(dirname "${BASH_SOURCE[0]}")
image_name="papertrail_build"
work_dir="."
init() {
    if [ ! -x "$(command -v podman)" ]; then sudo apt-get -y install podman; fi
}

build() {
    init
    podman build -t papertrail_build --rm -f $scriptdir/../Dockerfile
    podman image save -o $1 papertrail_build
}

test() {
    init
    if [[ ! $(podman image list | grep ${image_name}) ]]; then
        if [ ! -e "$1" ]; then echo "Cannot find source container image (\$1): $1;" && exit -1; fi
        podman load --input $1
    fi
    if [ ! -e ${work_dir}/dataset ]; then 
        if [ ! -e ${work_dir}/dataset.zip ]; then wget https://guillaumejaume.github.io/FUNSD/dataset.zip -o ${work_dir}/dataset.zip; fi
        unzip ${work_dir}/dataset.zip -d ${work_dir}
    fi
    docs_dir="${work_dir}/dataset/testing_data/images/"
    cache_dir="${work_dir}/cache"
    podman run --rm --name test localhost/${image_name} -v ${docs_dir}:/docs -v ${cache_dir}
    sleep 20
    ls -l $cache_dir
    podman stop test
}

cmd=$1
shift
$cmd $*
