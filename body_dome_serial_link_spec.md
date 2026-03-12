# protoArtoo ↔ AstroPixelsPlus — Body/Dome Serial Link Specification

**Projects:** `protoArtoo` (body) + `mattiasbrandt/AstroPixelsPlus` (dome fork)  
**Physical link:** Slip ring, full-duplex UART, 9600 baud 8N1  
**Date:** March 2026

---

## 0. What is confirmed from upstream source code

Before designing anything, these are the facts extracted from the actual
`reeltwo/AstroPixelsPlus` source — the base your fork starts from.

**NVS namespace:** `preferences.begin("astro", false)` — all keys max 15 chars.

**Existing serial preferences:**

| C++ constant | NVS key | Type | Meaning |
|---|---|---|---|
| `PREFERENCE_MARCSERIAL_ENABLED` | `"mserial"` | bool | Whether COMMAND_SERIAL opens at all |
| `PREFERENCE_MARCSERIAL2` | `"mserial2"` | int | Baud rate (default 9600) |
| `PREFERENCE_MARCSERIAL_PASS` | `"mserialpass"` | bool | Pass-through to daisy-chain — **commented out in code** |

**How COMMAND_SERIAL is wired:**
```cpp
// Upstream setup() — Serial2 opened with both pins, but TX side never written to
COMMAND_SERIAL.begin(
    preferences.getInt(PREFERENCE_MARCSERIAL2, MARC_SERIAL2_BAUD_RATE),
    SERIAL_8N1, SERIAL2_RX_PIN, SERIAL2_TX_PIN);

// if (preferences.getBool(PREFERENCE_MARCSERIAL_PASS, MARC_SERIAL_PASS))  ← COMMENTED OUT
marcduinoSerial.setStream(&COMMAND_SERIAL, &Serial);
// Second arg is the debug/pass-through stream.
// COMMAND_SERIAL.TX is physically available but nothing writes to it.
```

**How Reeltwo dispatches received commands:**  
`marcduinoSerial` reads from `COMMAND_SERIAL`, splits on `\r`, and calls
`CommandEvent::process(line)` for each complete command. All registered
gadget handlers receive it from there.

**The `#AP` command namespace** already used for dome self-config:
- `#APWIFI` / `#APWIFI0` / `#APWIFI1` — toggle WiFi
- `#APREMOTE` / `#APREMOTE0` / `#APREMOTE1` — toggle Droid Remote

**NVS key naming convention:** the `m`-prefix is used for Marcduino-related
settings (`mserial`, `mserial2`, `mwifi`, `msound`, `mrandom`, etc.).
New body-link keys will follow this same convention.

---

## 1. What the serial link needs to do

There are two separate concerns that both travel over the same wire:

**Control traffic (body ← dome):**  
When the dome runs a full-droid sequence (`:SE01` Scream, `:SE07` Happy etc.),
it must send the body-side portion back over the slip ring — sound `$`, arm
sequences `:SE3x`, direct servo `:OP/:CL/:MV`. This is all standard Marcduino
ASCII. The upstream TX path simply does not write anything; we need to add it.

**Health traffic (body ↔ dome):**  
Both sides need to know whether the other controller is alive. In the upstream
code there is no concept of this at all. We define it cleanly from scratch.

These two concerns need different code changes and are described separately.

---

## 2. Control traffic: dome TX to body

### 2.1 What to add to the dome fork

A single helper function that writes a Marcduino command to `COMMAND_SERIAL`:

```cpp
// ─────────────────────────────────────────────────────────────────────────
// sendBodyCommand() — dome → body over slip ring UART TX
//
// This is the only place in the dome firmware that writes to COMMAND_SERIAL.
// In the upstream code the TX pin is opened but never used; the pass-through
// (MARC_SERIAL_PASS) was commented out. We use it directly to send body-side
// commands during full-droid sequences.
//
// Call from any sequence handler that needs the body to do something.
// Do NOT call for dome-only commands like * @ — those are handled internally.
// ─────────────────────────────────────────────────────────────────────────
static void sendBodyCommand(const char* cmd)
{
    if (!preferences.getBool(PREFERENCE_MARCSERIAL_ENABLED, MARC_SERIAL_ENABLED))
        return;
    COMMAND_SERIAL.print(cmd);
    COMMAND_SERIAL.print('\r');
}
```

