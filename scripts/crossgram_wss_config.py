#!/usr/bin/env python3
"""Emit the public-only runtime configuration for a CrossGram WSS build."""
import argparse
import base64
import ipaddress
import json
import os
import re
from pathlib import Path
from urllib.parse import urlsplit


def validate_url(value: str) -> str:
    if not value or len(value) > 4096 or re.search(r"[\s\\\x00-\x1f\x7f]", value):
        raise ValueError("invalid WSS URL characters")
    candidate = urlsplit(value)
    if (candidate.scheme != "wss" or candidate.username is not None
            or candidate.password is not None or "@" in candidate.netloc
            or candidate.fragment or not candidate.path.startswith("/")):
        raise ValueError("expected wss://DNS-host/path without credentials")
    host = candidate.hostname or ""
    if len(host) > 253 or "." not in host or not all(
            re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?", label)
            for label in host.split(".")):
        raise ValueError("expected a valid DNS hostname")
    try:
        ipaddress.ip_address(host)
    except ValueError:
        pass
    else:
        raise ValueError("literal IP addresses are disallowed")
    if candidate.port is not None and not 1 <= candidate.port <= 65535:
        raise ValueError("invalid port")
    return value


def public_pem(encoded: str) -> str:
    raw = base64.b64decode(encoded, validate=True).decode("ascii")
    if "PRIVATE KEY" in raw or not (
        ("-----BEGIN RSA PUBLIC KEY-----" in raw and "-----END RSA PUBLIC KEY-----" in raw)
        or ("-----BEGIN PUBLIC KEY-----" in raw and "-----END PUBLIC KEY-----" in raw)
    ):
        raise ValueError("use a PEM RSA public key, never a private key")
    return raw.strip() + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--require-public-key", action="store_true")
    args = parser.parse_args()

    endpoint = validate_url(os.getenv("CROSSGRAM_WSS_URL") or "wss://x.wmc.de5.net/mtproto")
    encoded = os.getenv("CROSSGRAM_RSA_PUBLIC_PEM_B64") or ""
    if not encoded and args.require_public_key:
        parser.error("CROSSGRAM_RSA_PUBLIC_PEM_B64 must be set for a public release")
    if encoded:
        content = {
            "url": endpoint,
            "rsa_public_key": public_pem(encoded),
            "dc_ids": [1, 2, 3, 4, 5],
        }
        path = args.output
    else:
        content = {
            "url": endpoint,
            "rsa_public_key": "REPLACE WITH THE CROSSGRAM SERVER RSA PUBLIC PEM",
            "dc_ids": [1, 2, 3, 4, 5],
        }
        path = args.output.with_name(args.output.name + ".example")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(content, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {path} (public key included: {bool(encoded)})")


if __name__ == "__main__":
    main()
