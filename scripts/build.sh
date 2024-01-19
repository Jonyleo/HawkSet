#!/usr/bin/bash

mkdir ${TOOL_ROOT}/src/obj-intel64 -p
make -C ${TOOL_ROOT}/src obj-intel64/hawkset.so