### 2.2 Where to call it

Inside each full-droid sequence handler (`:SE01`–`:SE16`), add calls for the
body-side component of that sequence. The dome side runs its own internal
animation; this sends the parallel body action. Example for `:SE01` (Scream):

```cpp
case 1:  // :SE01 — Full Scream sequence
    // Dome side: panel wave + logic display + holo effects  ← existing code
    // Body side: play scream sound + open arms
    sendBodyCommand("$S");     // body plays scream sound bank
    sendBodyCommand(":SE30");  // body does arm open/close sequence
    break;
```

**Sequences that need body TX calls — audit each one:**
`:SE01`, `:SE02`, `:SE03`, `:SE04`, `:SE06`, `:SE07`, `:SE08`,
`:SE11`–`:SE14`, `:SE58`. Any sequence that in classic MarcDuino builds
would have the Body Master do something now needs a `sendBodyCommand()` call.

> This is a TODO-per-sequence audit task — it should be done while looking
> at your actual fork's sequence handler code, not assumed from documentation.

---

## 3. Health traffic: mutual heartbeat

### 3.1 New preference — `"mbodylink"`

Following the `m`-prefix convention:

```cpp
// Add with the other PREFERENCE_ defines at the top of AstroPixelsPlus.ino
#define PREFERENCE_BODY_LINK_ENABLED  "mbodylink"
#define BODY_LINK_ENABLED             true   // on by default in this fork
```

This single boolean controls whether:
- The dome sends `#APHB` heartbeats to the body
- The dome tracks and exposes body connection state in status/UI

### 3.2 Runtime state (not persisted)

```cpp
// Near top of AstroPixelsPlus.ino, in the global state section:
static uint32_t sBodyLastSeenMs    = 0;   // millis() when last #PAHB received (0=never)
static uint32_t sBodyHeartbeatRx   = 0;   // count of #PAHB frames received from body

// Helper used in status + UI
static bool bodyLinkConnected()
{
    return sBodyHeartbeatRx > 0 &&
           (millis() - sBodyLastSeenMs) < 5000UL;
}
```

5 seconds gives five missed heartbeats before declaring lost — conservative
enough to survive brief slip ring contact noise.

### 3.3 Heartbeat command names

| Command | Direction | Meaning |
|---|---|---|
| `#PAHB\r` | Body → Dome | protoArtoo heartbeat ("PA Heartbeat") |
| `#APHB\r` | Dome → Body | AstroPixelsPlus heartbeat ("AP Heartbeat") |

`#APHB` fits the existing `#AP` namespace. `#PAHB` mirrors it with the
protoArtoo `#PA` namespace. Neither collides with any existing command.

### 3.4 Intercepting `#PAHB` from the body

The upstream `marcduinoSerial.setStream()` pipes `COMMAND_SERIAL` directly
into Reeltwo's `CommandEvent::process()`. We need to intercept `#PAHB`
**before** it reaches Reeltwo, because Reeltwo has no handler for it and we
need the raw timing.

**Change the serial read path from piped to manual:**

In `setup()`, replace:
```cpp
// BEFORE — Reeltwo reads COMMAND_SERIAL and dispatches everything
marcduinoSerial.setStream(&COMMAND_SERIAL, &Serial);
```

With:
```cpp
// AFTER — We read COMMAND_SERIAL ourselves in loop(), intercept heartbeat,
// pass everything else to CommandEvent::process() exactly as before.
// Debug output stays on Serial.
// Note: do NOT call marcduinoSerial.setStream() at all when bodylink is active.
```

Then in `loop()`, add a manual read loop **before** `AnimatedEvent::process()`:

