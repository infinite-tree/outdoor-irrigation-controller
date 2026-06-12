#!/usr/bin/env bash
# Start a timed watering run via the wifi_station HTTP API.
#
# Usage:
#   start_watering.sh <zone> <duration_minutes>
#
# Zone values (match the station web UI):
#   1  Zone 1
#   2  Zone 2
#   3  Greenhouse
#   4  Zones 1+2
#   5  Water cannon
#
# Environment:
#   STATION_HOST  Station IP or hostname (default: 192.168.2.219)
#   LOG_FILE      Log path (default: /var/log/irrigation.log)
#
# Example cron — Zone 2 for 1 hour at 10:00 and 16:00:
#   CRON_TZ=America/Los_Angeles
#   0 10 * * * /path/to/start_watering.sh 2 60
#   0 16 * * * /path/to/start_watering.sh 2 60

set -euo pipefail

STATION_HOST="${STATION_HOST:-192.168.2.219}"
LOG_FILE="${LOG_FILE:-/var/log/irrigation.log}"

usage() {
  sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

log() {
  echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG_FILE"
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage 0
fi

if [[ $# -ne 2 ]]; then
  echo "error: expected <zone> <duration_minutes>" >&2
  usage 1
fi

ZONE="$1"
DURATION_MINUTES="$2"

if ! [[ "$ZONE" =~ ^[1-5]$ ]]; then
  echo "error: zone must be 1–5 (got: $ZONE)" >&2
  exit 1
fi

if ! [[ "$DURATION_MINUTES" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: duration must be a positive integer (minutes)" >&2
  exit 1
fi

response=$(curl -sf -w '\n%{http_code}' -X POST \
  -d "duration=${DURATION_MINUTES}&zone=${ZONE}" \
  "http://${STATION_HOST}/start" 2>&1) || {
  log "zone=${ZONE} duration=${DURATION_MINUTES} ERROR: curl failed — $response"
  exit 1
}

http_code=$(echo "$response" | tail -n1)
body=$(echo "$response" | sed '$d')

case "$http_code" in
  302)
    log "zone=${ZONE} duration=${DURATION_MINUTES} OK: watering started"
    ;;
  409)
    log "zone=${ZONE} duration=${DURATION_MINUTES} SKIP: station already watering"
    ;;
  *)
    log "zone=${ZONE} duration=${DURATION_MINUTES} ERROR: HTTP $http_code — $body"
    exit 1
    ;;
esac
