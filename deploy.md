# How to deploy, configure, and view streams?

Note: Please ensure that docker and its components are installed on you Linux machine.

---

### 1. Build the Docker image
While in the project's root directory, run:
```
docker build -t genea-video-streamer:latest .
```

---

### 2. Add RTSP URLs using the sample configuration file
- Inside `config/rtsp-multi-stream.yaml`, add/modify the `streams` section
- Refer to the comments in the config file for help.

---

### 3. Launch the Docker container using docker compose
While in the project's root directory, run:
```
docker compose up -d
```

---

### 4. View the streams on browser
- Access http://localhost:8080
- Select Live or Playback
- Select a stream name from the drop-down

(if on a headless server, please enable port-forwarding, or configure a reverse proxy)

---

