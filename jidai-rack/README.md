# JIDAI RACK

**BUSHIDO, RONIN, ORIGAMI and SHOGUN in one rack, in one plugin.**

JIDAI RACK puts the Jidai Collection in a single rack cabinet. Every device is compiled into one binary, so cables run sample by sample between all of them. This page covers using the rack and building it. For an overview of the collection, see the [main README](../README.md). The user manual is in [`../docs/manual/`](../docs/manual/JIDAI_RACK_Manual.md).

## The window

- **Header.** FRONT and BACK, the four cable views, FOLD ALL, the RACKS menu of starter racks, the rack's total latency (LAT), and the UI scale. The window opens at 1200 x 672 and can shrink to 960 x 540. The rack scrolls.
- **Device browser.** The column on the left, with a search box at the top. Devices are grouped as SEQUENCER (BUSHIDO), VOICE (RONIN), DRUMS (SHOGUN), EFFECT (RONIN FX, ORIGAMI) and UTILITY (RACK I/O). Each card shows how many of that device are already in the rack. Drag a card onto the rack to add a device where you drop it, or click it to add one at the bottom. The button at the top right collapses the browser to a thin strip.
- **The rack.** Devices sit between two rails, top to bottom. Each one has a tab strip with its fold button, name, OPEN/CLOSED, its page tabs, its latency, BYPASS and a remove button. Drag a device by its strip to move it. Drag it back onto the browser, or press its remove button, to take it out.

## Devices

