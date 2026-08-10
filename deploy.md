# Deployment Guide

## Option 1: Streaming Only (HLS)

### 1. Build the Docker image

```bash
docker build -t genea-video-streamer:latest .
```

### 2. Configure RTSP sources

Edit `config/rtsp-multi-stream.yaml` and add your camera RTSP URLs in the `streams` section.

### 3. Run with docker-compose

```bash
docker-compose up -d
```

Only the `rtsp-server`, `rtsp-generator-*`, and `streamer` services will start.

### 4. Access the web player

Open `http://localhost:8080` in your browser to view live streams.

---

## Option 2: Streaming + AI Inference (Full Stack)

### 1. Build the Docker image

```bash
docker build -t genea-video-streamer:latest .
```

### 2. Configure streaming

Edit `config/rtsp-multi-stream.yaml` with your RTSP camera URLs.

### 3. Configure inference (Optional)

Edit `config/inference.yaml` to enable/disable per-stream detection:

```yaml
ai_inference:
  model: yolov8n
  streams:
    - stream_id: camera-1
      enabled: true
      hls_input_dir: /data/hls_output/camera-1
      detections_output_dir: /data/detections/camera-1
```

Set `enabled: false` to disable inference on a stream.

### 4. Run the full stack

```bash
docker-compose up -d
```

This starts:
- RTSP sources (test generators)
- Streaming service (generates HLS)
- Inference service (runs YOLOv8 on HLS segments)

### 5. Monitor services

```bash
# View logs
docker-compose logs -f streamer
docker-compose logs -f inference

# Check container status
docker-compose ps
```

### 6. Access interfaces

- **Web Player:** http://localhost:8080 (live streams + detection gallery)
- **Detection Stats:** http://localhost:8080/api/detections/stats
- **Recent Detections:** http://localhost:8080/api/detections/recent?limit=10
- **Stream Health:** http://localhost:8080/api/health

### 7. Stop services

```bash
docker-compose down
```

---

## Troubleshooting

### Inference container exits with error

Check logs:
```bash
docker-compose logs inference
```

Common issues:
- `No config file found` — Ensure `config/inference.yaml` exists
- `Failed to extract frame` — Check HLS segments exist at configured paths
- `No enabled streams` — Verify at least one stream has `enabled: true`

### No detections appearing

1. Verify HLS segments are being generated:
   ```bash
   docker exec genea-inference ls -la /data/hls_output/camera-1/
   ```

2. Check inference logs:
   ```bash
   docker-compose logs inference | grep -i detection
   ```

3. Verify database:
   ```bash
   docker exec genea-inference sqlite3 /app/detections.db \
     "SELECT COUNT(*) FROM detections;"
   ```

### Performance issues

- Reduce `inference_interval_s` in config.yaml (currently 5s)
- Enable only needed streams (`enabled: true`)
- Monitor CPU usage:
  ```bash
  docker stats genea-inference
  ```

---

## Advanced Configuration

### Low-Latency Playback

Edit `config/rtsp-multi-stream.yaml`:

```yaml
playback:
  live_mode:
    back_buffer_length_s: 5
    sync_segment_count: 1
    max_buffer_length_s: 15
```

### Custom Detection Classes

Edit `config/inference.yaml`:

```yaml
ai_inference:
  classes: [person, car, truck, bus]  # Add more COCO classes
  confidence_threshold: 0.6  # Raise threshold for fewer false positives
```

### Database Retention

Edit `config/inference.yaml`:

```yaml
ai_inference:
  retention_days: 30  # Keep detection frames for 30 days
```

Old records are automatically purged on startup.

---

## Production Checklist

- [ ] RTSP URLs tested and accessible
- [ ] Firewall rules allow port 8080 (HTTP)
- [ ] Sufficient disk space for HLS segments and detection frames
- [ ] Inference service has CPU allocation (see docker-compose.yml)
- [ ] Logs monitored for errors
- [ ] Database backup strategy in place
- [ ] Health check endpoints monitored (e.g., `/api/health`)

