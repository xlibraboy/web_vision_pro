FROM ubuntu:20.04

# Use official Indonesian mirror for best compatibility and load-balancing
RUN sed -i 's|http://archive.ubuntu.com/ubuntu/|http://id.archive.ubuntu.com/ubuntu/|g' /etc/apt/sources.list && \
    sed -i 's|http://security.ubuntu.com/ubuntu/|http://id.archive.ubuntu.com/ubuntu/|g' /etc/apt/sources.list
ENV DEBIAN_FRONTEND=noninteractive

# Install system dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    wget \
    unzip \
    fonts-noto-core \
    libopencv-dev \
    qt5-default \
    libqt5svg5-dev \
    libxcb-xinerama0 \
    libxcb-keysyms1 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-shm0 \
    libxcb-util1 \
    libxcb-render-util0 \
    libxcb-randr0 \
    libxcb-shape0 \
    libxcb-sync1 \
    libxcb-xfixes0 \
    libxcb-xinput0 \
    libxkbcommon-x11-0 \
    libx11-xcb-dev \
    && rm -rf /var/lib/apt/lists/*

# Pylon Installation
# We copy the deb file from the .docker directory (which is the context root for the build)
COPY .docker/pylon_6.2.0.21487-deb0_amd64.deb /tmp/pylon.deb
RUN dpkg -i /tmp/pylon.deb || apt-get install -f -y && rm /tmp/pylon.deb

# Pylon Supplementary Package (MPEG-4)
COPY BaslerDocuments/pylon-supplementary-package-for-mpeg-4_1.0.1.118-deb0_amd64.deb /tmp/pylon-mpeg4.deb
RUN dpkg -i /tmp/pylon-mpeg4.deb || apt-get install -f -y && rm /tmp/pylon-mpeg4.deb

WORKDIR /app

# Copy project files
COPY . /app

# Build directory
RUN mkdir -p build

CMD ["/bin/bash"]
