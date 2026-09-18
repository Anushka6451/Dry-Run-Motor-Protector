// ============================================================
// Shared logic for every Pump Protector page.
// ============================================================

// ---------------- Auth (mock / local storage session) ----------------
function loadAuth() {
  try {
    const raw = localStorage.getItem('pumpAuth');
    return raw ? JSON.parse(raw) : null;
  } catch {
    return null;
  }
}

function saveAuth(a) {
  localStorage.setItem('pumpAuth', JSON.stringify(a));
}

function clearAuth() {
  localStorage.removeItem('pumpAuth');
}

// Call at the top of protected pages (home/dashboard/pump/alerts/profile)
function requireAuth() {
  if (!loadAuth()) {
    // If not logged in, provide a convenient default guest session or redirect
    const guest = { name: 'Farmer', email: 'farmer@borewell.io' };
    saveAuth(guest);
  }
}

// Call at the top of index.html/signin.html so an already authed user can jump right in
function redirectIfAuthed(target) {
  if (loadAuth()) {
    window.location.href = target;
  }
}

function doLogout() {
  clearAuth();
  window.location.href = 'index.html';
}

// ---------------- Bottom navigation ----------------
const NAV_ITEMS = [
  { id: 'home', label: 'Home', href: 'home.html', icon: '<path d="M3 11l9-8 9 8M5 10v10h14V10"/>' },
  { id: 'dashboard', label: 'Data', href: 'dashboard.html', icon: '<path d="M3 3v18h18M7 15l4-4 3 3 5-6"/>' },
  { id: 'pump', label: 'Pump', href: 'pump.html', icon: '<path d="M18.36 6.64a9 9 0 1 1-12.73 0M12 2v10"/>' },
  { id: 'alerts', label: 'Alerts', href: 'alerts.html', icon: '<path d="M18 8a6 6 0 0 0-12 0c0 7-3 9-3 9h18s-3-2-3-9M13.73 21a2 2 0 0 1-3.46 0"/>' },
  { id: 'profile', label: 'Profile', href: 'profile.html', icon: '<circle cx="12" cy="8" r="4"/><path d="M4 21c0-4 4-6 8-6s8 2 8 6"/>' }
];

function injectNav(activeId) {
  const mount = document.getElementById('bottomNav');
  if (!mount) return;
  const items = NAV_ITEMS.map(item => `
    <a class="nav-item ${item.id === activeId ? 'active' : ''}" href="${item.href}" id="nav_${item.id}">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">${item.icon}</svg>
      <span>${item.label}</span>
    </a>`).join('');
  mount.outerHTML = `<nav class="bottom-nav" id="bottomNav">${items}</nav>`;
}

// ---------------- Toast Notification ----------------
function toast(msg, type = 'info') {
  let t = document.getElementById('toast');
  if (!t) {
    t = document.createElement('div');
    t.id = 'toast';
    t.className = 'toast';
    document.body.appendChild(t);
  }
  t.textContent = msg;
  t.className = `toast show ${type}`;
  clearTimeout(t._hideTimer);
  t._hideTimer = setTimeout(() => t.classList.remove('show'), 2500);
}

// ---------------- API Calls ----------------
async function fetchStatus() {
  try {
    const res = await fetch('/api/status');
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const state = await res.json();
    return { state, reachable: true, live: !!state.online };
  } catch (err) {
    return { state: {}, reachable: false, live: false };
  }
}

async function postMotor(on) {
  const res = await fetch('/api/motor', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ on })
  });
  if (!res.ok) {
    const data = await res.json().catch(() => ({}));
    throw new Error(data.error || `Failed to ${on ? 'start' : 'stop'} pump`);
  }
  return await res.json();
}

async function postThreshold(value) {
  const res = await fetch('/api/threshold', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ value })
  });
  if (!res.ok) {
    const data = await res.json().catch(() => ({}));
    throw new Error(data.error || 'Failed to update threshold');
  }
  return await res.json();
}

async function postReset() {
  const res = await fetch('/api/reset', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' }
  });
  return await res.json();
}

// ---------------- Water Sensor Evaluation ----------------
function isWaterPresent(waterStatus, waterRaw) {
  if (waterStatus !== null && waterStatus !== undefined && waterStatus !== '') {
    const str = String(waterStatus).trim().toUpperCase();
    if (['1', 'PRESENT', 'WATER PRESENT', 'OK', 'NORMAL', 'DETECTED', 'YES'].includes(str)) {
      return true;
    }
    if (['0', 'ABSENT', 'WATER ABSENT', 'DRY', 'LOW', 'EMPTY', 'NONE', 'NO'].includes(str)) {
      return false;
    }
    const num = Number(str);
    if (!isNaN(num)) return num > 0;
  }
  if (waterRaw !== null && waterRaw !== undefined) {
    return Number(waterRaw) >= 1500;
  }
  return null;
}

