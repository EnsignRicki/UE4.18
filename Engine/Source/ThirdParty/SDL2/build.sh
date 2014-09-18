#!/bin/sh

set -e
SDL_DIR=SDL-gui-backend
BUILD_DIR=build-$SDL_DIR

rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR
cd $BUILD_DIR

CFLAGS=-fPIC cmake ../$SDL_DIR
make
cp --remove-destination libSDL2.a ../$SDL_DIR/lib/Linux/x86_64-unknown-linux-gnu/libSDL2.a
set +e

