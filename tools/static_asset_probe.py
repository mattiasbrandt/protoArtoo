#!/usr/bin/env python3
"""Byte-exact static-asset probe for the controller's HTTP server.

Fetches assets over a raw socket rather than through an HTTP client library, so
the evidence is the wire itself: how many chunks arrived, whether the chunked
terminator was ever sent, how long the transfer took, and how the transfer
ended. An HTTP client hides exactly the distinctions this has to make -- a
response that stopped after one chunk and one that completed normally both look
like "200 with a short body" from above.

Expected sizes come from the staged gzip image the firmware actually ships
(.pio/build/<env>/fsdata_gz, produced by tools/gzip_fsdata.py), so a served
length is compared against the bytes on the device's filesystem rather than
against the uncompressed source.

Usage:
    python3 tools/static_asset_probe.py --host 10.0.0.22
    python3 tools/static_asset_probe.py --host 10.0.0.22 --json report.json
"""

import argparse
import concurrent.futures
import gzip
import json
import os
import socket
import sys
import time

DEFAULT_ASSETS = [
    "/fw-version.json",
    "/status_stream.js",
    "/shell.js",
    "/web_api.js",
    "/index.html",
    "/app.js",
    "/style.css",
    "/rc.js",
    "/seq.js",
]

# Long enough that a stalled send hits the server's own five-second SO_SNDTIMEO
# and returns an answer, rather than the probe timing out first and reporting a
# client-side artifact as a server behaviour.
RECV_TIMEOUT_S = 20.0

# esp_http_server keeps the connection open regardless of the request's
# Connection header, so reading to end-of-socket would cost RECV_TIMEOUT_S on
# every healthy asset. Reading until the response is structurally complete --
# the chunked terminator, or Content-Length bytes -- is what makes a stall
# distinguishable from a keep-alive idle, and keeps a whole run to seconds.
CHUNK_TERMINATOR = b"0\r\n\r\n"


def staged_size(stage_dir, path):
    """Bytes of the shipped asset: the gzipped copy if one was staged, else raw."""
    name = path.lstrip("/")
    for candidate in (name + ".gz", name):
        full = os.path.join(stage_dir, candidate)
        if os.path.isfile(full):
            return os.path.getsize(full), os.path.basename(full)
    return None, None


def parse_chunks(body):
    """Walk chunked framing. Returns (chunk sizes, decoded bytes, terminated)."""
    sizes = []
    decoded = bytearray()
    i = 0
    while True:
        eol = body.find(b"\r\n", i)
        if eol < 0:
            return sizes, bytes(decoded), False
        try:
            size = int(body[i:eol].split(b";")[0], 16)
        except ValueError:
            return sizes, bytes(decoded), False
        i = eol + 2
        if size == 0:
            return sizes, bytes(decoded), True
        if i + size > len(body):
            # Body ended mid-chunk: the sender stopped without framing it.
            decoded += body[i:]
            sizes.append(len(body) - i)
            return sizes, bytes(decoded), False
        decoded += body[i:i + size]
        sizes.append(size)
        i += size + 2  # chunk data plus its trailing CRLF


def response_complete(raw):
    """True once the bytes so far hold a structurally complete response."""
    split = raw.find(b"\r\n\r\n")
    if split < 0:
        return False
    head = raw[:split].decode("latin-1").lower()
    body = raw[split + 4:]
    body_len = len(body)
    if "transfer-encoding: chunked" in head:
        # Tested against the body alone, never against the whole buffer: the
        # header block ends in "\r\n\r\n" too, and an ETag whose value happens
        # to end in '0' -- every asset whose gzipped size is a round hundred --
        # makes the last five header bytes identical to the chunk terminator.
        # Matching on those reported a healthy response as an empty one
        # whenever the headers arrived in a segment of their own.
        return body_len > 0 and body.endswith(CHUNK_TERMINATOR)
    for line in head.split("\r\n"):
        if line.startswith("content-length:"):
            try:
                return body_len >= int(line.split(":", 1)[1].strip())
            except ValueError:
                return False
    return False


