# Installing Web Vision Pro

Two deployments are supported and share the same source tree:

| | Native `.deb` | Docker image |
|---|---|---|
| Target | Ubuntu 22.04 (jammy), amd64 | Any host with Docker; the image is Ubuntu 20.04 |
| Install | `apt install ./web-vision-pro_<version>_amd64.deb` | `./docker-build-run.sh` |
| Upgrade | same command with the newer file | rebuild the image |

Ubuntu 20.04 has no native package: its Qt 5.12 is too old for the OPC UA API the
application uses, which is why the Docker image exists. Both deployments require the
Basler pylon 6.2 runtime (see below) — it is vendor-licensed and never shipped with the
application.

## Prerequisite: Basler pylon 6.2

Download the pylon 6.2 deb for Linux x86-64 from Basler and install it once per machine:

```bash
sudo dpkg -i pylon_6.2.0.21487-deb0_amd64.deb
sudo apt-get -f install          # only if the line above reports missing dependencies
```

The runtime installs to `/opt/pylon`. Nothing else on the machine is affected.
(For Docker, pylon is installed inside the image at build time; see `.docker/Dockerfile`.)

## Native install (Ubuntu 22.04)

```bash
sudo apt install ./web-vision-pro_1.2.0-1_amd64.deb
```

`apt` (not `dpkg -i`) pulls the Qt5/OpenCV/X11 dependencies from the Ubuntu archive.

- **Upgrade**: download the newer `.deb` from GitHub Releases and run the same command; dpkg
  replaces the payload and keeps your settings and recordings. The package replaces the
  earlier `papervision` one, so `apt install ./web-vision-pro_<version>_amd64.deb` also
  upgrades a machine that still has the old package installed.
- **Uninstall**: `sudo apt remove web-vision-pro` — settings in `~/.config/PaperVision` and the
  recordings stay on disk. `apt purge` behaves the same here (the package tracks no
  system-wide configuration files).
- **Reinstall/fix**: `sudo apt install --reinstall ./web-vision-pro_<version>_amd64.deb`.

### Where things live

| Item | Location |
|---|---|
| Application payload, bundled QtOpcUa/open62541 | `/opt/web-vision-pro` |
| Launcher (start menu entry `Web Vision Pro`) | `/usr/bin/web-vision-pro` |
| Settings (`SystemConfig.ini`) | `~/.config/PaperVision` |
| Recordings (default event storage) | `~/.local/share/papervision` |
| Per-camera pylon parameter files | `<event storage>/cameras/camera_<id>.pfs` |
| GigE kernel buffers (25 MB sockets) | `/usr/lib/sysctl.d/99-web-vision-pro.conf` |
| MTU/jumbo-frame helper | `/usr/lib/web-vision-pro/setup-network.sh` |

The event storage folder stays configurable in **System Configuration > Recording &
Storage**; an existing `EventStoragePath` setting wins over the default above.

### GigE network tuning

Kernel socket buffers come with the package and apply at boot (`sudo sysctl --system`
applies them immediately). Jumbo frames are per-interface and stay a manual step:

```bash
sudo /usr/lib/web-vision-pro/setup-network.sh          # auto-detects the camera NIC
sudo /usr/lib/web-vision-pro/setup-network.sh eno2     # or name it explicitly
```

### Environment overrides

`PAPERVISION_CONFIG_DIR` and `PAPERVISION_DATA_DIR` override the settings and recordings
roots (used by both deployments, e.g. to point recordings at a dedicated data disk
without touching the UI).

## Moving from Docker to the native package

1. Install the `.deb` (and pylon on the host, if it only lived inside the image).
2. Copy the recordings, e.g. `mkdir -p ~/.local/share/papervision && cp data/event_* ~/.local/share/papervision/`.
3. Copy the settings: `mkdir -p ~/.config/PaperVision && cp data/config/PaperVision/SystemConfig.ini ~/.config/PaperVision/`.
4. Launch `web-vision-pro`. Metadata whose stored paths still point at the container's
   `/app/data` are adopted automatically when the matching file is present in the
   storage folder.

## Building the package

Run on Ubuntu 22.04 (or a `ubuntu:22.04` container) — the bundled QtOpcUa plugin is
built against the target distro's Qt, because Qt plugins reject build-version
mismatches:

```bash
export PYLON_DEB_URL=https://.../pylon_6.2.0.21487-deb0_amd64.deb   # or PYLON_DEB_FILE=/path/to.deb
packaging/build-deb.sh
```

Result: `packaging/out/web-vision-pro_<version>_amd64.deb` plus `SHA256SUMS.txt`. The script
installs build dependencies, builds open62541 + QtOpcUa from source, runs the regression
tests (`--skip-tests` to skip) and then CMake/CPack. QtOpcUa and open62541 end up bundled
under `/opt/web-vision-pro` because the Ubuntu archive does not package QtOpcUa.

Releases are automated: bump `AppVersion::kNumber`, move the CHANGELOG entries under the
new version, commit, and push a `vX.Y.Z` tag — `.github/workflows/release-deb.yml` builds
the package and attaches it (with checksums) to the GitHub Release. The repository needs
the `PYLON_DEB_URL` secret for that.
