#!/bin/bash

################################################################################
# Docker Build and Run Script with Auto-Cleanup
#
# This script automatically:
# 1. Stops and removes old container (if exists)
# 2. Builds the new image
# 3. Runs the new container
#
# Usage:
#   ./docker-build-run.sh [options]
#
# Options:
#   -n, --no-cache    Build without cache (--no-cache)
#   -s, --stop-only   Only stop and remove container, don't build/run
#   -c, --clean       Remove old images after build
#   -h, --help        Show this help
################################################################################

set -e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

CONTAINER_NAME="paper_vision_node"
IMAGE_NAME="web-vision-pro:1.0"
COMPOSE_FILE=".docker/docker-compose.yml"

print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

show_help() {
    cat << EOF
Docker Build and Run Script

USAGE:
    $0 [options]

OPTIONS:
    -n, --no-cache    Build Docker image without cache
    -s, --stop-only   Only stop and remove container, don't build/run
    -c, --clean       Remove old images after building new one
    -h, --help        Show this help

EXAMPLES:
    $0                  # Normal build and run
    $0 --no-cache       # Build without cache (fresh build)
    $0 --stop-only      # Just stop and remove container
    $0 --clean          # Build and clean old images
EOF
}

# Parse options
NO_CACHE=false
STOP_ONLY=false
CLEAN_IMAGES=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -n|--no-cache) NO_CACHE=true; shift ;;
        -s|--stop-only) STOP_ONLY=true; shift ;;
        -c|--clean) CLEAN_IMAGES=true; shift ;;
        -h|--help) show_help; exit 0 ;;
        *) print_error "Unknown option: $1"; show_help; exit 1 ;;
    esac
done

cleanup_container() {
    if docker ps -a --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
        print_info "Stopping container: $CONTAINER_NAME"
        docker stop "$CONTAINER_NAME" 2>/dev/null || true
        print_info "Removing container: $CONTAINER_NAME"
        docker rm "$CONTAINER_NAME" 2>/dev/null || true
        print_success "Old container removed"
    else
        print_info "No existing container found"
    fi
}

build_image() {
    print_info "Building Docker image: $IMAGE_NAME"
    
    if [ "$NO_CACHE" = true ]; then
        docker compose -f "$COMPOSE_FILE" build --no-cache
    else
        docker compose -f "$COMPOSE_FILE" build
    fi
    
    print_success "Image built successfully"
}

clean_old_images() {
    print_info "Cleaning up old images..."
    docker image prune -f --filter "until=1h" 2>/dev/null || true
    print_success "Old images cleaned"
}

run_container() {
    print_info "Starting new container: $CONTAINER_NAME"
    docker compose -f "$COMPOSE_FILE" up -d
    print_success "Container started"
    echo ""
    print_info "View logs: docker logs -f $CONTAINER_NAME"
    print_info "Attach to container: docker attach $CONTAINER_NAME"
    print_info "Stop: docker stop $CONTAINER_NAME"
}

# Main execution
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  Docker Build and Run Script${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""

# Always cleanup container first
cleanup_container

if [ "$STOP_ONLY" = true ]; then
    print_info "Stop-only mode: container removed"
    exit 0
fi

# Build new image
build_image

# Clean old images if requested
if [ "$CLEAN_IMAGES" = true ]; then
    clean_old_images
fi

# Run new container
run_container

echo ""
print_success "Done! Application should be running."
