#!/usr/bin/env bash
set -eu

if [ -n "${CLAUDE_ENV_FILE:-}" ]; then
    OTA_HOST_DEFAULT="${PROTOARTOO_OTA_HOST:-artoo.local}"
    OTA_IP_RESOLVED=""

    if command -v getent >/dev/null 2>&1; then
        OTA_IP_RESOLVED="$(getent ahostsv4 "$OTA_HOST_DEFAULT" | awk 'NR==1 {print $1}')"
    elif command -v avahi-resolve-host-name >/dev/null 2>&1; then
        OTA_IP_RESOLVED="$(avahi-resolve-host-name "$OTA_HOST_DEFAULT" 2>/dev/null | awk '{print $2}')"
    fi

    echo "export OTA_HOST=$OTA_HOST_DEFAULT" >> "$CLAUDE_ENV_FILE"
    if [ -n "$OTA_IP_RESOLVED" ]; then
        echo "export OTA_IP=$OTA_IP_RESOLVED" >> "$CLAUDE_ENV_FILE"
    fi
    echo 'export UPLOAD_PORT=/dev/ttyUSB0' >> "$CLAUDE_ENV_FILE"

    # Probe running firmware version so agents can compare against the build
    # without the operator running curl manually each session.
    DEVICE_STATUS=""
    if [ -n "$OTA_IP_RESOLVED" ]; then
        DEVICE_STATUS="$(curl -s --max-time 2 "http://$OTA_IP_RESOLVED/api/status" 2>/dev/null || true)"
    fi
    if [ -n "$DEVICE_STATUS" ]; then
        FW_VER="$(printf '%s' "$DEVICE_STATUS" | python3 -c \
            "import sys,json; d=json.load(sys.stdin); print(d.get('firmwareVersion','?'))" \
            2>/dev/null || echo '?')"
        echo "export DEVICE_FW_VERSION=$FW_VER" >> "$CLAUDE_ENV_FILE"
        echo 'export DEVICE_REACHABLE=true' >> "$CLAUDE_ENV_FILE"
    else
        echo 'export DEVICE_REACHABLE=false' >> "$CLAUDE_ENV_FILE"
    fi
fi
