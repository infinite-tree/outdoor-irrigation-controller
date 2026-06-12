(function () {
  const POLL_MS = 3000;
  const STATUS_RETRY_ATTEMPTS = 4;
  const STATUS_RETRY_DELAY_MS = 700;
  const ACTION_POLL_MS = 500;
  const ACTION_MAX_WAIT_MS = 45000;
  let controlsRunning = null;
  let schedules = [];
  let nextTempId = 1;
  let actionInFlight = false;

  const ZONE_ROWS = [
    ['Zone 1', 'z1'],
    ['Zone 2', 'z2'],
    ['Greenhouse', 'gh'],
    ['Water cannon', 'wc']
  ];

  const ZONE_OPTIONS = [
    ['1', 'Zone 1'],
    ['2', 'Zone 2'],
    ['3', 'Greenhouse'],
    ['4', 'Zones 1+2'],
    ['5', 'Water cannon']
  ];

  const DAY_LABELS = ['Su', 'Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa'];

  function chipHtml(label, status) {
    const cls = status === 'on' ? 'chip-on' : status === 'error' ? 'chip-err' : 'chip-off';
    const word = status === 'on' ? 'On' : status === 'error' ? 'Error' : 'Off';
    return '<div class="chip ' + cls + '"><b>' + label + '</b>' + word + '</div>';
  }

  function pad2(n) {
    return n < 10 ? '0' + n : String(n);
  }

  function timeValue(hour, minute) {
    return pad2(hour) + ':' + pad2(minute);
  }

  function zoneOptionsHtml(selected) {
    let html = '';
    for (let i = 0; i < ZONE_OPTIONS.length; i++) {
      const val = ZONE_OPTIONS[i][0];
      const label = ZONE_OPTIONS[i][1];
      html += '<option value="' + val + '"' + (String(selected) === val ? ' selected' : '') + '>' + label + '</option>';
    }
    return html;
  }

  function setNowMain(text) {
    document.getElementById('now-main').textContent = text;
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

  function resetControls(running) {
    controlsRunning = null;
    renderControls(running);
  }

  function waitForStatusChange(expectRunning) {
    const busyMsg = expectRunning ? 'Starting watering…' : 'Stopping…';
    const deadline = Date.now() + ACTION_MAX_WAIT_MS;

    function poll() {
      return fetchStatusWithRetry(STATUS_RETRY_ATTEMPTS, STATUS_RETRY_DELAY_MS)
        .then(function (s) {
          if (!!s.timer_running === expectRunning) {
            actionInFlight = false;
            renderStatus(s);
            return;
          }
          setNowMain(busyMsg);
          if (Date.now() >= deadline) {
            actionInFlight = false;
            renderStatus(s);
            return;
          }
          return new Promise(function (resolve) {
            setTimeout(resolve, ACTION_POLL_MS);
          }).then(poll);
        })
        .catch(function () {
          setNowMain(busyMsg);
          if (Date.now() >= deadline) {
            actionInFlight = false;
            setNowMain('Offline');
            resetControls(!expectRunning);
            throw new Error('offline');
          }
          return new Promise(function (resolve) {
            setTimeout(resolve, ACTION_POLL_MS);
          }).then(poll);
        });
    }

    return poll();
  }

  function submitControlForm(form, running) {
    const btn = form.querySelector('[type=submit]');
    const isStop = running;
    const busyLabel = isStop ? 'Stopping…' : 'Starting…';

    if (btn) {
      btn.disabled = true;
      btn.value = busyLabel;
    }
    actionInFlight = true;
    setNowMain(isStop ? 'Stopping…' : 'Starting watering…');

    return fetch(isStop ? '/stop' : '/start', {
      method: 'POST',
      body: isStop ? null : new FormData(form),
      redirect: 'manual'
    })
      .then(function (r) {
        if (r.status === 302 || r.status === 0 || r.ok) {
          return waitForStatusChange(!isStop);
        }
        return r.text().then(function (text) {
          throw new Error(text || 'Request failed');
        });
      })
      .catch(function (err) {
        actionInFlight = false;
        setNowMain(err.message || 'Request failed');
        resetControls(running);
      });
  }

  function bindControlForm(el, running) {
    const form = el.querySelector('form');
    if (!form) {
      return;
    }
    form.addEventListener('submit', function (e) {
      e.preventDefault();
      submitControlForm(form, running);
    });
  }

  function renderControls(running) {
    if (controlsRunning === running) {
      return;
    }
    controlsRunning = running;

    const el = document.getElementById('controls');
    if (running) {
      el.innerHTML =
        '<form action="/stop" method="post">' +
        '<input class="btn btn-stop" type="submit" value="Stop watering">' +
        '</form>';
      bindControlForm(el, true);
      return;
    }
    el.innerHTML =
      '<form action="/start" method="post">' +
      '<div class="ctrl-row">' +
      '<input type="number" name="duration" min="1" value="10" inputmode="numeric" aria-label="Minutes">' +
      '<select name="zone" aria-label="What to water">' +
      zoneOptionsHtml('2') +
      '</select></div>' +
      '<input class="btn" type="submit" value="Start watering">' +
      '</form>';
    bindControlForm(el, false);
  }

  function renderLastTable(history) {
    const h = history || {};
    let html = '';
    for (let i = 0; i < ZONE_ROWS.length; i++) {
      const label = ZONE_ROWS[i][0];
      const key = ZONE_ROWS[i][1];
      const row = h[key] || { when: 'Never', duration: '—' };
      const muted = row.when === 'Never';
      html +=
        '<tr><th scope="row">' + label + '</th>' +
        '<td' + (muted ? ' class="muted"' : '') + '>' + row.when + '</td>' +
        '<td' + (muted ? ' class="muted"' : '') + '>' + row.duration + '</td></tr>';
    }
    document.getElementById('last-table').innerHTML = html;
  }

  function renderNextRun(next) {
    const el = document.getElementById('next-run');
    if (!next || !next.active) {
      el.classList.add('hidden');
      el.innerHTML = '';
      return;
    }
    el.classList.remove('hidden');
    el.innerHTML =
      '<b>Next scheduled</b>' +
      (next.when || '—') + ' · ' + (next.label || '');
  }

  function renderStatus(s) {
    document.getElementById('now-main').textContent = s.now_main || '—';
    document.getElementById('now-sub').textContent = s.now_sub || '';

    const solenoidAlert = document.getElementById('solenoid-alert');
    if (s.solenoid_error) {
      solenoidAlert.classList.remove('hidden');
    } else {
      solenoidAlert.classList.add('hidden');
    }

    const vfdAlert = document.getElementById('vfd-alert');
    if (s.vfd_error) {
      vfdAlert.classList.remove('hidden');
    } else {
      vfdAlert.classList.add('hidden');
    }

    const z = s.zones || {};
    document.getElementById('chips').innerHTML =
      chipHtml('Z1', z.z1) +
      chipHtml('Z2', z.z2) +
      chipHtml('GH', z.gh) +
      chipHtml('WC', z.wc);

    renderNextRun(s.next_scheduled);
    renderLastTable(s.last_watering);
    renderControls(!!s.timer_running);
  }

  function setSchedMessage(text, isError) {
    const el = document.getElementById('sched-msg');
    if (!text) {
      el.classList.add('hidden');
      el.textContent = '';
      return;
    }
    el.classList.remove('hidden', 'ok', 'err');
    el.classList.add(isError ? 'err' : 'ok');
    el.textContent = text;
  }

  function defaultSchedule() {
    return {
      id: 0,
      _key: 'new-' + (nextTempId++),
      zone: 2,
      duration: 60,
      hour: 10,
      minute: 0,
      freq: 'weekly',
      interval_days: 2,
      weekdays: [0, 1, 1, 1, 1, 1, 0]
    };
  }

  function normalizeSchedule(raw) {
    const weekdays = Array.isArray(raw.weekdays) ? raw.weekdays.slice(0, 7) : [0, 0, 0, 0, 0, 0, 0];
    while (weekdays.length < 7) {
      weekdays.push(0);
    }
    return {
      id: raw.id || 0,
      _key: 'id-' + (raw.id || ('new-' + (nextTempId++))),
      zone: raw.zone || 2,
      duration: raw.duration || 10,
      hour: raw.hour || 0,
      minute: raw.minute || 0,
      freq: raw.freq === 'interval' ? 'interval' : 'weekly',
      interval_days: raw.interval_days || 2,
      weekdays: weekdays
    };
  }

  function scheduleCardHtml(sched, index) {
    const key = sched._key;
    const weeklyActive = sched.freq === 'weekly';
  const dayBoxes = DAY_LABELS.map(function (label, dayIndex) {
      const checked = sched.weekdays[dayIndex] ? ' checked' : '';
      return (
        '<label class="day-chip">' +
        '<input type="checkbox" data-field="weekday" data-day="' + dayIndex + '"' + checked + '>' +
        '<span>' + label + '</span></label>'
      );
    }).join('');

    return (
      '<article class="sched-card" data-key="' + key + '">' +
      '<div class="sched-row">' +
      '<label>Zone</label>' +
      '<select data-field="zone" aria-label="Zone">' + zoneOptionsHtml(sched.zone) + '</select>' +
      '</div>' +
      '<div class="sched-row">' +
      '<label>Minutes</label>' +
      '<input type="number" data-field="duration" min="1" inputmode="numeric" value="' + sched.duration + '" aria-label="Duration in minutes">' +
      '</div>' +
      '<div class="sched-row">' +
      '<label>Time</label>' +
      '<input type="time" data-field="time" value="' + timeValue(sched.hour, sched.minute) + '" aria-label="Start time">' +
      '</div>' +
      '<div class="freq-toggle" role="group" aria-label="Frequency type">' +
      '<button type="button" class="freq-btn' + (weeklyActive ? ' active' : '') + '" data-freq="weekly">Weekdays</button>' +
      '<button type="button" class="freq-btn' + (!weeklyActive ? ' active' : '') + '" data-freq="interval">Every N days</button>' +
      '</div>' +
      '<div class="freq-panel weekly-panel' + (weeklyActive ? '' : ' hidden') + '">' +
      '<div class="weekdays">' + dayBoxes + '</div>' +
      '</div>' +
      '<div class="freq-panel interval-panel' + (!weeklyActive ? '' : ' hidden') + '">' +
      '<div class="sched-row">' +
      '<label>Every</label>' +
      '<input type="number" data-field="interval_days" min="1" max="365" inputmode="numeric" value="' + sched.interval_days + '" aria-label="Days between runs">' +
      '</div>' +
      '</div>' +
      '<button type="button" class="btn-delete" data-action="delete">Delete schedule</button>' +
      '</article>'
    );
  }

  function renderScheduleList() {
    const list = document.getElementById('sched-list');
    if (!schedules.length) {
      list.innerHTML = '<p class="sched-empty">No schedules yet. Tap “Add schedule” to create one.</p>';
      return;
    }
    list.innerHTML = schedules.map(scheduleCardHtml).join('');
  }

  function readCard(card) {
    const key = card.getAttribute('data-key');
    const existing = schedules.find(function (s) { return s._key === key; }) || defaultSchedule();
    const timeParts = (card.querySelector('[data-field=time]').value || '00:00').split(':');
    const weekdays = [0, 0, 0, 0, 0, 0, 0];
    card.querySelectorAll('[data-field=weekday]').forEach(function (input) {
      const day = parseInt(input.getAttribute('data-day'), 10);
      weekdays[day] = input.checked ? 1 : 0;
    });
    const weeklyBtn = card.querySelector('.freq-btn[data-freq=weekly]');
    const freq = weeklyBtn.classList.contains('active') ? 'weekly' : 'interval';
    return {
      id: existing.id || 0,
      _key: key,
      zone: parseInt(card.querySelector('[data-field=zone]').value, 10),
      duration: parseInt(card.querySelector('[data-field=duration]').value, 10),
      hour: parseInt(timeParts[0], 10) || 0,
      minute: parseInt(timeParts[1], 10) || 0,
      freq: freq,
      interval_days: parseInt(card.querySelector('[data-field=interval_days]').value, 10) || 1,
      weekdays: weekdays
    };
  }

  function syncSchedulesFromDom() {
    const cards = document.querySelectorAll('.sched-card');
    schedules = Array.prototype.map.call(cards, readCard);
  }

  function loadSchedules() {
    return fetch('/schedules')
      .then(function (r) { return r.json(); })
      .then(function (data) {
        schedules = (data.schedules || []).map(normalizeSchedule);
        renderScheduleList();
      })
      .catch(function () {
        setSchedMessage('Could not load schedules', true);
      });
  }

  function saveSchedules() {
    syncSchedulesFromDom();
    const payload = schedules.map(function (s) {
      return {
        id: s.id || 0,
        zone: s.zone,
        duration: s.duration,
        hour: s.hour,
        minute: s.minute,
        freq: s.freq,
        interval_days: s.interval_days,
        weekdays: s.weekdays
      };
    });

    const btn = document.getElementById('save-schedules');
    btn.disabled = true;
    btn.textContent = 'Saving…';

    return fetch('/schedules', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })
      .then(function (r) {
        if (!r.ok) {
          return r.text().then(function (text) { throw new Error(text || 'Save failed'); });
        }
        return loadSchedules();
      })
      .then(function () {
        setSchedMessage('Schedules saved', false);
        refresh();
      })
      .catch(function (err) {
        setSchedMessage(err.message || 'Save failed', true);
      })
      .finally(function () {
        btn.disabled = false;
        btn.textContent = 'Save schedules';
      });
  }

  function switchTab(tabName) {
    document.querySelectorAll('.tab').forEach(function (btn) {
      const active = btn.getAttribute('data-tab') === tabName;
      btn.classList.toggle('active', active);
      btn.setAttribute('aria-selected', active ? 'true' : 'false');
    });
    document.querySelectorAll('.tab-panel').forEach(function (panel) {
      panel.classList.toggle('active', panel.id === 'panel-' + tabName);
    });
    if (tabName === 'schedules' && !schedules.length) {
      loadSchedules();
    }
  }

  function refresh() {
    if (actionInFlight) {
      return;
    }
    fetchStatusWithRetry(STATUS_RETRY_ATTEMPTS, STATUS_RETRY_DELAY_MS)
      .then(renderStatus)
      .catch(function () {
        setNowMain('Offline');
      });
  }

  document.querySelectorAll('.tab').forEach(function (btn) {
    btn.addEventListener('click', function () {
      switchTab(btn.getAttribute('data-tab'));
    });
  });

  document.getElementById('add-schedule').addEventListener('click', function () {
    syncSchedulesFromDom();
    schedules.push(defaultSchedule());
    renderScheduleList();
  });

  document.getElementById('save-schedules').addEventListener('click', saveSchedules);

  document.getElementById('sched-list').addEventListener('click', function (e) {
    const deleteBtn = e.target.closest('[data-action=delete]');
    if (deleteBtn) {
      const card = deleteBtn.closest('.sched-card');
      syncSchedulesFromDom();
      const key = card.getAttribute('data-key');
      schedules = schedules.filter(function (s) { return s._key !== key; });
      renderScheduleList();
      return;
    }

    const freqBtn = e.target.closest('.freq-btn');
    if (freqBtn) {
      const card = freqBtn.closest('.sched-card');
      card.querySelectorAll('.freq-btn').forEach(function (btn) {
        btn.classList.toggle('active', btn === freqBtn);
      });
      const weekly = freqBtn.getAttribute('data-freq') === 'weekly';
      card.querySelector('.weekly-panel').classList.toggle('hidden', !weekly);
      card.querySelector('.interval-panel').classList.toggle('hidden', weekly);
    }
  });

  refresh();
  setInterval(refresh, POLL_MS);
})();
