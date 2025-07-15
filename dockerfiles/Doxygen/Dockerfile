FROM ubuntu:latest

# Install dependencies for building Doxygen and CMake-based projects
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        git \
        build-essential \
        cmake \
        flex \
        bison \
        graphviz \
        python3 \
        pkg-config \
        qtbase5-dev \
        wget \
        ninja-build \
        curl

# Clone and build Doxygen from the official repository
RUN git clone https://github.com/doxygen/doxygen.git /opt/doxygen
WORKDIR /opt/doxygen
RUN mkdir build && cd build && \
    cmake -G "Unix Makefiles" .. && \
    make -j$(nproc) && \
    make install

# Add Doxygen to PATH
ENV PATH="/usr/local/bin:${PATH}"

# Default workdir for your repo
WORKDIR /workspace
