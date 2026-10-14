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
#   -e, --emulated    Run with 2 emulated cameras (no camera hardware needed)
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
    -e, --emulated    Run with 2 emulated cameras (no camera hardware needed)
    -h, --help        Show this help

EXAMPLES:
    $0                  # Normal build and run
    $0 -e               # Build and run with emulated cameras
    $0 --no-cache       # Build without cache (fresh build)
    $0 --stop-only      # Just stop and remove container
    $0 --clean          # Build and clean old images
EOF
}

# Parse options
NO_CACHE=false
STOP_ONLY=false
CLEAN_IMAGES=false
EMULATED=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -n|--no-cache) NO_CACHE=true; shift ;;
        -s|--stop-only) STOP_ONLY=true; shift ;;
        -c|--clean) CLEAN_IMAGES=true; shift ;;
        -e|--emulated) EMULATED=true; shift ;;
        -h|--help) show_help; exit 0 ;;
        *) print_error "Unknown option: $1"; show_help; exit 1 ;;
    esac
done

COMPOSE_ARGS=(-f "$COMPOSE_FILE")
if [ "$EMULATED" = true ]; then
    COMPOSE_ARGS+=(-f ".docker/docker-compose.emulated.yml")
fi

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

ARCHIVE_SERVERS=()

cleanup_archive_servers() {
    local pid
    for pid in "${ARCHIVE_SERVERS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    ARCHIVE_SERVERS=()
}

start_archive_server() {
    local dir="$1"
    local port="$2"
    python3 -m http.server "$port" --directory "$dir" >/dev/null 2>&1 &
    ARCHIVE_SERVERS+=("$!")
}

# Explicit URLs win; otherwise serve local archives over HTTP so the Docker
# build can fetch them: pylon from .docker/, MPEG-4 from .docker/ or
# BaslerDocuments/. The MPEG-4 package is optional (MP4 export only).
resolve_pylon_sources() {
    local archive

    if [ -z "${PYLON_DEB_URL:-}" ]; then
        archive=$(ls .docker/pylon_*.tar.gz .docker/pylon_*.deb 2>/dev/null | head -n 1)
        if [ -n "$archive" ]; then
            start_archive_server .docker 8765
            export PYLON_DEB_URL="http://host.docker.internal:8765/$(basename "$archive")"
            print_info "Serving local pylon archive: $archive"
        fi
    fi

    if [ -z "${PYLON_DEB_URL:-}" ]; then
        print_error "No pylon source found"
        echo ""
        print_info "Put a local pylon installer in .docker/ (pylon_*_x86_64.tar.gz or pylon_*.deb),"
        print_info "or host one and export its URL:"
        echo "  export PYLON_DEB_URL=https://.../pylon_6.2.0.21487_x86_64.tar.gz"
        exit 1
    fi

    if [ -z "${PYLON_MPEG4_DEB_URL:-}" ]; then
        archive=$(ls .docker/pylon-supplementary-package-for-mpeg-4_*.deb \
                     BaslerDocuments/pylon-supplementary-package-for-mpeg-4_*.deb 2>/dev/null | head -n 1)
        if [ -n "$archive" ]; then
            start_archive_server "$(dirname "$archive")" 8766
            export PYLON_MPEG4_DEB_URL="http://host.docker.internal:8766/$(basename "$archive")"
            print_info "Serving local MPEG-4 package: $archive"
        else
            print_warning "No MPEG-4 package found - building without it (MP4 export will be unavailable)"
        fi
    fi
}

trap cleanup_archive_servers EXIT

build_image() {
    print_info "Building Docker image: $IMAGE_NAME"
    
    if [ "$NO_CACHE" = true ]; then
        docker compose "${COMPOSE_ARGS[@]}" build --no-cache
    else
        docker compose "${COMPOSE_ARGS[@]}" build
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
    docker compose "${COMPOSE_ARGS[@]}" up -d
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

# Resolve pylon sources (or start a local archive server)
resolve_pylon_sources

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
