# Multi-stage build for genea-video-streamer

# Stage 1: Builder
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libavformat-dev \
    libavcodec-dev \
    libavutil-dev \
    libyaml-cpp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY . .

RUN mkdir -p build && \
    cd build && \
    cmake -S /build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . -j$(nproc)

# Stage 2: Runtime
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libavformat-dev \
    libavcodec-dev \
    libavutil-dev \
    libyaml-cpp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binary from builder
COPY --from=builder /build/build/streamer /app/streamer

# Create directories for data
RUN mkdir -p /etc/streamer /data/segments

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD pgrep -f "streamer" > /dev/null || exit 1

# Run streamer
ENTRYPOINT ["/app/streamer"]
CMD ["--config", "/etc/streamer/rtsp-ingest.yaml"]
