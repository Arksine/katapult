#!/bin/bash
# Katapult build test script.  Based on Klipper's ci-build.sh script.

# Stop script early on any error
set -eu

for TARGET in test/configs/*.config ; do
    echo "::group::=============== Katapult Build $TARGET"
    set -x
    make clean
    make distclean
    unset CC
    cp ${TARGET} .config
    make olddefconfig
    make V=1
    set +x
    echo "=============== Finished $TARGET"
    echo "::endgroup::"
done