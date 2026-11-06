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
