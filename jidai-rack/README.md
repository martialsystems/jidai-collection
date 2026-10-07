# JIDAI RACK

One plugin that holds the Jidai Collection in a single rack: BUSHIDO, the 3 x 12 step sequencer, and RONIN, the modular synthesizer. Both engines are compiled into the one binary; neither plugin hosts the other.

- **A real rack.** Devices are screwed in between two rails, flush, top to bottom, with empty rack space below. Each device keeps its own panel; its ears carry the screws, the fold arrow (fold it to a strip; its cables stay patched, hidden), its name and its remove button.
- **Device browser.** The column on the left. A search box at the top filters by name. Devices are grouped Sequencer (BUSHIDO), Voice (RONIN) and Effect (RONIN, processing audio at its EXT IN). Each row shows the name, one short line, and how many are already on the rack. Drag a row onto the rack to add one where you drop it, or click it to add one at the bottom. Grab a device by an ear to move it; drag it back onto the browser, or out of the window, to remove it. The x on its ear removes it too. Any number of each; an empty rack is fine. A new instance starts with one BUSHIDO above one RONIN.
- **Cables** run between any two jacks in the rack, across devices: a BUSHIDO CV into a RONIN HZ/V, a RONIN MG into the BUSHIDO CLOCK, and so on. Outputs fan out, inputs sum, and the newest cable that closes a loop is delayed one sample.
- **Panels.** Each device keeps its own panel, knobs, and screen. Both screens have banks A and B, up to 999 entries each. Bank A is the factory set: 28 BUSHIDO patterns, 13 RONIN presets. Bank B starts with the rack patches LOOP BASS and RING SEED, which set both instruments and the cables between them. SAVE stores the panel as the next entry in the lit bank.
- **Audio.** The VST3 is an effect: stereo in, stereo out. Host audio feeds the first RONIN's EXT IN. The rack's output is the sum of every RONIN's OUTPUT (Effect off is dry, Effect on is wet, Level scales it). BUSHIDO's MIDI out still runs inside the rack; it has no plugin MIDI port yet.
- **Standalone** opens the computer's default input and output. With no input it runs with silence in.

## Build

JUCE 8, CMake 3.22+. The BUSHIDO and RONIN sources are fetched at pinned commits.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Targets: `JidaiRack_VST3`, `JidaiRack_Standalone`, `JidaiRackTests` (rack unit tests), `JidaiPresetTests` (factory banks and rack patches), `JidaiRackProbe` (drives the window offscreen and writes PNGs; on Linux run it under `xvfb-run -a`).

To build against local checkouts, pass `-DFETCHCONTENT_SOURCE_DIR_JUCE=…`, `-DFETCHCONTENT_SOURCE_DIR_BUSHIDO=…` and `-DFETCHCONTENT_SOURCE_DIR_RONIN=…`.

Smoke launch: `"JIDAI RACK" --smoke out.wav` runs one second of the default rack on the default output and writes it to `out.wav`. With no output device it prints `SMOKE SKIP` and exits 0.

## Legal

Copyright (c) 2026 Martial Systems LLC. All rights reserved.

BUSHIDO is inspired by the Korg SQ-10 and RONIN by the Korg MS-50. Korg, SQ-10 and MS-50 are trademarks of their respective owners. Martial Systems is not affiliated with or endorsed by Korg.
