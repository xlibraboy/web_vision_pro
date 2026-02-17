#!/bin/bash

echo "PaperVision System - Environment Setup"
echo "======================================"

# Check for Docker
if ! command -v docker &> /dev/null; then
    echo "[ERROR] Docker is not installed."
    exit 1
fi

# Check for Docker Compose
if ! command -v docker-compose &> /dev/null; then
    echo "[WARNING] docker-compose not found. Note: 'docker compose' (v2) might be available."
fi

# Create data directories
mkdir -p data/recordings/cache
mkdir -p data/recordings/archive
mkdir -p data/logs
mkdir -p data/snapshots

# Set permissions (simplistic)
chmod -R 777 data

echo "[INFO] Directories created."
echo "[INFO] Environment ready."
echo "To build and run:"
echo "  cd .docker && docker-compose up --build"
