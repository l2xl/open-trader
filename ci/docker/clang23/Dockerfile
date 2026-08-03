# CI build image for Open Trader: Ubuntu noble + clang-23 (C++23 reflection
# target, see CONTRIBUTING.md) plus the project's build dependencies.
#
# clang-23 has no stable release yet, so apt.llvm.org's package feed for it
# is a moving, best-effort nightly build that comes and goes without notice
# (see the "Install LLVM apt repository" failures this image replaces).
# Baking a known-good install into an image we own and pin by digest avoids
# depending on that feed being in a working state at every CI run.
#
# apt.llvm.org/llvm.sh is *not* used to add the repo: its built-in version
# table still maps 23 to the unsuffixed "llvm-toolchain-noble" repo, which
# tracks whatever is current trunk. LLVM has since tagged llvmorg-23.1.0-rc2
# (branched for release), so that unsuffixed repo now serves clang-24, not
# clang-23 — llvm.sh's table lags apt.llvm.org's actual layout. Pointing
# straight at the version-suffixed "llvm-toolchain-noble-23" suite (the repo
# apt.llvm.org publishes for a version once it has branched off trunk) is
# what actually resolves clang-23 packages.
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates wget gnupg \
    && wget -qO /etc/apt/trusted.gpg.d/apt.llvm.org.asc https://apt.llvm.org/llvm-snapshot.gpg.key \
    && echo 'deb https://apt.llvm.org/noble/ llvm-toolchain-noble-23 main' \
         > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
      clang-23 lld-23 lldb-23 clangd-23 \
      cmake make ninja-build pkg-config git zstd \
      libboost-dev libboost-context-dev libboost-system-dev libboost-url-dev \
      libssl-dev libfontconfig-dev \
    && rm -rf /var/lib/apt/lists/*
