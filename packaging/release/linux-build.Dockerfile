FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
ARG BUILD_UID=1001
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config python3 git ca-certificates curl \
    autoconf automake libtool gettext libglib2.0-dev libgtk-4-dev libadwaita-1-dev \
    librsvg2-dev libgdk-pixbuf-2.0-dev libpoppler-glib-dev libcurl4-openssl-dev \
    libssl-dev libp11-kit-dev libmagic-dev libqpdf-dev zlib1g-dev libjansson-dev \
    libxml2-dev libpcsclite-dev openssl patchelf squashfs-tools file \
    xvfb xauth xdotool dbus-x11 desktop-file-utils librsvg2-bin locales \
    && (getent passwd "$BUILD_UID" || useradd --uid "$BUILD_UID" --create-home builder) \
    && localedef -i en_US -f UTF-8 en_US.UTF-8 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
