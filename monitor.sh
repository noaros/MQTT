#!/usr/bin/env bash
# Print the board's serial output. Usage: ./monitor.sh [port] [baud]
set -euo pipefail

PORT=${1:-/dev/ttyACM0}
BAUD=${2:-115200}

(stty "$BAUD" raw -echo; cat) < "$PORT"
