# web_vision_pro

Industrial vision system for paper machine inspection using Basler GigE cameras, Qt5 GUI, OpenCV processing, and Pylon 6.x API.

Machine reference (drives, speed anchors, sheet-break sensors, camera positions on the mm ruler): [docs/machine-reference.md](docs/machine-reference.md).

## Project Structure

```
src/
├── core/          # CameraManager, EventController, EventDatabase
├── gui/           # Qt widgets and windows
├── processing/    # OpenCV algorithms
├── config/        # Configuration handling
└── communication/ # External interfaces
```

## Build

```bash
mkdir -p build && cd build
cmake ..
make
```

## Docker

Image builds reuse cached layers: a rebuild skips `apt-get update`, the Qt/OpcUa source builds and the
pylon downloads unless a layer's inputs change, so a cached rebuild can silently keep obsolete packages.
Before rebuilding the image, ask the user whether to refresh system packages (a fresh `apt-get update`):
- no rebuild in weeks/months (`docker image inspect web-vision-pro:1.0 --format '{{.Created}}'`), or a
  dependency is obsolete / needs a patch -> recommend `./docker-build-run.sh --no-cache`
- otherwise -> default cached build (`./docker-build-run.sh`)

Never force `--no-cache` / a fresh `apt-get update` without asking first.

## Packaging / release (.deb)

Deployment guide: [docs/installation.md](docs/installation.md).

- Target is **Ubuntu 22.04 (jammy), amd64**, built on jammy (CI runner or `ubuntu:22.04` container) with
  `packaging/build-deb.sh` — it needs `PYLON_DEB_URL` (repo secret in CI) because the build links against the
  Basler pylon SDK. Ubuntu 20.04 stays Docker-only: its Qt 5.12 lacks the OPC UA API the app uses.
- Release flow: bump `AppVersion::kNumber`, move the CHANGELOG entries under `## [x.y.z] - date`, commit,
  push tag `vX.Y.Z`. `.github/workflows/release-deb.yml` builds the package (with tests) and attaches the
  `.deb` + checksums to the GitHub Release. Tag and `AppVersion::kNumber` must match.
- The package (native successor of the old `papervision` one, which it replaces) bundles QtOpcUa +
  open62541 under `/opt/web-vision-pro` (Ubuntu does not package QtOpcUa; the plugin must be built
  against the target distro's Qt patch version). Basler pylon is a vendor prerequisite and must never
  be redistributed inside the package.
- Filesystem roots go through `src/config/AppPaths` (override env -> Docker `/app/data` -> XDG); keep it
  that way instead of adding new absolute paths.

## Conventions

- C++17, CMake 3.10+, Qt5, OpenCV 4.x, Pylon 6.x
- Classes: `PascalCase`, member vars: `snake_case_`, constants: `UPPER_SNAKE_CASE`
- Indentation: 4 spaces, K&R braces
- Use `nullptr`, not `NULL`
- Commit format: `type: description` (types: feat, fix, docs, style, refactor, test, chore, docker)
- Keep [CHANGELOG.md](CHANGELOG.md) current: user-visible changes go under `## [Unreleased]`
  (`feat` -> Added, `fix` -> Fixed, `refactor`/`style`/`chore` usually need no entry), one
  bullet per line. A release moves the entries under a new `## [x.y.z] - date` heading and
  bumps `AppVersion::kNumber` in `src/config/AppVersion.h`. The app renders the file in
  Help > Changelog.