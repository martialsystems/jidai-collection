# The Jidai Collection

**Patchable instruments from Martial Systems.**

The Jidai Collection is a line of plugins built like a hardware rack: every piece has jacks, every jack takes a cable, and the pieces patch into each other as if they sat in the same case. They share one patch format and one cable feel, so a sequence from one drives a voice in another with a single drag.

## The instruments

### BUSHIDO
**A 3 x 12 analog step sequencer with real patch cables.**
Three rows of twelve steps send pitch CV, gates and a trigger per step. Row C is a third voltage or the gate length of each step. Loop 12 steps, chain A and B into 24, or swap rows each pass. Clock it internally or from any pulse in the rack, and drive any synth in your DAW over MIDI.

[github.com/martialsystems/Bushido](https://github.com/martialsystems/Bushido)

### RONIN
**A semi-modular synthesizer you patch as an effect.**
A full rack in one plugin: VCO, a self-oscillating diode-bridge filter, two VCAs, two envelopes, an LFO, noise, a ring modulator, sample and hold, and more. Run your track through it, let it play itself, or blend it back in with one MIX knob.

[github.com/martialsystems/Ronin](https://github.com/martialsystems/Ronin)

### SHOGUN
**A Jidai Collection instrument, in its own repository.**

[github.com/martialsystems/shogun](https://github.com/martialsystems/shogun)

## JIDAI RACK

**BUSHIDO and RONIN in one rack, in one plugin.**
A rack window in the style of a hardware cabinet: drag devices in from the browser, screw in as many of each as you like, and cable any jack to any other. Both engines are compiled into one VST3 effect and one Standalone app. Each screen has banks A and B. Bank A is that instrument's factory set, cleared for now to one INIT entry. Bank B holds the rack patches (none yet) and your saves. Source in [`jidai-rack/`](jidai-rack/).

## Better together

Patch BUSHIDO into RONIN's Hz/V and trigger inputs and RONIN plays sequences. Patch RONIN's LFO back into BUSHIDO's clock and the rack clocks itself. Cables run both ways between the two, and feedback loops are allowed: the newest cable in a loop is delayed by one sample, the way a hardware rack behaves, so self-patching stays stable.

## Formats

VST3 effects. RONIN runs on macOS (universal) and Linux; BUSHIDO builds as VST3, AU and Standalone. Both are version 0.1 betas.

## Legal

Copyright © 2026 Martial Systems LLC. All rights reserved. BUSHIDO, RONIN, SHOGUN and the Jidai Collection are products of Martial Systems LLC.

BUSHIDO and RONIN are inspired by classic Korg gear. Korg is a trademark of its owner. Martial Systems is not affiliated with or endorsed by Korg.
