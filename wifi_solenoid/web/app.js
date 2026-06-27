(function () {
  const POLL_MS = 3000;
  const STATUS_RETRY_ATTEMPTS = 4;
  const STATUS_RETRY_DELAY_MS = 700;
  const STATUS_OFFLINE_AFTER = 3;

  let pending = { z1: '0', z2: '0' };
  let saving = false;
  let lastServerZones = null;
  let statusPollInFlight = false;
  let statusOfflineStreak = 0;

  function chipHtml(label, on) {
    const cls = on ? 'chip-on' : 'chip-off';
    const word = on ? 'On' : 'Off';
    return '<div class="chip ' + cls + '"><b>' + label + '</b>' + word + '</div>';
  }

  function formatAge(seconds) {
    const s = parseInt(seconds, 10);
    if (isNaN(s) || s < 0) {
      return '';
    }
    if (s < 60) {
      return s + 's ago';
    }
    if (s < 3600) {
      const m = Math.floor(s / 60);
      return m === 1 ? '1 min ago' : m + ' min ago';
    }
    const h = Math.floor(s / 3600);
    const rem = Math.floor((s % 3600) / 60);
    if (rem === 0) {
      return h === 1 ? '1 hr ago' : h + ' hr ago';
    }
    return h + ' hr ' + rem + ' min ago';
  }

  function formatReadTime(s) {
    let line = '';
    if (s.pressure_read_epoch) {
      line = new Date(s.pressure_read_epoch * 1000).toLocaleTimeString([], {
        hour: 'numeric',
        minute: '2-digit'
      });
    }
    if (s.pressure_read_seconds_ago != null) {
      const age = formatAge(s.pressure_read_seconds_ago);
      line = line ? age + ' · ' + line : age;
    }
    if (s.pressure_stale) {
      line = line ? line + ' · stale' : 'stale';
    }
    return line;
  }

  function renderPressure(s) {
    const panel = document.getElementById('pressure-panel');
    const val = document.getElementById('pressure-val');
    const meta = document.getElementById('pressure-meta');
    if (!panel || !val || !meta) {
      return;
    }

    if (!s.pressure_enabled) {
      panel.classList.add('hidden');
      return;
    }
    panel.classList.remove('hidden');

    if (!s.pressure_valid && !s.pressure_stale) {
      val.textContent = '—';
      meta.textContent = s.pressure_error || 'No reading yet';
      meta.classList.toggle('stale', false);
      return;
    }

    if (s.pressure_psi != null) {
      const prefix = s.pressure_stale ? '~' : '';
      val.textContent = prefix + s.pressure_psi + ' psi';
    } else {
      val.textContent = '—';
    }

    meta.textContent = formatReadTime(s);
    meta.classList.toggle('stale', !!s.pressure_stale);
  }

  function renderChips(s) {
    const z = s.zones || {};
    document.getElementById('chips').innerHTML =
      chipHtml('Zone 1', z.z1 === 'on') +
      chipHtml('Zone 2', z.z2 === 'on');
  }

  function renderControls(s) {
    const z = s.zones || {};
    pending.z1 = z.z1 === 'on' ? '1' : '0';
    pending.z2 = z.z2 === 'on' ? '1' : '0';
    lastServerZones = { z1: z.z1, z2: z.z2 };

    document.getElementById('controls').innerHTML =
      '<form id="zone-form">' +
      '<div class="ctrl-row"><label for="zone1">Zone 1</label>' +
      '<select id="zone1" name="zone1" aria-label="Zone 1">' +
      '<option value="0">Off</option><option value="1">On</option></select></div>' +
      '<div class="ctrl-row"><label for="zone2">Zone 2</label>' +
      '<select id="zone2" name="zone2" aria-label="Zone 2">' +
      '<option value="0">Off</option><option value="1">On</option></select></div>' +
      '<input class="btn" type="submit" value="Update zones">' +
      '<div class="status-msg" id="status-msg"></div>' +
      '</form>';

    document.getElementById('zone1').value = pending.z1;
    document.getElementById('zone2').value = pending.z2;

    document.getElementById('zone-form').addEventListener('submit', function (e) {
      e.preventDefault();
      applyZones();
    });
  }

  function setMessage(text, isError) {
    const el = document.getElementById('status-msg');
    if (!el) return;
    el.textContent = text || '';
    el.classList.toggle('err', !!isError);
  }

  function applyZones() {
    if (saving) return;
    const z1 = document.getElementById('zone1').value;
    const z2 = document.getElementById('zone2').value;
    saving = true;
    setMessage('Updating…', false);
    document.querySelector('.btn').disabled = true;

    fetch('/set_zone', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'zone1=' + encodeURIComponent(z1) + '&zone2=' + encodeURIComponent(z2)
    })
      .then(function (r) {
        if (!r.ok) throw new Error('request failed');
        setMessage('Zones updated', false);
        return refresh();
      })
      .catch(function () {
        setMessage('Update failed', true);
      })
      .finally(function () {
        saving = false;
        const btn = document.querySelector('.btn');
        if (btn) btn.disabled = false;
      });
  }

  function renderStatus(s) {
    renderPressure(s);
    renderChips(s);
    const z = s.zones || {};
    if (!document.getElementById('zone-form')) {
      renderControls(s);
    } else if (!saving) {
      if (!lastServerZones || lastServerZones.z1 !== z.z1 || lastServerZones.z2 !== z.z2) {
        document.getElementById('zone1').value = z.z1 === 'on' ? '1' : '0';
        document.getElementById('zone2').value = z.z2 === 'on' ? '1' : '0';
        lastServerZones = { z1: z.z1, z2: z.z2 };
      }
    }
  }

  function fetchStatus() {
    return fetch('/status').then(function (r) {
      if (!r.ok) {
        throw new Error('status');
      }
      return r.json();
    });
  }

  function fetchStatusWithRetry(maxAttempts, delayMs) {
    function attempt(tryNum) {
      return fetchStatus().catch(function () {
        if (tryNum < maxAttempts) {
          return new Promise(function (resolve) {
            setTimeout(resolve, delayMs);
          }).then(function () {
            return attempt(tryNum + 1);
          });
        }
        throw new Error('offline');
      });
    }
    return attempt(1);
  }

  function renderOffline() {
    document.getElementById('chips').innerHTML =
      '<div class="chip chip-off" style="grid-column:span 2"><b>Status</b>Offline</div>';
  }

  function refresh() {
    if (saving || statusPollInFlight) {
      return Promise.resolve();
    }
    statusPollInFlight = true;
    return fetchStatusWithRetry(STATUS_RETRY_ATTEMPTS, STATUS_RETRY_DELAY_MS)
      .then(function (s) {
        statusOfflineStreak = 0;
        renderStatus(s);
      })
      .catch(function () {
        statusOfflineStreak++;
        if (statusOfflineStreak >= STATUS_OFFLINE_AFTER) {
          renderOffline();
        }
      })
      .finally(function () {
        statusPollInFlight = false;
      });
  }

  refresh();
  setInterval(refresh, POLL_MS);
})();
