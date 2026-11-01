# ChaosMaker

A multitrack DAW for Linux in C++ and Qt QML, with its own effects and instruments.

## Build and run

    cmake --preset default
    cmake --build --preset default
    build/appChaosMaker

## Test

    ctest --preset default                 # every test
    ctest --preset default -R tempomap     # one part
    build/backendtests tempomap            # one part, printing each test
    build/backendtests --list              # backend test names
    build/wrappertests qtpresets            # Qt preset/IR integration tests
    ctest --test-dir build -R transport_ui  # mouse/keyboard BPM tests (with Qt Test installed)

Needs Qt 6.10 or newer (Quick and Concurrent), the JACK headers, nlohmann json and CMake. The sanitizer presets (`rt`, `tsan`, `asan`) also need Clang.

Licence: GPL v3.

GuitarSuite now includes Amp and Cabinet IR. Add Amp before Cabinet IR, then load your own
mono/stereo WAV cabinet impulse response. The loader prepares up to 200 ms on a worker thread;
IR audio is referenced by the `.gspreset` file, not embedded in it. Keep the WAV with the preset
when moving it. Missing IRs leave the playing chain unchanged and show an error.

The separate track Equalizer offers factory curves and custom `.eqpreset` save/load. These
presets store all ten band gains and can be applied to another track.

Edit tempo with the bottom-right BPM number (Enter or click elsewhere to apply), or its −/+ buttons.
The pedalboard stays open while you adjust the transport. Tempo drives both the visual timeline
and BPM-synced Tremolo. Octaver and Pitch Shifter are available in GuitarSuite; start with clear
single notes before Overdrive/Amp. Their spectral wet voices have about 1024 samples of delay
(21.3 ms at 48 kHz), while dry audio stays immediate. Pitch settings are saved in `.gspreset`.

Crunch and Distortion are separate live-monitor pedals. Crunch offers lighter drive with Tone/Body;
Distortion adds Tight, Bass/Mids/Treble and a sweepable mid frequency. Start with
Noise Gate → Crunch or Distortion → Amp → Cabinet IR. Both save in GuitarSuite presets.
Scroll inside a pedal card to reach all controls.

Parametric EQ, Fuzz and Phaser are also available. Parametric EQ has three frequency/gain/Q bands
and can sit after Cabinet IR. Use Fuzz before Amp/Cab for an alternative distortion character;
Phaser can follow EQ and run at a manual rate or sync to the bottom-bar BPM. All three save in
GuitarSuite presets. The separate track ten-band EQ and its `.eqpreset` files remain independent.

GuitarSuite factory presets: choose **Djent** or **Jazz** in the pedalboard's Factory Preset
selector and click **Apply preset**. Applying one replaces the pedal chain but keeps input trim
and the first loaded cabinet IR with its Mix/Level settings; the preset enables that cabinet.
Without a loaded IR, its Cabinet slot passes dry audio until you load one. Adjust controls and
use **Save preset** to keep a custom version. Factory presets do not change BPM or the separate
track EQ. Targeted checks: `build/wrappertests qtpresets.factory_` and
`build/transporttests factory_preset_picker_applies_djent_and_jazz` (offscreen Qt environment).

Djent uses a fast gate, restrained distortion gain, a 1.1 kHz mid lift and a broad 6 kHz cut
for defined rhythm playing. Its low-end filtering leaves more body for low-tuned palm mutes.
Load a cabinet IR to finish the tone; adjust the gate threshold for your guitar's output/noise.

Real recording and playback: arm exactly one **Audio**, **Vocal**, or **Electric Guitar** track,
select its input and an output, then press **Record** in the bottom bar. Press **Stop** to finish
and return to the start, then **Play** to hear the take. **Pause** finishes the take and keeps the
position; pressing Record again ends recording while playback continues. Disarming also ends
the take. The timeline and time display now follow the audio engine's sample clock. Track volume,
mute and solo affect playback; repeat loops from the start through the current clip extent (at
least one bar). Recording disables repeat and prevents seeking until the take is finalized.

Takes contain the raw mono input, before trim, EQ, volume and GuitarSuite. Live monitoring still
uses the shared GuitarSuite chain; **recorded clips currently play back dry**. Independent track
inserts, WAV import and recording-latency compensation are not yet implemented. WAV takes live in
Qt's application-data folder, normally `~/.local/share/ChaosMaker/ChaosMaker/Recordings/`.
A full disk buffer ends the take and reports an error instead of skipping samples. Each take is
limited to one hour or 1 GB, whichever comes first, since playback audio is held in RAM.

Enable **Settings → Restore session on startup** to automatically save and reopen your tracks,
clips, tempo/meter, track EQ and GuitarSuite chain, including input trim and cabinet IR references.
It defaults to off. Changes are saved after a short idle interval and on normal exit; closing
while recording finishes the take first. Startup restores stopped, with tracks disarmed. The
session file is `last-session.json` beside the Recordings folder. Keep recorded WAVs and external
cabinet IRs in place. Missing takes stay visible as offline clips, and damaged sessions or failed
pedal restoration are reported without overwriting the saved session. This is startup recovery,
not a portable project-file format; the top-bar New/Open/Save project actions remain pending.

Tremolo now has **Sustain only** at the top of its card. It leaves the initial attack clear, waits
**Attack delay** (default 180 ms), then increases modulation over **Fade in** (250 ms). A new pick
restarts the delay. **Note threshold** (−55 dB by default) separates notes from background noise.
Scroll within the card for these controls. It detects amplitude changes, not musical intent, so
very soft legato or heavily compressed input may not retrigger. Place it before Delay/Reverb and
adjust the threshold for your signal. Manual rate and BPM sync both work; old presets keep the
original continuous tremolo unless Sustain only is enabled.

Targeted checks for this milestone:

    build/backendtests recording.
    build/backendtests tremolosustain.
    build/backendtests tremolosync.
    build/wrappertests qtrecording.
    build/wrappertests qtpresets.session_
    ctest --test-dir build -R transport_ui --output-on-failure
