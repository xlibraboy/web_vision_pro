#!/bin/bash
################################################################################
# Builds web-vision-pro_<version>_amd64.deb for Ubuntu 22.04 (jammy).
#
# Run on jammy - the CI runner, or a throwaway container:
#   docker run --rm -v "$PWD:/src" -w /src ubuntu:22.04 bash packaging/build-deb.sh
#
# Mirrors .docker/Dockerfile: builds open62541 + QtOpcUa from source against
# the DISTRO Qt (Qt plugins reject build-version mismatches, so the bundled
# plugin has to match the target release), installs Basler pylon for linking
# (the vendor SDK is never redistributed), then drives CMake/CPack.
#
# Inputs (one required unless pylon is already in /opt/pylon):
#   PYLON_DEB_URL=https://...   download URL of the Basler pylon 6.2 deb
#   PYLON_DEB_FILE=/path.deb    local copy of the same deb
#   QMAKE=/path/to/qmake        optional qmake override
#
# Options:
#   --skip-tests    do not build or run the regression tests
#   --skip-deps     do not install build dependencies
################################################################################
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$REPO_ROOT/packaging/out"
BUILD_DIR="$REPO_ROOT/build-deb"
WORK_DIR="${WORK_DIR:-/tmp/papervision-packaging}"

RUN_TESTS=1
INSTALL_DEPS=1
for arg in "$@"; do
    case "$arg" in
        --skip-tests) RUN_TESTS=0 ;;
        --skip-deps)  INSTALL_DEPS=0 ;;
        -h|--help)    sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
else
    SUDO="sudo"
fi

say() { printf '\n== %s\n' "$*"; }

# --- Target release ---------------------------------------------------------
. /etc/os-release
if [ "${VERSION_ID:-}" != "22.04" ]; then
    echo "WARNING: this builds for Ubuntu 22.04 but the host is ${PRETTY_NAME:-unknown}." >&2
    echo "         The QtOpcUa plugin is built against the host Qt and may not load" >&2
    echo "         on 22.04; use a jammy host or container for release builds." >&2
fi

# --- Build dependencies -----------------------------------------------------
BUILD_PKGS=(
    build-essential cmake git wget pkg-config dpkg-dev
    qtbase5-dev qtbase5-dev-tools libqt5svg5-dev libqt5test5
    libopencv-dev
)
if [ "$INSTALL_DEPS" -eq 1 ]; then
    say "Installing build dependencies"
    missing=()
    for pkg in "${BUILD_PKGS[@]}"; do
        dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "install ok installed" \
            || missing+=("$pkg")
    done
    if [ "${#missing[@]}" -gt 0 ]; then
        $SUDO apt-get update
        $SUDO apt-get install -y --no-install-recommends "${missing[@]}"
    else
        echo "All build dependencies already installed."
    fi
fi

# --- Basler pylon (link-time SDK; not redistributed) ------------------------
if [ -d /opt/pylon ]; then
    say "Basler pylon already present in /opt/pylon"
else
    say "Installing Basler pylon (build-time SDK)"
    pylon_deb="$WORK_DIR/pylon-sdk.deb"
    mkdir -p "$WORK_DIR"
    if [ -n "${PYLON_DEB_FILE:-}" ]; then
        cp "$PYLON_DEB_FILE" "$pylon_deb"
    elif [ -n "${PYLON_DEB_URL:-}" ]; then
        wget -q -O "$pylon_deb" "$PYLON_DEB_URL"
    else
        echo "ERROR: /opt/pylon is missing and neither PYLON_DEB_FILE nor PYLON_DEB_URL is set." >&2
        echo "       Download the Basler pylon 6.2 deb and point one of them at it." >&2
        exit 1
    fi
    $SUDO dpkg -i "$pylon_deb" || $SUDO apt-get install -f -y
    rm -f "$pylon_deb"
fi

# --- open62541 (OPC UA stack used by the QtOpcUa backend) -------------------
if ls /usr/local/lib/libopen62541.so* >/dev/null 2>&1; then
    say "open62541 already installed"
else
    say "Building open62541 v1.5.4"
    rm -rf "$WORK_DIR/open62541" "$WORK_DIR/open62541-build"
    git clone --depth 1 --branch v1.5.4 \
        https://github.com/open62541/open62541.git "$WORK_DIR/open62541"
    cmake -S "$WORK_DIR/open62541" -B "$WORK_DIR/open62541-build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DUA_ENABLE_AMALGAMATION=OFF \
        -DUA_ENABLE_ENCRYPTION=OFF \
        -DUA_BUILD_EXAMPLES=OFF \
        -DUA_BUILD_TOOLS=OFF \
        -DUA_BUILD_UNIT_TESTS=OFF
    cmake --build "$WORK_DIR/open62541-build" --parallel "$(nproc)"
    $SUDO cmake --install "$WORK_DIR/open62541-build"
fi

# --- QtOpcUa (not in the Ubuntu archive; bundled into the package) ----------
if ls /usr/lib/x86_64-linux-gnu/libQt5OpcUa.so* >/dev/null 2>&1; then
    say "QtOpcUa already installed"
else
    say "Building QtOpcUa 5.15.2 against the distro Qt"
    rm -rf "$WORK_DIR/qtopcua" "$WORK_DIR/qtopcua-build"
    git clone --depth 1 --branch 5.15.2 \
        https://github.com/qt/qtopcua.git "$WORK_DIR/qtopcua"
    QMAKE_BIN="${QMAKE:-}"
    if [ -z "$QMAKE_BIN" ]; then
        for candidate in /usr/lib/qt5/bin/qmake qmake; do
            if command -v "$candidate" >/dev/null 2>&1; then
                QMAKE_BIN="$candidate"
                break
            fi
        done
    fi
    if [ -z "$QMAKE_BIN" ]; then
        echo "ERROR: qmake not found (install qtbase5-dev-tools or set QMAKE)." >&2
        exit 1
    fi
    mkdir -p "$WORK_DIR/qtopcua-build"
    ( cd "$WORK_DIR/qtopcua-build" \
        && "$QMAKE_BIN" "$WORK_DIR/qtopcua/qtopcua.pro" \
        && make -j"$(nproc)" \
        && $SUDO make install )
fi

# /usr/local/lib (open62541) needs the loader cache refreshed either way.
$SUDO ldconfig

# --- Application + package --------------------------------------------------
say "Configuring the application"
mkdir -p "$OUT_DIR"
TEST_FLAG="-DBUILD_TESTING=OFF"
[ "$RUN_TESTS" -eq 1 ] && TEST_FLAG="-DBUILD_TESTING=ON"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/ \
    -DPAPERVISION_PACKAGING=ON \
    "$TEST_FLAG" \
    -DCPACK_OUTPUT_FILE_PREFIX="$OUT_DIR"

say "Building WebVisionPro_App"
cmake --build "$BUILD_DIR" --target WebVisionPro_App --parallel "$(nproc)"

if [ "$RUN_TESTS" -eq 1 ]; then
    say "Building and running the regression tests"
    cmake --build "$BUILD_DIR" --target WebVisionPro_RegressionTest --parallel "$(nproc)"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

say "Building the .deb"
cmake --build "$BUILD_DIR" --target package

( cd "$OUT_DIR" && sha256sum ./*.deb > SHA256SUMS.txt )

say "Done"
ls -l "$OUT_DIR"
