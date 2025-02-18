ARG BASE_OS="ubuntu:20.04"
FROM ${BASE_OS}

SHELL ["/bin/bash", "-c"] 

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update && apt install -y \
    apt-utils build-essential man wget git htop dstat procps gdb telnet time \
    gcc-10 g++-10 nano cmake autoconf automake libtool libkmod-dev libudev-dev \
    uuid-dev libjson-c-dev bash-completion libkeyutils-dev pandoc pkg-config

# Install ndctl and daxctl
RUN mkdir /tmp/downloads 
RUN git clone https://github.com/pmem/ndctl /tmp/downloads/ndctl
WORKDIR /tmp/downloads/ndctl
RUN git checkout tags/v71.1

RUN ./autogen.sh && \
    apt install --reinstall -y systemd && \
    ./configure CFLAGS='-g -O2' --prefix=/usr --sysconfdir=/etc --libdir=/usr/lib  --disable-docs && \
    make -j$(nproc) && \
    make install

# Download PMDK
WORKDIR /tmp/downloads
RUN git clone https://github.com/pmem/pmdk.git
WORKDIR /tmp/downloads/pmdk
# Get PMDK version
ARG PMDK_VERSION=1.12.1
RUN git pull && git checkout tags/${PMDK_VERSION}
# Don't build documentation
RUN touch .skip-doc
RUN make EXTRA_CFLAGS="-Wno-error" -j$(nproc) && make install

ENV LD_LIBRARY_PATH=/usr/local/lib

ENV TOOL_ROOT=/root
ENV PIN_ROOT=${TOOL_ROOT}/deps/pin/
ENV PM_MOUNT=/mnt/pmem

WORKDIR ${TOOL_ROOT}

COPY src/ ${TOOL_ROOT}/src/
COPY scripts/ ${TOOL_ROOT}/scripts/
COPY examples/ ${TOOL_ROOT}/examples/
COPY config/ ${TOOL_ROOT}/config
COPY yaml.diff ${TOOL_ROOT}/yaml.diff

RUN echo "" > ${TOOL_ROOT}/scripts/env.sh

RUN ${TOOL_ROOT}/scripts/build.sh

