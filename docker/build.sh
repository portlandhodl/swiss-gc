#!/bin/sh
# Build Swiss inside Docker. Usage: docker/build.sh [make target]  (default: full release build)
# Examples: docker/build.sh        -> swiss_r*/ release package
#           docker/build.sh dev    -> quick dev build (cube/swiss/swiss.dol)
set -e
cd "$(dirname "$0")/.."
docker build -t swiss-gc-build docker
docker run --rm -v "$PWD:/swiss" swiss-gc-build sh -c "make $* && chown -R $(id -u):$(id -g) /swiss"
