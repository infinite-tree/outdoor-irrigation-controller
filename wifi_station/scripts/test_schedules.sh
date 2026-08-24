#!/usr/bin/env bash
# Integration test for station schedule API.
set -euo pipefail

HOST="${1:-192.168.2.216}"
BASE="http://${HOST}"

log() { echo "[test] $*"; }
fail() { echo "[test] FAIL: $*" >&2; exit 1; }

log "target ${BASE}"

curl -sf --connect-timeout 5 "${BASE}/status" >/dev/null || fail "status unreachable"

STATUS="$(curl -sf --connect-timeout 5 "${BASE}/status")"
python3 - <<'PY' "$STATUS" || fail "status missing next_scheduled"
import json, sys
d = json.loads(sys.argv[1])
assert "next_scheduled" in d, "next_scheduled missing"
PY

EMPTY="$(curl -sf --connect-timeout 5 "${BASE}/schedules")"
python3 - <<'PY' "$EMPTY" || fail "initial schedules invalid"
import json, sys
d = json.loads(sys.argv[1])
assert "schedules" in d and isinstance(d["schedules"], list)
PY

PAYLOAD='[{"id":0,"zone":2,"duration":5,"hour":10,"minute":0,"freq":"weekly","interval_days":2,"weekdays":[0,1,1,1,1,1,0]}]'

PUT_CODE="$(curl -s -o /tmp/sched_put_resp.txt -w '%{http_code}' --connect-timeout 5 \
  -X PUT -H 'Content-Type: application/json' \
  -d "$PAYLOAD" "${BASE}/schedules")"
[[ "$PUT_CODE" == "200" ]] || fail "PUT schedules returned ${PUT_CODE}: $(cat /tmp/sched_put_resp.txt)"

LOADED="$(curl -sf --connect-timeout 5 "${BASE}/schedules")"
python3 - <<'PY' "$LOADED" || fail "schedules not saved"
import json, sys
d = json.loads(sys.argv[1])
assert len(d["schedules"]) == 1
s = d["schedules"][0]
assert s["zone"] == 2
assert s["duration"] == 5
assert s["freq"] == "weekly"
assert s["id"] > 0
PY

log "stress polling /status"
for i in $(seq 1 20); do
  curl -sf --connect-timeout 5 "${BASE}/status" >/dev/null || fail "status failed on poll ${i}"
  sleep 0.15
done

log "stress polling /schedules"
for i in $(seq 1 10); do
  curl -sf --connect-timeout 5 "${BASE}/schedules" >/dev/null || fail "schedules failed on poll ${i}"
  sleep 0.15
done

JS="$(curl -sf --connect-timeout 5 "${BASE}/app.js")"
[[ -n "$JS" ]] || fail "app.js empty"
echo "$JS" | grep -q 'saveCurrentSchedule' || fail "app.js missing save handler"
echo "$JS" | grep -q 'projectedSchedulesPayload' || fail "app.js missing schedule edit fix (stale cached firmware or old build)"
echo "$JS" | grep -q "chipHtml('RM'" || fail "app.js missing remote running chip"

HTML="$(curl -sf --connect-timeout 5 "${BASE}/")"
[[ -n "$HTML" ]] || fail "index empty"
echo "$HTML" | grep -q 'panel-schedules' || fail "index missing schedules tab"

STATUS2="$(curl -sf --connect-timeout 5 "${BASE}/status")"
python3 - <<'PY' "$STATUS2" || fail "next_scheduled inactive after save"
import json, sys
d = json.loads(sys.argv[1])
n = d.get("next_scheduled", {})
assert n.get("active") is True, n
assert "when" in n and "label" in n
PY

CLEAR_CODE="$(curl -s -o /tmp/sched_clear_resp.txt -w '%{http_code}' --connect-timeout 5 \
  -X PUT -H 'Content-Type: application/json' \
  -d '[]' "${BASE}/schedules")"
[[ "$CLEAR_CODE" == "200" ]] || fail "clear schedules returned ${CLEAR_CODE}"

CLEARED="$(curl -sf --connect-timeout 5 "${BASE}/schedules")"
python3 - <<'PY' "$CLEARED" || fail "schedules not cleared"
import json, sys
d = json.loads(sys.argv[1])
assert d["schedules"] == []
PY

log "all schedule tests passed"
