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
  let savedSnapshot = '[]';
  let editingKey = null;
  let editingIsNew = false;

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

  function renderClock(clock) {
    const el = document.getElementById('now-clock');
    if (el) {
      el.textContent = clock || '—';
    }
  }

  function renderPressure(s) {
    const wrap = document.getElementById('now-pressure');
    const val = document.getElementById('now-pressure-val');
    if (!wrap || !val) {
      return;
    }
    wrap.classList.remove('pressure-alarm', 'pressure-stale');
    if ((s.pressure_valid || s.pressure_stale) && s.pressure_psi != null) {
      const prefix = s.pressure_stale ? '~' : '';
      val.textContent = prefix + s.pressure_psi + ' psi';
      if (s.pressure_stale) {
        wrap.classList.add('pressure-stale');
      }
      if (s.pressure_low_alarm || s.pressure_high_alarm) {
        wrap.classList.add('pressure-alarm');
      }
    } else {
      val.textContent = '—';
    }
  }

  function renderStatus(s) {
    renderClock(s.clock);
    renderPressure(s);
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

  function payloadFromSchedules(list) {
    return list.map(function (s) {
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
  }

  function projectedSchedulesPayload() {
    if (!editingKey) {
      return payloadFromSchedules(schedules);
    }
    const updated = readEditCard();
    if (!updated) {
      return payloadFromSchedules(schedules);
    }
    return payloadFromSchedules(
      schedules.map(function (s) {
        return s._key === editingKey ? updated : s;
      })
    );
  }

  function schedulesDirty() {
    return JSON.stringify(projectedSchedulesPayload()) !== savedSnapshot;
  }

  function updateListLock() {
    const addBtn = document.getElementById('add-schedule');
    if (addBtn) {
      addBtn.disabled = !!editingKey;
    }
  }

  function updateEditSaveButton() {
    const btn = document.querySelector('#sched-edit [data-action=save]');
    if (!btn) {
      return;
    }
    btn.classList.toggle('btn-save-stale', !editingIsNew && !schedulesDirty());
  }

  function zoneLabel(zoneNum) {
    for (let i = 0; i < ZONE_OPTIONS.length; i++) {
      if (String(ZONE_OPTIONS[i][0]) === String(zoneNum)) {
        return ZONE_OPTIONS[i][1];
      }
    }
    return 'Zone ' + zoneNum;
  }

  function formatDuration(minutes) {
    const m = parseInt(minutes, 10) || 0;
    if (m < 60) {
      return m + ' min';
    }
    const hrs = Math.floor(m / 60);
    const rem = m % 60;
    if (rem === 0) {
      return hrs === 1 ? '1 hr' : hrs + ' hrs';
    }
    return hrs + ' hr ' + rem + ' min';
  }

  function formatTime12(hour, minute) {
    const suffix = hour >= 12 ? 'PM' : 'AM';
    let h = hour % 12;
    if (h === 0) {
      h = 12;
    }
    return h + ':' + pad2(minute) + ' ' + suffix;
  }

  function formatFrequency(sched) {
    if (sched.freq === 'interval') {
      const n = sched.interval_days || 1;
      return n === 1 ? 'Every day' : 'Every ' + n + ' days';
    }
    const days = sched.weekdays || [];
    const selected = [];
    for (let i = 0; i < 7; i++) {
      if (days[i]) {
        selected.push(DAY_LABELS[i]);
      }
    }
    if (selected.length === 7) {
      return 'Every day';
    }
    if (selected.length === 5 &&
        days[1] && days[2] && days[3] && days[4] && days[5] && !days[0] && !days[6]) {
      return 'Weekdays';
    }
    if (selected.length === 0) {
      return 'No days';
    }
    return selected.join(',');
  }

  function formatTimeAt(hour, minute) {
    return '@' + formatTime12(hour, minute).toLowerCase();
  }

  function scheduleSummaryMain(sched) {
    return (
      '<span class="zone">' + zoneLabel(sched.zone) + '</span>' +
      '<span class="sched-sep">|</span>' +
      formatDuration(sched.duration) +
      '<span class="sched-sep">|</span>' +
      formatFrequency(sched) +
      '<span class="sched-sep">|</span>' +
      '<span class="sched-time">' + formatTimeAt(sched.hour, sched.minute) + '</span>'
    );
  }

  function scheduleRowHtml(sched) {
    const locked = !!editingKey;
    const disabled = locked ? ' disabled' : '';
    return (
      '<tr><td colspan="3">' +
      '<button type="button" class="sched-row-btn"' + disabled +
      ' data-action="edit" data-key="' + sched._key + '">' +
      '<div class="sched-row-line sched-row-main">' + scheduleSummaryMain(sched) + '</div>' +
      '</button></td></tr>'
    );
  }

  function scheduleEditHtml(sched, isNew) {
    const weeklyActive = sched.freq === 'weekly';
    const hdr = isNew ? 'New schedule' : 'Edit schedule';
    const note = isNew
      ? 'This schedule will appear in the list after you save.'
      : 'The list above shows saved schedules only.';
    const dayBoxes = DAY_LABELS.map(function (label, dayIndex) {
      const checked = sched.weekdays[dayIndex] ? ' checked' : '';
      return (
        '<label class="day-chip">' +
        '<input type="checkbox" data-field="weekday" data-day="' + dayIndex + '"' + checked + '>' +
        '<span>' + label + '</span></label>'
      );
    }).join('');

    return (
      '<div class="sched-edit-hdr">' + hdr + '</div>' +
      '<p class="sched-edit-note">' + note + '</p>' +
      '<article class="sched-card" data-key="' + sched._key + '">' +
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
      '<div class="sched-edit-actions">' +
      '<button type="button" class="btn btn-save" data-action="save">Save schedule</button>' +
      '<button type="button" class="btn-done" data-action="cancel">Cancel</button>' +
      (isNew ? '' : '<button type="button" class="btn-delete" data-action="delete">Delete</button>') +
      '</div>' +
      '</article>'
    );
  }

  function renderScheduleRowsOnly() {
    const list = document.getElementById('sched-list');
    const visible = schedules.filter(function (s) { return s._key !== editingKey; });

    if (!visible.length) {
      if (editingKey) {
        list.innerHTML = '<p class="sched-empty">Saved schedules appear here.</p>';
      } else {
        list.innerHTML = '<p class="sched-empty">No schedules yet. Tap “Add schedule” to create one.</p>';
      }
      return;
    }

    list.innerHTML =
      '<table class="sched-table"><tbody>' +
      visible.map(scheduleRowHtml).join('') +
      '</tbody></table>';
  }

  function renderEditPanel() {
    const edit = document.getElementById('sched-edit');
    if (!editingKey || !schedules.length) {
      edit.classList.add('hidden');
      edit.innerHTML = '';
      return;
    }
    const sched = schedules.find(function (s) { return s._key === editingKey; });
    if (!sched) {
      editingKey = null;
      edit.classList.add('hidden');
      edit.innerHTML = '';
      return;
    }
    edit.classList.remove('hidden');
    edit.innerHTML = scheduleEditHtml(sched, editingIsNew);
    updateEditSaveButton();
  }

  function renderScheduleViews() {
    renderScheduleRowsOnly();
    renderEditPanel();
    updateListLock();
  }

  function readEditCard() {
    const card = document.querySelector('#sched-edit .sched-card');
    if (!card) {
      return null;
    }
    const key = card.getAttribute('data-key');
    const existing = schedules.find(function (s) { return s._key === key; }) || defaultSchedule();
    const timeParts = (card.querySelector('[data-field=time]').value || '00:00').split(':');
    const weekdays = [0, 0, 0, 0, 0, 0, 0];
    card.querySelectorAll('[data-field=weekday]').forEach(function (input) {
      const day = parseInt(input.getAttribute('data-day'), 10);
      weekdays[day] = input.checked ? 1 : 0;
    });
    const weeklyBtn = card.querySelector('.freq-btn[data-freq=weekly]');
    const freq = weeklyBtn && weeklyBtn.classList.contains('active') ? 'weekly' : 'interval';
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

  function commitEditToMemory(refreshRows) {
    if (!editingKey) {
      return;
    }
    const updated = readEditCard();
    if (!updated) {
      return;
    }
    const idx = schedules.findIndex(function (s) { return s._key === editingKey; });
    if (idx >= 0) {
      schedules[idx] = updated;
    }
    if (refreshRows === true) {
      renderScheduleRowsOnly();
    }
  }

  function restoreScheduleFromSnapshot(key) {
    const saved = JSON.parse(savedSnapshot);
    const idx = schedules.findIndex(function (s) { return s._key === key; });
    if (idx < 0) {
      return;
    }
    const current = schedules[idx];
    const match = saved.find(function (s) { return s.id && s.id === current.id; });
    if (match) {
      schedules[idx] = normalizeSchedule(match);
      schedules[idx]._key = key;
    }
  }

  function openEdit(key) {
    if (editingKey) {
      if (editingKey !== key) {
        setSchedMessage('Save or cancel the current schedule first', true);
      }
      return;
    }
    editingKey = key;
    editingIsNew = false;
    setSchedMessage('', false);
    renderScheduleViews();
  }

  function cancelEdit() {
    if (!editingKey) {
      return;
    }
    const key = editingKey;
    if (editingIsNew) {
      schedules = schedules.filter(function (s) { return s._key !== key; });
    } else {
      restoreScheduleFromSnapshot(key);
    }
    editingKey = null;
    editingIsNew = false;
    setSchedMessage('', false);
    renderScheduleViews();
  }

  function markSchedulesDirty() {
    commitEditToMemory(false);
    updateEditSaveButton();
  }

  function setSavedSnapshotFromSchedules() {
    savedSnapshot = JSON.stringify(payloadFromSchedules(schedules));
    updateEditSaveButton();
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

  function loadSchedules() {
    return fetch('/schedules')
      .then(function (r) {
        if (!r.ok) {
          throw new Error('Could not load schedules');
        }
        return r.json();
      })
      .then(function (data) {
        schedules = (data.schedules || []).map(normalizeSchedule);
        editingKey = null;
        editingIsNew = false;
        renderScheduleViews();
        setSavedSnapshotFromSchedules();
        setSchedMessage('', false);
      })
      .catch(function (err) {
        setSchedMessage(err.message || 'Could not load schedules', true);
      });
  }

  function persistSchedules(closeAfter) {
    commitEditToMemory(false);
    const payload = payloadFromSchedules(schedules);
    const saveBtn = document.querySelector('#sched-edit [data-action=save]');

    if (saveBtn) {
      saveBtn.classList.add('btn-saving');
      saveBtn.textContent = 'Saving…';
    }

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
        if (closeAfter) {
          editingKey = null;
          editingIsNew = false;
          renderScheduleViews();
        }
        setSchedMessage('Schedule saved', false);
        refresh();
      })
      .catch(function (err) {
        setSchedMessage(err.message || 'Save failed', true);
        updateEditSaveButton();
      })
      .finally(function () {
        if (saveBtn) {
          saveBtn.classList.remove('btn-saving');
          saveBtn.textContent = 'Save schedule';
        }
      });
  }

  function saveCurrentSchedule() {
    if (!editingKey) {
      return Promise.resolve();
    }
    commitEditToMemory(false);
    return persistSchedules(true);
  }

  function deleteCurrentSchedule() {
    if (!editingKey) {
      return;
    }
    schedules = schedules.filter(function (s) { return s._key !== editingKey; });
    editingKey = null;
    editingIsNew = false;
    renderScheduleViews();
    return persistSchedules(false);
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
    if (tabName === 'schedules') {
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
    if (editingKey) {
      setSchedMessage('Save or cancel the current schedule first', true);
      return;
    }
    const sched = defaultSchedule();
    schedules.push(sched);
    editingKey = sched._key;
    editingIsNew = true;
    setSchedMessage('', false);
    renderScheduleViews();
  });

  document.getElementById('sched-edit').addEventListener('input', markSchedulesDirty);
  document.getElementById('sched-edit').addEventListener('change', markSchedulesDirty);

  document.getElementById('sched-edit').addEventListener('click', function (e) {
    const saveBtn = e.target.closest('[data-action=save]');
    if (saveBtn) {
      saveCurrentSchedule();
      return;
    }

    const cancelBtn = e.target.closest('[data-action=cancel]');
    if (cancelBtn) {
      cancelEdit();
      return;
    }

    const deleteBtn = e.target.closest('[data-action=delete]');
    if (deleteBtn) {
      deleteCurrentSchedule();
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
      markSchedulesDirty();
    }
  });

  document.getElementById('sched-list').addEventListener('click', function (e) {
    const editBtn = e.target.closest('[data-action=edit]');
    if (editBtn && !editBtn.disabled) {
      openEdit(editBtn.getAttribute('data-key'));
    }
  });

  refresh();
  setInterval(refresh, POLL_MS);
})();
