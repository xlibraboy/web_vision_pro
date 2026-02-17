#!/bin/bash
CONTAINER_NAME="paper_vision_node"
LOG_FILE="memory_log.txt"

echo "Timestamp, Memory Usage" > $LOG_FILE

echo "Monitoring memory for $CONTAINER_NAME..."

while true; do
    if docker ps | grep -q $CONTAINER_NAME; then
        MEM=$(docker stats $CONTAINER_NAME --no-stream --format "{{.MemUsage}}")
        TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")
        echo "$TIMESTAMP, $MEM" | tee -a $LOG_FILE
    else
        echo "Container not running..."
    fi
    sleep 2
done
