#!/usr/bin/env bash
# ruview-server.sh — run RuView's sensing server (Docker) as the sink for the Atech node.
#
#   ./ruview-server.sh            start (web UI http://localhost:3000, UDP sink :5005)
#   ./ruview-server.sh stop
#   ./ruview-server.sh logs
#
# Point the board at this machine's LAN IP:
#   atech send csi_set_sink '{"ip":"<this-machine-ip>","port":5005}'
set -euo pipefail
NAME=ruview-sink
IMAGE="${RUVIEW_IMAGE:-ruvnet/wifi-densepose:latest}"
case "${1:-start}" in
  start)
    docker rm -f "$NAME" >/dev/null 2>&1 || true
    # The image refuses to start unauthenticated on 0.0.0.0 (RuView#864); keep a
    # per-machine API token under ~/.atech so the UI/WebSocket need it.
    TOKEN_FILE="${ATECH_HOME:-$HOME/.atech}/ruview_token"
    mkdir -p "$(dirname "$TOKEN_FILE")"
    [ -s "$TOKEN_FILE" ] || (openssl rand -hex 32 2>/dev/null || head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n') > "$TOKEN_FILE"
    # The server binds its UDP data plane to loopback unless told otherwise
    # (ADR-296), which a Docker port mapping cannot reach. Bind it to all
    # interfaces and allow only private LAN sources (override RUVIEW_UDP_ALLOW).
    ALLOW="${RUVIEW_UDP_ALLOW:-192.168.0.0/16,10.0.0.0/8,172.16.0.0/12}"
    docker run -d --name "$NAME" -p 3000:3000 -p 3001:3001 -p 5005:5005/udp \
      -e RUVIEW_API_TOKEN="$(cat "$TOKEN_FILE")" -e CSI_SOURCE=esp32 "$IMAGE" \
      --udp-bind 0.0.0.0 --udp-allow "$ALLOW" >/dev/null
    echo "RuView sensing server up: http://localhost:3000  (UDP sink :5005)"
    echo "API token (also in $TOKEN_FILE): $(cat "$TOKEN_FILE")"
    echo "this machine's addresses: $(hostname -I 2>/dev/null || ipconfig getifaddr en0 2>/dev/null || true)"
    ;;
  stop)  docker rm -f "$NAME" >/dev/null && echo "stopped" ;;
  logs)  docker logs -f "$NAME" ;;
  *)     echo "usage: $0 [start|stop|logs]" >&2; exit 64 ;;
esac
