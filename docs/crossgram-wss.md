# CrossGram Desktop: MTProto over WSS / Cloudflare CDN

This is a **native, opt-in build** of `stevessr/tdesktop`, not a runtime
patch injected from another repository. The normal Telegram Desktop build is
unaffected unless configured with `-D CROSSGRAM_WSS_BUILD=ON`.

## Install / configuration

Put `crossgram-wss.json` beside the executable (on macOS, next to the binary
under `Telegram.app/Contents/MacOS`), then start the CrossGram build.

```json
{
  "url": "wss://x.wmc.de5.net/mtproto",
  "rsa_public_key": "PASTE THE SERVER'S RSA-2048 PUBLIC PEM HERE",
  "dc_ids": [1, 2, 3, 4, 5]
}
```

The file must use the **public key of the running CrossGram server**, not a
locally generated key and never a private key. The CrossGram server stores its
key in `data/rsa-key.json`; export the `publicKeyPem` field. The server
deployment helper `deploy/generate-client-config.mjs` demonstrates how to
read this field; its TCP JSON format is NOT accepted by this native WSS client.
Keep newlines inside the PEM encoded using `\\n` in JSON.

The CrossGram client uses a separate `crossgram` data key by default, to
avoid loading Telegram authorization files. `-key=name` is prefixed with
`crossgram-`. Authorizations are cryptographically bound to the selected
WSS URL, RSA public key and DC mapping inside the encrypted account record.
Do not alter those values on an existing signed-in account. A mismatch,
missing config, unmapped DC, CDN-only DC or invalid RSA key fails closed and
does not dial Telegram endpoints. A fresh account is required to switch
servers. No automatic migration from the official Telegram account is provided.

## Wire format

The application retains native Telegram Desktop MTProto authorization and
obfuscated abridged framing. Only the socket transport changes:

```text
Client  --TLS 1.2+/WSS-->  Cloudflare HTTPS proxy :443
                              |
                         HTTPS origin :8443
                              |
                         nginx proxy_upgrade
                              |
                       CrossGram WebSocket /mtproto
                              |
                           MTProto
```

Binary WebSocket frames carry a continuous MTProto byte stream. WebSocket
message boundaries are **not** MTProto packet boundaries. TLS peer
certificate verification and SNI are mandatory, and no plain `ws://`, raw
MTProto TCP, MTProxy, protocol downgrade or edge-IP pinning is supported in
this build.

Connection timeout: 15 seconds. Stalled output: 30 seconds. Application
send and receive queues: 8 MiB each, with 128 KiB outbound chunks. The
upstream MTProto session remains responsible for reconnection and pings.
The logged error category never includes credentials or URL query parameters.

## Server / Cloudflare requirements

1. Enable the CrossGram MTProto plugin's **WebSocket** route, such as
   `wsPath: /mtproto` on the shared HTTP server. The service's
   `packages/mtproto/src/transport/ws-server.ts` implements the binary stream
   bridge. The existing TCP listener on 4430 is not an HTTP origin.
2. Terminate HTTPS using a reverse proxy with a valid certificate. In nginx,
   proxy only `location = /mtproto` to the *actual* internal WebSocket
   listener (for a shared route, e.g. `127.0.0.1:3140`) and forward the
   `Upgrade` and `Connection` headers. Keep the management `/api` route
   private. Do not expose a plaintext backend to the Internet.
3. Create an orange-cloud Cloudflare DNS record; enable WebSockets; use
   **Full (strict)** and the public edge port 443. An Origin Rule may change
   the TLS **origin** port to 8443. It does not encrypt plaintext origins,
   nor convert port 4430 MTProto TCP into HTTPS.

The URL `wss://x.wmc.de5.net/mtproto` is a deployment target example, not
proof the origin, proxy route or public key has been provisioned.

## Build and GitHub Actions

Native CMake: `-D CROSSGRAM_WSS_BUILD=ON` (requires Qt6 WebSockets).
Qt's Windows and Linux build recipes include the `qtwebsockets` module.
The standalone workflow `.github/workflows/crossgram-wss.yml` builds
Windows x64 and Linux x64 Debug artifacts separately on dispatch / PR,
and publishes them on tags named `crossgram-wss-v*`.

Repository **variables** (not secrets) for a deployable release:

- `CROSSGRAM_API_ID`, `CROSSGRAM_API_HASH`: real application API
  credentials. Test API credentials are allowed only for non-release CI;
  the tag release refuses to use them.
- `CROSSGRAM_RSA_PUBLIC_PEM_B64`: base64 of the **public** PEM from
  the deployed server. The release will fail if absent.
- `CROSSGRAM_WSS_URL`: optional deployment URL (defaults to the
  `x.wmc.de5.net` target above).

The build uses `scripts/crossgram_wss_config.py` to package the validated
public-key configuration. On non-release runs without this variable, it
produces a clearly marked `.example` file, **not** a login-ready client.
Do not place any server private key or session data in Actions variables.
