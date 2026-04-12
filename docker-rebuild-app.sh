#!/bin/bash

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

CONTAINER_NAME="paper_vision_node"
COMPOSE_FILE=".docker/docker-compose.yml"

print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  Docker App Rebuild Script${NC}"
echo -e "${CYAN}========================================${NC}"

if ! command -v docker >/dev/null 2>&1; then
    print_error "Docker is not installed."
    exit 1
fi

if ! docker info >/dev/null 2>&1; then
    print_error "Docker daemon is not available."
    exit 1
fi

if ! docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    print_info "Container not found. Creating it without rebuilding the image..."
    docker compose -f "$COMPOSE_FILE" up -d --no-build
fi

if ! docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    print_info "Starting container: $CONTAINER_NAME"
    docker start "$CONTAINER_NAME" >/dev/null
fi

print_info "Rebuilding application inside container..."
docker exec "$CONTAINER_NAME" /bin/bash -lc "mkdir -p /app/build && cd /app/build && cmake .. && cmake --build . --target PaperVision_App -- -j\$(nproc)"

print_info "Restarting container to relaunch PaperVision_App..."
docker restart "$CONTAINER_NAME" >/dev/null

print_success "Application rebuilt and container restarted."
print_info "Logs: docker logs -f $CONTAINER_NAME"
