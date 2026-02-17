#!/bin/bash
# Shortcut to run pylonviewer from inside the container
cd /home/autoinst578/web_vision_pro/.docker
docker compose run --rm -d vision_node /opt/pylon/bin/pylonviewer
echo "pylonviewer is launching in a new temporary container..."
echo "It will automatically be removed when you close it."
