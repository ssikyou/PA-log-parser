#!/bin/bash

PWD=`pwd`
export PATH=$PWD/toolchains/ndk-arm-android21-toolchain/bin:$PATH
export CC=arm-linux-androideabi-gcc
export AR=arm-linux-androideabi-ar
