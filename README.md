# Polarity

**Draw the frequencies you want to remove — or flip upside down.**

Polarity is a VST3 / standalone effect for Windows that turns polarity inversion into a drawable EQ curve. Instead of boosting or cutting gain, every point on the curve sets how much **inverted signal** is mixed into that frequency band:

- `0` — untouched
- `1` — complete null (the band disappears)
- `2` — full polarity inversion

Draw separate curves for the left and right channels, or link them together with **ST**. A zero-phase FIR engine applies the curve, so the nulls are real nulls instead of the phasey cancellation of a minimum-phase EQ.

![Polarity interface](Docs/screenshoot1.png)

## Highlights

- **L / ST / R curve editing** with draw, node and erase tools.
- **Zero-phase FIR processing** with host PDC; what you draw is what you hear.
- **Three quality tiers** — 2048 / 4096 / 8192 taps — trading latency for frequency resolution and null depth.
- **A/B curve slots** for fast comparisons, plus file presets.
- **NORM / DELTA monitoring** — DELTA plays exactly what the curves removed, so you can hear the inverted content in isolation.
- **Spectrum and inversion heatmap** overlaid on the curve, with adjustable slope, smoothing and release time.
- **Input/output waveform** with RMS or Peak envelope, adjustable window and click-to-freeze.
- **Mid/Side level and correlation meters** for mono-compatibility checks.
- **Host integration** — Mix, Bypass, Delta Out, A/B Slot, Quality and Edge Smoothing are real VST3 parameters with value text and edit gestures; FL Studio can automate and undo them.
- **Light paper/ink interface** with themed hover tooltips.
- **44.1 / 48 / 96 / 192 kHz**, mono and stereo, buffer sizes down to 32 samples.

## Download & install

The latest tagged release is **v1.0.0**. The `master` branch also contains a 1.0.1 performance pass that is not tagged yet.

1. Download `Polarity-<version>-win64.zip` from the [Releases page](https://github.com/Nejiwwkr/polarity/releases).
2. Extract it anywhere.
3. In PowerShell, from the extracted folder:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install_vst3.ps1
   ```

   This installs to the user-level `%APPDATA%\VST3` folder and does not require administrator rights. Use `-System` to install into `C:\Program Files\Common Files\VST3` instead.

4. Rescan plugins in your DAW. In FL Studio: **Options → Manage plugins → Find installed plugins**.

You can also copy `VST3\Polarity.vst3` into `%APPDATA%\VST3` manually. Do not keep both a user-level and a system-wide copy installed.

## Quick start

1. Insert Polarity on a track and open the editor.
2. Choose **L**, **ST** or **R**, then pick **DRAW** or **NODE**.
3. Draw the inversion amount:
   - drag up to `1` for a deep null;
   - drag up to `2` for a full polarity flip;
   - leave at `0` to keep the band untouched.
4. Switch to **DELTA** to hear only the removed/inverted part.
5. Use **A/B** to compare curves, and **QUALITY** to choose between latency and precision.
6. Save the result as a preset with **SAVE**, or let it travel with your project.

## What makes it different

A normal EQ cut is a minimum-phase filter: subtract it from the dry signal and you get comb filtering, not silence. Polarity implements a real-valued, zero-phase magnitude curve:

```text
Y(f) = (1 − A(f)) · X(f)        A(f) ∈ [0, 2]
```

At `A = 1` the band is actually cancelled. At `A = 2` it is polarity-inverted. At `A = 0` the signal passes through untouched. The FIR kernel is designed in the frequency domain and reported to the host as latency, so tracks stay aligned.

## Compatibility

| | |
|---|---|
| Platform | Windows 10 / 11, x64 |
| Formats | VST3, Standalone |
| Tested host | FL Studio (primary), pluginval 1.0.4 strictness 10 |
| Sample rates | 44.1 / 48 / 96 / 192 kHz |
| Buffer sizes | 32 – 2048 samples covered by tests |
| Channels | Mono and stereo |

Latency at 48 kHz:

| Quality | Taps | Latency |
|---|---|---|
| Low | 2048 | 21.3 ms |
| Normal | 4096 | 42.7 ms |
| High | 8192 | 85.3 ms |

The plugin runs at roughly 7% of one core at 4096 taps on a typical desktop (FL Studio measurement); closing the editor or bypassing reduces that further.

## Build from source

Requirements: CMake 3.22+, Visual Studio 2022 with the C++ workload, and the vendored JUCE 8.0.15 in `ThirdParty/JUCE-8.0.15/JUCE`.

```bash
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Release --target Polarity_VST3 Polarity_Standalone PolarityTests --parallel
```

Run the test suite:

```powershell
.\Build\PolarityTests_artefacts\Release\PolarityTests.exe
```

Package a release zip:

```powershell
powershell -ExecutionPolicy Bypass -File Tools\package_release.ps1
```

Run pluginval (downloads v1.0.4 on first use):

```powershell
powershell -ExecutionPolicy Bypass -File Tools\run_pluginval.ps1
```

## Validation

- DSP self-test and processor integration tests pass at 44.1 / 48 / 96 / 192 kHz, block sizes 32 / 512 / 2048, mono and stereo.
- Null depth at 1 kHz: about −125 dB (44.1 kHz), −111 dB (48 kHz), −100 dB (96 kHz), −108 dB (192 kHz) on the test curve.
- pluginval 1.0.4, strictness 10, GUI tests enabled: **SUCCESS**.
- State schema v1 with migration from 0.1.0 projects and presets.

## Known limitations

- Changing **QUALITY** changes the reported latency; the host may re-sync playback.
- Free-drawn curves and display settings are not host-automatable, so they do not appear in the host automation list or hint panel.
- Curve editing uses the plugin's own undo/redo, not the host undo history.
- Windows x64 only; the build is unsigned and has no installer.

## Documentation

- [Chinese technical documentation](Docs/README-zh.md) — design, implementation, build, tests and release details.
- [Changelog](CHANGELOG.md)
- [Releases](https://github.com/Nejiwwkr/polarity/releases)
