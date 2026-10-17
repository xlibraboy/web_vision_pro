#!/bin/sh
# Launcher installed as /usr/bin/web-vision-pro. Exports the library and plugin
# paths for the bundled QtOpcUa/open62541 and the vendor pylon runtime, then
# hands over to the real binary in /opt/web-vision-pro.
set -e

PREFIX=/opt/web-vision-pro

if [ ! -d /opt/pylon ]; then
    echo "web-vision-pro: Basler pylon was not found in /opt/pylon." >&2
    echo "web-vision-pro: install the Basler pylon 6.2 runtime first (see /usr/share/doc/web-vision-pro/INSTALL.md)." >&2
    exit 1
fi

LD_LIBRARY_PATH="$PREFIX/lib:/opt/pylon/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
QT_PLUGIN_PATH="$PREFIX/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
PYLON_ROOT=/opt/pylon
export LD_LIBRARY_PATH QT_PLUGIN_PATH PYLON_ROOT

exec "$PREFIX/bin/WebVisionPro_App" "$@"
