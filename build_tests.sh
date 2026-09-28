#!/usr/bin/env bash
# PRISM ENGINE — host build for the engine core + unit tests (no Android SDK needed).
set -euo pipefail
cd "$(dirname "$0")"
OUT="${OUT:-build-host}"
CXX="${CXX:-g++}"
STD="${STD:-c++20}"
mkdir -p "$OUT"
SRC=$(find engine/core/src engine/tests -name '*.cpp' | sort)
echo "[prism] compiling $(echo "$SRC" | wc -l) translation units ($CXX -std=$STD)"
$CXX -std=$STD -O2 -g -Wall -Wextra -Wno-unused-parameter \
     -Iengine/core/include -Iengine/tests -pthread \
     $SRC -o "$OUT/prism_tests"
echo "[prism] running engine test suite"
"$OUT/prism_tests" "$@"
