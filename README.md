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

Detailed technical design, architecture, module breakdown, and implementation plan are in [docs/design.md](docs/design.md).

### Quick Roadmap (1-Week Deadline)

| Phase | Name | Status | Eval Criteria |
|-------|------|--------|---------------|
| 0 | Foundation | ✅ Done | Code Quality |
| 1 | Capture & Probe | ✅ Done | Video Capture |
| 2 | Remux & Segments | 🔄 Next | Streaming Protocol, Functionality |
| 3 | Web Player | ⏳ Planned | Web Player, Functionality |
| 4 | Reliability | ⏳ Planned | Network Outage Handling, Reliability |
| 5 | Testing & CI | ⏳ Planned | Testing, Code Quality |
| 6 | Scalability | 📅 Optional | Scalability |
| 7 | AI/Optional | 📅 Optional | (Optional Task) |

See [docs/design.md § 10](docs/design.md#10-implementation-plan-aligned-with-evaluation-criteria) for full implementation plan with time estimates and evaluation criteria alignment.

## Current Status

**Phases 0–1 Complete:**
- ✅ CMake build system with LibAV + yaml-cpp
- ✅ YAML configuration loader with validation
- ✅ Logging framework (INFO/WARN/ERROR macros)
- ✅ RTSP source ingest (LibAV wrapper)
- ✅ Stream metadata probe (codec, resolution, fps, time base)
- ✅ Packet read loop with compressed packet handling
- ✅ Packet clock for timestamp normalization (monotonic enforcement, jitter handling)

**Next (Phases 2–5):**
1. HLS remux + segment generation (Phase 2)
2. Web player + HTTP serving (Phase 3)
3. Reconnect logic + reliability (Phase 4)
4. Unit/integration tests + CI (Phase 5)

**Timeline:** 5–5.5 days for core delivery (Phases 0–5); optional Phases 6–7 if time permits.

See [Implementation Plan](#implementation-plan-aligned-with-evaluation-criteria) in [docs/design.md](docs/design.md) for detailed roadmap aligned with evaluation criteria.
