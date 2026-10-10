## Download: 
- [Mac](https://github.com/martialsystems/jidai-collection/releases/latest/download/JIDAI-RACK-macOS.zip)
- [Windows](https://github.com/martialsystems/jidai-collection/releases/latest/download/JIDAI-RACK-Windows.zip)

> **Found a bug?** Please [open a GitHub issue](https://github.com/martialsystems/jidai-collection/issues/new/choose) and fill in the bug report form.

---

# The Jidai Collection

**Patchable instruments and effects from Martial Systems, and one rack to hold them all.**

JIDAI RACK is the plugin to install. BUSHIDO, RONIN, ORIGAMI and SHOGUN are already in it. Every jack takes a cable, and a cable runs from any device to any other, so a sequence from BUSHIDO drives a voice in RONIN with one drag. The separate plugins are one device at a time: BUSHIDO sending MIDI to other instruments, RONIN as one voice on a track, SHOGUN as a drum machine, and ORIGAMI as an insert.

## The devices

### BUSHIDO: step sequencer
A 3 x 12 analog-style step sequencer with real patch cables. Three rows of twelve steps send pitch CV, gates and a trigger per step. Loop 12 steps, chain two rows into 24, or swap rows on every pass. Clock it internally, from your DAW, or from any pulse in the rack, and drive any synth in your DAW over MIDI.
[github.com/martialsystems/bushido](https://github.com/martialsystems/bushido)

### RONIN: semi-modular synthesizer
A full modular voice in one plugin that you can also patch as an effect: VCO, a self-oscillating diode-bridge filter, two VCAs, two envelopes, an LFO, noise, a ring modulator, sample and hold, and more. Run your track through it, let it play itself, or blend it back in with one MIX knob.
[github.com/martialsystems/Ronin](https://github.com/martialsystems/Ronin)

### SHOGUN: drum computer
Fourteen drum voices and two synth voices, a step sequencer for every voice, and a 151-jack patch bay that cables to the rest of the collection. Lock it to your DAW's song position or clock it from a jack.
[github.com/martialsystems/shogun](https://github.com/martialsystems/shogun)

### ORIGAMI: triple wave folder
A stereo effect with three folding stages under one WAVE macro. Each stage has its own trim, symmetry and audio-rate VC input. Around the folder are a pre-fold VCA, emphasis, LEVEL COMP and a time-aligned dry/wet mix.
[github.com/martialsystems/origami](https://github.com/martialsystems/origami)

## JIDAI RACK

**BUSHIDO, RONIN, ORIGAMI and SHOGUN in one rack, in one plugin.**

JIDAI RACK is a rack cabinet in a plugin window. Drag devices in from the browser, screw in as many of each as you like, and cable any jack to any other.

- **Front and back.** The front shows each device's own panel. Press Tab to flip the rack and patch on the generated back plates, which show every jack, including back-only ones.
- **Four cable views.** ALL shows every cable. HIDE PASS-THRU draws long cables as short stubs tagged with their far end. SELECTED highlights the selected device's cables. HIDE shows only the plugs. Press K to cycle between them.
- **Open, closed or folded.** Open a device for its full panel, close it to a 1 U strip of its essential controls, or fold it down to its name bar. Its cables stay patched in every state, and every device has its own BYPASS and latency readout.
- **RACK I/O.** A single 1 U module at the top connects the rack to your DAW. Your DAW's audio comes out of HOST IN, and the DAW hears whatever is patched into MAIN OUT. MIDI arrives as NOTE (1 V/oct), HZ/V LIN, GATE and VEL. Host transport arrives as a 1/16 clock, RUN and RESET.
- **Latency compensation.** Every path into MAIN OUT is delayed to line up with the slowest one, and the rack reports the total to your DAW. Cables into MAIN OUT show their compensation, and the header shows the total LAT.
- **Feedback that behaves.** Loops are allowed. The newest cable in a loop is delayed by exactly one sample, so self-patching stays stable and repeatable.
- **Smart insert.** A new device is wired up for you: audio outputs go to MAIN OUT, an ORIGAMI takes the host input, and a RONIN under a BUSHIDO gets pitch and gate. Hold Shift while adding to skip this.
- **Role colours.** Cables take the colour of the signal they carry (audio, 1 V/oct, gate, CV and so on), and a badge warns when a cable joins mismatched pitch laws.
- **Your patches carry over.** Racks saved by earlier versions load and convert automatically, BUSHIDO and RONIN settings included, and the header shows a notice of what changed.
- **Presets.** BUSHIDO's and RONIN's screens each have banks A and B of up to 999 entries. Save your own patches into bank B. SHOGUN's KIT display opens its factory kits.
- **Starter racks.** The RACKS menu in the header loads ready-patched racks, grouped as INIT, ACID, EDM and FX.
- **Manual.** The full user manual is in [`docs/manual/`](docs/manual/JIDAI_RACK_Manual.md) (Markdown and PDF).

### Devices in the rack

| Device | Browser group | Size | Notes |
|---|---|---|---|
| BUSHIDO | Sequencer | 3 U | Runs on its own clock or follows the host. |
| RONIN | Voice | 4 U | A full RONIN, any number of them. |
| RONIN FX | Effect | 4 U | A RONIN that takes the host input at its EXT IN. |
| ORIGAMI | Effect | 3 U | The full ORIGAMI, with VC and VCA CV jacks. |
| SHOGUN | Drums | 5.8 U | The drum machine, with every voice's jacks on the back. Follows the host transport. |
| RACK I/O | Utility | 1 U | One per rack, always at the top. |

## Quick start

1. Download JIDAI RACK above and copy `JIDAI RACK.vst3` into your VST3 folder (`~/Library/Audio/Plug-Ins/VST3/` on macOS, `C:\Program Files\Common Files\VST3` on Windows), or run the standalone app. To build it yourself, see Build.
2. In your DAW, insert JIDAI RACK as an effect on a track. Send it MIDI to use RACK I/O's MIDI jacks.
3. Click BUSHIDO, then RONIN, in the browser. RONIN lands under BUSHIDO already patched for pitch and gate, with its output going to MAIN OUT.
4. Press Tab to flip to the back and start patching.

## Build

Needs CMake 3.22 or later and a C++20 compiler. JUCE 8.0.4 and the BUSHIDO, RONIN, ORIGAMI and SHOGUN sources are downloaded at pinned commits during configure.

```sh
cmake -S jidai-rack -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The plugin lands in `build/JidaiRack_artefacts/Release/`, in `VST3/JIDAI RACK.vst3` and `Standalone/`. See [`jidai-rack/README.md`](jidai-rack/README.md) for every target and for building against local checkouts.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

On Linux, run it under `xvfb-run -a` for the UI tests.

| Test | What it covers |
|---|---|
| `rack` | The rack graph, devices, cables, feedback, latency compensation and the cable standard rules. |
| `rack_ui` | Drives the rack window offscreen with mouse and keys, and writes screenshots. |
| `presets` | Factory banks, saved patches and rack state, including loading older versions. |
| `common`, `dsp`, `headers` | The shared jidai-common library: the cable standard, the shared DSP, and header hygiene. |
| `origami`, `origami_plugin` | ORIGAMI's DSP core and plugin. |

## Compatibility

- **Jidai Cable Standard v1.1.** Every device shares the same rules: ±5 V audio, 0/5 V gates read with a Schmitt trigger (high above 1.0 V, low below 0.5 V), role colours, and stable jack names.
- **Pitch.** 1 V/oct with C3 = 130.81 Hz at 0 V. RONIN's linear HZ/V input has its own jack, and RACK I/O provides both.
- **Formats.** JIDAI RACK is a VST3 effect with MIDI input, plus a standalone app. The release zips are macOS and Windows. The same CMake project builds on Linux.
- **RONIN in the rack** runs at its standard rate, so its jacks stay sample-accurate with the other devices. The standalone RONIN plugin keeps its 2x HQ mode.

## In this repository

- [`jidai-rack/`](jidai-rack/): the JIDAI RACK plugin.
- [`jidai-common/`](jidai-common/): the shared C++ library for the Jidai Cable Standard and the shared DSP, used by every device.

## Legal

Copyright © 2026 Martial Systems LLC. All rights reserved. See [LICENSE](LICENSE) for terms and trademark notices.
