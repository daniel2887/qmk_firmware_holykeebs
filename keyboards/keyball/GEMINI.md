# GEMINI.md - Keyball Tuner Project Knowledge Base

## 1. Project Overview
**Project**: Keyball Tuner & Firmware Acceleration
**Goal**: Provide a comprehensive solution for custom mouse acceleration on Keyball trackball keyboards. This includes:
1.  **Modular Firmware**: Support for multiple acceleration algorithms (Simple, LUT, Custom).
2.  **Tuning Tool**: A premium, real-time GUI for visually tuning the LUT-based algorithm.
**Core Technologies**:
-   **Firmware**: QMK (C), RawHID, Fixed-Point Arithmetic (Q8.8).
-   **Frontend**: HTML5, Vanilla JS, CSS (Dark Mode), WebHID API.

## 2. Project Structure & Ecosystem
This knowledge base covers the customized Keyball firmware ecosystem.

### Key Directories
*   **User Keymap**: `keyboards/keyball/keyball44/keymaps/daniel2887/`
    *   Primary workspace for the user `daniel2887`. Contains `keymap.c`, `config.h`, `rules.mk`.
    *   **Note**: All configuration changes (e.g., `KEYBALL_ACCEL_MODE`) are made here.
*   **Core Driver**: `keyboards/keyball/lib/keyball/`
    *   `keyball.c`: Core logic, custom acceleration algorithms, RawHID dispatch.
    *   `keyball.h`: Config definitions, structs, mode defines.
*   **Tools**: `keyboards/keyball/keyball44/keymaps/daniel2887/tools/`
    *   `keyball_tuner/tuner.html`: Standalone WebHID Tuner for LUT acceleration.

## 3. Architecture & Data Flow
The system consists of a standalone HTML tool that communicates with the QMK firmware via RawHID packets.


### 2a. Acceleration Architecture (Modular)
The firmware supports swappable acceleration algorithms via `config.h`:
*   **LUT Mode** (`KEYBALL_ACCEL_MODE_LUT`): The current default. Uses the 32-point Tuner-compatible curve.
*   **Legacy Mode** (`KEYBALL_ACCEL_MODE_SIMPLE`): Uses the simple linear `BASE + SPEED * FACTOR` formula from `config.h`.
*   **Custom Mode** (`KEYBALL_ACCEL_MODE_CUSTOM`): Reserved for future algorithms.

To switch modes, define `KEYBALL_ACCEL_MODE` in the user's `config.h`.

### 2b. LUT Mode Architecture (Tuner Integration)
**Scope**: Applicable ONLY when `KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_LUT`.

In LUT mode, the ecosystem functions as follows:

```mermaid
graph LR
    User[User Input] -->|Drag Curve| Frontend[tuner.html]
    Frontend -->|WebHID (0xFF60)| Firmware[QMK keyball.c]
    Firmware -->|Mouse Report| OS[Operating System]
    Firmware -->|CMD_GET_SPEED| Frontend
```

### Firmware (QMK)
-   **Location**: `keyboards/keyball/lib/keyball/keyball.c`
-   **State**: `keyball_accel_t` struct (LUT[32], global_gain, max_speed_limit).
-   **Math**: acceleration is applied using a 32-point Lookup Table (LUT) with linear interpolation.
-   **Protocol**: 32-byte RawHID packets.
-   **Key Definition**: `RAW_ENABLE = yes` in `rules.mk` is mandatory.

### Frontend (`tuner.html`)
-   **Location**: `.../tools/keyball_tuner/tuner.html`
-   **Graphing**: HTML5 Canvas. Custom `SplineInterpolator` (Monotone Cubic Hermite) ensures strict monotonicity.
-   **Communication**: `navigator.hid`. Requires HTTPS or `localhost` context.

## 4. RawHID Protocol Specification (LUT Mode Only)
**Scope**: Applicable ONLY when `KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_LUT`.

**Usage Page**: `0xFF60`, **Usage**: `0x61`
**Packet Size**: 32 bytes

| Command | ID | Parameters | Description |
| :--- | :--- | :--- | :--- |
| **SET_PT** | `0x10` | `idx`, `val_hi`, `val_lo` | Update single LUT point (and metadata). |
| **SET_ALL** | `0x11` | `offset`, `count`, `data...` | Bulk update LUT points (chunked). |
| **READ_ALL** | `0x12` | `offset` | Request config. Offset 0=Meta, 1..3=LUT chunks. |
| **GET_SPEED**| `0x20` | None | Request last movement speed (for visualizer). |

> **IMPORTANT**: The visualizer relies on **Peak Hold** logic in the firmware. The firmware tracks `max_speed` seen since the last `CMD_GET_SPEED` request.
> `CMD_READ_ALL` echos the `offset` in byte 1 to allow reliable chunk tracking in JS.

## 5. Critical Technical Constraints & Lessons Learned (Tuner/LUT)
**Scope**: These constraints primarily apply to the **LUT Algorithm** and the **Web Tuner**.

### A. Monotonicity is King
*   **Concept**: Acceleration curves must be strictly non-decreasing. A dip in the curve means the cursor *slows down* as you move the ball *faster*, which feels broken.
*   **Implementation**:
    *   **Drag Logic**: `enforceMonotonicPush(idx)` ensures moving a point pushes its neighbors up/down to maintain order.
    *   **Interpolation**: Standard cubic splines overshoot and oscillate. You **MUST** use **Monotone Cubic Hermite Interpolation** (Fritsch-Carlson method) to guarantee the curve stays strictly monotonic between control points.

