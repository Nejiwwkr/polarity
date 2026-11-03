# Changelog

## [1.1.0] - Unreleased

### Added

- 1024-tap low-latency quality tier (10.7 ms at 48 kHz).
- L/R and M/S routing with M/S curve editing; the editor relabels the layer
  buttons to M/ST/S and the route switches with a short fade-out/fade-in while
  the FIR history is re-primed. M/S mode makes uses such as mono bass or
  centre removal possible.

### Changed

- The convolution engine now uses zero-additional-latency partitioned FFT
  convolution (64-sample hop, double precision) with a direct head. CPU drops
  by roughly an order of magnitude: 8192 taps runs at about 70x realtime in the
  offline benchmark (previously about 9x). Reported latency is still taps / 2.
- State schema 2: previous quality indices are remapped for the new four-tier
  list. Existing projects and presets keep their FIR length.
- Spectrum, waveform and meter analysis now runs only while an editor is open,
  removing the remaining analysis cost in closed-window and offline rendering.
- Bypass and mix=0 skip the wet convolution while still keeping the FIR history
  warm, so re-enabling produces no stale-history transient.
- Waveform analysis updates at a time-based rate instead of every four blocks;
  spectrum band bin/weight tables are precomputed.
- Meter ballistics use time-based smoothing coefficients, independent of block size.
- Curve drags coalesce FIR redesigns to roughly 25 Hz; mouse-up still applies
  immediately.
- Changing QUALITY crossfades the dry delay length alongside the wet kernel fade.

## [1.0.0] - 2026-11-03

### Added

- Frequency-dependent polarity/null processing with L/ST/R curve editing.
- Zero-phase linear-phase FIR engine with 2048 / 4096 / 8192-tap quality tiers
  and host PDC reporting.
- A/B curve slots, NORM/DELTA output modes, file presets, and full project-state
  save/restore.
- Spectrum/heatmap overlay, waveform comparison with RMS/PEAK metrics and freeze,
  and M/S level plus correlation metering.
- Host parameters for Mix, Bypass, Delta Out, A/B Slot, Quality and Edge Smoothing,
  with VST3 value text, units and edit gestures.
- Themed in-plugin hover tooltips for every control.
- State schema version 1 with migration from 0.1.0 states.

### Changed

- Quality/edge/slot now persist as host parameters instead of hidden state.
- FIR convolution uses a 4-wide SSE2 dot product with chunked double accumulation;
  spectrum analysis updates at about 30 Hz instead of every block.
- Default install target is the user-level `%APPDATA%\VST3` folder; `-System`
  remains available for the shared VST3 folder.
- VST3 sub-categories are `Fx` and `EQ`; the plugin is grouped as an EQ effect.

### Validation

- Offline DSP and processor tests pass at 44.1 / 48 / 96 / 192 kHz.
- Block sizes 32 / 512 / 2048 are covered at 48 kHz.
- Mono and stereo bus layouts are tested, including mono/stereo parity.
- Quality switching under live playback, state round-trip and legacy migration
  are covered by the test suite.
- pluginval 1.0.4, strictness 10, GUI tests enabled: SUCCESS (exit code 0).

### Known limitations

- Changing QUALITY changes the reported latency; hosts may re-sync playback.
- Free-drawn curves and display settings are not host-automatable, so they do not
  appear in host automation or the host hint panel.
- Curve editing uses the plugin's own undo/redo history, not the host's undo.
- Windows x64 only; the build is unsigned and has no installer.
