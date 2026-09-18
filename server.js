require('dotenv').config();
const express = require('express');
const fetch = require('node-fetch');
const path = require('path');

const app = express();
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

const BLYNK_TOKEN = process.env.BLYNK_TOKEN;
// Blynk Cloud is region-sharded — match your console footer (e.g. "Region: BLR1").
const BLYNK_REGION = (process.env.BLYNK_REGION || 'blr1').toLowerCase();
const BLYNK_BASE = `https://${BLYNK_REGION}.blynk.cloud/external/api`;
const RELAY_PIN = process.env.RELAY_PIN || 'V0';

if (!BLYNK_TOKEN) {
  console.warn('WARNING: BLYNK_TOKEN is not set. Create a .env file with BLYNK_TOKEN=your_token');
}

// Returns the raw value or parsed value, or null if the pin doesn't exist / call fails.
// Handles both raw strings ("RUNNING", "NORMAL", "00:00:35") and JSON arrays/numbers.
async function safeGetPin(pin) {
  if (!BLYNK_TOKEN) return null;
  try {
    const res = await fetch(`${BLYNK_BASE}/get?token=${BLYNK_TOKEN}&${pin}`);
    if (!res.ok) return null;
    const text = (await res.text()).trim();
    if (!text) return null;

    try {
      const parsed = JSON.parse(text);
      if (Array.isArray(parsed)) {
        return parsed.length === 1 ? parsed[0] : parsed;
      }
      return parsed;
    } catch {
      // Plain text like "RUNNING", "NORMAL", "OFF", "00:00:35"
      return text;
    }
  } catch (err) {
    return null;
  }
}

async function setPin(pin, value) {
  if (!BLYNK_TOKEN) throw new Error('BLYNK_TOKEN is not configured');
  const res = await fetch(`${BLYNK_BASE}/update?token=${BLYNK_TOKEN}&${pin}=${encodeURIComponent(value)}`);
  if (!res.ok) {
    const errText = await res.text().catch(() => '');
    throw new Error(`Blynk SET ${pin} failed: ${res.status} ${errText}`);
  }
  return true;
}

async function isDeviceOnline() {
  if (!BLYNK_TOKEN) return false;
  try {
    const res = await fetch(`${BLYNK_BASE}/isHardwareConnected?token=${BLYNK_TOKEN}`);
    if (!res.ok) return false;
    const text = (await res.text()).trim().toLowerCase();
    return text === 'true' || text === '1';
  } catch {
    return false;
  }
}

// Read full device status across all 12 datastreams.
app.get('/api/status', async (req, res) => {
  try {
    const [
      online, relay, pumpStatus, waterStatus, waterRaw,
      protectionStatus, runtime, fault, countdown,
      wifiStatus, relayStatus, waterThreshold, systemState
    ] = await Promise.all([
      isDeviceOnline(),
      safeGetPin(RELAY_PIN),
      safeGetPin('V1'),
      safeGetPin('V2'),
      safeGetPin('V3'),
      safeGetPin('V4'),
      safeGetPin('V5'),
      safeGetPin('V6'),
      safeGetPin('V7'),
      safeGetPin('V8'),
      safeGetPin('V9'),
      safeGetPin('V10'),
      safeGetPin('V11')
    ]);

    res.json({
      online,
      relay: relay === null ? null : (relay === 1 || relay === '1' || relay === true),
      pumpStatus: pumpStatus !== null ? String(pumpStatus) : null,
      waterStatus: waterStatus !== null ? String(waterStatus) : null,
      waterRaw: waterRaw === null ? null : Number(waterRaw),
      protectionStatus: protectionStatus !== null ? String(protectionStatus) : null,
      runtime: runtime !== null ? String(runtime) : null,
      fault: fault !== null ? String(fault) : null,
      countdown: countdown === null ? null : Number(countdown),
      wifiStatus: wifiStatus !== null ? String(wifiStatus) : null,
      relayStatus: relayStatus === null ? null : (relayStatus === 1 || relayStatus === '1' || relayStatus === true),
      waterThreshold: waterThreshold === null ? null : Number(waterThreshold),
      systemState: systemState !== null ? String(systemState) : null,
      device: {
        region: BLYNK_REGION,
        relayPin: RELAY_PIN,
        tokenConfigured: !!BLYNK_TOKEN
      }
    });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Farmer's pump on/off request -> RELAY_PIN (defaults to V0)
app.post('/api/motor', async (req, res) => {
  try {
    const targetState = req.body.on ? 1 : 0;
    await setPin(RELAY_PIN, targetState);
    res.json({ success: true, on: req.body.on });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Optional: calibrate the dry-detection threshold -> V10
app.post('/api/threshold', async (req, res) => {
  try {
    const val = Number(req.body.value);
    if (isNaN(val)) return res.status(400).json({ error: 'Invalid threshold number' });
    await setPin('V10', val);
    res.json({ success: true, value: val });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

// Reset / Stop emergency command
app.post('/api/reset', async (req, res) => {
  try {
    await setPin(RELAY_PIN, 0);
    res.json({ success: true, message: 'Pump stopped / reset command sent' });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

const PORT = process.env.PORT || 3001;
const server = app.listen(PORT, () => {
  console.log(`Pump Protector dashboard running at http://localhost:${PORT}`);
});

// Handle graceful termination
process.on('SIGINT', () => {
  server.close(() => {
    console.log('Server terminated');
    process.exit(0);
  });
});
process.on('SIGTERM', () => {
  server.close(() => {
    console.log('Server terminated');
    process.exit(0);
  });
});
