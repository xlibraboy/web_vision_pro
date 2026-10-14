#!/bin/sh
# Launcher installed as /usr/bin/papervision. Exports the library and plugin
# paths for the bundled QtOpcUa/open62541 and the vendor pylon runtime, then
# hands over to the real binary in /opt/papervision.
set -e

PREFIX=/opt/papervision

if [ ! -d /opt/pylon ]; then
    echo "papervision: Basler pylon was not found in /opt/pylon." >&2
    echo "papervision: install the Basler pylon 6.2 runtime first (see /usr/share/doc/papervision/INSTALL.md)." >&2
    exit 1
fi

LD_LIBRARY_PATH="$PREFIX/lib:/opt/pylon/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
QT_PLUGIN_PATH="$PREFIX/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
PYLON_ROOT=/opt/pylon
export LD_LIBRARY_PATH QT_PLUGIN_PATH PYLON_ROOT

exec "$PREFIX/bin/PaperVision_App" "$@"
