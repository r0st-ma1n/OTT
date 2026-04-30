# s3xtaOTT

`s3xtaOTT` is a JUCE-based multiband dynamics plugin inspired by the classic OTT workflow: aggressive upward and downward compression across three bands with a compact macro-oriented UI.

Current build targets:
- `VST3`
- `Standalone`

## Overview

The processor splits the signal into `LOW`, `MID`, and `HIGH` bands using a 3-band Linkwitz-Riley crossover, applies simultaneous upward and downward dynamics to each band, then reconstructs the output with wet/dry mixing and output trim.

The plugin is intentionally `OTT-style`, not a strict clone of any specific implementation.

## Main UI Controls

- `Amount`: overall processing intensity
- `Time`: macro control for effective attack/release behaviour
- `Mix`: wet/dry balance
- `Out`: output gain trim
- `LOW / MID / HIGH`: solo monitoring for each crossover band

## Internal Features

- 3-band Linkwitz-Riley crossover
- Upward and downward compression per band
- Stereo-link aware envelope handling
- Per-sample parameter smoothing in the main control path
- Band gain reduction meters
- Stereo output meter with clip indication
- Input spectrum analyzer

## Parameters Exposed In The Processor

Macro controls:
- `Amount`
- `Time`
- `Mix`
- `Out Gain`

Crossover and dynamics:
- `Xover F1`
- `Xover F2`
- `Stereo Link`
- `Attack`
- `Release`

Band trim:
- `Low Gain`
- `Mid Gain`
- `High Gain`

Monitoring:
- `Solo Low`
- `Solo Mid`
- `Solo High`

## Project Structure

- `Source/PluginProcessor.*`: audio processing, parameters, crossover, compression, metering, analyzer feed
- `Source/PluginEditor.*`: plugin UI and meter rendering
- `Builds/VisualStudio2026/`: generated Visual Studio solution and project files
- `JuceLibraryCode/`: Projucer-generated JUCE glue code
- `s3xtaOTT.jucer`: JUCE Projucer project

## Build

Primary solution:
- `Builds/VisualStudio2026/s3xtaOTT.sln`

Environment expectations:
- `Visual Studio 2026`
- MSVC toolset `v145`
- Windows SDK `10.0`
- JUCE modules available at the paths referenced by the generated `.vcxproj` files

Recommended build method:
1. Open `Builds/VisualStudio2026/s3xtaOTT.sln` in Visual Studio.
2. Select `Debug|x64` or `Release|x64`.
3. Build the required target:
   - `s3xtaOTT - VST3`
   - `s3xtaOTT - Standalone Plugin`

Outputs are generated under:
- `Builds/VisualStudio2026/x64/Debug/`
- `Builds/VisualStudio2026/x64/Release/`

Typical artifacts:
- `VST3/s3xtaOTT.vst3`
- `Standalone Plugin/s3xtaOTT.exe`

## Ableton Live: How To Make The VST3 Show Up

If the plugin builds successfully but does not appear in Ableton, the issue is usually installation path, plugin scan state, or architecture mismatch.

Use this workflow on Windows:

1. Build the `VST3` target.
2. Copy the entire `s3xtaOTT.vst3` bundle into the system VST3 folder:
   - `C:\Program Files\Common Files\VST3\`
3. Open Ableton Live.
4. Go to `Options > Preferences > Plug-ins`.
5. Make sure `Use VST3 Plug-In System Folders` is enabled.
6. Click `Rescan`.

If it still does not appear:
- Hold `Alt` while clicking `Rescan` in Ableton to force a full rescan.
- Confirm Ableton and the plugin are both `64-bit`.
- Run Ableton once as Administrator if the scan cache or permissions are getting in the way.
- Check that the file is really `s3xtaOTT.vst3` in the system folder, not just the inner `.dll`.
- Verify the plugin was built successfully in the same configuration you copied from.

Important:
- For VST3, Ableton expects the `.vst3` bundle/folder, not a loose `.dll`.
- In this project, the packaged plugin is produced inside the build output under the `VST3` directory.

## Notes

- The current UI is focused on the main macro controls plus band solo/metering.
- Additional processor parameters already exist and can be surfaced in the editor later.
- `Standalone` is useful for quick runtime checks before testing inside a DAW.
