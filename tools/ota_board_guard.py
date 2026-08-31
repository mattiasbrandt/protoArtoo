"""Pre-flight board-identity guard for OTA pushes (#252, Finding 1).

``tools/ota_upload.py`` takes ``--host`` as a plain address and hands it
straight to espota; nothing on the push path ever asked the target device what
it actually is. ``user.mk`` assigns ``OTA_IP = 10.0.0.22`` (the artoo-esp32
controller) with ``=``, not ``?=``, so a Makefile default cannot out-rank it --
a bare ``make ota BUILD_ENV=firebeetle2`` silently pushes a P4 image at the
artoo board. This module is the guard: query the target's own
``GET /api/identity`` and refuse the push when the board it reports does not
match the board the ``--env`` builds for.

Fail-closed by construction. An unreachable host, a non-answering host, a
malformed response, or a response missing the ``board`` field are all refused
exactly like a genuine mismatch -- this guard exists to stop a destructive
push, so "I could not confirm the board" and "I confirmed the wrong board"
carry the same verdict. Every refusal raises ``OtaBoardGuardError``; there is
no code path that logs a doubt and proceeds anyway.
"""
from __future__ import annotations

import json
import urllib.error
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUDGETS_PATH = REPO_ROOT / "tools" / "build_budgets.json"

# The board identifiers /api/identity actually returns
# (src/web/api_identity_serializers.cpp:51-58) -- read, not assumed.
BOARD_ARTOO_ESP32 = "artoo_esp32"
BOARD_FIREBEETLE2 = "firebeetle2"

# docs/api.md and the boot-log evidence in #252 both show the dashboard HTTP
# server on port 80 (src/web/web_request_psychic.cpp:660); ArduinoOTA's own
# UDP 3232 answers no HTTP and is not what this guard queries.
DEFAULT_IDENTITY_PORT = 80
DEFAULT_IDENTITY_TIMEOUT_SECONDS = 5.0


class OtaBoardGuardError(RuntimeError):
    """The push must not proceed. Every raise site here is a refusal, not a warning."""


def expected_board_for_env(env_name: str, budgets_path: Path | None = None) -> str:
    """Which board does this PlatformIO env build for?

    Reads the same registry ``Makefile:46`` reads for ``PLATFORMIO_CORE_DIR``
    selection: ``tools/build_budgets.json`` -> ``platforms.esp32p4.envs``.
    That list is exact env names (it now includes ``firebeetle2_ota``, added
    by the slice that created it), so membership is a direct positive test for
    P4 -- no prefix-matching against the six artoo ``*_ota`` env names, which
    the coordinator brief rejected as fragile. ``platforms.esp32.envs`` is not
    populated (no P4-style registry exists for the artoo-esp32 side), so
    "not in the P4 list" is the else-branch for artoo-esp32, exactly as for
    ``P4_ENVS`` in the Makefile.
    """
    path = budgets_path or DEFAULT_BUDGETS_PATH
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError as error:
        raise OtaBoardGuardError(
            f"could not read {path} to determine which board {env_name!r} builds "
            f"for; refusing to push blind ({error})"
        ) from error
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as error:
        raise OtaBoardGuardError(
            f"{path} is not valid JSON; refusing to push blind ({error})"
        ) from error

    p4_envs = data.get("platforms", {}).get("esp32p4", {}).get("envs") or []
    if env_name in p4_envs:
        return BOARD_FIREBEETLE2
    return BOARD_ARTOO_ESP32


def fetch_reported_board(
    host: str,
    port: int = DEFAULT_IDENTITY_PORT,
    timeout_seconds: float = DEFAULT_IDENTITY_TIMEOUT_SECONDS,
) -> str:
    """What board does the host at the other end of this push say it is?

    Every failure mode -- refused connection, timeout, non-200, a body that is
    not JSON, a body that is not an object, a missing or non-string ``board``
    field -- raises ``OtaBoardGuardError`` rather than returning a sentinel.
    A sentinel here would be a second, quieter way for the guard to be
    bypassed by whatever calls this function next.
    """
    url = f"http://{host}:{port}/api/identity"
    request = urllib.request.Request(
        url, headers={"Accept": "application/json"}, method="GET"
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
            if response.status != 200:
                raise OtaBoardGuardError(
                    f"{url} returned HTTP {response.status}; refusing to push "
                    "blind -- confirm the host is the intended controller and "
                    "is serving normally before retrying"
                )
            body = response.read()
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        raise OtaBoardGuardError(
            f"could not reach {url} ({error}); refusing to push blind -- the "
            "board-identity check could not run, so which board would receive "
            "this image is unknown. Confirm the host is powered, on the "
            "network, and reachable at this address before retrying"
        ) from error

    try:
        payload = json.loads(body)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise OtaBoardGuardError(
            f"{url} did not return valid JSON; refusing to push blind ({error})"
        ) from error
    if not isinstance(payload, dict):
        raise OtaBoardGuardError(
            f"{url} did not return a JSON object; refusing to push blind"
        )

    board = payload.get("board")
    if not isinstance(board, str) or not board:
        raise OtaBoardGuardError(
            f"{url} response has no usable 'board' field; refusing to push "
            "blind -- this may be firmware old enough to predate board "
            "identity reporting, which is exactly the case this guard cannot "
            "vouch for"
        )
    return board


def enforce_board_match(
    env_name: str,
    host: str,
    port: int = DEFAULT_IDENTITY_PORT,
    timeout_seconds: float = DEFAULT_IDENTITY_TIMEOUT_SECONDS,
    budgets_path: Path | None = None,
) -> str:
    """Refuse the push unless the host confirms it is the board ``env_name`` builds for.

    Returns the confirmed board name on success, so the caller can print
    positive evidence rather than silence before a destructive action.
    """
    expected = expected_board_for_env(env_name, budgets_path)
    reported = fetch_reported_board(host, port, timeout_seconds)
    if reported != expected:
        raise OtaBoardGuardError(
            f"refusing to push: --env {env_name!r} builds firmware for board "
            f"{expected!r}, but {host} answered /api/identity as board "
            f"{reported!r}. Pushing anyway would flash the wrong board. Point "
            f"--host at the {expected} controller, or run this with an env "
            f"that builds for {reported!r} to match {host}."
        )
    return reported
