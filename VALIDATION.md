# Validation record — version 0.1.0

The processor and VST3 results below describe the original local build. They are retained as historical validation, not a claim that every host or toolchain was tested. The standalone audio-engine suite was also rebuilt and passed all eleven tests during source-publication preparation on September 22, 2026.

Built on Windows x64 using Visual Studio 2022 and the existing local JUCE 8.0.12 checkout. The VST3 wrapper reports SDK 3.8.0.

## Audio engine

Eleven functional contracts cover mono stereo-generation and preservation of the input mono sum; Width and Stereoize at zero; continuous learning and exact hold; state retention through reset and sample-rate changes; silence and inaudible input gating; low-frequency side suppression; known correlation relationships; independence from host block partition; improvement toward the target curve using an independent bandpass analyzer; and finite output under extreme controls/input values at 44.1, 48, 96, and 192 kHz. Extreme tests include maximum generated-side settings.

## Plugin processor

Checks cover mono-to-stereo/stereo layouts, oversized and irregular host blocks, the two-click learning/hold workflow, manual coefficient edits, extended generation range, project and file preset recall, saving pending edits, incomplete-state recall, bypass, and metering after the final dry/wet blend. A native JUCE editor snapshot and eight-second mono/processed synthetic auditions are generated from the compiled code.

## Actual VST3 binary

A separate JUCE VST3 host scans and loads the built bundle, processes audio, starts/stops learning through host parameters, verifies the entire held VST3 state remains byte-identical during further processing, recalls all parameters and coefficients, loads the state in a fresh instance, and creates/closes the custom editor inside an offscreen native host window.

## Scope

The target curve is approximate and source-dependent. Processing uses eight overlapping regions; the display uses selective bandpass measurements. The original mid is preserved before final gain, while stereo peaks/timbre can change. The output is not peak limited. These checks do not substitute for listening and testing in the user's own DAW/session.
