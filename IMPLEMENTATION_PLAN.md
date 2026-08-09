# Implementation Plan: Updated with Genea Assignment Requirements

**Date:** 2026-08-09  
**Assignment:** Live Video Streaming Solution (1-week deadline)  
**Alignment:** Phases 0–7 mapped to PDF evaluation criteria

---

## Executive Summary

**Current Status:**
- ✅ Phases 0–1 complete (Foundation + Capture)
- 🔄 Phase 2–5 underway (Core functionality + testing)
- 📅 Phases 6–7 deferred (Scalability + optional AI)

**Critical Path (5–5.5 days):**  
Phases 0–5 deliver a working end-to-end RTSP → HLS → browser streaming solution with reliability and test coverage.

**After Phase 5:** Polish, documentation, and optional enhancements (Phases 6–7).

---

## PDF Requirements Coverage

### Core Requirements ✅
| Requirement | Phase(s) | Status |
|---|---|---|
| Video Capture (IP Camera/RTSP) | 1 | ✅ Implemented |
| Streaming Protocol (open-source) | 2 | 🔄 HLS (MPEG-TS) in progress |
| Web Player (live + playback) | 3 | ⏳ Planned |
| Network Outage Handling | 4 | ⏳ Planned |
| Code Quality & Comments | 0–5 | 🔄 Ongoing |
| Testing (failure scenarios) | 5 | ⏳ Planned |
| Scalability | 6 | 📅 Deferred |
| Documentation | 0–5 | 🔄 Partial (design.md), polishing in Phase 5 |

### Known Gaps (Not in MVP)
- **AWS Kinesis Video Streams:** Not planned (local HLS only); can add as Phase 2.5 if required
- **Docker/Deployment:** Demo scripts included; formal Dockerfile optional (Phase 3.5)
- **Evaluator-facing Design Doc:** design.md is internal; Phase 5 will polish for submission

---

## Phase Breakdown with Time Estimates

### Phase 0: Foundation (0.5 days)
**✅ COMPLETED**  
- CMake with pkg-config for libavformat/libavcodec/libavutil/yaml-cpp
- Config loader (YAML → AppConfig struct) with validation
- Logger (LOG_INFO/WARN/ERROR macros with timestamps)
- Error handling conventions (LibAV error strings, exceptions for config)

### Phase 1: Capture & Probe (1 day)
**✅ COMPLETED**  
- `IVideoSource` abstract interface
- `RtspSource` implementation (LibAV wrapper, RTSP options, stream selection)
- Metadata probe: codec name, resolution, fps, time base
- Packet read loop with `av_read_frame()` and packet stats logging
- **Bonus:** `PacketClock` for timestamp normalization (enforces monotonic PTS/DTS)

### Phase 2: Remux & Segments (1.5 days)
**🔄 IN PROGRESS**  
**Deliverables:**
- `HlsMuxer` class: writes remuxed packets to MPEG-TS segments (.ts)
- `StreamCopyPlanner`: validates codec compatibility (no re-encode path)
- Playlist generation: `live.m3u8` (rolling window) + `archive.m3u8` (for seek/playback)
- Configurable segment duration (default 2–4s for latency)
- Output directory: `segments/live.m3u8`, `segments/archive.m3u8`, `segments/*.ts`

**Why:** Completes streaming protocol requirement (HLS is open-source, browser-friendly).

### Phase 3: Web Player (1 day)
**⏳ PLANNED**  
**Deliverables:**
- `web/player.html`: HLS.js player with live/archive mode toggle
- Playback controls: play/pause, seek, volume, quality auto-select
- Error display for unsupported codecs
- HTTP serving script (`scripts/serve_hls.sh`): Nginx or Python SimpleHTTPServer
- Demo: `./run_local_demo.sh` spins up camera mock + streamer + player

**Why:** Functional deliverable visible to evaluator (browser playback).

### Phase 4: Reliability (1 day)
**⏳ PLANNED**  
**Deliverables:**
- `ReconnectPolicy`: exponential backoff (1s → 30s max with jitter)
- `PipelineHealth`: packet counters, reconnect count, last frame time
- Stale-source detection: no packets for N seconds triggers reconnect
- Segment continuity: monotonic timestamps + discontinuity markers after reconnect
- Health log dump on shutdown (packets in/out, reconnect stats)

