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
Timeline beat/bar spacing follows BPM, including after mouse-wheel zoom: faster tempos show
closer beat lines. Recorded clips keep their position and duration in seconds; changing BPM
does not time-stretch audio.
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

Djent copies the pedal order, bypass states and exact parameter values from `best_djent.gspreset`:
Noise Gate → Distortion → Amp → Cabinet IR → Parametric EQ (bypassed) → Crunch → Reverb.
It uses a fast gate, a 1.1 kHz distortion mid lift, added Crunch and room Reverb.
Load a cabinet IR to finish the tone; adjust the gate threshold for your guitar's output/noise.

Real recording and playback: arm one or more **Audio**, **Vocal**, **Acoustic Guitar**, or **Electric Guitar** tracks,
select each track's input with **IN**, choose an output, then press **Record** in the bottom bar.
All armed tracks start on the same sample and create separate WAVs/clips with their own
recording modes and effects. Tracks may share an input or use different inputs. Use each track's **MON** button for live monitoring through its effects, independently of **REC**.
Arming a track does not enable monitoring; turning monitoring off does not stop capture.
Press **Stop** to finish all takes and return to the start, then **Play** to hear them.
**Pause** finishes all takes and keeps the position; pressing Record again ends capture and
pauses transport, leaving the red playhead at the take's end. Disarming one track finishes
only its take; the others keep recording.
Playback pauses automatically at the final clip's end and leaves the red playhead there; Repeat and active recording continue.
Transport shortcuts: **Space** plays/pauses, and **R** starts/stops recording.
Shortcuts do not trigger while typing in text fields, and holding a key does not repeat them.
Tracks armed after recording starts join capture on the next take; MON can change at any time.
Input/mode changes and removal of participating tracks stay locked until the group finishes.
The JACK adapter provides 31 dedicated source routes, shared by tracks using the same source;
routes are retained until the app closes. The timeline and time display follow the sample clock. Track volume,
mute and solo affect playback; repeat loops from the start through the current clip extent (at
least one bar). Recording disables repeat and prevents seeking until the take is finalized.

Double-click the empty area below the track cards in the left track list to add a Vocal track.
The + button opens the existing track-type chooser.

The bottom mixer shows your session tracks and updates when tracks are added, removed or
reopened. Its volume faders (0–200%), mute and solo controls share the track cards' settings.
Double-click a volume fader in the mixer or a track card to restore unity gain (100%, 0 dB).
Drag a track's name header up or down to reorder it; timeline lanes and mixer strips follow the same order.
Ctrl-click clips to toggle multiple selections, or Shift-drag a rectangle to select intersecting clips on multiple tracks.
Normal dragging in empty timeline space selects a time range snapped to the beat grid; enable Repeat to loop that range.
Drag a new range to update an active loop, or click empty timeline space to clear the range and repeat the full project.
Drag any selected clip to move the group while preserving track assignments and spacing.
Drag a selected clip's trim edge to trim the group, or press Delete/right-click Delete selected clips to remove it.
Each group edit is one undo/redo step; edits that exceed audio or timeline boundaries are rejected together.
Select two adjacent split pieces and right-click **Merge selected clips** to join them in one undoable edit.
Merge requires the same track, source audio, contiguous source samples and matching gain/effects mode.
Double-click a track name in the left track list to rename it. Press Enter or click another
control to save, or Escape to cancel. Blank names keep the previous name. Names update in
the mixer and effects window and are saved with the project/session.
Click the colour square beside a track name to choose a swatch or a custom colour.
The colour appears on the track card, timeline lane marker, clips and mixer strip.
Colours are saved with projects and startup recovery; older projects receive default colours.
Drag a strip's right edge to resize it; scroll horizontally when the strips exceed the window.
Each strip has a vertical volume fader and two vertical meters, IN and OUT. Each meter has
left/right halves separated by a thin line. IN measures source audio before inserts and gain;
OUT measures the track after effects, volume, mute and solo, before the master output clamp.
Meters work for playback and every monitored track. Mono live input fills both halves.
Hover for channel levels in dBFS; click a meter to clear that track's latched clip indicators.

**Electric Guitar, Acoustic Guitar and Vocal takes record stereo audio with FX**, after input
trim, track EQ and GuitarSuite, before monitor volume/mute and before mixing backing tracks.
Choose the track input and set its tone through FX → Equalizer / GuitarSuite, arm the track, and
press Record; playback preserves the recorded effects even if you later change the pedals.
Monitoring can be off or muted without silencing the recording. Stop ends capture immediately;
leave recording running until delay/reverb tails have faded if you want them in the take.
Audio takes still contain raw mono input. Enable MON on any supported input track for live monitoring.
Each track owns its GuitarSuite chain, pedal order, bypass settings, input trim and EQ state.
Enabling MON on another track adds its sound to the monitor mix; opening or editing a track's FX keeps
each track's input and processing independent. FX opens one non-modal window with a searchable sidebar: switch between Equalizer
and GuitarSuite without closing the editor. Track settings and pedal controls stay in place.
Vocal tracks expose the Equalizer in their FX window, with recording mode controls still
available. GuitarSuite is offered on the supported non-Vocal tracks.
Effects are printed into new wet takes; editing a chain later does not reprocess existing clips.
WAV import and recording-latency compensation are not yet implemented. WAV takes live in Qt's
application-data folder, normally `~/.local/share/ChaosMaker/ChaosMaker/Recordings/`.
A failed capture or missing callback input stops the recording group and keeps completed samples;
writer errors are reported instead of silently skipping samples. Each take is
limited to one hour or 1 GB, whichever comes first, since playback audio is held in RAM.

Enable **Settings → Restore session on startup** to automatically save and reopen your tracks,
clips, tempo/meter, and every track's EQ and GuitarSuite chain, including input trim and cabinet
IR references. Older shared-chain sessions reopen with a separate copy on each track.
It defaults to off. Changes are saved after a short idle interval and on normal exit; closing
while recording finishes the take first. Startup restores stopped, with record-arm and live monitoring off. The
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
