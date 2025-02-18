# HawkSet

This document describes the content of this repo, the source code for HawkSet, as described in "HawkSet: Automatic, Application-Agnostic, and Efficient Concurrent PM Bug Detection", accepted to Eurosys 2025.

# Overview

```
HawkSet-exp
│   Dockerfile           - HawkSet's container
│   yaml.diff            - libyaml's patch file for compatibility with Intel PIN
│
└─── src                 - HawkSet's source code
└─── scripts             - HawkSet's helper scripts and entrypoint
└─── config              - Configuration files for the synchronization primitives
└─── examples            - Simple PM applications for testing
```

## HawkSet's Source code (source/)

HawkSet is implemented as an Intel PIN tool (https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-dynamic-binary-instrumentation-tool.html).

It instruments memory based instructions, such as stores, loads, CAS, and memory fence instructions in order to simulate the state of the cache for PM mapped memory. This is implemented in `source/hawkset.cpp`.
The PM-Aware lockset mechanisms are implemented in `source/lockset.*pp` and invoked in the main tool, while vector clocks are implemented in `source/vector_clock.hpp`.

`source/utils.hpp` implements several utilities, most important being the backtrace optimizations outlined in the paper.

`source/cache.hpp` implements all required classes and methods to ensure locksets and vector clocks can be uniquely identified by an address, enabling very fast and short-circuitable comparisons.

# Building

HawkSet can be built and ran from source, or containerized via docker for ease of use. Depending on which option you choose, the requirements vary.

## Option 1: Build from source

### Requirements

HawkSet was implemented and tested on ubuntu 20.04, and requires the following packages:

- apt-utils
- build-essential
- man
- wget
- git
- htop
- dstat
- procps
- gdb
- telnet
- time
- gcc-10
- g++-10
- nano
- cmake
- autoconf
- automake
- libtool
- libkmod-dev
- libudev-dev 
- uuid-dev 
- libjson-c-dev 
- bash-completion 
- libkeyutils-dev 
- pandoc 
- pkg-config

These can be installed via the following command:

```bash
apt update && apt install -y \
    apt-utils build-essential man wget git htop dstat procps gdb telnet time \
    gcc-10 g++-10 nano cmake autoconf automake libtool libkmod-dev libudev-dev \
    uuid-dev libjson-c-dev bash-completion libkeyutils-dev pandoc pkg-config
```

### Building

Run the following command:

```
./scripts/build.sh
```

This script downloads both Intel PIN and libyaml, which are required for compiling HawkSet, and finally compiles it. Subsequent calls to this script will not redownload these requirements.

## Option 2: Docker


### Requirements

You can find some information on how to install docker in the official documentation (https://docs.docker.com/engine/install/ubuntu/).

### Building

To build the HawkSet container, run the following command:

```
docker build . -t hawkset:$HAWKSET_VERSION --build-arg PMDK_VERSION=1.12.1
```

# Running

`scripts/HawkSet` is the entrypoint for running HawkSet, however, it depends on a few environment variables (listed in `scripts/env.sh`).
To aid in the evocation of HawkSet, `scripts/run.sh` can be used, which automatically sets the correct environment variables and evokes the entrypoint script while passing arguments along.

With that in mind, running HawkSet is as simple as running the following command:

```
./scripts/run.sh <tool args> -- <application> <application args>
```

HawkSet can be configured with several arguments, these are represented by `<tool args>`, and are as follows:

```
 -cfg                            - Configuration file (repeatable)
 -irh [default 1]                - Initialization removal heuristic (0 or 1)
 -out [default stdout]           - Bug reports output
 -pm_mount [default /mnt/pmem0/] - PM mount in filesystem
```
## Running with Docker

The exact manner in which you run HawkSet's docker container depends on the use case. Most likely, you will want to create another container that inherits from HawkSet where you build the application under test.
You can find some examples in https://github.com/Jonyleo/HawkSet-exp.

However, once you have the docker container properly setup and running, you can follow the same instructions as the version built from source, detailed above.
