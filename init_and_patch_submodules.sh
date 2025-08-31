#!/bin/bash

rm -rf $RANBOOSTER_PATH/3p/phy
git submodule update --init --recursive

cp $RANBOOSTER_PATH/patches/ofh_lib.patch $RANBOOSTER_PATH/3p/phy
cd $RANBOOSTER_PATH/3p/phy
git apply ofh_lib.patch
