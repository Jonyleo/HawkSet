ARG BASE_OS="ubuntu:20.04"
FROM ${BASE_OS}

SHELL ["/bin/bash", "-c"] 

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update && apt install -y \
    apt-utils build-essential man wget git htop dstat procps gdb telnet time \
    gcc-10 g++-10 nano cmake autoconf automake libtool libkmod-dev libudev-dev \
    uuid-dev libjson-c-dev bash-completion libkeyutils-dev pandoc pkg-config

RUN rm /usr/bin/g++
RUN rm /usr/bin/gcc

RUN ln -s /usr/bin/gcc-10 /usr/bin/gcc
RUN ln -s /usr/bin/g++-10 /usr/bin/g++

# Install ndctl and daxctl
RUN mkdir /downloads 
RUN git clone https://github.com/pmem/ndctl /downloads/ndctl
WORKDIR /downloads/ndctl
RUN git checkout tags/v71.1

RUN ./autogen.sh && \
    apt install --reinstall -y systemd && \
    ./configure CFLAGS='-g -O2' --prefix=/usr --sysconfdir=/etc --libdir=/usr/lib  --disable-docs && \
    make -j$(nproc) && \
    make install

ENV TOOL_ROOT=/root
RUN mkdir ${TOOL_ROOT}/deps
WORKDIR ${TOOL_ROOT}/deps

# Download PIN
#ARG PIN_VERSION=pin-3.30-98830-g1d7b601b3
ARG PIN_VERSION=pin-3.28-98749-g6643ecee5
ENV PIN_VERSION=${PIN_VERSION}
RUN wget -q http://software.intel.com/sites/landingpage/pintool/downloads/${PIN_VERSION}-gcc-linux.tar.gz && \
    tar -xf ${PIN_VERSION}-gcc-linux.tar.gz && \
    rm -f ${PIN_VERSION}-gcc-linux.tar.gz
ENV PIN_ROOT=${TOOL_ROOT}/deps/${PIN_VERSION}-gcc-linux/

# Download PMDK
WORKDIR ${TOOL_ROOT}/deps
RUN git clone https://github.com/pmem/pmdk.git
WORKDIR ${TOOL_ROOT}/deps/pmdk
# Confirm that version was passed as an argument
ARG PMDK_VERSION
RUN test -n "$PMDK_VERSION"
ARG REFRESH
RUN git pull && git checkout ${PMDK_VERSION}
# Don't build documentation
RUN touch .skip-doc
RUN make EXTRA_CFLAGS="-Wno-error" -j$(nproc) && make install

# Download YAML
WORKDIR ${TOOL_ROOT}/deps
RUN git clone https://github.com/yaml/libyaml
ENV YAML_ROOT=${TOOL_ROOT}/deps/libyaml

WORKDIR ${YAML_ROOT}

RUN git checkout f8f760f7387d2cc56a2fc7b1be313a3bf3f7f58c

COPY yaml.diff yaml.diff
RUN git apply yaml.diff

WORKDIR ${YAML_ROOT}/src

RUN make libyaml.a

RUN mv libyaml.a ..

ENV PM_MOUNT=/mnt/pmem/
ENV LD_LIBRARY_PATH=/usr/local/lib

WORKDIR $TOOL_ROOT

COPY src/ $TOOL_ROOT/src/
COPY scripts/ $TOOL_ROOT/scripts/
COPY examples/ $TOOL_ROOT/examples/
COPY config/ $TOOL_ROOT/config

RUN $TOOL_ROOT/scripts/build.sh

