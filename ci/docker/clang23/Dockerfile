# CI build image for Open Trader: Ubuntu noble + clang-23 (C++23 reflection
# target, see CONTRIBUTING.md) plus the project's build dependencies.
#
# clang-23 has no stable release yet, so apt.llvm.org's package feed for it
# is a moving, best-effort nightly build that comes and goes without notice
# (see the "Install LLVM apt repository" failures this image replaces).
# Baking a known-good install into an image we own and pin by digest avoids
# depending on that feed being in a working state at every CI run.
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates wget gnupg software-properties-common \
    && wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh \
    && chmod +x /tmp/llvm.sh \
    && /tmp/llvm.sh 23 \
    && apt-get install -y --no-install-recommends \
      cmake ninja-build pkg-config git zstd \
      libboost-dev libboost-context-dev libboost-system-dev libboost-url-dev \
      libssl-dev libfontconfig-dev \
    && rm -rf /tmp/llvm.sh /var/lib/apt/lists/*
