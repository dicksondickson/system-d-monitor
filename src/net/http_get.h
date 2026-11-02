#pragma once
#include <stddef.h>
#include <stdint.h>

// Minimal HTTP/1.0 GET that streams the body to a callback (spec.md §4.2):
// HTTP/1.0 + Connection: close means no chunked encoding, and nothing is
// buffered beyond 512 bytes at a time.
namespace http {

enum class Err : uint8_t { OK, DNS, CONNECT, TIMEOUT, STATUS, BAD_RESPONSE };

struct Result {
  Err err = Err::OK;
  int status = 0;        // HTTP status code
  char server[24] = "";  // "Server:" response header
  char ip[16] = "";      // the address the host name resolved to
  size_t bytes = 0;      // body bytes delivered
  uint32_t ms = 0;       // total time
};

using Sink = void (*)(const char* data, size_t len, void* ctx);

Result get(const char* host, uint16_t port, const char* path, Sink sink, void* ctx,
           uint32_t connect_ms, uint32_t read_ms);

// Short reason for the footer: "dns dgx042.lan", "refused", "timeout", "http 503", …
void describe(const Result& r, const char* host, char* out, size_t len);

}  // namespace http
