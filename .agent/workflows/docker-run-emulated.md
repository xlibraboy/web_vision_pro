---
description: how to run the web_vision_pro docker container with EMULATED cameras (no real hardware needed)
---

Follow these steps to run the PaperVision application with emulated Basler cameras.

1. Ensure you have authorized local Docker containers to access your X11 display (for GUI).
// turbo
```bash
xhost +local:root && xhost +
```

2. Start the container in detached mode using Docker Compose with the emulation override.
// turbo
```bash
docker compose -f .docker/docker-compose.yml -f .docker/docker-compose.emulated.yml up -d
```

3. Monitor the application logs to ensure it starts correctly.
```bash
docker logs -f paper_vision_node
```

4. To stop the container when finished:
// turbo
```bash
docker compose -f .docker/docker-compose.yml -f .docker/docker-compose.emulated.yml down
```
