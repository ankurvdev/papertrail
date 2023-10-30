#!/bin/bash
set -e
set -o pipefail
set -x

scriptdir=$(dirname "${BASH_SOURCE[0]}")
image_name="papertrail_build"

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
        cat "$1" | podman image import - ${image_name}
    fi
    podman run --rm localhost/${image_name}
}

cmd=$1
shift
$cmd $*
