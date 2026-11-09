#!/usr/bin/env bash
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
g++ -std=c++23 -Wall -Wextra -Werror -Isrc "$DIR/tests/core_test.cpp" "$DIR/src/Pixelizer.cpp" "$DIR/src/Schematic.cpp" -o /tmp/gd-schem-core-test
/tmp/gd-schem-core-test
