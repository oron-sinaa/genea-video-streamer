# Profiling Suite

Simple performance profiling system for genea-video-streamer.

## Quick Start

```bash
# Make scripts executable
chmod +x profiling/*.sh

# Rebuild project (required)
cd build && cmake .. && make -j4

# Run full profiling suite (15-20 minutes)
cd profiling
./run_all_tests.sh

# View results
cat results/REPORT.txt
```

## What Gets Tested

| Scenario | Streams | Purpose |
|----------|---------|---------|
| **baseline** | 1 | Single stream baseline metrics |
| **multi_stream** | 5 | Linear scaling validation |
| **scale_test** | 10 | Stress and bottleneck identification |

## Metrics Collected

Per each 5-second interval:

- **API Metrics** (from `/api/health`):
  - `active_streams` - Running streams
  - `total_packets` - Muxed packets
  - `total_reconnects` - Network recovery attempts
  - `error_streams` - Failed streams

- **System Metrics**:
  - `cpu_percent` - Process CPU usage
  - `memory_mb` - RSS memory in MB
  - `disk_read_mb` - Disk read rate (MB/s)
  - `disk_write_mb` - Disk write rate (MB/s)

## Output Files

```
profiling/results/
├── baseline_metrics.csv          # Raw data: baseline (1 stream)
├── baseline.log                  # Streamer logs: baseline
├── multi_stream_metrics.csv      # Raw data: multi-stream (5 streams)
├── multi_stream.log              # Streamer logs: multi-stream
├── scale_test_metrics.csv        # Raw data: scale test (10 streams)
├── scale_test.log                # Streamer logs: scale test
└── REPORT.txt                    # Summary statistics and averages
```

## Running Individual Tests

```bash
# Run just baseline (5 minutes)
./run_test.sh baseline 300

# Run multi-stream for 10 minutes
./run_test.sh multi_stream 600

# Generate report from existing results
./generate_report.sh
```

## Interpreting Results

**REPORT.txt** shows:

```
Scenario: baseline
  CPU (avg): 12.5%              ← Average CPU usage
  Memory (avg): 185 MB          ← Average memory
  Disk Read (avg): 0.5 MB/s     ← Read throughput
  Disk Write (avg): 2.3 MB/s    ← Write throughput
  Packets written: 1500         ← Total packets muxed
  Reconnects: 0                 ← Network reconnections
  Error samples: 0              ← Times errors occurred
```

**Linear Scaling:**
- Multi-stream CPU should be ~2.5x baseline
- Scale test CPU should be ~5-6x baseline
- Memory should scale linearly per stream (~150-200 MB each)

**Bottleneck Signs:**
- CPU stays at 100% → CPU bound, reduce segment duration or streams
- Disk write > 10 MB/s with 10 streams → I/O bound, use faster storage
- Memory > 2 GB with 10 streams → Memory bound, reduce buffer sizes

## Customizing Tests

Edit scenario YAML files to:
- Change RTSP URLs (`rtsp_url` field)
- Adjust segment duration (`segment_duration_s`)
- Modify playback profiles (buffer sizes)
- Change resource limits (`max_streams`, `max_memory_per_stream`)

Example:
```yaml
# scenarios/baseline.yaml
streams:
  - name: "camera-1"
    rtsp_url: "rtsp://your-camera:554/stream"  # ← Update this
    ...

hls:
  segment_duration_s: 3  # ← Increase to reduce CPU
  archive_retention_hours: 1
```

## Prerequisites

- Project compiled: `cd build && cmake .. && make`
- Linux system with `/proc/diskstats` available
- Binary at: `build/streamer`
- jq (optional, for manual JSON parsing)

## Troubleshooting

**"streamer binary not found"**
```bash
cd /path/to/project
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j4
```

**"streamer crashed during startup"**
- Check scenario YAML syntax
- Verify RTSP URLs are correct or remove them for dry-run
- Check port 8080 is available: `lsof -i :8080`

**API metrics show zeros**
- RTSP sources may not be available (expected in dry-run)
- Check `/api/streams` for stream status in logs
- Profiler still measures system metrics even if streams fail

**Disk I/O stats showing 0.0**
- May occur on first few samples
- Results stabilize after 30 seconds
- Check actual writes in logs: `grep "segment" results/*.log`

## Example Full Run

```bash
$ ./run_all_tests.sh 300
========================================
GENEA VIDEO STREAMER PROFILING SUITE
========================================

Duration per test: 300 seconds

Running: baseline
Starting test: baseline (duration: 300s)
Config: ./scenarios/baseline.yaml
Waiting for server to start...
Server ready
Collecting metrics for 300 seconds...
Test complete: baseline
Results: ./results/baseline_metrics.csv

Running: multi_stream
...

Running: scale_test
...

Generating report...

========================================
PROFILING COMPLETE
========================================

Results directory: ./results

$ cat results/REPORT.txt
============================================
PERFORMANCE REPORT
============================================

Generated: Fri Aug 10 10:30:45 UTC 2026

Scenario: baseline
  CPU (avg): 12.5%
  Memory (avg): 185 MB
  Disk Read (avg): 0.5 MB/s
  Disk Write (avg): 2.3 MB/s
  Packets written: 1500
  Reconnects: 0
  Error samples: 0

Scenario: multi_stream
  CPU (avg): 31.2%
  Memory (avg): 920 MB
  Disk Read (avg): 1.2 MB/s
  Disk Write (avg): 11.5 MB/s
  Packets written: 7500
  Reconnects: 0
  Error samples: 0

...
```

## Notes

- Tests run sequentially; total time = (duration + overhead) × number of scenarios
- Results directory is cleaned before each full run
- Logs are preserved for debugging
- CSV files can be imported to Excel/Sheets for graphing
- All output is append-safe; can rerun tests without losing data