```cpp
// ─────────────────────────────────────────────────────────────────────────
// Manual COMMAND_SERIAL read — replaces marcduinoSerial.setStream() dispatch.
//
// We intercept #PAHB for body-link health tracking before other handlers
// see the command. Everything else is forwarded to CommandEvent::process()
// which delivers it to all registered Reeltwo gadget handlers normally.
//
// Line format: ASCII text terminated by \r (standard Marcduino).
// ─────────────────────────────────────────────────────────────────────────
static void handleBodySerial()
{
    static char sBuf[64];
    static uint8_t sLen = 0;

    while (COMMAND_SERIAL.available())
    {
        char c = (char)COMMAND_SERIAL.read();
        if (c == '\r' || c == '\n')
        {
            if (sLen > 0)
            {
                sBuf[sLen] = '\0';

                if (strncmp(sBuf, "#PAHB", 5) == 0)
                {
                    // Body heartbeat — update tracking, do not dispatch to Reeltwo
                    sBodyLastSeenMs  = millis();
                    sBodyHeartbeatRx++;
                }
                else
                {
                    // All other commands — dispatch normally
                    CommandEvent::process(sBuf);
                }
                sLen = 0;
            }
        }
        else if (sLen < sizeof(sBuf) - 1)
        {
            sBuf[sLen++] = c;
        }
        else
        {
            // Buffer overflow — discard and reset
            sLen = 0;
        }
    }
}
```

Call in `loop()`:
```cpp
void loop()
{
    if (preferences.getBool(PREFERENCE_MARCSERIAL_ENABLED, MARC_SERIAL_ENABLED) &&
        preferences.getBool(PREFERENCE_BODY_LINK_ENABLED,  BODY_LINK_ENABLED))
    {
        handleBodySerial();   // ← replaces marcduinoSerial dispatch for this path
    }
    AnimatedEvent::process();
    // ... rest of loop
}
```

> **If body link is disabled** (the `mbodylink` pref is false), fall back
> to `marcduinoSerial.setStream()` so the serial port still works as a
> standard Marcduino receiver for non-protoArtoo builds.

### 3.5 Sending `#APHB` from the dome

In `loop()`, after `handleBodySerial()`:

```cpp
static void handleBodyLinkHeartbeat()
{
    if (!preferences.getBool(PREFERENCE_MARCSERIAL_ENABLED, MARC_SERIAL_ENABLED))
        return;
    if (!preferences.getBool(PREFERENCE_BODY_LINK_ENABLED, BODY_LINK_ENABLED))
        return;

    static uint32_t sLastHbMs = 0;
    if (millis() - sLastHbMs >= 1000UL)
    {
        sLastHbMs = millis();
        COMMAND_SERIAL.print("#APHB\r");
    }
}
```

---

## 4. Exposing status — dome `/api/status`

The upstream web handler returns a JSON status object. Add a `body_link` block:

```cpp
// Inside the /api/status JSON builder in your fork's web handler:
// (exact location depends on how your fork structured this — find the
//  JSON response for the status endpoint and add here)

doc["body_link"]["enabled"]    = preferences.getBool(PREFERENCE_BODY_LINK_ENABLED,
                                                      BODY_LINK_ENABLED);
doc["body_link"]["connected"]  = bodyLinkConnected();
doc["body_link"]["last_rx_ms"] = (sBodyHeartbeatRx > 0)
                                  ? (int32_t)(millis() - sBodyLastSeenMs)
                                  : -1;
doc["body_link"]["hb_rx"]      = sBodyHeartbeatRx;
```

**Result:**
```json
{
  "body_link": {
    "enabled":    true,
    "connected":  true,
    "last_rx_ms": 312,
    "hb_rx":      2041
  }
}
```

`last_rx_ms` is `-1` when no heartbeat has ever been received this session.
`hb_rx` is the total count of `#PAHB` frames received since last reboot.

---

## 5. Dome web UI — settings page

### 5.1 New setting: "Body Controller Link"

Add to the dome's serial settings section. Follows the same toggle pattern
as the existing Marc Serial Enabled / Marc WiFi Enabled controls:

