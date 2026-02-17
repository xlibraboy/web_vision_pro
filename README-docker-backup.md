# Docker Image Save/Restore Script

A comprehensive bash script for backing up and restoring Docker images.

## Features

- ✅ **Save Docker images** to compressed tar archives
- ✅ **Restore Docker images** from tar archives
- ✅ **SHA256 checksum verification** for data integrity
- ✅ **Automatic backup directory management**
- ✅ **Colored output** for better readability
- ✅ **List all backups** with size and timestamp information

## Installation

The script is located at `docker-image-manager.sh` and is ready to use.

```bash
# Make it executable (already done)
chmod +x docker-image-manager.sh
```

## Usage

### Save a Docker Image

```bash
# Save with auto-generated filename
./docker-image-manager.sh save myapp:latest

# Save with custom filename
./docker-image-manager.sh save myapp:latest /path/to/backup.tar

# Save image without tag (uses 'latest' by default)
./docker-image-manager.sh save ubuntu
```

**Output location**: By default, images are saved to `./docker-backups/` directory.

### Restore a Docker Image

```bash
# Restore from a tar archive
./docker-image-manager.sh restore ./docker-backups/myapp_latest_20260210_130924.tar

# Restore with checksum verification (automatic if .sha256 file exists)
./docker-image-manager.sh restore /path/to/backup.tar
```

### List All Backups

```bash
# Show all saved Docker images
./docker-image-manager.sh list
```

### Help

```bash
./docker-image-manager.sh help
```

## Configuration

### Custom Backup Directory

Set the `DOCKER_BACKUP_DIR` environment variable:

```bash
# One-time use
DOCKER_BACKUP_DIR=/mnt/backups ./docker-image-manager.sh save myapp:latest

# Permanent (add to ~/.bashrc or ~/.zshrc)
export DOCKER_BACKUP_DIR=/mnt/backups
```

## Examples

### Backup Your Vision Application

```bash
# Save the vision application image
./docker-image-manager.sh save vision-app:latest

# Output example:
# [INFO] Saving Docker image: vision-app:latest
# [INFO] Image size: 1.2 GB
# [INFO] Output file: ./docker-backups/vision-app_latest_20260210_130924.tar
# [SUCCESS] Image saved successfully!
# [INFO] Archive size: 450M
# [INFO] Checksum: a1b2c3d4e5f6...
```

### Transfer Image to Another Machine

```bash
# On source machine - save the image
./docker-image-manager.sh save pylon-opencv:6.2.0

# Copy to target machine (via scp, usb, etc.)
scp ./docker-backups/pylon-opencv_6.2.0_*.tar user@target:/tmp/

# On target machine - restore the image
./docker-image-manager.sh restore /tmp/pylon-opencv_6.2.0_*.tar
```

### Backup Multiple Images

```bash
#!/bin/bash
# backup-all-images.sh

images=(
    "vision-app:latest"
    "pylon-opencv:6.2.0"
    "basler-camera:production"
)

for image in "${images[@]}"; do
    ./docker-image-manager.sh save "$image"
done
```

## File Naming Convention

Auto-generated filenames follow this pattern:
```
<image-name>_<tag>_<timestamp>.tar
```

Examples:
- `myapp_latest_20260210_130924.tar`
- `ubuntu_22.04_20260210_131500.tar`
- `registry.example.com_myapp_v1.0_20260210_132000.tar`

## Checksum Verification

The script automatically:
1. Generates SHA256 checksums when saving images
2. Saves checksums to `.sha256` files alongside tar archives
3. Verifies checksums when restoring (if checksum file exists)

## Error Handling

The script includes comprehensive error checking:
- ✅ Verifies Docker is installed and running
- ✅ Checks if images exist before saving
- ✅ Validates file existence before restoring
- ✅ Provides clear error messages with color coding

## Tips

1. **Compress backups further**: Use `gzip` for additional compression
   ```bash
   gzip ./docker-backups/myapp_latest_*.tar
   ```

2. **Automated backups**: Add to cron for scheduled backups
   ```bash
   # Daily backup at 2 AM
   0 2 * * * /path/to/docker-image-manager.sh save myapp:latest
   ```

3. **Remote backups**: Combine with rsync or cloud storage
   ```bash
   ./docker-image-manager.sh save myapp:latest
   rsync -av ./docker-backups/ user@backup-server:/backups/
   ```

## Troubleshooting

### "Docker daemon is not running"
```bash
sudo systemctl start docker
```

### "Permission denied"
```bash
# Add user to docker group
sudo usermod -aG docker $USER
# Log out and back in
```

### "No space left on device"
```bash
# Clean up old Docker images
docker image prune -a

# Or specify a different backup directory with more space
DOCKER_BACKUP_DIR=/mnt/large-disk ./docker-image-manager.sh save myapp:latest
```
