#pragma once

#include <QString>

/**
 * Filesystem roots the application reads and writes.
 *
 * Resolution order for every root (first match wins):
 *   1. explicit override - PAPERVISION_CONFIG_DIR / PAPERVISION_DATA_DIR;
 *   2. container layout  - /app/data[/config] when that directory exists
 *                          (the Docker image bind-mounts the repository there);
 *   3. native XDG        - $XDG_CONFIG_HOME (~/.config) and
 *                          $XDG_DATA_HOME/papervision (~/.local/share/papervision)
 *                          for a .deb install running outside any container.
 */
namespace AppPaths {

/** Base for QSettings UserScope files; QSettings appends the "PaperVision" folder. */
QString configBaseDir();

/** Default event storage folder (recordings, metadata, thumbnails). */
QString dataDir();

/** Per-camera pylon feature files (*.pfs); created on demand. */
QString cameraParamsDir();

}
