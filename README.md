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

## Better together

Patch BUSHIDO into RONIN's Hz/V and trigger inputs and RONIN plays sequences. Patch RONIN's LFO back into BUSHIDO's clock and the rack clocks itself. Cables run both ways between the two, and feedback loops are allowed: the newest cable in a loop is delayed by one sample, the way a hardware rack behaves, so self-patching stays stable.

## Formats

VST3 effects. RONIN runs on macOS (universal) and Linux; BUSHIDO builds as VST3, AU and Standalone. Both are version 0.1 betas.

## Legal

Copyright © 2026 Martial Systems LLC. All rights reserved. BUSHIDO, RONIN and the Jidai Collection are products of Martial Systems LLC.

BUSHIDO is inspired by the Korg SQ-10, and RONIN is inspired by the Korg MS-50. Korg, SQ-10 and MS-50 are trademarks of their respective owners. Martial Systems is not affiliated with or endorsed by Korg.
