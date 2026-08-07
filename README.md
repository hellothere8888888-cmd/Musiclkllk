# PABLO Sampler

An MPC-style sampler **VST3 plugin for FL Studio** (and any VST3 host), themed
after Kanye West's *The Life of Pablo* — saturated orange, pale-pink blocks,
black scrawl. Load samples, chop them, repitch and reverse individual chops,
play them from your laptop keyboard, record new material, and split any sample
into stems (drums / bass / other / vocals) with an on-device AI model.

> You don't need to compile anything. Download the pre-built plugin from the
> latest CI run or release (see **Install** below).

## Features

- **Multiple sample tracks**, MPC-style — add as many as you like via the `+`
  tab, drag-and-drop audio anywhere, or hit **LOAD**.
- **Zoomable waveform editor** — scroll wheel zooms right down to individual
  samples; shift-wheel or drag to scroll; a mini overview strip at the bottom
  shows where you are.
- **Chopping**
  - Auto-chop by **transient detection** (three sensitivities) or **equal
    slices** (4 / 8 / 16 / 32).
  - **Double-click** the waveform to add a chop; **drag** any marker to move
    it; select a chop and press **Delete** (or right-click → Delete) to remove.
  - **Per-chop pitch** (±24 semitones), **per-chop reverse**, and a **PITCH
    ALL** button that copies the selected chop's pitch to every chop.
  - A separate **global pitch** knob shifts every chop on every track at once.
- **Play chops from your laptop keyboard** — the bottom rows act like MPC pads
  (`Z X C V B N M ,` = chops 1-8, `A S D F G H J K` = 9-16, and so on). Inside
  FL Studio the typing keyboard also drives it as MIDI, so it always works.
- **4×4 pad grid** that flashes on triggers and shows the key + reverse state
  for each chop, with banks when a track has more than 16 chops.
- **Record** the plugin's audio input straight into a new sample track.
- **AI stem splitting** — one click splits the active sample into four stems,
  each landing as its own new track ready to chop.

## Install (Windows / FL Studio)

1. Grab **PABLO-Sampler-Windows-VST3** from the
   [latest successful build](../../actions) (the `windows` job → *Artifacts*),
   or from a tagged [Release](../../releases).
2. Unzip and copy the **`PABLO Sampler.vst3`** folder into your VST3 folder,
   normally:
   ```
   C:\Program Files\Common Files\VST3
   ```
   Keep the whole `.vst3` **folder** intact — the bundled `onnxruntime.dll`
   inside `Contents\Resources` is what powers stem splitting.
3. In FL Studio: **Options → Manage plugins → Find more plugins**, then add
   PABLO Sampler from the plugin database (it registers as an instrument).

## Using it in FL Studio

- **Playing chops**: add PABLO Sampler as an instrument on a channel, load or
  drop a sample, chop it, then play the piano roll / typing keyboard. Chop *n*
  is triggered by MIDI note `Base Note + n` (Base Note defaults to C5 / note 60,
  so C5 = chop 1). The pad grid and your laptop keyboard trigger the same chops.
- **Recording input**: PABLO exposes a stereo input bus. Route the FL mixer
  track you want to capture into the mixer track hosting PABLO (or use the
  plugin's sidechain input), arm **REC**, and stop to drop the take into a new
  track. (If you only need to chop existing files, you can ignore this.)
- **Stem splitting**: select a track and click **SPLIT STEMS**. The first time,
  PABLO downloads the AI model (~170 MB, HTDemucs) into your user data folder;
  after that it runs offline. Each stem becomes a new track.

## Building from source

Requires CMake ≥ 3.24 and a C++20 compiler. JUCE and the ONNX Runtime C-API
header are fetched automatically by CMake.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure   # unit tests
```

On Linux, install the JUCE dependencies first (see `.github/workflows/build.yml`
for the exact `apt` list). On Windows, CI copies `onnxruntime.dll` into the
bundle; for a local build, drop the matching `onnxruntime.dll` next to the
plugin or into `%APPDATA%\PabloSampler\runtime\` to enable stem splitting.

Stem splitting can be disabled entirely with `-DPABLO_ENABLE_STEMS=OFF` (the
rest of the plugin is unaffected).

## How stem splitting works

The plugin never ships the model — it downloads
[`htdemucs_fp16weights.onnx`](https://huggingface.co/StemSplitio/htdemucs-onnx)
(MIT-licensed HTDemucs export with STFT/iSTFT baked into the graph) on first
use. ONNX Runtime is loaded dynamically at runtime, so if the DLL or model is
missing the **SPLIT STEMS** button simply disables itself and everything else
keeps working. Audio is resampled to 44.1 kHz, processed in 7.8 s segments with
25 % overlap and triangular-window overlap-add, then resampled back.

## License

GPLv3 — see [LICENSE](LICENSE). Uses [JUCE](https://juce.com) under the GPL,
[ONNX Runtime](https://onnxruntime.ai) (MIT), the HTDemucs ONNX model (MIT),
and the *Permanent Marker* font (OFL).

*Not affiliated with or endorsed by Kanye West, G.O.O.D. Music, or Akai. "MPC"
is a trademark of inMusic; this is an independent, tribute-styled instrument.*
