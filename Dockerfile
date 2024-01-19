ARG BASE_OS="ubuntu:20.04"
FROM ${BASE_OS}

SHELL ["/bin/bash", "-c"] 

ENV DEBIAN_FRONTEND noninteractive

RUN apt update 
RUN apt install -y \
    apt-utils build-essential man wget git htop dstat procps gdb telnet time \
    gcc g++ libevent-dev libfabric-dev libseccomp-dev autoconf automake \
    pkg-config libglib2.0-dev libncurses5-dev libboost-all-dev \
    asciidoc asciidoctor bash-completion xmlto libtool libglib2.0-0 libfabric1 \
    graphviz libncurses5 libkmod2 libkmod-dev libudev-dev uuid-dev \
    libjson-c-dev libkeyutils-dev pandoc cmake libelf-dev nano \
    libtbb-dev libjemalloc-dev

# Install ndctl and daxctl
RUN mkdir /downloads 
RUN git clone https://github.com/pmem/ndctl /downloads/ndctl
WORKDIR /downloads/ndctl
RUN git checkout tags/v71.1
RUN ./autogen.sh && \
    apt install --reinstall -y systemd && \
    ./configure CFLAGS='-g -O2' --prefix=/usr --sysconfdir=/etc --libdir=/usr/lib  && \
    make -j$(nproc) && \
    make install

ENV TOOL_ROOT=/root
RUN mkdir $TOOL_ROOT/dependencies
WORKDIR $TOOL_ROOT/dependencies

# Download PIN
ARG PIN_VERSION=pin-3.28-98749-g6643ecee5
ENV PIN_VERSION=${PIN_VERSION}
RUN wget -q http://software.intel.com/sites/landingpage/pintool/downloads/${PIN_VERSION}-gcc-linux.tar.gz && \
    tar -xf ${PIN_VERSION}-gcc-linux.tar.gz && \
    rm -f ${PIN_VERSION}-gcc-linux.tar.gz
ENV PIN_ROOT=$TOOL_ROOT/dependencies/${PIN_VERSION}-gcc-linux/

# Download and install Zydis
ARG ZYDIS_VERSION=tags/v4.0.0
RUN git clone --recursive https://github.com/zyantific/zydis.git
WORKDIR $TOOL_ROOT/dependencies/zydis
RUN git checkout $ZYDIS_VERSION
WORKDIR $TOOL_ROOT/dependencies/zydis/build
RUN cmake .. && make && make install
WORKDIR $TOOL_ROOT/dependencies/zydis/dependencies/zycore/build
RUN cmake .. && make && make install

# Download PMDK
WORKDIR $TOOL_ROOT/dependencies
RUN git clone https://github.com/pmem/pmdk.git
WORKDIR $TOOL_ROOT/dependencies/pmdk
# Confirm that version was passed as an argument
ARG PMDK_VERSION
RUN test -n "$PMDK_VERSION"
ARG REFRESH
RUN git pull && git checkout ${PMDK_VERSION}
# Don't build documentation
RUN touch .skip-doc
RUN make EXTRA_CFLAGS="-Wno-error" -j$(nproc) && make install

# Download YAML
WORKDIR $TOOL_ROOT/dependencies
RUN git clone https://github.com/yaml/libyaml

COPY patches $TOOL_ROOT/patches

ENV PM_MOUNT /mnt/pmem/
ENV LD_LIBRARY_PATH /usr/local/lib


WORKDIR $TOOL_ROOT
