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

detect_xauthority() {
    if [ -n "${XAUTHORITY:-}" ] && [ -f "$XAUTHORITY" ]; then
        return 0
    fi

    local fallback="${HOME}/.Xauthority"
    if [ -f "$fallback" ]; then
        export XAUTHORITY="$fallback"
        return 0
    fi

    local mutter_auth
    mutter_auth=$(find "/run/user/$(id -u)" -maxdepth 1 -name '.mutter-Xwaylandauth.*' -type f 2>/dev/null | head -n 1 || true)
    if [ -n "$mutter_auth" ] && [ -f "$mutter_auth" ]; then
        export XAUTHORITY="$mutter_auth"
        return 0
    fi

    return 1
}

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

if ! detect_xauthority; then
    print_error "No usable Xauthority file found for DISPLAY=${DISPLAY:-unset}."
    exit 1
fi

if ! docker image inspect web-vision-pro:1.0 >/dev/null 2>&1; then
    print_error "Local image web-vision-pro:1.0 is missing. Refusing to build or pull."
    exit 1
fi

if docker ps -a --format '{{.Names}}' | grep -q "^paper_vision_node_run$"; then
    print_info "Removing temporary container: paper_vision_node_run"
    docker rm -f paper_vision_node_run >/dev/null
fi

print_info "Recreating container with rebuild-only startup..."
docker compose -f "$COMPOSE_FILE" up -d --no-build --force-recreate
sleep 2

if ! docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
    print_error "Container failed to stay up. Check logs: docker logs $CONTAINER_NAME"
    exit 1
fi

print_success "Application rebuilt and container started."
print_info "Logs: docker logs -f $CONTAINER_NAME"
