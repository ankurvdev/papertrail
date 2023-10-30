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
    if [[ "$1" != "" ]]; then podman save -o $1 papertrail_build ; fi
}

test() {
    init
    if [[ ! $(podman image list | grep ${image_name}) ]]; then
        if [ ! -e "$1" ]; then echo "Cannot find source container image (\$1): $1;" && exit -1; fi
        podman load --input $1
    fi
    if [ ! -e ${work_dir}/dataset ]; then 
        if [ ! -e ${work_dir}/dataset.zip ]; then curl -L https://guillaumejaume.github.io/FUNSD/dataset.zip -o ${work_dir}/dataset.zip; fi
        unzip ${work_dir}/dataset.zip -d ${work_dir}
    fi
    docs_dir="${work_dir}/dataset/testing_data/images/"
    cache_dir="${work_dir}/cache"
    rm -rf ${cache_dir} || true
    mkdir -p $cache_dir
    container_id=$(podman run -d --rm --name test -v ./dataset/testing_data/images/:/data:Z -v ./cache:/cache:Z localhost/${image_name})
    logcontents="somethingelse"
    prevlogcontents=""
    while [ "${logcontents}" != "${prevlogcontents}" ]; do
        sleep 60
        prevlogcontents=$logcontents
        logcontents=$(podman logs $container_id)
    done
    ls -l $cache_dir
    podman logs ${container_id}
    podman stop ${container_id}
    podman rm ${container_id} || true
}

cmd=$1
shift
$cmd $*
