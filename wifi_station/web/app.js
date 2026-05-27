(function () {
  const POLL_MS = 3000;

  const ZONE_ROWS = [
    ['Zone 1', 'z1'],
    ['Zone 2', 'z2'],
    ['Greenhouse', 'gh'],
    ['Water cannon', 'wc']
  ];

  function chipHtml(label, status) {
    const cls = status === 'on' ? 'chip-on' : status === 'error' ? 'chip-err' : 'chip-off';
    const word = status === 'on' ? 'On' : status === 'error' ? 'Error' : 'Off';
    return '<div class="chip ' + cls + '"><b>' + label + '</b>' + word + '</div>';
  }

  function renderControls(running) {
    const el = document.getElementById('controls');
    if (running) {
      el.innerHTML =
        '<form action="/stop" method="post">' +
        '<input class="btn btn-stop" type="submit" value="Stop watering">' +
        '</form>';
      return;
    }
    el.innerHTML =
      '<form action="/start" method="post">' +
      '<div class="ctrl-row">' +
      '<input type="number" name="duration" min="1" value="10" inputmode="numeric" aria-label="Minutes">' +
      '<select name="zone" aria-label="What to water">' +
      '<option value="1">Zone 1</option>' +
      '<option value="2">Zone 2</option>' +
      '<option value="3">Greenhouse</option>' +
      '<option value="4">Zones 1+2</option>' +
      '<option value="5">Water cannon</option>' +
      '</select></div>' +
      '<input class="btn" type="submit" value="Start watering">' +
      '</form>';
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

    renderLastTable(s.last_watering);
    renderControls(!!s.timer_running);
  }

  function refresh() {
    fetch('/status')
      .then(function (r) { return r.json(); })
      .then(renderStatus)
      .catch(function () {
        document.getElementById('now-main').textContent = 'Offline';
      });
  }

  refresh();
  setInterval(refresh, POLL_MS);
})();
