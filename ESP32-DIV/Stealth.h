#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Stealth — receive only. Nothing leaves the antennas.
 *
 * Settings > Stealth Mode. While it is on, every tool whose job is to
 * transmit refuses to start and says why, and every tool that listens keeps
 * working.
 *
 * ── The part that is easy to get wrong ──────────────────────────────────────
 *
 * Listening is not always silent. Three things in this firmware transmit
 * while looking like receivers, and they are the reason this file exists
 * rather than a list of jammers:
 *
 *   - a Wi-Fi scan defaults to ACTIVE. WiFi.scanNetworks()'s third argument
 *     is `passive` and it is false unless you say otherwise, so a plain
 *     "scan for networks" sends a probe request on every channel with this
 *     device's MAC in it.
 *   - a BLE scan defaults to active too. setActiveScan(true) sends SCAN_REQ
 *     to every advertiser it hears, which is a broadcast of "something is
 *     here and it is interested in you".
 *   - the wardriver uploads to WiGLE, which means associating to an access
 *     point.
 *
 * So those are not blocked, they are made quiet: the scans go passive and
 * the upload is refused. A tool that listens should keep listening.
 *
 * ── What this does not do ───────────────────────────────────────────────────
 *
 * It does not turn the radios off. The Wi-Fi and BLE stacks stay
 * initialised, because every receiver here needs them, and an initialised
 * stack is one API call away from transmitting. This is a gate on the
 * firmware's own transmit paths, not a hardware interlock, and it is exactly
 * as good as the list of gates in check_stealth.py -- which is why that list
 * is checked rather than trusted.
 *
 * It also says nothing about the beacon firmware. PUEO_ROLE=beacon is a
 * transmitter from end to end; there is no stealth mode in it and there is
 * no sense in one.
 * ────────────────────────────────────────────────────────────────────────── */

#include <Arduino.h>

namespace Stealth {

/* Is stealth on? Cheap enough to call inside a scan-argument list, which is
 * where several of the callers are. */
bool on();

/* Refuse to run `feature`, and say so on the screen.
 *
 * Returns true when the caller must return immediately -- it has already
 * set feature_exit_requested, so the dispatch loop will not run the
 * feature's loop(). Returns false, and draws nothing, when stealth is off.
 *
 *     void someTransmitterSetup() {
 *       if (Stealth::refuse("Beacon Spammer")) return;
 *       ...
 *     }
 */
bool refuse(const char* feature);

/* The same refusal, for one action inside a screen that is otherwise
 * passive. Leaves feature_exit_requested alone, so declining to transmit
 * does not also close the list the user was reading. */
bool refuseAction(const char* action);

}  // namespace Stealth
