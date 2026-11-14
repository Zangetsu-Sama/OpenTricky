# OpenTricky macOS build image: SSX3-recomp's cross toolchain (ssx3-macx:
# clang 18 + ld64.lld, glslc) plus the translator's Python packages.
FROM ssx3-macx:latest
RUN apt-get update && apt-get install -y --no-install-recommends binutils-multiarch && rm -rf /var/lib/apt/lists/*
RUN python3 -m pip install --break-system-packages --no-cache-dir capstone numpy pillow
WORKDIR /work
