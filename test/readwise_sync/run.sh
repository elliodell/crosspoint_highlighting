#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build_dir="${TMPDIR:-/tmp}/crosspoint-readwise-sync-tests"
json_dir="${ARDUINOJSON_DIR:-.pio/libdeps/x4pro/ArduinoJson/src}"
mkdir -p "$build_dir"
c++ -std=c++20 -Wall -Wextra -Werror -Wno-unused-parameter -fno-exceptions \
  -I"$json_dir" -Itest/readwise_sync/stubs -Itest/library_builder/stubs \
  -Isrc -Ilib/Memory test/readwise_sync/ReadwiseSyncTest.cpp -o "$build_dir/readwise-sync"
"$build_dir/readwise-sync"
