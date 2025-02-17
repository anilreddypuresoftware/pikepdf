#!/bin/bash
set -ex

if [ ! -f /usr/local/lib/libz.a ]; then
    pushd zlib
    ./configure && make  install
    popd
fi

if [ ! -f  /usr/local/lib/libjpeg.a ]; then
    pushd jpeg
    ./configure && make  install
    popd
fi

if [ ! -f /usr/local/lib/libqpdf.a ]; then
    pushd qpdf
    if [[ $(uname -p) == 'aarch64' ]]; then
        ./configure --disable-oss-fuzz && make install-libs
    else
        ./configure --disable-oss-fuzz && make -j install-libs
    fi
    find /usr/local/lib -name 'libqpdf.so*' -type f -exec strip --strip-debug {} \+
    popd
fi

ldconfig