function formatWaterStatus(waterStatus, waterRaw) {
  const present = isWaterPresent(waterStatus, waterRaw);
  if (present === null) return 'Checking…';
  return present ? 'Water Present' : 'Water Absent (Dry)';
}

// ---------------- Pump State Helper ----------------
function pumpIsRunning(state) {
  if (!state) return false;
  if (state.relayStatus === true || state.relayStatus === 1 || state.relayStatus === '1') return true;
  if (state.relay === true || state.relay === 1 || state.relay === '1') return true;
  if (state.pumpStatus) {
    const s = String(state.pumpStatus).trim().toUpperCase();
    if (['RUNNING', 'STARTING', 'ON'].includes(s)) return true;
  }
  if (state.systemState) {
    const s = String(state.systemState).trim().toUpperCase();
    if (['RUNNING', 'STARTING'].includes(s)) return true;
  }
  return false;
}

// ---------------- Status Styling Helpers ----------------
function stateColor(sysState, fault) {
  if (fault && String(fault).toUpperCase() !== 'NONE') return 'red';
  if (!sysState) return 'gray';
  const s = String(sysState).trim().toUpperCase();
  if (s === 'RUNNING' || s === 'ON') return 'green';
  if (s === 'STARTING' || s === 'WAITING FOR WATER') return 'amber';
  if (s.includes('FAULT') || s.includes('PROTECT') || s.includes('WARNING') || s.includes('LOSS')) return 'red';
  if (s === 'OFF' || s === 'IDLE') return 'gray';
  return 'gray';
}

function describeState(s, fault) {
  if (fault && String(fault).toUpperCase() !== 'NONE') {
    return `Alert: ${fault}`;
  }
  const u = (s || '').trim().toUpperCase();
  if (u === 'RUNNING' || u === 'ON') return 'Your pump is running normally';
  if (u === 'STARTING') return 'Startup bypass active (90s)';
  if (u === 'WATER WARNING') return 'Water loss detected — checking response timer';
  if (u === 'PROTECTING' || u.includes('FAULT')) return 'Dry run detected — pump stopped to protect motor';
  if (u === 'WAITING FOR WATER') return 'Waiting for water level to return';
  if (u === 'OFF' || u === 'IDLE') return 'Pump is off and ready';
  return 'System ready';
}

function connLabel(reachable, live) {
  if (!reachable) return 'Server Offline';
  return live ? 'Device Online' : 'Pump Unit Offline';
}

function setPill(id, text, color) {
  const el = document.getElementById(id);
  if (!el) return;
  el.className = 'status-pill pill-' + color;
  const label = el.querySelector('span:last-child');
  if (label) label.textContent = text;
}

function setCheck(id, ok, text) {
  const icon = document.getElementById(id);
  if (icon) {
    icon.className = 'check-icon ' + (ok ? 'ok' : 'bad');
  }
  const label = document.getElementById(id + 'Text');
  if (label) label.textContent = text;
}

function setConnDot(id, live) {
  const el = document.getElementById(id);
  if (!el) return;
  el.className = 'conn-dot ' + (live ? 'live' : 'down');
}

// ---------------- Alert Log (Local client store with deduplication) ----------------
let _lastLoggedEvent = '';

function loadAlerts() {
  try {
    return JSON.parse(localStorage.getItem('pumpAlerts')) || [];
  } catch {
    return [];
  }
}

function saveAlerts(list) {
  localStorage.setItem('pumpAlerts', JSON.stringify(list));
}

function logAlertIfNew(fault, sysState) {
  const hasFault = fault && String(fault).toUpperCase() !== 'NONE';
  const hasLock = sysState && (String(sysState).toUpperCase().includes('FAULT') || String(sysState).toUpperCase().includes('PROTECT'));
  
  if (!hasFault && !hasLock) {
    _lastLoggedEvent = '';
    return;
  }
  
  const title = hasFault ? fault : sysState;
  const key = `${title}`;
  if (key === _lastLoggedEvent) return;
  _lastLoggedEvent = key;

  const list = loadAlerts();
  list.unshift({
    fault: title,
    detail: hasFault ? 'Dry-run or system fault triggered automatic pump shutdown' : 'Pump safety lock activated',
    time: new Date().toISOString()
  });
  saveAlerts(list.slice(0, 50));
}

function clearAlerts() {
  saveAlerts([]);
}

// ---------------- Polling Loop ----------------
function startPolling(renderFn, intervalMs = 1500) {
  let isPolling = false;
  async function tick() {
    if (isPolling) return;
    isPolling = true;
    try {
      const { state, reachable, live } = await fetchStatus();
      if (live) {
        logAlertIfNew(state.fault, state.systemState);
      }
      renderFn(state, reachable, live);
    } catch (err) {
      console.error('Polling error:', err);
    } finally {
      isPolling = false;
    }
  }
  tick();
  return setInterval(tick, intervalMs);
}

// Register service worker if available
if ('serviceWorker' in navigator) {
  navigator.serviceWorker.register('/sw.js').catch(() => {});
}
