#!/usr/bin/env bash
# Remove build output.
set -euo pipefail
cd "$(dirname "$0")"

rm -rf build