```html
<!-- Add in the serial/communication settings section,
     after the existing "Marc Serial" block -->

<tr>
  <td>Body Controller Link</td>
  <td>
    <select name="mbodylink" onchange="this.form.submit()">
      <option value="0" <%= bodyLinkEnabled ? "" : "selected" %>>Disabled</option>
      <option value="1" <%= bodyLinkEnabled ? "selected" : "" %>>Enabled (protoArtoo)</option>
    </select>
  </td>
</tr>
<tr>
  <td colspan="2" class="note">
    Enable when paired with <strong>protoArtoo</strong> body controller.
    Dome will send heartbeats to body and track body connection state.
    <em>Marc Serial must also be enabled.</em><br>
    <strong>Note:</strong> When enabled, Sound Player should be set to
    <em>None</em> — all audio is handled by the body controller.
  </td>
</tr>
```

### 5.2 Live body link status on the settings page

Below the toggle, show live connection state (populated on page load via
the `/api/status` JSON, same pattern as other live fields in the UI):

```html
<tr id="bodyLinkStatusRow">
  <td>Body Status</td>
  <td>
    <span id="bodyLinkBadge">--</span>
    <span id="bodyLinkDetail" style="margin-left:8px; color:#888;"></span>
  </td>
</tr>
```

```javascript
// Add to the status polling / page load script
function updateBodyLinkStatus(bodyLink) {
    var badge  = document.getElementById('bodyLinkBadge');
    var detail = document.getElementById('bodyLinkDetail');
    if (!badge) return;

    if (!bodyLink.enabled) {
        badge.textContent      = 'Disabled';
        badge.className        = 'badge-off';
        detail.textContent     = '';
    } else if (bodyLink.connected) {
        badge.textContent      = 'Connected';
        badge.className        = 'badge-ok';
        detail.textContent     = bodyLink.last_rx_ms + 'ms ago';
    } else if (bodyLink.hb_rx > 0) {
        badge.textContent      = 'Lost';
        badge.className        = 'badge-err';
        detail.textContent     = 'Last seen ' + bodyLink.hb_rx + ' heartbeats ago';
    } else {
        badge.textContent      = 'Not seen';
        badge.className        = 'badge-warn';
        detail.textContent     = 'No heartbeat received since reboot';
    }
}

// Wire into your existing status fetch:
// updateBodyLinkStatus(statusJson.body_link);
```

**Three connection states explained:**

| Badge | Meaning |
|---|---|
| **Connected** (green) | `#PAHB` received within last 5 s |
| **Lost** (red) | Was connected this session, now silent >5 s — slip ring issue or body crash |
| **Not seen** (amber) | Never received a heartbeat this power cycle — wiring, body not booted, or feature disabled in protoArtoo |

"Not seen" vs "Lost" is important to distinguish — "Not seen" is a
configuration/wiring problem, "Lost" is a runtime dropout.

### 5.3 Dome dashboard — compact indicator

On the main status/dashboard page, one line alongside the other health fields:

```html
<tr>
  <td>Body</td>
  <td><span id="bodyIndicator">--</span></td>
</tr>
```

---

## 6. protoArtoo — matching implementation

### 6.1 Sending `#PAHB` — DomeLinkTask

In `dome_link.cpp`, the TX path drains the command queue. Add the body
heartbeat alongside it:

```cpp
void DomeLinkTask(void* params)
{
    uint32_t lastHbMs = 0;

    for (;;)
    {
        // Drain TX queue — dome control commands
        DomeTxCmd cmd;
        while (xQueueReceive(domeTxQueue, &cmd, 0) == pdTRUE)
            domeSendRaw(cmd.buf);  // writes to UART + '\r'

        // Body heartbeat — 1 Hz
        if (millis() - lastHbMs >= 1000UL)
        {
            lastHbMs = millis();
            domeSendRaw("#PAHB");
            robotState.bodyHbTx++;
        }

        // RX — handled in same task, see marcduino_rx.cpp
        handleDomeRx();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### 6.2 Receiving `#APHB` — marcduino_rx.cpp

In the `#` prefix case of the RX parser:

