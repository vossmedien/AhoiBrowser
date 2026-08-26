# Ahoi HTTP-auth fixture

This loopback-only fixture covers two Basic realms on one host, multiple
accounts in one realm, nested protection paths, the same realm on different
ports and transports, Digest, a non-forwarding synthetic proxy challenge,
repeated `401`, same-origin and cross-origin redirects, password replacement,
and a subresource challenge.

Use a locally trusted certificate generated outside the repository (for
example with `mkcert 127.0.0.1 localhost`) and start:

```sh
python3 fixtures/http-auth/server.py \
  --cert /absolute/path/to/127.0.0.1+1.pem \
  --key /absolute/path/to/127.0.0.1+1-key.pem
```

The single readiness line contains ports only. `/receipts` stores timestamp,
transport, realm, path, outcome, and a truncated username hash. It never stores
or returns a password, `Authorization`, or `Proxy-Authorization` value.

All fixture account values are deliberately synthetic and are defined only in
`server.py`. Do not copy them into screenshots, test reports, or release logs.
