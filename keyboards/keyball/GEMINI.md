# GEMINI.md - Keyball Keyboard Project Knowledge Base

## 1. Project Overview
**Project**: Keyball Tuner & Firmware Pointer Acceleration
**Goal**: Provide a comprehensive solution for custom mouse acceleration on Keyball trackball keyboards. This includes:
1.  **Modular Firmware**: Support for multiple acceleration algorithms (Simple, LUT-based, etc.).
2.  **Tuning Tool**: A premium, real-time GUI for visually tuning the LUT-based algorithm.
**Core Technologies**:
-   **Firmware**: QMK (C), RawHID, Fixed-Point Arithmetic (Q8.8).
-   **Frontend**: HTML5, Vanilla JS, CSS (Dark Mode), WebHID API.

## 2. Project Structure & Ecosystem
This knowledge base covers the customized Keyball firmware ecosystem.

### Key Directories
*   **User Keymap**: `keyboards/keyball/keyball44/keymaps/daniel2887/`
    *   Primary workspace for the user `daniel2887`. Contains `keymap.c`, `config.h`, `rules.mk`.
    *   `keymap.c` configures various keyball behaviors according to user daniel2887's personal preferences. Any edits to this file should preserve user-facing behavior (e.g., key mapppings, trackball acceleration, etc.) unless changes in this behavior are explicitly requested. If the request is unclear, ask for clarification.
    *   **Note**: All configuration changes (e.g., `KEYBALL_ACCEL_MODE`) are made here.
*   **Core Driver**: `keyboards/keyball/lib/keyball/`
    *   `keyball.c`: Core logic, custom acceleration algorithms, RawHID dispatch.
    *   `keyball.h`: Config definitions, structs, mode defines.
*   **Tools**: `keyboards/keyball/keyball44/keymaps/daniel2887/tools/`
    *   `keyball_tuner/tuner.html`: Standalone WebHID Tuner for LUT acceleration.

### Terminal Commands
*   **grep**: When searching a path using `grep`, use forward slashes `/` instead of backslashes `\` in the path.
    *   Example: `grep -r "some_string" C:/foo/bar/buz/`

## 3. Architecture & Data Flow
The system consists of a standalone HTML tool that communicates with the QMK firmware via RawHID packets.

### 2a. Acceleration Architecture (Modular)
All acceleration algorithms require `KEYBALL_POINTER_ACCEL_ENABLE` to be defined in `config.h`. The firmware supports swappable acceleration algorithms also via `config.h`, :
*   **Legacy Mode** (`KEYBALL_ACCEL_MODE_SIMPLE`): Uses the simple linear `BASE + SPEED * FACTOR` formula from `config.h`.
*   **Legacy Mode** (`KEYBALL_ACCEL_MODE_LUT`): Uses a 32-point lookup table (LUT) to map raw trackpoint speed to a lower (decelerated) or higher (accelerated) value.

To switch modes, define `KEYBALL_ACCEL_MODE` in the user's `config.h`.

### 2b. LUT Mode Architecture (Tuner Integration)
The Tuner tool currently only supports `KEYBALL_ACCEL_MODE == KEYBALL_ACCEL_MODE_LUT`, but may expand to support live tuning of other acceleration modes in the future.

In LUT mode, the tool functions as follows:

### Acceleration calculated in firmware (QMK)
-   **Location**: `keyboards/keyball/lib/keyball/keyball.c`
-   **State**: `keyball_accel_t` struct (LUT[32], global_gain, max_speed_limit).
-   **Math**: acceleration is applied using a 32-point Lookup Table (LUT) with linear interpolation.
-   **Protocol**: 32-byte RawHID packets.
-   **Key Definition**: `RAW_ENABLE = yes` in `rules.mk` is mandatory.

### Acceleration tuned in frontend tuner tool (`tuner.html`)
-   **Location**: `keyboards/keyball/keyball44/keymaps/daniel2887/tools/keyball_tuner/tuner.html`
-   **Graphing**: HTML5 Canvas. Custom `SplineInterpolator` (Monotone Cubic Hermite) ensures strict monotonicity.
-   **Communication**: `navigator.hid`. Requires HTTPS or `localhost` context.

## 4. RawHID Protocol Specification
Communication between the tuner tool and the firmware is via RawHID packets:
* **Usage Page**: `0xFF60`, **Usage**: `0x61`
* **Packet Size**: 32 bytes

Commands sent to the keyboard via this protocol are defined in `keyboards/keyball/lib/keyball/keyball.c`. See enums such as CMD_SET_CURVE_PT, CMD_SET_CURVE_ALL, CMD_READ_ALL, CMD_GET_SPEED, CMD_RESET_CONFIG, etc.

> **IMPORTANT**: The visualizer relies on **Peak Hold** logic in the firmware. The firmware tracks `max_speed` seen since the last `CMD_GET_SPEED` request.

## 5. Drashna Acceleration Integration (Dec 2025)

### A. Non-Intrusive Integration Strategy
*   **Goal**: Integrate the community `pointing_device_accel` module without modifying its source code, allowing for easy upstream updates.
*   **Challenges**:
    *   The module expects QMK hook machinery (`process_record`, `keyboard_post_init` chained calls) which isn't present in our standalone driver environment.
    *   It references custom keycodes that are not defined in our keymap.
*   **Solution**: **Shim Architecture**
    1.  **Original Source**: `lib/keyball/pointing_device_accel.c` is kept byte-identical to the upstream version.
    2.  **Shim Header**: `lib/keyball/pointing_device_internal.h` acts as a local proxy. It:
        *   Forwards includes to the real `quantum/pointing_device_internal.h`.
        *   Provides dummy definitions for the custom keycodes (mapped to safe `QK_USER` range) so the module compiles.
    3.  **Keyball Glue**: `lib/keyball/keyball.c` implements the necessary hook functions (`_kb` suffixes) as no-ops.
    4.  **Runtime Config**: Instead of keycodes, we configure the module by writing directly to its internal state structs via `pointing_device_accel_set_*()` functions during `keyball_set_acceleration_data()`.

### B. Configuration & State
*   **Mode ID**: `KEYBALL_ACCEL_MODE_DRASHNA`.
*   **Parameters**: Added `accel_drashna` struct to `keyball_accel_t`:
    *   `takeoff`: Minimum speed to start accelerating.
    *   `growth_rate`: Steepness of the sigmoid curve.
    *   `offset`: X-axis shift for the curve.
    *   `limit`: Maximum acceleration factor cap.
*   **Data Flow**:
    *   `keymap.c` defines defaults in `keyboard_post_init_user()`.
    *   `keyball.c` receives the config struct.
    *   `keyball.c` syncs the values to Drashna's global config using the module's setter API.
    *   `apply_acceleration()` now dispatches to `pointing_device_task_pointing_device_accel()` which handles its own accumulation and state.