FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
# Host tests (tests/): gcc and lcov are pinned to their Ubuntu 22.04 release
# versions, which stay in the archive.  libc6-dev is not pinned: it must match
# the libc6 already in the ubuntu:22.04 base image, which gets security updates.
RUN apt-get update && apt-get install -y --no-install-recommends \
	make gcc-arm-none-eabi binutils-arm-none-eabi \
	libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
	python3 git ca-certificates srecord \
	gcc=4:11.2.0-1ubuntu1 lcov=1.15-1 libc6-dev \
	&& rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
