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

# clang-23 has no runtime/standard library of its own on Linux; it uses
# whatever libstdc++ headers are on the system. Ubuntu 24.04's default
# libstdc++-dev tracks GCC 13, which predates full C++23 library support
# (std::ranges::to, ...) that the project's own code (not the vendored
# deps below) uses. libstdc++-14-dev pulls in newer headers without
# switching off GNU libstdc++ (staying ABI-compatible with the
# apt-provided Boost .so's this links against).
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates wget gnupg \
    && wget -qO /etc/apt/trusted.gpg.d/apt.llvm.org.asc https://apt.llvm.org/llvm-snapshot.gpg.key \
    && echo 'deb https://apt.llvm.org/noble/ llvm-toolchain-noble-23 main' \
         > /etc/apt/sources.list.d/llvm.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
      clang-23 lld-23 lldb-23 clangd-23 \
      cmake make ninja-build pkg-config git zstd \
      python3 python3-venv python3-pip \
      libboost-dev libboost-context-dev libboost-system-dev libboost-url-dev \
      libssl-dev libfontconfig-dev \
      libstdc++-14-dev \
    && rm -rf /var/lib/apt/lists/*

# ThorVG (vendored via CPM) drives its own meson sub-build, which probes
# PATH for generic compiler names (clang++, c++, g++, ...) rather than
# taking CMAKE_CXX_COMPILER from the parent build. Only versioned
# clang-23/clang++-23 binaries exist otherwise, so meson's compiler
# detection fails. Symlink the generic names to clang-23 rather than
# installing gcc/g++, to keep the whole build on one compiler.
RUN ln -s /usr/bin/clang-23 /usr/bin/clang \
    && ln -s /usr/bin/clang++-23 /usr/bin/clang++

# oneTBB (vendored via CPM, not our code) builds itself with -Wall -Wextra
# -Werror via its own target_compile_options, appended by CMake *after*
# CMAKE_CXX_FLAGS on the command line — so passing -Wno-unused-template
# via CMAKE_CXX_FLAGS has no effect: TBB's own -Wall re-enables the
# -Wunused group (which -Wunused-template belongs to) later in the same
# invocation, and the last occurrence of a warning flag wins. clang-23
# fires that warning by default in TBB's headers where the clang-23
# snapshot this last built clean against didn't. Wrap the real binary so
# the suppression is always the last argument, regardless of what CMake
# or meson put before it — this reaches every caller (CMake's
# CMAKE_CXX_COMPILER=clang++-23, and meson via the clang++ symlink above)
# without touching CMakeLists.txt or vendored code.
RUN mv /usr/bin/clang++-23 /usr/bin/clang++-23.real \
    && printf '#!/bin/sh\nexec /usr/bin/clang++-23.real "$@" -Wno-unused-template\n' \
         > /usr/bin/clang++-23 \
    && chmod +x /usr/bin/clang++-23
