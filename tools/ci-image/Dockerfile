# Reproducible build host for the Linux x64, Windows (cross) and UWP
# (cross) legs: Ubuntu 24.04 with the pinned toolchain. CI uses this image
# once published (see .github/workflows/ci-image.yaml); locally run it
# through Podman so any host builds exactly like CI (x86_64 hosts; on ARM64
# pass --platform=linux/amd64 explicitly, the LLVM asset below is X64-only):
# podman run --rm -v $PWD:/work:z -v $SDK:/sdk:z -w /work \
#   ghcr.io/1vs100/1vs100-pc-ci:v1 \
#   cmake -S native -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
#     -DREXSDK_DIR=/sdk -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV CLANG_VERSION=22
ENV LLVM_RELEASE=22.1.8
ENV CMAKE_VERSION=3.28.3
ENV XWIN_VERSION=0.10.0

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl git sudo gcc g++ \
    python3 python3-pip pkg-config \
    ninja-build nasm unzip xz-utils file \
    msitools gcab \
    libx11-dev libx11-xcb-dev libxext-dev libxcursor-dev libxrandr-dev \
    libxfixes-dev libxi-dev libxinerama-dev libxss-dev libxkbcommon-dev \
    libxtst-dev libxdamage-dev libxrender-dev libxxf86vm-dev \
    libxcomposite-dev libibus-1.0-dev libwayland-dev wayland-protocols \
    libegl1-mesa-dev libgl1-mesa-dev libasound2-dev libpulse-dev \
    libpipewire-0.3-dev libdbus-1-dev libudev-dev \
 && rm -rf /var/lib/apt/lists/*

# LLVM 22.1.8, same selective extract as .github/actions/setup-clang.
RUN set -eux; \
    p="LLVM-$LLVM_RELEASE-Linux-X64"; \
    mkdir -p /opt/llvm-tmp; \
    curl -fsSL --retry 5 --retry-all-errors --retry-delay 10 \
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_RELEASE/$p.tar.xz" \
      | tar -xJ -C /opt/llvm-tmp --strip-components=1 --wildcards \
        "$p/bin/clang" "$p/bin/clang++" "$p/bin/clang-cl" "$p/bin/clang-$CLANG_VERSION" \
        "$p/bin/lld" "$p/bin/lld-link" "$p/bin/llvm-ar" "$p/bin/llvm-lib" \
        "$p/bin/llvm-rc" "$p/bin/llvm-mt" "$p/lib/clang/*"; \
    mkdir -p /opt/llvm-22; \
    mv /opt/llvm-tmp/bin /opt/llvm-tmp/lib /opt/llvm-22/; \
    rm -rf /opt/llvm-tmp; \
    ln -s /opt/llvm-22/bin/clang /usr/local/bin/clang; \
    ln -s /opt/llvm-22/bin/clang++ /usr/local/bin/clang++; \
    ln -s /opt/llvm-22/bin/clang-cl /usr/local/bin/clang-cl; \
    ln -s /opt/llvm-22/bin/lld-link /usr/local/bin/lld-link; \
    ln -s /opt/llvm-22/bin/llvm-rc /usr/local/bin/llvm-rc; \
    ln -s /opt/llvm-22/bin/llvm-mt /usr/local/bin/llvm-mt; \
    ln -s /opt/llvm-22/bin/llvm-lib /usr/local/bin/llvm-lib; \
    ln -s /opt/llvm-22/bin/lld /usr/local/bin/lld; \
    ln -s /opt/llvm-22/bin/llvm-ar /usr/local/bin/llvm-ar; \
    ln -s /opt/llvm-22/bin/clang-22 /usr/local/bin/clang-22; \
    clang --version

# Pinned CMake from PyPI, same version as the CI Pin CMake step.
RUN PIP_BREAK_SYSTEM_PACKAGES=1 python3 -m pip install --no-cache-dir "cmake==$CMAKE_VERSION" \
 && cmake --version

# xwin 0.10.0, same download plus hash check as CI.
RUN set -eux; \
    name="xwin-$XWIN_VERSION-x86_64-unknown-linux-musl"; \
    url="https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VERSION/$name.tar.gz"; \
    curl -fsSL -o "/tmp/$name.tar.gz" "$url"; \
    expected="$(curl -fsSL "$url.sha256" | awk '{ print $1 }')"; \
    echo "$expected  /tmp/$name.tar.gz" | sha256sum -c -; \
    tar -xzf "/tmp/$name.tar.gz" -C /tmp; \
    install -m 755 "/tmp/$name/xwin" /usr/local/bin/xwin; \
    rm -rf "/tmp/$name" "/tmp/$name.tar.gz"; \
    xwin --version
