#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build_dir="${TMPDIR:-/tmp}/crosspoint-readwise-books-tests"
json_dir="${ARDUINOJSON_DIR:-.pio/libdeps/x4pro/ArduinoJson/src}"
mkdir -p "$build_dir"
crypto_lib=""
if [ "$(uname -s)" != "Darwin" ]; then crypto_lib="-lcrypto"; fi
c++ -std=c++20 -Wno-deprecated-declarations -Wall -Wextra -Werror -Wno-unused-parameter -fno-exceptions \
  -I"$json_dir" -Itest/readwise_books/stubs -Itest/readwise_sync/stubs -Itest/library_builder/stubs \
  -Isrc -Ilib/Memory test/readwise_books/ReadwiseBooksTest.cpp $crypto_lib -o "$build_dir/readwise-books"
"$build_dir/readwise-books"

# Apply the same guarded SDK patch as firmware builds before testing its real
# request loop. These generated SDK edits remain outside the committed tree.
python3 - <<'PY'
from pathlib import Path
class BuildEnvironment:
    def subst(self, value):
        assert value == '$PROJECT_DIR'
        return str(Path.cwd())
exec(compile(Path('scripts/patch_readwise_tls.py').read_text(), 'scripts/patch_readwise_tls.py', 'exec'),
     {'Import': lambda _: None, 'env': BuildEnvironment()})
PY
c++ -std=c++17 -Wall -Wextra -Werror -DFREEINK_NET_WOLFSSL=1 -fsanitize=address,undefined \
  -Ifreeink-sdk/libs/network/SecureNet/test/host/stubs -Ifreeink-sdk/libs/network/SecureNet/include \
  test/readwise_books/RetryTransportTest.cpp -o "$build_dir/retry-transport"
"$build_dir/retry-transport"