**Why:** Directly addresses "Network Outage Handling" evaluation criterion.

### Phase 5: Testing & Quality (1 day)
**⏳ PLANNED**  
**Deliverables:**
- **Unit tests:**
  - `test_config.cpp`: YAML parsing, validation, edge cases
  - `test_packet_clock.cpp`: ✅ Already done (jitter, backward PTS, AV_NOPTS_VALUE)
  - `test_reconnect_policy.cpp`: exponential backoff, jitter, max cap
- **Integration tests:**
  - `test_rtsp_to_hls.cpp`: sample video file → HLS → playable
  - Failure scenarios: source disconnect, corrupt packets, disk full
- **CI/CD:**
  - GitHub Actions workflow: build + lint + run tests + coverage report
- **Documentation polish:**
  - design.md summary for evaluator
  - README setup/run instructions
  - Inline code comments (already in place from Phase 0–1)

**Why:** Addresses "Testing," "Code Quality," and "Documentation" criteria.

---

### Phase 6: Scalability (Optional, if time permits)
**📅 DEFERRED (2–3 days)**  
**Deliverables:**
- `PipelineController`: manages 1 to N independent pipelines
- Thread-per-pipeline model (one thread per RTSP source)
- Thread-safe packet queues between capture, clock, and mux stages
- Thread-safe logger (mutex-protected fprintf or async queue)
- Per-stream health metrics isolation
- Config updated: single `rtsp:` → array `sources:` with names

**Why:** Addresses "Scalability" evaluation criterion; deferred to preserve MVP simplicity.

---

### Phase 7: AI/Optional (Optional, if time permits)
**📅 DEFERRED (3+ days)**  
**Deliverables:**
- `InferenceWorker`: ONNX Runtime or OpenVINO for object detection
- Event extraction: person/car with timestamp and segment ID
- `EventIndex`: SQLite or in-memory store for events
- Query API: `/api/search?object=person&from=T1&to=T2`
- Profiling report: CPU/memory profile, bottleneck analysis

**Why:** Addresses "Optional Task" (AI Inference, Enhanced Object Search, Performance Optimization).

---

## Critical Path for 1-Week Deadline

```
Day 1 (0.5 days):   Phase 0 ✅
Day 1 (1 day):      Phase 1 ✅
Day 2–2.5 (1.5 days): Phase 2 🔄
Day 3 (1 day):      Phase 3
Day 4 (1 day):      Phase 4
Day 5 (1 day):      Phase 5
Days 6–7 (optional): Phase 6 or polish

Total: 5–5.5 days → leaves 1.5–2 days for:
- Documentation polish
- Unforeseen bugs/issues
- Optional: Phase 6 (scalability) or Phase 7 (AI)
- Optional: Docker, deployment scripts
```

---

## Known Gaps & How to Address Them

| Gap | Impact | Mitigation |
|-----|--------|-----------|
| AWS Kinesis Video Streams | Not in scope (local HLS only) | If evaluator requires it: add Phase 2.5 (AWS put-media API) after Phase 2 |
| Docker / Container Deployment | Optional (demo scripts sufficient) | If needed: add Phase 3.5 (Dockerfile + docker-compose.yml) |
| Formal Evaluator Design Doc | design.md is internal reference | Phase 5: Extract ARCHITECTURE.md summary + update README with deployment guidance |

---

## Success Criteria (Aligned with Evaluation)

By end of Phase 5:
- ✅ **Functionality:** RTSP → HLS → browser works end-to-end
- ✅ **Code Quality:** Clean, modular, well-commented, error handling
- ✅ **Reliability:** Reconnect logic, health monitoring, graceful shutdown
- ✅ **Testing:** Unit + integration tests pass; failure scenarios handled
- ✅ **Documentation:** README, design.md, inline comments
- ⏳ **Scalability:** Architecture documented (Phase 6 deferred)

Optional (if time):
- ✅ **Phase 6:** Multi-pipeline scalability
- ✅ **Phase 7:** AI inference + search

---

## Next Immediate Action

**Start Phase 2:** Implement `HlsMuxer` and `StreamCopyPlanner` for HLS remuxing.  
Target: End of day 2–2.5 to have working RTSP → HLS → segments pipeline.
