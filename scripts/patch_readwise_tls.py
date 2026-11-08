"""Make the pinned SecureNet CA-verifying path enforce the requested hostname.

Upstream intentionally uses insecure mode for its existing callers. Readwise
uses setCACert(), so its token requires both chain and hostname verification.
See https://www.wolfssl.com/documentation/manuals/wolfssl/chapter07.html.
"""

from pathlib import Path

Import("env")

source = Path(env.subst("$PROJECT_DIR")) / "freeink-sdk/libs/network/SecureNet/src/SecureClient.cpp"
text = source.read_text()
marker = "// CrossPoint Highlighting: require authenticated TLS peers."

if marker not in text:
    replacements = [
        (
            "void SecureClient::setCACert(const char* rootCA) { _rootCA = rootCA; }",
            "void SecureClient::setCACert(const char* rootCA) { _rootCA = rootCA; _insecure = false; }",
        ),
        (
            """  } else if (_rootCA) {
    wolfSSL_CTX_load_verify_buffer(ctx, reinterpret_cast<const unsigned char*>(_rootCA),
                                   strlen(_rootCA), WOLFSSL_FILETYPE_PEM);
  }
""",
            """  } else {
    // CrossPoint Highlighting: require authenticated TLS peers.
    wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER, nullptr);
    if (!_rootCA || wolfSSL_CTX_load_verify_buffer(ctx, reinterpret_cast<const unsigned char*>(_rootCA),
                                                   strlen(_rootCA), WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS) {
      stop();
      return 0;
    }
  }
""",
        ),
        (
            "  _ssl = ssl;\n  wolfSSL_SetIOReadCtx(ssl, &_transport);",
            """  _ssl = ssl;
  if (!_insecure && wolfSSL_check_domain_name(ssl, host) != WOLFSSL_SUCCESS) {
    stop();
    return 0;
  }
  wolfSSL_SetIOReadCtx(ssl, &_transport);""",
        ),
    ]
    for old, new in replacements:
        if text.count(old) != 1:
            raise RuntimeError("SecureNet TLS patch no longer matches; inspect the SDK before building")
        text = text.replace(old, new, 1)
    source.write_text(text)
    print("Patched SecureNet authenticated TLS path")

# A partly applied or removed patch must fail the build, not send a token.
for required in (
    "wolfSSL_check_domain_name(ssl, host) != WOLFSSL_SUCCESS",
    "wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER, nullptr)",
    "if (!_rootCA || wolfSSL_CTX_load_verify_buffer",
    "_rootCA = rootCA; _insecure = false;",
):
    if required not in text:
        raise RuntimeError("SecureNet authenticated TLS checks are missing")

# A preconnected non-idempotent POST must not be transparently repeated when
# its response is lost. Existing SDK users retain their previous retry policy.
header = source.parent.parent / "include/SecureHttpClient.h"
http = header.read_text()
retry_marker = "void setRetryStaleConnections(bool retry)"
if retry_marker not in http:
    replacements = [
        ("  void setReuse(bool reuse) { _reuse = reuse; }",
         "  void setReuse(bool reuse) { _reuse = reuse; }\n"
         "  void setRetryStaleConnections(bool retry) { _retryStaleConnections = retry; }"),
        ("  bool _reuse = true;", "  bool _reuse = true;\n  bool _retryStaleConnections = true;"),
        ("if (reusing && attempt == 0) continue;", "if (_retryStaleConnections && reusing && attempt == 0) continue;"),
        ("if (reusing && attempt == 0 && !_aborted) continue;",
         "if (_retryStaleConnections && reusing && attempt == 0 && !_aborted) continue;"),
    ]
    for old, new in replacements:
        if http.count(old) != 1:
            raise RuntimeError("SecureNet retry patch no longer matches; inspect the SDK before building")
        http = http.replace(old, new, 1)
    header.write_text(http)
    print("Patched SecureNet optional stale-connection retry")
if (http.count("if (_retryStaleConnections && reusing && attempt == 0") != 2
        or "if (reusing && attempt == 0" in http
        or "bool _retryStaleConnections = true;" not in http):
    raise RuntimeError("SecureNet explicit retry policy is missing")
