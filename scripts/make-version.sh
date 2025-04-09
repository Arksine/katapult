#!/bin/sh
# Detect the version of Katapult

a="/$0"; a="${a%/*}"; a="${a:-.}"; a="${a##/}/"; SRCDIR=$(cd "$a/.."; pwd)

if [ -e "${SRCDIR}/.git" ]; then
    git describe --tags --long --always --dirty
elif [ -f "${SRCDIR}/.version" ]; then
    cat "${SRCDIR}/.version"
else
    echo "?"
fi
