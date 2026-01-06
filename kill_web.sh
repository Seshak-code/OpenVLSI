#!/bin/bash

# Find and kill the process running on port 8000
PID=$(lsof -ti :8000)

if [ -z "$PID" ]; then
    echo "No process found running on port 8000."
else
    echo "Killing web server (PID: $PID) on port 8000..."
    kill -9 $PID
    echo "Web server killed."
fi
