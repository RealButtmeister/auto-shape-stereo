# Auto Shape Stereo

A Windows x64 VST3 stereo effect from Honey Badger Audio. It shapes existing stereo information and can generate stereo detail from a mono source. The default correlation target keeps bass focused, progressively spreads the mids, and slightly narrows the very top.

This repository contains editable source, tests, and build scripts. Build outputs, audio demonstrations, presets, and the JUCE dependency are not included.

## Use it

1. Insert **Auto Shape Stereo** on an audio track, instrument, bus, or master.
2. Play a representative passage or loop.
3. Click **Auto Shape**. The button displays **LEARNING** and the eight band settings update continuously.
4. Click it again when you like the result. **HELD** means the learned settings are fixed; the effect keeps processing audio.
5. Save your DAW project, or use **Save** to write an `.autoshape` preset.

Click Auto Shape again to resume from the captured settings. There is no automatic time limit. Editing a learned band stops learning and applies your edit. Drag the amber graph points to edit the target; target changes guide subsequent learning and do not rewrite the held shape by themselves. Reset shape restores the band coefficients and reference targets while keeping your global controls.

Projects and file presets retain the global controls, targets, and exact learned coefficients. Reloading always opens in **Held** mode, including a preset saved during learning. Meters, filter history, and audio-dependent transient/center protection continue responding during Held mode; learned width and generation coefficients do not drift.

## Controls

| Control | Range / behavior |
| --- | --- |
| Width | 0–300%. Scales both existing and generated side information. Zero produces mono. |
| Stereoize | 0–100%. Scales the generated stereo detail, including for mono inputs. |
| Low-end Mono | Off at 0 Hz; cutoff up to 1 kHz. Rolls off side information below the cutoff. |
| Mono Slope | 12, 24, or 48 dB/octave. This is a smooth filter, not a brick-wall boundary. |
| Transients | 0–100%. Reduces generated width and learned widening around attacks. |
| Center | 0–100%. Restrains excessive side energy to keep the image anchored. The original mid signal is preserved at every value. |
| Adapt Speed | 0.25–15 seconds. Sets learning response time; it is not an automatic stop timer. |
| Character | Tight, Natural, Diffuse. Different static phase/delay patterns for generated stereo. |
| Mix | Dry/processed blend. |
| Output | −24 to +12 dB. Default −3 dB for headroom. Output is not peak limited. |
| Target points | Eight editable desired correlations from +0.05 to +1.00. |
| Per-band Width | 0–3× existing side level, before global Width. |
| Per-band Generated | 0–4× generated detail, before Stereoize and global Width. |
| Bypass | Smooth transition to the original input, including unity output level. |

The graph shows input and output correlation in eight selective frequency bands. Quiet bands are dimmed. Correlation measures how the channels relate, not a percentage of perceived width. The output trace and OUT peak display measure the final plugin output, including Mix and Bypass.

## Practical behavior

- Mono-to-stereo and stereo-to-stereo layouts are supported. The plugin requires stereo output.
- Mono synthesis adds equal and opposite side signals. Their mono sum cancels, preserving the input mid signal before the final output gain.
- Learning pauses on silence and ignores regions with too little energy. It does not invent missing frequency content.
- The target is a tendency, not an exact-match guarantee. Tonal signals, existing panning, bass protection, transients, and user limits affect the achieved correlation.
- Global Width is an artistic scale applied to the learned shape. Center and transient protection can intentionally hold back extreme settings.
- The engine uses complementary overlapping filters and static allpass decorrelation. It reports zero algorithmic latency. Generated stereo still contains short delayed components, especially in Diffuse mode.
- Start with the defaults. Use Tight for sharp material, lower Stereoize for already-wide mixes, and reduce Output if its meter indicates peaks at or above 0 dBFS.
- This first version has eight processing/measurement regions, not a copy of Voxengo's 32-band analyzer. Different analyzers and averaging windows can display different curves.

## Install

After building, copy the complete **Auto Shape Stereo.vst3** bundle folder from `build\AutoShapeStereoVST3_artefacts\Release\VST3` into a VST3 location scanned by your DAW, then rescan plugins. The standard system location is `%CommonProgramFiles%\VST3`; the per-user location is `%LOCALAPPDATA%\Programs\Common\VST3`. Keep the bundle's `Contents` folder intact.

`package.ps1` assembles a local `dist` folder and ZIP after a Release build. `Install-Auto-Shape-Stereo.ps1` installs that bundle in the per-user VST3 folder and refuses to replace an existing installation. If your DAW does not scan the folder automatically, add it in the plugin manager.

## Build and validate

Requirements: Windows x64, Visual Studio 2022 with the Desktop development with C++ workload and Windows SDK, CMake 3.22+, and [JUCE 8.0.12](https://github.com/juce-framework/JUCE/tree/8.0.12). Run from this repository in PowerShell with CMake on your PATH:

```powershell
git clone --branch 8.0.12 --depth 1 https://github.com/juce-framework/JUCE.git external/JUCE
.\build.ps1 -JuceDir "$PWD\external\JUCE"
```

You can instead point `-JuceDir` at an existing JUCE checkout. The build script configures the x64 Release build, compiles the VST3 and standalone app, and runs CTest. It does not install the plugin.

Build outputs are in `build\AutoShapeStereoVST3_artefacts\Release`. `AutoShapeEngineTests` checks audio contracts. `AutoShapePluginTests` checks host-facing layouts, learning/hold, bypass, state recall, and manual edits. With an output-folder argument it also writes a native editor screenshot and a synthetic mono/processed audio demo. `AutoShapeHostTests` exercises the actual VST3 bundle, including editor creation; run the full suite in a normal desktop session.

The effect does not use an external service, account, or network connection. Downloading JUCE is a separate build setup step.

## License and dependency notices

No project-source license has been selected in this repository. Public availability does not itself grant a reuse or redistribution license. JUCE is an external dependency with its own licensing options and bundled notices; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The dependency notice does not license this project's source. The local packaging script is not a substitute for reviewing the notices required for your chosen binary distribution.