### B. X-Axis Scaling ("Max Raw")
*   **Concept**: Users need to tune for their specific trackball flick speeds. "Max Raw" defines the sensor speed at the right edge of the graph.
*   **Gotcha**: When changing "Max Raw" (e.g., 127 -> 80):
    *   You must **clamp** existing control points to the new max.
    *   The **last control point** should "snap" to the new edge so the curve always spans the full graph.
    *   **Presets** (Linear, S-Curve) must scale their X-coordinates relative to the current `Max Raw` (e.g., `x = 127 * scale` becomes `x = 80`).

### C. Firmware Arithmetic (Q8.8)
*   QMK runs on MCUs where float math is slow/heavy.
*   We use **Fixed Point Q8.8**:
    *   `1.0x` gain = `256`.
    *   `1.5x` gain = `384`.
    *   Formula: `output = input * (lut_val * global_gain) >> 8`.
*   **Do Not** send floats to the firmware. Convert to integers in JS (`Math.round(val * 256)`).

### D. UX & Polish "Do's"
*   **Real-time Feedback**:
    *   **Live Cursor**: Dashed crosshair shows instantaneous `(raw_speed, acceleration)` intersection. It helps users establish "where am I on this curve?".
    *   **Ghost Trail**: A "hysteresis" trail of recent movement points (e.g., from a fling). This allows users to "fling then tune" - performing a gesture, seeing the trace of raw speeds it generated, and then adjusting the curve to match that specific speed range.
    *   **Implementation**: A rolling history buffer captures speeds during movement; it freezes when motion stops so the user can reference it while tuning. It resets only on new movement.
*   **Smoothing**: The visualization cursor needs a lerp (`current += (target - current) * 0.2`) to look professional. Raw feedback is too jittery.
*   **Visualization**: Draw the actual LUT points (gray x's) on top of the curve. This builds trust that the FW interprets the curve exactly as drawn.
*   **Inputs**: Number inputs should correct invalid values immediately on blur (e.g., entering `200` clamps to `127`).

## 6. Critical User Journeys
1.  **Connection**: User clicks "Connect" -> `navigator.hid.requestDevice`.
2.  **Sync**: On connect, Frontend sends `CMD_READ_ALL`. Firmware responds. Frontend populates graph/sliders.
3.  **Tuning**:
    *   User adjusts "Global Sensitivity" (Gain).
    *   User moves control points.
    *   **Live Update**: Frontend debounces slightly but sends `CMD_SET_CURVE_ALL` almost real-time so users feel changes instantly.
4.  **Verification**: User flicks trackball.
    *   Visualizer shows "Raw Speed".
    *   Cursor moves on graph.
5.  **Saving**: User copies the generated C code from "Keymap Config" panel into their `keymap.c` and recompiles. (We do not support EEPROM persistence yet to keep FW simple).

## 7. Protocol V2: Persistence & Versioning (Critical)

**Why Versioning?**
The firmware now stores the exact Control Points (`keyball_point_t`) alongside the LUT. This allows users to reload their editing state perfectly.
However, control points only make sense if the **Interpolation Algorithm** used to generate the curve from them is identical on both FW creation time and UI load time.

### The Golden Rule: `ACCEL_ALGO_VER`
*   **Current Version**: `1` (Monotone Cubic Hermite Spline).
*   **The Rule**: If you **EVER** change the interpolation logic in `solveCurve()` (e.g., switch to Catmull-Rom, Bezier, or change tension parameters), you **MUST increment `ACCEL_ALGO_VER`** in both:
    *   `keyball.h` (`#define ACCEL_ALGO_VER_2 2`)
    *   `tuner.html` (Logic to handle ver 2).
*   **Mismatch Behavior**:
    *   If FW returns `ver=1` and UI uses `ver=2`: The UI **MUST NOT** load the points blindly. It should fall back to **Reverse Engineering (RDP)** the LUT to generate *new* compatible control points that approximate the shape.
    *   **Reason**: Loading Ver 1 points into a Ver 2 algorithm will produce a curve that looks different from what the user tuned, breaking trust.

### Protocol Specification (V2)
| Offset | Content |
| :--- | :--- |
| **0** | `[CMD, ECHO, MAX_H, MAX_L, GAIN_H, GAIN_L, VER, NUM_POINTS]` |
| **4** | Points 0-6 (28 bytes) -> `[X_H, X_L, Y_H, Y_L]...` (7 pts max) |
| **5** | Points 7-13 (28 bytes) -> `[X_H, X_L, Y_H, Y_L]...` (7 pts max) |
| **Cmd 0x13** | `CMD_SET_POINTS`: `[CMD, IDX, CNT, VER, P0_X_H, P0_X_L, P0_Y_H, P0_Y_L...]` |

> **Note**: Both X and Y coordinates in V2 are stored as **Q8.8 Fixed Point** (uint16_t). `Val = Round(Real * 256)`.

## 8. Future Development / Roadmap
*   **EEPROM Support**: Currently configuration is lost on reboot unless compiled into `keymap.c`. Future: Save structs to EEPROM.
*   **Profile Switching**: Store multiple curves in FW and switch via keycode.
*   **Per-Axis Tuning**: Separate curves for X and Y (rarely needed for trackballs but possible).

## 9. Troubleshooting Cheatsheet
*   **"Raw Sensor Speed" stays at 0**: Check `RAW_ENABLE = yes`. Ensure firmware was re-flashed. Check `console.log` for input reports.
*   **Curve looks weird/flat**: Check `Max Accel` scale. Check if `Max Raw` is too low for your flick usage.
*   **Tooltips/Cursor not working**: Ensure `draw()` loop order is correct (Grid -> Curve -> Cursor -> Tooltips).
