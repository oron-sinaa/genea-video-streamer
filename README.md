# genea-video-streamer
Implementation of streaming and inference solution for the Genea interview assessment.

## Goal

Implement an open-source live video streaming solution using C++ and LibAV that:

1. Captures from IP camera via RTSP only.
2. Streams to a destination endpoint.
3. Supports browser-based live view and playback.
4. Handles network outages and common failure conditions.

## Technical Direction

- Language: C++
- Media stack: LibAV (`libavformat`, `libavcodec`, `libavutil`, `libswscale`)
- Delivery protocol: HLS (open standard) through packet remuxing
- Web playback: HLS.js-based player
- Video policy: codec copy only (`-c copy` equivalent). No transcoding/encoding.

## Scope Limitation

1. Ingest source is RTSP only.
2. Output is produced by remuxing compressed packets, not decoding/re-encoding frames.
3. Browser compatibility depends on source camera codec and GOP settings.
4. Recommended camera profile for MVP: H.264 stream with regular keyframes (for stable HLS playback).

## Working Style

1. Keep implementation simple and human-readable.
2. Do not perform premature optimization.
3. Build strictly step by step, validating each stage before moving forward.

## Implementation Chronology (Priority Order)

1. Foundation: CMake project, config model, logging, error handling conventions.
2. Capture: RTSP input and stream probing.
3. Pipeline core: demux packet ingest -> timestamp normalization -> packet remux.
4. Streaming output: HLS segment + playlist generation for live and playback (codec copy only).
5. Web player: browser UI for live playback and archive playback.
6. Reliability: reconnect/backoff, health metrics, outage behavior.
7. Quality: unit/integration tests and CI checks.
8. Scalability: multi-stream pipeline manager and deployment guidance.
9. Optional: AI inference, object search, performance profiling.

## Design Document

Detailed design and module breakdown is in [docs/design.md](docs/design.md).

## Current Status

Phase 0 scaffold is in place: CMake target, application entry point, module directories,
RTSP configuration template, web player placeholder, and scripts. RTSP ingest and HLS
remux behavior have not been implemented yet.
