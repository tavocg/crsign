FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 openssl ca-certificates xvfb xauth dbus-x11 fonts-dejavu-core \
    libgl1 libegl1 libxkbcommon0 libwayland-client0 libwayland-cursor0 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /test
