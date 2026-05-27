(function () {
  const POLL_MS = 3000;

  let pending = { z1: '0', z2: '0' };
  let saving = false;

  function chipHtml(label, on) {
    const cls = on ? 'chip-on' : 'chip-off';
    const word = on ? 'On' : 'Off';
    return '<div class="chip ' + cls + '"><b>' + label + '</b>' + word + '</div>';
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
    renderChips(s);
    if (!document.getElementById('zone-form')) {
      renderControls(s);
    } else if (!saving) {
      document.getElementById('zone1').value = s.zones.z1 === 'on' ? '1' : '0';
      document.getElementById('zone2').value = s.zones.z2 === 'on' ? '1' : '0';
    }
  }

  function refresh() {
    return fetch('/status')
      .then(function (r) { return r.json(); })
      .then(renderStatus)
      .catch(function () {
        document.getElementById('chips').innerHTML =
          '<div class="chip chip-off" style="grid-column:span 2"><b>Status</b>Offline</div>';
      });
  }

  refresh();
  setInterval(refresh, POLL_MS);
})();
