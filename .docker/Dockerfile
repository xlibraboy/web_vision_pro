FROM ubuntu:20.04

# Use Singapore mirror (faster for most SEA locations)
RUN sed -i 's|http://archive.ubuntu.com/ubuntu/|http://sg.archive.ubuntu.com/ubuntu/|g' /etc/apt/sources.list && \
    sed -i 's|http://security.ubuntu.com/ubuntu/|http://sg.archive.ubuntu.com/ubuntu/|g' /etc/apt/sources.list
ENV DEBIAN_FRONTEND=noninteractive

# Install software-properties-common for add-apt-repository
RUN apt-get update && apt-get install -y --no-install-recommends \
    software-properties-common \
    gnupg \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Qt 5.15.2 PPA (beineri) — required for production-ready QtOpcUa 5.15
RUN add-apt-repository -y ppa:beineri/opt-qt-5.15.2-focal && \
    apt-get update && \
    rm -rf /var/lib/apt/lists/*

# Install system dependencies using Qt 5.15 from beineri PPA
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    wget \
    unzip \
    fonts-noto-core \
    libopencv-dev \
    qt515base \
    qt515svg \
    qt515tools \
    qt515declarative \
    qt515networkauth-no-lgpl \
    libssl-dev \
    libgl1-mesa-dev \
    pkg-config \
    python3 \
    perl \
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

# Activate Qt 5.15 from PPA
ENV PATH=/opt/qt515/bin:$PATH
ENV LD_LIBRARY_PATH=/opt/qt515/lib:$LD_LIBRARY_PATH
ENV CMAKE_PREFIX_PATH=/opt/qt515

# Qt OpcUa from source + open62541 backend plugin
RUN git clone --depth 1 --branch v1.5.4 https://github.com/open62541/open62541.git /tmp/open62541-src && \
    cmake -S /tmp/open62541-src -B /tmp/open62541-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DUA_ENABLE_AMALGAMATION=OFF \
        -DUA_ENABLE_ENCRYPTION=OFF \
        -DUA_BUILD_EXAMPLES=OFF \
        -DUA_BUILD_TOOLS=OFF \
        -DUA_BUILD_UNIT_TESTS=OFF && \
    cmake --build /tmp/open62541-build --parallel && \
    cmake --install /tmp/open62541-build && \
    git clone --depth 1 --branch 5.15.2 https://github.com/qt/qtopcua.git /tmp/qtopcua-src && \
    mkdir /tmp/qtopcua-build && cd /tmp/qtopcua-build && \
    qmake /tmp/qtopcua-src/qtopcua.pro && \
    make -j"$(nproc)" && \
    make install && \
    rm -rf /tmp/open62541-src /tmp/open62541-build /tmp/qtopcua-src


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
