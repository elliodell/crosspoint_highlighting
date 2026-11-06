#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build_dir="${TMPDIR:-/tmp}/crosspoint-readwise-sync-tests"
json_dir="${ARDUINOJSON_DIR:-.pio/libdeps/x4pro/ArduinoJson/src}"
mkdir -p "$build_dir"
crypto_lib=""
if [ "$(uname -s)" != "Darwin" ]; then crypto_lib="-lcrypto"; fi
c++ -std=c++20 -Wno-deprecated-declarations -Wall -Wextra -Werror -Wno-unused-parameter -fno-exceptions \
  -I"$json_dir" -Itest/readwise_sync/stubs -Itest/library_builder/stubs \
  -Isrc -Ilib/Memory test/readwise_sync/ReadwiseSyncTest.cpp src/highlights/ReadwisePlan.cpp $crypto_lib -o "$build_dir/readwise-sync"
"$build_dir/readwise-sync"
