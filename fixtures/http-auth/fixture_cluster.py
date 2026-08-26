#!/usr/bin/env python3
"""Lifecycle and TLS setup for the local AhoiBrowser HTTP-auth fixtures."""

from __future__ import annotations

import os
import ssl
import subprocess
import tempfile
import threading
from pathlib import Path
from typing import List, Mapping, Optional, TextIO, Tuple

from fixture_server import (
    CREDENTIALS,
    LOOPBACK_HOST,
    PLAIN_HTTP_SPACES,
    PRIMARY_SPACES,
    SECONDARY_SPACES,
    DigestNonceStore,
    FixtureHTTPServer,
    FixtureState,
    ProxyFixtureRequestHandler,
    SanitizedEventLog,
)


def generate_temporary_certificate(directory: Path) -> Tuple[Path, Path]:
    """Generate a short-lived localhost certificate using the system OpenSSL."""

    directory.mkdir(parents=True, exist_ok=True)
    certificate = directory / "localhost-cert.pem"
    private_key = directory / "localhost-key.pem"
    config = directory / "openssl-localhost.cnf"
    config.write_text(
        "[req]\n"
        "distinguished_name = dn\n"
        "prompt = no\n"
        "x509_extensions = v3_req\n"
        "[dn]\n"
        "CN = localhost\n"
        "[v3_req]\n"
        "basicConstraints = critical,CA:FALSE\n"
        "keyUsage = critical,digitalSignature,keyEncipherment\n"
        "extendedKeyUsage = serverAuth\n"
        "subjectAltName = @alt_names\n"
        "[alt_names]\n"
        "DNS.1 = localhost\n"
        "IP.1 = 127.0.0.1\n",
        encoding="utf-8",
    )
    command = [
        "openssl",
        "req",
        "-x509",
        "-newkey",
        "ec",
        "-pkeyopt",
        "ec_paramgen_curve:P-256",
        "-sha256",
        "-nodes",
        "-days",
        "2",
        "-keyout",
        str(private_key),
        "-out",
        str(certificate),
        "-config",
        str(config),
        "-extensions",
        "v3_req",
    ]
    try:
        subprocess.run(
            command,
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
    except FileNotFoundError as error:
        raise RuntimeError(
            "openssl is required to create the local TLS fixture"
        ) from error
    except subprocess.CalledProcessError as error:
        raise RuntimeError(
            "openssl certificate generation failed: %s" % error.stderr
        ) from error
    os.chmod(private_key, 0o600)
    return certificate, private_key


class FixtureCluster:
    """Lifecycle wrapper around all HTTP-auth fixture origins."""

    def __init__(
        self,
        *,
        runtime_directory: Optional[Path] = None,
        primary_port: int = 0,
        secondary_port: int = 0,
        cross_port: int = 0,
        http_port: int = 0,
        proxy_port: int = 0,
        log_stream: Optional[TextIO] = None,
    ) -> None:
        self._owned_tempdir: Optional[tempfile.TemporaryDirectory[str]] = None
        if runtime_directory is None:
            self._owned_tempdir = tempfile.TemporaryDirectory(
                prefix="ahoibrowser-http-auth-"
            )
            runtime_directory = Path(self._owned_tempdir.name)
        self.runtime_directory = runtime_directory
        self.runtime_directory.mkdir(parents=True, exist_ok=True)
        self.certificate, self.private_key = generate_temporary_certificate(
            self.runtime_directory
        )
        self.event_log = SanitizedEventLog(log_stream)
        self.nonce_store = DigestNonceStore()
        self.cross_observations: List[bool] = []

        self._cross = FixtureHTTPServer(
            (LOOPBACK_HOST, cross_port),
            FixtureState(
                "cross",
                (),
                self.event_log,
                self.nonce_store,
                cross_observations=self.cross_observations,
            ),
        )
        self.cross_https_url = "https://%s:%d" % (
            LOOPBACK_HOST,
            self._cross.server_address[1],
        )
        self._primary = FixtureHTTPServer(
            (LOOPBACK_HOST, primary_port),
            FixtureState(
                "primary",
                PRIMARY_SPACES,
                self.event_log,
                self.nonce_store,
                cross_https_url=self.cross_https_url,
            ),
        )
        self._secondary = FixtureHTTPServer(
            (LOOPBACK_HOST, secondary_port),
            FixtureState(
                "secondary",
                SECONDARY_SPACES,
                self.event_log,
                self.nonce_store,
            ),
        )
        self._plain = FixtureHTTPServer(
            (LOOPBACK_HOST, http_port),
            FixtureState(
                "plain-http",
                PLAIN_HTTP_SPACES,
                self.event_log,
                self.nonce_store,
            ),
        )
        self._proxy = FixtureHTTPServer(
            (LOOPBACK_HOST, proxy_port),
            FixtureState(
                "proxy",
                (),
                self.event_log,
                self.nonce_store,
            ),
            ProxyFixtureRequestHandler,
        )
        self._servers = (
            self._primary,
            self._secondary,
            self._cross,
            self._plain,
            self._proxy,
        )
        self._threads: List[threading.Thread] = []

        tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls_context.load_cert_chain(str(self.certificate), str(self.private_key))
        for server in (self._primary, self._secondary, self._cross):
            server.socket = tls_context.wrap_socket(server.socket, server_side=True)

    @property
    def primary_https_url(self) -> str:
        return "https://%s:%d" % (LOOPBACK_HOST, self._primary.server_address[1])

    @property
    def secondary_https_url(self) -> str:
        return "https://%s:%d" % (LOOPBACK_HOST, self._secondary.server_address[1])

    @property
    def plain_http_url(self) -> str:
        return "http://%s:%d" % (LOOPBACK_HOST, self._plain.server_address[1])

    @property
    def proxy_url(self) -> str:
        return "http://%s:%d" % (LOOPBACK_HOST, self._proxy.server_address[1])

    def start(self) -> "FixtureCluster":
        if self._threads:
            return self
        for server in self._servers:
            thread = threading.Thread(
                target=server.serve_forever,
                name="http-auth-fixture-%s" % server.fixture_state.role,
                daemon=True,
            )
            thread.start()
            self._threads.append(thread)
        return self

    def stop(self) -> None:
        shutdown_threads = [
            threading.Thread(target=server.shutdown, daemon=True)
            for server in self._servers
        ]
        for thread in shutdown_threads:
            thread.start()
        for thread in shutdown_threads:
            thread.join(timeout=5)
        for server in self._servers:
            server.server_close()
        for thread in self._threads:
            thread.join(timeout=5)
        self._threads.clear()
        for path in (
            self.private_key,
            self.certificate,
            self.runtime_directory / "openssl-localhost.cnf",
        ):
            try:
                path.unlink()
            except FileNotFoundError:
                pass
        if self._owned_tempdir is not None:
            self._owned_tempdir.cleanup()
            self._owned_tempdir = None

    def describe(self) -> Mapping[str, object]:
        return {
            "primary_https_url": self.primary_https_url,
            "secondary_https_url": self.secondary_https_url,
            "cross_https_url": self.cross_https_url,
            "plain_http_url": self.plain_http_url,
            "proxy_url": self.proxy_url,
            "certificate": str(self.certificate),
            "credentials": CREDENTIALS,
            "note": "All credentials are synthetic and local-test-only.",
        }

    def __enter__(self) -> "FixtureCluster":
        return self.start()

    def __exit__(self, *_args: object) -> None:
        self.stop()
