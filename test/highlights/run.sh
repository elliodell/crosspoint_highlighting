#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build_dir="${TMPDIR:-/tmp}/crosspoint-highlight-tests"
mkdir -p "$build_dir"
c++ -std=c++20 -Wall -Wextra -Werror -Wno-unused-parameter -fno-exceptions \
  -Itest/highlights/stubs -Itest/clipping_store/stubs -Itest/library_builder/stubs \
  -Isrc -Ilib/Memory \
  test/highlights/HighlightCaptureTest.cpp src/highlights/HighlightCapture.cpp \
  -o "$build_dir/highlights"
"$build_dir/highlights"