- **RACK I/O** (1 U, always at the top, one per rack) is the rack's only connection to your DAW. A new rack starts with just RACK I/O.
  - HOST IN L/R: the DAW's audio, coming into the rack.
  - MAIN OUT L/R: what the DAW hears. OUT R follows OUT L while it is unpatched. MAIN sets the level.
  - MIDI: NOTE (1 V/oct, 0 V = C3), HZ/V LIN (1 V = C3, for RONIN's linear input), GATE and VEL.
  - TRANSPORT: CLK 1/16 (one pulse per sixteenth note while the DAW plays), RUN and RESET.
- **BUSHIDO** (3 U): the 3 x 12 step sequencer.
- **RONIN** (4 U): the semi-modular synthesizer. **RONIN FX** is the same device with the DAW's audio patched into its EXT IN.
- **ORIGAMI** (3 U): the stereo triple wave folder.
- **SHOGUN** (5.8 U open, 4 U on the back): the drum machine. A new SHOGUN follows the host transport. Click its KIT display for the factory kits.

When you add a device, the rack patches it for you. Audio outputs go to MAIN OUT, an ORIGAMI takes HOST IN, and a RONIN placed directly under a BUSHIDO gets row A's pitch and gate. Hold Shift while adding to place a device without cables.

## Patching

- **Front and back.** The front shows each device's own panel. The back shows a generated rear plate for every device with every jack, including back-only ones. Press Tab to flip.
- **Making cables.** Drag from an empty jack to another jack. Compatible jacks ring green, the rest are dimmed, and an incompatible jack tells you why it can't connect. Drag a plugged end to reroute that same cable, or drop it in empty space to remove it. Option-drag (Mac) or Alt-drag (Windows) stacks another cable on a jack that is already patched. Click a cable to bring it to the front. Pick a color on the cable bar to dim the jacks that can't take it. Right-click a cable for Remove, Bring to front and Colour, or a jack for Connect to..., Disconnect and Go to. Cmd+Z / Ctrl+Z undoes cable changes.
- **How signals combine.** One output can feed many inputs. Several cables into one input are summed. The newest cable that closes a loop is delayed by one sample and marked z⁻¹.
- **Cable views.** Press K to cycle between them.
  - ALL: every cable as a rope.
  - HIDE PASS-THRU: ropes only between jacks on the same device or on neighbouring devices. Longer cables become short stubs tagged with their far end.
  - SELECTED: the selected device's cables at full strength, the rest faint.
  - HIDE: a plug in each patched jack and no ropes.

  A cable whose far end is out of view, such as on a closed device or the other side of the rack, is drawn as a tagged stub.
- **Colours and badges.** A cable takes the colour of its source jack's signal role. Badges show latency compensation on cables into MAIN OUT, arrival skew between paths, and warnings such as mismatched pitch laws.
- **Open, closed and folded.** Press C to open or close the selected device. Closed is a 1 U strip of essentials with no jacks, so open it or flip to the back to patch. Press F to fold the selected device to its strip, or Shift+F to fold or unfold every device.
- **Other keys.** Esc cancels a cable drag or clears the selection. Delete removes the selected cable.

## Sound and timing

- **Latency compensation.** Each path into MAIN OUT is delayed to match the slowest one, and the rack reports that total to the DAW.
- **RONIN** runs at its standard rate in the rack so its jacks stay sample-accurate with the other devices. The standalone RONIN plugin keeps its 2x HQ mode.
- **ORIGAMI** reports 0 samples at QUALITY 1x and 46 at 2x. The rack compensates for both.
- **SHOGUN** reports 0, 23 or 26 samples at oversampling 1x, 2x or 4x.
- **BUSHIDO's MIDI** still runs inside the rack, but the rack has no MIDI output port yet.

## Presets and saved racks

BUSHIDO's and RONIN's screens each have banks A and B with up to 999 entries. Bank A is that instrument's factory set. Bank B holds the rack patches and your saves, and SAVE stores the panel as the next entry in the lit bank. SHOGUN's factory kits open from its KIT display. The RACKS menu loads the starter racks.

Racks saved by earlier versions load and convert automatically. The old host routing becomes a RACK I/O with matching cables, and BUSHIDO and RONIN settings move to their current formats. The header then shows a notice of what changed. A rack saved by a newer version is not loaded.

## Build

Needs CMake 3.22 or later and a C++20 compiler. JUCE 8.0.4 and the BUSHIDO, RONIN, ORIGAMI and SHOGUN sources are downloaded at pinned commits during configure.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure    # on Linux: xvfb-run -a ctest ...
```

| Target | What it is |
|---|---|
| `JidaiRack_VST3`, `JidaiRack_Standalone` | The plugin and the standalone app. |
| `JidaiRackTests` (`rack`) | Rack unit tests: graph, devices, cables, feedback and latency. |
| `JidaiRackProbe` (`rack_ui`) | Drives the window offscreen with mouse and keys and writes screenshots to `rack-shots/`. |
| `JidaiPresetTests` (`presets`) | Factory banks, saved patches, rack state and loading older versions. |
| `JidaiCommonTests`, `JidaiDspTests`, `JidaiHeaderHygiene` (`common`, `dsp`, `headers`) | The shared jidai-common library. |
| `OrigamiTests`, `OrigamiPluginTests` (`origami`, `origami_plugin`) | ORIGAMI, built from its own repository. |

To build against local checkouts, pass `-DFETCHCONTENT_SOURCE_DIR_JUCE=…`, `-DFETCHCONTENT_SOURCE_DIR_BUSHIDO=…`, `-DFETCHCONTENT_SOURCE_DIR_RONIN=…`, `-DFETCHCONTENT_SOURCE_DIR_ORIGAMI=…` and `-DFETCHCONTENT_SOURCE_DIR_SHOGUN=…`.

BUSHIDO, RONIN, ORIGAMI and SHOGUN each carry their own copy of jidai-common. The rack compiles all four against the copy in [`../jidai-common`](../jidai-common), so the binary has one copy of each header.

**Smoke test.** `"JIDAI RACK" --smoke out.wav` runs one second of the default rack on the default audio output and writes it to `out.wav`. With no output device it prints `SMOKE SKIP` and exits 0.

## Legal

Copyright © 2026 Martial Systems LLC. All rights reserved. See [LICENSE](LICENSE) for terms and trademark notices.
