#!/usr/bin/bash

DIR=$( dirname "${BASH_SOURCE[0]}" )
source ${DIR}/env.sh

export PIN_VERSION=pin-3.28-98749-g6643ecee5
export PMDK_VERSION=1.12.1
export YAML_ROOT=${TOOL_ROOT}/deps/libyaml

mkdir ${TOOL_ROOT}/deps -p

if [ ! -d $PIN_ROOT ] ; then
	echo "Downloading PIN"
	# Download PIN
	cd ${TOOL_ROOT}/deps
	mkdir $PIN_ROOT
	wget -q http://software.intel.com/sites/landingpage/pintool/downloads/${PIN_VERSION}-gcc-linux.tar.gz && \
	    tar -xf ${PIN_VERSION}-gcc-linux.tar.gz -C $PIN_ROOT --strip-components 1 && \
	    rm -f ${PIN_VERSION}-gcc-linux.tar.gz
fi

if [ ! -d ${TOOL_ROOT}/deps/libyaml ] ; then
	echo "Downloading YAML" 
	# Download YAML
	cd ${TOOL_ROOT}/deps
	git clone https://github.com/yaml/libyaml

	cd ${TOOL_ROOT}/deps/libyaml
	git checkout f8f760f7387d2cc56a2fc7b1be313a3bf3f7f58c
	git apply ${TOOL_ROOT}/yaml.diff

	cd ${TOOL_ROOT}/deps/libyaml/src
	make libyaml.a
fi

mkdir ${TOOL_ROOT}/src/obj-intel64 -p
make -C ${TOOL_ROOT}/src obj-intel64/hawkset.so