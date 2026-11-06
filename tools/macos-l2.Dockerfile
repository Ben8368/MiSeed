FROM ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install --yes --no-install-recommends \
    ca-certificates \
    git \
    python3 \
    gcc=4:13.2.0-7ubuntu1 \
    tar \
    gzip \
    gcc-13-aarch64-linux-gnu=13.3.0-6ubuntu2~24.04.1cross1 \
    gcc-aarch64-linux-gnu=4:13.2.0-7ubuntu1 \
    binutils-aarch64-linux-gnu=2.42-4ubuntu2.10 \
    make=4.3-4.1build2 \
    bc=1.07.1-3ubuntu4 \
    bison=2:3.8.2+dfsg-1ubuntu0.24.04.1 \
    flex=2.6.4-8.2build1 \
    libssl-dev=3.0.13-0ubuntu3.16 \
    libelf-dev=0.190-1.1ubuntu0.1 \
    libc6-dev-arm64-cross=2.39-0ubuntu8cross1 \
    device-tree-compiler=1.7.0-2build1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
