# drogon 1.9.14 regression: `Expect: 100-continue` requests can hang forever

**Symptom:** `POST /api/v1/auth/admin-login` (and potentially any request whose client
sends `Expect: 100-continue`) hangs with no response until the client times out. No
handler logs appear — the request never finishes parsing.

**Cause:** A behavior change between drogon **v1.9.13** and **v1.9.14** in
`lib/src/HttpRequestParser.cc` (`parseRequest`, commit `c79f2d3a`, PR #2560
"accept zero-length body with Expect: 100-continue").

## The change

v1.9.13:

```cpp
if (expect == "100-continue" && request_->getVersion() >= Version::kHttp11)
{
    if (remainContentLength_ == 0)
    {
        // error
        return -k400BadRequest;      // chunked framing lands here -> fast 400
    }
    else
    {
        // send "HTTP/1.1 100 Continue" interim response
    }
}
```

v1.9.14:

```cpp
if (expect == "100-continue" && request_->getVersion() >= Version::kHttp11)
{
    // zero-length body: nothing to negotiate, omit the interim response
    if (remainContentLength_ != 0)
    {
        // send "HTTP/1.1 100 Continue" interim response
    }
    // chunked framing falls through with NO interim response and NO error
}
```

`remainContentLength_` is only populated from `Content-Length`. For
`Transfer-Encoding: chunked` it stays `0` at this decision point (the parser is in
`kExpectChunkLen` and accumulates chunk sizes only as chunks arrive).

## Resulting behavior

| Request framing | `Expect: 100-continue` | v1.9.13 | v1.9.14 |
|---|---|---|---|
| `Content-Length: N > 0` | yes | 100 Continue, then body | same |
| `Content-Length: 0` | yes | **400** (wrong) | request proceeds (fixed by #2560) |
| **`Transfer-Encoding: chunked`** | **yes** | **400** (crude but answered) | **no interim response → deadlock → client timeout** |
| `Content-Length` **and** `Transfer-Encoding` | any | processed | **400** (PR #2550, request-smuggling hardening) |

The hang mechanism: RFC 9110 §10.1.3 clients that send `Expect: 100-continue`
wait for the interim `100 Continue` before transmitting the body. The server waits
for the body (`kExpectChunkLen`). Neither side moves → the request hangs until the
client gives up. Nothing is logged because the request never reaches routing.

This combination is exactly what some real clients produce — e.g. Java
`HttpURLConnection` with `setChunkedStreamingMode()` plus a body large enough to
trigger `Expect: 100-continue`, curl with chunked uploads, and proxies that
forward `Expect` while re-chunking the body.

## Confirming it

1. Rebuild the service against drogon **v1.9.13** and repeat the failing request —
   it should answer immediately (with 400 in the chunked case, which itself is a
   useful signal about what the client is sending).
2. Reproduce against a 1.9.14 server without any GNP code involved:

   ```bash
   printf 'hello' > /tmp/body.txt
   curl -v -X POST http://127.0.0.1:PORT/anything \
        -H 'Transfer-Encoding: chunked' -H 'Expect: 100-continue' \
        --data-binary @/tmp/body.txt
   ```

   On 1.9.14 this never gets a response; on 1.9.13 it gets `400` immediately.
3. Capture the request headers of the hanging call (`curl -v`, DevTools, or
   `tcpdump` at the service) and check for `Expect: 100-continue` together with
   chunked framing. If the request instead arrives with a plain
   `Content-Length` JSON body, this regression is NOT the cause and the search
   continues — but note that no code path in `AuthController::adminSignIn`,
   `UserService::validateAdminUserCredentials`, or `PermissionService::getUserPermissions`
   can hang by itself (all DB work goes through drogon's callback/resume bridge,
   and drogon wraps `Task<HttpResponsePtr>` handlers in try/catch).

## Fixes (until upstream ships one)

- **Stay on v1.9.13.** The CI workflow in this repo pins drogon to `v1.9.13`.
- **Or patch drogon's `HttpRequestParser.cc`** (one guard — "send the interim
  response whenever a body is still expected"):

  ```cpp
  if (expect == "100-continue" &&
      request_->getVersion() >= Version::kHttp11)
  {
-     if (remainContentLength_ != 0)
+     if (status_ != HttpRequestParseStatus::kGotAll)
      {
          ... // send 100 Continue
      }
  }
  ```

  This keeps the v1.9.14 zero-body fix and restores the interim response for
  chunked framing.

- **Or make the client/proxy stop sending `Expect: 100-continue` with chunked
  bodies** (curl: `-H 'Expect:'`; most reverse proxies can strip the header).

There is no application-level workaround: the request never finishes parsing, so
no route, filter, or advice ever sees it.

## Related notes

- `validateAdminUserCredentials` also gained `PasswordUtils::normalizeBcryptHash`
  (PHP-style `$2y$` hashes) and a catch-all that guarantees an error response —
  those are independent robustness fixes, not the hang cause.
