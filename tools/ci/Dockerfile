# Build host for codegen and the linux-amd64, windows and uwp targets.
# tools/ci/container.sh builds it locally and ci-image.yaml publishes it,
# both tagged by tools/ci/image_tag.sh.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
COPY apt-packages.txt versions.env install_llvm.sh /opt/ci/

RUN apt-get update \
 && xargs -a /opt/ci/apt-packages.txt apt-get install -y --no-install-recommends \
 && rm -rf /var/lib/apt/lists/*

RUN /opt/ci/install_llvm.sh /opt/llvm
ENV PATH=/opt/llvm/bin:$PATH

RUN . /opt/ci/versions.env \
 && PIP_BREAK_SYSTEM_PACKAGES=1 python3 -m pip install --no-cache-dir "cmake==$CMAKE_VERSION" \
 && cmake --version

# The checkout is owned by the host user, not the container user.
RUN git config --system --add safe.directory '*'