```cpp
case '#':
    if (strncmp(line, "#APHB", 5) == 0)
    {
        // Dome heartbeat — update connection tracking
        // No reply needed; protoArtoo tracks from lastSeenMs
        robotState.domeLastSeenMs = millis();
        robotState.domeHbRx++;
    }
    else
    {
        xQueueSend(configQueue, &(ConfigCmd{line}), 0);
    }
    break;
```

### 6.3 RobotState — dome link fields

```cpp
struct RobotState {
    // ... existing fields ...

    // Dome link health — body perspective
    uint32_t bodyHbTx;          // #PAHB frames sent to dome
    uint32_t domeHbRx;          // #APHB frames received from dome
    uint32_t domeLastSeenMs;    // millis() of last #APHB  (0 = never)

    // inline helper
    bool domeConnected() const {
        return domeHbRx > 0 && (millis() - domeLastSeenMs) < 5000UL;
    }
};
```

### 6.4 protoArtoo `/api/status` — dome link block

```json
{
  "dome_link": {
    "connected":    true,
    "last_rx_ms":   287,
    "hb_rx":        2103,
    "hb_tx":        2104
  }
}
```

`hb_tx` and `hb_rx` should be approximately equal if both sides are running
at 1 Hz. A large difference indicates one direction has a problem.

---

## 7. Summary — all new additions

### New NVS key (dome only)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `"mbodylink"` | bool | `true` (this fork) | Body controller link enabled |

### New commands

| Command | Direction | Rate |
|---|---|---|
| `#PAHB\r` | Body → Dome | 1 Hz |
| `#APHB\r` | Dome → Body | 1 Hz |

### New dome C++ symbols

| Symbol | Type | Where |
|---|---|---|
| `PREFERENCE_BODY_LINK_ENABLED` | `#define` const | Top of `.ino` with other prefs |
| `BODY_LINK_ENABLED` | `#define` default | Same |
| `sBodyLastSeenMs` | `static uint32_t` | Global state |
| `sBodyHeartbeatRx` | `static uint32_t` | Global state |
| `bodyLinkConnected()` | `static bool` helper | Global state |
| `sendBodyCommand()` | `static void` | Sequence handlers |
| `handleBodySerial()` | `static void` | Called from `loop()` |
| `handleBodyLinkHeartbeat()` | `static void` | Called from `loop()` |

### No new NVS keys in protoArtoo

Heartbeat interval and disconnect threshold are compile-time constants.
Runtime state lives in `RobotState`.

---

## 8. Implementation order

Recommended to implement and test in this order — each step is independently
verifiable:

1. **Add `sendBodyCommand()`** to dome fork — no behaviour change yet, just
   the function.
2. **Add `sendBodyCommand()` calls** to `:SE01`–`:SE16` sequence handlers.
   Test: trigger a sequence in dome web UI → body serial shows the command.
3. **Add `#PAHB` TX** to protoArtoo `DomeLinkTask`. Test: logic analyser or
   serial monitor on slip ring shows `#PAHB\r` at 1 Hz.
4. **Replace `marcduinoSerial.setStream()`** with `handleBodySerial()` in dome
   fork. Test: existing dome commands still work (panels, holos, logic).
5. **Add `#PAHB` intercept** in `handleBodySerial()`. Test: dome sees
   heartbeats, `sBodyHeartbeatRx` increments.
6. **Add `#APHB` TX** in dome `handleBodyLinkHeartbeat()`. Test: body
   `domeHbRx` increments.
7. **Add `#APHB` handling** in protoArtoo `marcduino_rx.cpp`. Test:
   `domeConnected()` returns true.
8. **Add `body_link` to dome `/api/status`** — verify JSON fields.
9. **Add web UI status badge** — verify all three states display correctly.
10. **Add `dome_link` to protoArtoo `/api/status`** — verify JSON fields.

---

*This document is the implementation contract between protoArtoo and
mattiasbrandt/AstroPixelsPlus. All code patterns follow the actual upstream
AstroPixelsPlus architecture — NVS key naming convention, Reeltwo dispatch
patterns, and ESPAsyncWebServer HTML structure.*  
*Last updated: March 2026*