def fetch(host, port, path):
    """One request on its own connection. Everything measured is on the wire."""
    request = (
        "GET {path} HTTP/1.1\r\n"
        "Host: {host}\r\n"
        "Accept-Encoding: gzip\r\n"
        "\r\n"
    ).format(path=path, host=host).encode("ascii")

    result = {"path": path}
    started = time.monotonic()
    raw = bytearray()
    sock = socket.create_connection((host, port), timeout=RECV_TIMEOUT_S)
    try:
        sock.settimeout(RECV_TIMEOUT_S)
        sock.sendall(request)
        result["ending"] = "socket-closed"
        while True:
            piece = sock.recv(4096)
            if not piece:
                break
            raw += piece
            if response_complete(raw):
                result["ending"] = "complete"
                break
    except socket.timeout:
        result["ending"] = "probe-timeout"
    except OSError as exc:
        result["ending"] = "socket-error: {0}".format(exc)
    finally:
        sock.close()

    result["elapsedS"] = round(time.monotonic() - started, 2)
    result["rawBytes"] = len(raw)

    split = raw.find(b"\r\n\r\n")
    if split < 0:
        # No complete header block: the connection was refused, reset or shed
        # before the response started. Reported as its own outcome rather than
        # as a zero-length body, which would read as a served-but-empty file.
        result["status"] = None
        result["servedBytes"] = 0
        result["chunks"] = None
        result["chunkTerminated"] = None
        result["gunzipBytes"] = None
        result["noResponse"] = True
        return result

    head = raw[:split].decode("latin-1")
    body = bytes(raw[split + 4:])
    lines = head.split("\r\n")
    result["status"] = lines[0]
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            key, value = line.split(":", 1)
            headers[key.strip().lower()] = value.strip()
    result["contentEncoding"] = headers.get("content-encoding")
    result["transferEncoding"] = headers.get("transfer-encoding")
    result["contentLength"] = headers.get("content-length")

    if headers.get("transfer-encoding", "").lower() == "chunked":
        sizes, decoded, terminated = parse_chunks(body)
        result["chunks"] = sizes
        result["chunkTerminated"] = terminated
    else:
        decoded = body
        result["chunks"] = None
        result["chunkTerminated"] = None
    result["servedBytes"] = len(decoded)

    # A byte count matching is necessary but not sufficient: the failure this
    # probe exists for produced correct headers over a body the browser could
    # not decode. Decompressing is what proves the payload is usable.
    if result["contentEncoding"] == "gzip":
        try:
            result["gunzipBytes"] = len(gzip.decompress(decoded))
        except (OSError, EOFError, gzip.BadGzipFile) as exc:
            result["gunzipBytes"] = None
            result["gunzipError"] = str(exc)
    else:
        result["gunzipBytes"] = None
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="10.0.0.22")
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--env", default="artoo_esp32",
                        help="PlatformIO env whose staged fsdata_gz holds the shipped sizes")
    parser.add_argument("--asset", action="append", dest="assets",
                        help="Asset path to probe; repeatable. Defaults to the size ladder.")
    parser.add_argument("--json", help="Write the full report here")
    parser.add_argument("--gap", type=float, default=0.5,
                        help="Seconds between requests, so one probe does not measure the last one")
    parser.add_argument("--concurrent", type=int, default=0, metavar="N",
                        help="Fetch every asset over N simultaneous connections, the way a "
                             "browser opens a page, instead of one at a time")
    parser.add_argument("--rounds", type=int, default=1,
                        help="Repeat the whole sweep this many times")
    args = parser.parse_args()

    stage_dir = os.path.join(".pio", "build", args.env, "fsdata_gz")
    if not os.path.isdir(stage_dir):
        print("no staged image at {0}; run `pio run -e {1}` first".format(stage_dir, args.env),
              file=sys.stderr)
        return 2

    assets = args.assets or DEFAULT_ASSETS

    def probe(path):
        expected, staged_name = staged_size(stage_dir, path)
        row = fetch(args.host, args.port, path)
        row["expectedBytes"] = expected
        row["stagedFile"] = staged_name
        if expected is None:
            row["verdict"] = "unknown-asset"
        elif row.get("noResponse"):
            row["verdict"] = "no-response"
        elif row["servedBytes"] == 0:
            row["verdict"] = "empty"
        elif row["servedBytes"] != expected or row.get("chunkTerminated") is False:
            row["verdict"] = "truncated"
        elif row.get("gunzipError"):
            row["verdict"] = "corrupt"
        else:
            row["verdict"] = "ok"
        return row

    rows = []
    for round_index in range(args.rounds):
        if args.concurrent > 0:
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrent) as pool:
                batch = list(pool.map(probe, assets))
        else:
            batch = []
            for path in assets:
                batch.append(probe(path))
                time.sleep(args.gap)
        for row in batch:
            row["round"] = round_index
        rows += batch

    header = "{0:<22}{1:>10}{2:>10}{3:>8}{4:>8}  {5}"
    print(header.format("asset", "expected", "served", "time", "chunks", "verdict"))
    for row in rows:
        print(header.format(
            row["path"],
            row["expectedBytes"] if row["expectedBytes"] is not None else "?",
            row["servedBytes"],
            "{0}s".format(row["elapsedS"]),
            len(row["chunks"]) if row["chunks"] is not None else "-",
            "{0} ({1})".format(row["verdict"], row["ending"]),
        ))

    failures = [r for r in rows if r["verdict"] not in ("ok", "unknown-asset")]
    mode = "{0} concurrent connections".format(args.concurrent) if args.concurrent else "sequential"
    print("\n{0} of {1} fetches served whole and gunzipped, {2}, {3} round(s)".format(
        len(rows) - len(failures), len(rows), mode, args.rounds))

    if args.json:
        with open(args.json, "w") as handle:
            json.dump({"host": args.host, "env": args.env, "concurrent": args.concurrent,
                       "rounds": args.rounds, "assets": rows}, handle, indent=2)
        print("report: {0}".format(args.json))

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
