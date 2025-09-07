# Copyright (c) Microsoft Corporation.
# Licensed under the MIT license.
FROM mcr.microsoft.com/mirror/docker/library/ubuntu:24.04

LABEL org.opencontainers.image.source="https://github.com/microsoft/RANBooster"
LABEL org.opencontainers.image.authors="Microsoft Corporation"
LABEL org.opencontainers.image.licenses="MIT"
LABEL org.opencontainers.image.description="jrt-controller for Ubuntu 24.04"

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-c"]

RUN echo "*** Installing packages"

RUN apt update --fix-missing
RUN apt install -y wget cmake clang gcc g++ git libnuma-dev python3-pyelftools \
                    meson ninja-build pkg-config libbpf-dev libelf-dev iproute2 \
                    linux-tools-common linux-tools-generic libxdp-dev 

RUN mkdir -p /dpdk-ranbooster && wget -O - https://fast.dpdk.org/rel/dpdk-24.11.3.tar.xz | \
    tar -xJ -C /dpdk-ranbooster --strip-components=1

WORKDIR /dpdk-ranbooster
RUN meson setup build
RUN ninja -C build
RUN ninja -C build install && ldconfig

ENV RTE_SDK=/dpdk-ranbooster

COPY . /ranbooster
WORKDIR /ranbooster

RUN echo "source /ranbooster/setup_ranbooster_env.sh" >> /etc/bash.bashrc

RUN source /ranbooster/setup_ranbooster_env.sh && ./init_and_patch_submodules.sh

RUN mkdir -p /ranbooster/build
WORKDIR /ranbooster/build
RUN source /ranbooster/setup_ranbooster_env.sh && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j $(nproc)
