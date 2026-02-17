#!/bin/bash

################################################################################
# Docker Image Save/Restore Script
# 
# This script provides utilities to save Docker images to tar archives and
# restore them later. Useful for backup, transfer, or offline deployments.
#
# Usage:
#   ./docker-image-manager.sh save <image_name[:tag]> [output_file]
#   ./docker-image-manager.sh restore <tar_file>
#   ./docker-image-manager.sh list
#   ./docker-image-manager.sh help
################################################################################

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default backup directory
BACKUP_DIR="${DOCKER_BACKUP_DIR:-./docker-backups}"

################################################################################
# Helper Functions
################################################################################

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

show_help() {
    cat << EOF
Docker Image Save/Restore Manager

USAGE:
    $0 <command> [options]

COMMANDS:
    save <image_name[:tag]> [output_file]
        Save a Docker image to a tar archive
        - image_name: Name of the Docker image (with optional tag)
        - output_file: Optional output filename (default: <image_name>_<tag>_<timestamp>.tar)
        
    restore <tar_file>
        Restore a Docker image from a tar archive
        - tar_file: Path to the tar archive
        
    list
        List all saved Docker image archives in the backup directory
        
    help
        Show this help message

EXAMPLES:
    # Save an image with default filename
    $0 save myapp:latest
    
    # Save an image with custom filename
    $0 save myapp:latest /path/to/myapp-backup.tar
    
    # Restore an image
    $0 restore ./docker-backups/myapp_latest_20260210.tar
    
    # List all backups
    $0 list

ENVIRONMENT VARIABLES:
    DOCKER_BACKUP_DIR
        Directory for storing Docker image backups (default: ./docker-backups)

EOF
}

check_docker() {
    if ! command -v docker &> /dev/null; then
        print_error "Docker is not installed or not in PATH"
        exit 1
    fi
    
    if ! docker info &> /dev/null; then
        print_error "Docker daemon is not running or you don't have permission"
        exit 1
    fi
}

create_backup_dir() {
    if [ ! -d "$BACKUP_DIR" ]; then
        print_info "Creating backup directory: $BACKUP_DIR"
        mkdir -p "$BACKUP_DIR"
    fi
}

################################################################################
# Main Functions
################################################################################

save_image() {
    local image_name="$1"
    local output_file="$2"
    
    if [ -z "$image_name" ]; then
        print_error "Image name is required"
        echo "Usage: $0 save <image_name[:tag]> [output_file]"
        exit 1
    fi
    
    # Check if image exists
    if ! docker image inspect "$image_name" &> /dev/null; then
        print_error "Image '$image_name' not found"
        print_info "Available images:"
        docker images
        exit 1
    fi
    
    # Generate default filename if not provided
    if [ -z "$output_file" ]; then
        create_backup_dir
        
        # Parse image name and tag
        local img_base="${image_name%%:*}"
        local img_tag="${image_name##*:}"
        
        # If no tag specified, use 'latest'
        if [ "$img_base" = "$img_tag" ]; then
            img_tag="latest"
        fi
        
        # Clean up name for filename (replace / with _)
        img_base="${img_base//\//_}"
        
        # Generate timestamp
        local timestamp=$(date +%Y%m%d_%H%M%S)
        
        output_file="${BACKUP_DIR}/${img_base}_${img_tag}_${timestamp}.tar"
    fi
    
    # Get image size
    local image_size=$(docker image inspect "$image_name" --format='{{.Size}}' | awk '{printf "%.2f MB", $1/1024/1024}')
    
    print_info "Saving Docker image: $image_name"
    print_info "Image size: $image_size"
    print_info "Output file: $output_file"
    
    # Save the image
    if docker save -o "$output_file" "$image_name"; then
        # Get file size
        local file_size=$(du -h "$output_file" | cut -f1)
        print_success "Image saved successfully!"
        print_info "Archive size: $file_size"
        print_info "Location: $output_file"
        
        # Calculate and display checksum
        print_info "Calculating SHA256 checksum..."
        local checksum=$(sha256sum "$output_file" | cut -d' ' -f1)
        echo "$checksum  $(basename "$output_file")" > "${output_file}.sha256"
        print_info "Checksum: $checksum"
        print_info "Checksum saved to: ${output_file}.sha256"
    else
        print_error "Failed to save image"
        exit 1
    fi
}

restore_image() {
    local tar_file="$1"
    
    if [ -z "$tar_file" ]; then
        print_error "Tar file path is required"
        echo "Usage: $0 restore <tar_file>"
        exit 1
    fi
    
    if [ ! -f "$tar_file" ]; then
        print_error "File not found: $tar_file"
        exit 1
    fi
    
    # Verify checksum if available
    if [ -f "${tar_file}.sha256" ]; then
        print_info "Verifying checksum..."
        if (cd "$(dirname "$tar_file")" && sha256sum -c "$(basename "${tar_file}.sha256")" &> /dev/null); then
            print_success "Checksum verification passed"
        else
            print_warning "Checksum verification failed!"
            read -p "Continue anyway? (y/N): " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                print_info "Restore cancelled"
                exit 0
            fi
        fi
    else
        print_warning "No checksum file found, skipping verification"
    fi
    
    local file_size=$(du -h "$tar_file" | cut -f1)
    print_info "Restoring Docker image from: $tar_file"
    print_info "Archive size: $file_size"
    
    # Load the image
    if docker load -i "$tar_file"; then
        print_success "Image restored successfully!"
        print_info "Loaded images:"
        docker images --format "table {{.Repository}}\t{{.Tag}}\t{{.ID}}\t{{.Size}}" | head -n 5
    else
        print_error "Failed to restore image"
        exit 1
    fi
}

list_backups() {
    create_backup_dir
    
    print_info "Docker image backups in: $BACKUP_DIR"
    echo ""
    
    if [ -z "$(ls -A "$BACKUP_DIR"/*.tar 2>/dev/null)" ]; then
        print_warning "No backup files found"
        exit 0
    fi
    
    printf "%-50s %10s %20s\n" "FILENAME" "SIZE" "MODIFIED"
    printf "%s\n" "--------------------------------------------------------------------------------"
    
    for file in "$BACKUP_DIR"/*.tar; do
        if [ -f "$file" ]; then
            local filename=$(basename "$file")
            local size=$(du -h "$file" | cut -f1)
            local modified=$(date -r "$file" "+%Y-%m-%d %H:%M:%S")
            printf "%-50s %10s %20s\n" "$filename" "$size" "$modified"
        fi
    done
    
    echo ""
    local total_size=$(du -sh "$BACKUP_DIR" 2>/dev/null | cut -f1)
    print_info "Total backup size: $total_size"
}

################################################################################
# Main Script
################################################################################

# Check if Docker is available
check_docker

# Parse command
case "${1:-}" in
    save)
        save_image "$2" "$3"
        ;;
    restore)
        restore_image "$2"
        ;;
    list)
        list_backups
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        print_error "Invalid command: ${1:-<none>}"
        echo ""
        show_help
        exit 1
        ;;
esac
