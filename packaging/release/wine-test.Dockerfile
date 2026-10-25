FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive WINEDEBUG=-all WINEARCH=win64
RUN apt-get update && apt-get install -y --no-install-recommends \
    wine64 wine python3 openssl ca-certificates xvfb xauth fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /test
