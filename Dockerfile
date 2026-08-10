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
    curl \
    ffmpeg \
    bc \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binary from builder
COPY --from=builder /build/build/streamer /app/streamer

# Copy web player assets
COPY --from=builder /build/web /app/web

# Copy profiling suite for performance testing
COPY --from=builder /build/profiling /app/profiling

# Make profiling scripts executable
RUN chmod +x /app/profiling/*.sh

# Create directories for data and results
RUN mkdir -p /etc/streamer /data/hls_output /app/profiling/results

# Expose HTTP server port
EXPOSE 8080

# Health check using HTTP API (Phase 6)
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/api/health || exit 1

# Run streamer with config from mounted volume
ENTRYPOINT ["/app/streamer"]
CMD ["/etc/streamer/config.yaml"]
