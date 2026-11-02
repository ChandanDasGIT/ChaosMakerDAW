# ChaosMaker development follow trail

Updated: 2026-11-02

## Active task queue — 2026-11-02

This queue takes priority over the older roadmap below. Work on one task at a time,
verify only the affected behavior, report the result, and wait for the user's explicit
"go ahead" before starting the next task. Current code has advanced beyond the older
checkpoint; its dry-only/shared-chain and placeholder-project descriptions are historical.

1. **Done: update the factory Djent preset from `best_djent.gspreset`.** Copy exact
   parameter values, pedal order and bypass states, including Crunch, Reverb and
   bypassed Parametric EQ. Retain factory cabinet reuse instead of embedding the
   source preset's machine-local cabinet path. Verified exact source JSON settings
   (excluding local IR references and retaining the factory name), successful app/test
   builds, both `qtpresets.factory_` cases and the focused factory-picker UI case.
2. **Done: fix BPM changes not being reflected in the timeline.** The timeline's
   fixed beat width ignored its BPM property. It now keeps zoom in pixels per second
   and derives beat/bar spacing from BPM, so increasing BPM brings beat lines closer
   together without moving or stretching recorded audio in time. Zoom does not break
   the tempo binding. The UI harness now binds tempo/meter like the production app.
   A new interaction test reproduced the original failure before the fix; nine focused
   UI cases passed afterward, covering BPM entry/buttons, decimal/extreme tempos,
   meter, tempo after zoom, unchanged recorded timing, beat-snapped moves/undo,
   trimming and waveforms. App and UI test builds passed.
3. **Done: replace the bottom mixer placeholder tracks with session tracks.**
   Main.qml passes the shared TrackListModel to MixerPanel. Strips follow track
   creation/removal and project replacement, display actual names, and use model
   volume/mute/solo controls. Volume supports the same 0–200% range as track cards
   and shows dB. Removed the disconnected MASTER strip and Stereo/Mono selector.
   Empty sessions show an add-track hint; horizontal scrolling and per-strip width
   resizing remain available. Two focused UI cases passed for add/remove/reset,
   names, width retention after removal, overflow, actual mute/solo clicks, keyboard
   fader movement and external model updates after interaction. App and UI builds
   passed.
4. **Done: bottom-bar input/output meters and vertical volume fader.** The user
   clarified two meters per track (IN/OUT), each split into L/R halves by a thin
   line. Added compact vertical meters alongside the fader with GuitarSuite's
   green/amber/red level colors, dBFS tooltips and click-to-clear clip indicators.
   Per-track atomic peak meters cover playback and the currently monitored track;
   source input is measured before inserts/gain and output after FX, fader and
   mute/solo, before the master clamp. Printed takes bypass inserts as before.
   Simultaneous live/playback contributions on the same track are summed for
   metering, without including other tracks. Mono live input fills both halves.
   UI polling provides decay; snapshot ownership protects removed-track meters.
   Verified two backend meter cases normally and with realtime sanitizer, four
   existing monitor cases, ten affected recording/playback cases, controller meter
   polling/removal/project reset, dry/printed controller integration and three mixer
   UI cases. Visually checked /tmp/chaosmaker-mixer-meters.png. App rebuilt.
5. **Done: simultaneous recording and live monitoring on multiple tracks.**
   JACK now has 31 stable dedicated mono source routes plus the legacy default
   port. Tracks can share a source or select separate sources; each armed track
   monitors through its own trim/EQ/GuitarSuite and fader/mute/solo. Callback routing
   and scratch storage are prepared outside audio. One group command starts all
   take writers at the same sample; per-track dry/wet modes remain independent.
   Disarm finishes that track's writer and attaches its clip when ready, while
   others continue; Stop/Pause/Record toggle finish the group. Tracks armed later
   monitor immediately but enter capture on the next take. Participating source/
   mode/removal edits remain locked until all writers are released and finalized.
   Input loss or capture failure stops the group, preserves completed samples and
   reports the failure. Closing finalizes every take. No allocation, I/O, waiting
   or destruction of shared snapshots occurs in audio. Timeline previews and the
   GuitarSuite meters now follow all active tracks, using per-track level data.
   Fixed reentrant FX publication during project replacement discovered by the new
   reopen test; binding suppresses publication until the track models are ready.
   Verification: four new backend cases (different inputs, stereo wet/dry alignment,
   variable blocks, meters, disarm, Stop/Pause, missing input, failed writer and
   queued-start shutdown) pass normally, realtime sanitizer and ASan/UBSan
   (leak detection off under tracing). Three new controller cases cover routing,
   shared sources, FX isolation, locks, partial disarm, shutdown, errors and project
   reopen. Existing recording, project-file, clip-lock and meter checks pass.
   Focused UI checks cover both recording lanes, one-lane disarm and affected FX/
   mixer/editing interactions. Production app ran offscreen for three seconds with
   an isolated two-track recovery session and no QML diagnostics; live interface
   routing/listening has not been measured. Routes remain cached until app close;
   device hotplug and recording latency compensation remain separate work.
   Waiting for go-ahead before task 6 (remove GuitarSuite from Vocal tracks).
6. Remove the GuitarSuite option from Vocal tracks.

The user will manually check clip editing and project files. Do not treat those
checks as passed until results are reported.

### Planned audio import — implementation deferred

Target a first release with mono/stereo WAV, MP3 and FLAC import through a file
picker, editable clips and reliable project save/reopen. Proposed batches:

1. **Decoder foundation:** use one shared decoder (proposed: libsndfile with WAV,
   FLAC and MP3 support verified in the installed build). Cover common WAV variants,
   mono/stereo, damaged files, decoded-memory limits and sample-rate handling with
   focused fixtures. The existing take WAV reader only accepts the app's own layout.
2. **Session import:** add Import Audio with multiple selection; proposed default
   is one new Audio track per file, named from the file, starting at the playhead.
   Decode and build waveforms in the background, provide progress/cancellation,
   and attach each file only when ready. Imported tracks start with neutral FX.
3. **Editing/project integration:** make import undoable and mark the project
   modified; use the shared decoder on reopen. Verify trims, splits, stereo,
   sample-rate differences and missing-file handling.
4. **Portability/convenience:** project-local media copies with relative references,
   covering recordings and cabinet dependencies too, followed by drag-and-drop.

Retain referenced media for the initial release; portable bundling is a later batch.
Automatic tempo matching, time stretching and multichannel routing are deferred.
Each implementation batch needs its own go-ahead after the preceding batch is verified.
Decoder references: https://libsndfile.github.io/libsndfile/formats.html and
https://github.com/libsndfile/libsndfile (MP3 support depends on build configuration).

## Current checkpoint — read this first

ChaosMaker is a Linux C++20/Qt Quick DAW prototype with a working live guitar rig,
dry recording/playback, and optional startup-session restoration. This summary and
future plan supersede the historical implementation notes below; older statements
such as “transport is visual only” describe earlier checkpoints.

### Implemented so far

- CMake app/backend/test targets and normal, realtime, thread and address-sanitizer
  presets; Qt-free audio/DSP backend, Qt controllers/models, and QML frontend.
- JACK/PipeWire mono input and stereo monitoring, input/output routing and buffer
  selection; RT command queues, immutable snapshots, smoothed parameters, input
  calibration, peak meters and latched clipping indicators.
- Track creation/removal, record-arm, volume/mute/solo, timeline scrolling/zoom,
  ruler navigation, model-owned clips and basic clip resizing. Audio and Vocal
  tracks and instrument type labels exist; labels do not imply playable samplers.
- GuitarSuite with 18 modules: Noise Gate, Overdrive, Crunch, Distortion, Delay,
  Compressor, Wah, Chorus, Tremolo, Tuner, Reverb, Amp, Cabinet IR, Octaver,
  Pitch Shifter, Parametric EQ, Fuzz and Phaser. Shared pedal controls, ordering,
  bypass and custom .gspreset save/load; worker-loaded WAV cabinet IRs; Djent and
  Jazz factory chains. Separate ten-band track EQ and .eqpreset files.
- Editable decimal BPM and meter; Tremolo/Phaser tempo sync; a nonmodal pedalboard;
  sustain-only Tremolo with attack detection, delay, fade-in and threshold.
- Sample-clock Play/Pause/Stop/Seek/Repeat and real dry mono WAV recording through
  a bounded ring and disk worker. Completed takes become playable timeline clips.
  Track gain/mute/solo affect playback. Normal close finalizes an active take.
- Optional Settings → Restore session on startup: automatic atomic session saves
  and restoration of tracks/media references, tempo/meter, track EQ and GuitarSuite.
  Startup is stopped/disarmed; missing media and invalid session data are reported.
- Backend DSP/transport/recording tests, Qt model and device-injection integration
  tests, actual mouse/keyboard UI checks and offscreen app startup checks.

User-authored Distortion changes are part of this baseline. Preserve them; the
latest recording/session/Tremolo implementation did not alter either Distortion file.

### Known limits

Recorded clips currently play **dry**; GuitarSuite is one shared live monitor chain.
Playback inserts and track EQ routing are not yet implemented. There is no recording
latency compensation or measured end-to-end latency for this checkpoint. General WAV
import, portable project New/Open/Save, undo/redo, metronome, device hotplug and playable
MIDI/sampler instruments remain pending. The separate mixer panel still has placeholder
controls; the working faders/mute/solo are on the track cards. Startup take decoding is
synchronous. Pitch effects add roughly 1024 samples of wet-path delay.

### Future plan, in order

1. **Validate the current workflow on the interface.** Record a short guitar phrase,
   Stop, disarm, Play, then close/reopen with restoration enabled. Check tempo/meter,
   clips, pedal settings, missing-file messages and Tremolo retriggering. Record the
   interface, sample rate, buffer size and audible results. Automated tests do not
   replace this listening/timing check.
2. **Add per-track playback inserts and EQ.** Keep the source WAV dry, but allow a
   recorded guitar to play through its track's GuitarSuite/EQ. Give tracks independent
   processor state, put the fader after FX, and test two tracks for state isolation,
   bypass, ordering, mute/solo and unchanged recorded samples. Route the mixer panel
   to these same model controls instead of maintaining independent visual state.
3. **Compensate recording latency.** Measure the actual route and use device capture/
   playback latency plus a calibrated offset to align overdubs. Verify with a known
   loopback signal and variable callback sizes; distinguish one-block time from
   measured round-trip latency.
4. **Implement portable projects and WAV import.** Add real New/Open/Save/Save As,
   versioned project/media references, missing-media recovery and background decoding.
   Keep startup recovery separate from user-saved projects. Move large startup take
   loading off the UI thread. Test relocation and device sample-rate changes.
5. **Complete editing and device handling.** Add undo/redo for document edits, clip
   move/trim/split, metronome, input-channel selection and device refresh/reconnection.
6. **Continue the broader DAW roadmap.** MIDI/piano roll, playable sampler instruments,
   chord player and offline export follow the recording/project foundations.

These are planned milestones, not claims that all phase 3–6 features are complete.
Continue from the next requested milestone; preserve user changes and run tests
relevant to the changed behavior rather than unrelated pedal suites.

### Verification baseline

The preceding implementation turn built appChaosMaker successfully. Six recording,
two sustain, affected Tremolo-sync and monitor-level cases passed; recording and
sustain also passed realtime and address/undefined-behavior sanitizer checks (leak
detection disabled under the tracing environment). Session-model and two controller
integration cases passed. Eleven UI test methods passed (13 with Qt init/cleanup).
A full-app offscreen check restored/autosaved a seeded track, WAV, 97.5 BPM, 7/8 meter
and sustain setting with no QML diagnostics. These are recorded prior results, not
a fresh full-suite run during Git/documentation preparation.

Useful targeted commands:

```sh
cmake --build --preset default
build/backendtests recording.
build/backendtests tremolosustain.
build/backendtests tremolosync.
build/wrappertests qtrecording.
build/wrappertests qtpresets.session_
ctest --test-dir build -R '^transport_ui$' --output-on-failure
```

### Git baseline preparation — 2026-11-01

The user requested repository initialization and an initial commit covering the
features above. `git init -b main` was attempted but failed because this session
mounts `.git` read-only (`.git/hooks/: Read-only file system`). **No repository or
commit was created by the agent.** The reviewed initial message is in the ignored
local file `initial-commit.txt`; it documents implemented features, preserved user
Distortion edits, prior validation and current limitations. Build trees, private
app directories, local commit preparation and the empty shell scratch file `grep`
are excluded by .gitignore. No remote or push was requested.

To finish from a terminal with writable repository metadata, run in this directory:

```sh
git init -b main
git add .
git diff --cached --check
git commit -F initial-commit.txt
git status --short
```

Use the user's configured Git author identity; do not invent one if Git requests it.
Once committed, `git log -1 --oneline` is the authoritative baseline identifier.

## Historical implementation notes

### Original UI goal

Build a simple REAPER-inspired DAW interface in the Qt Quick frontend. Keep the work
in small stages and document each checkpoint.

### Early UI status

The first UI shell is in place, and the first backend-to-QML track vertical slice is implemented.

- `Main.qml` now arranges the top bar, track list, beat timeline, transport, mixer, and existing engine status strip.
- `frontend/window/TopBar.qml` contains New/Open/Save, undo/redo, metronome, and a system output-device selector. Output devices come from `pactl list sinks` descriptions; choosing one runs `pactl set-default-sink` and links the app's JACK outputs to matching device playback ports. Project actions, undo/redo, and metronome are still visual placeholders.
- `frontend/timeline/TrackList.qml` now reads a C++ model, adds tracks, and changes solo, mute, record-arm, and volume state in that model.
- `frontend/timeline/Timeline.qml` renders empty lanes until actual project clip data exists. Double-clicking an empty grid cell creates a one-meter-unit clip at that snapped beat on that track; the clip is stored in the C++ model and rendered from there. Existing clips block overlap creation. Its horizontal range follows the project's furthest clip end (minimum one viewport), with no fixed bar count.
- `frontend/transportbar/TransportBar.qml` has transport affordances, a time readout, editable numerator/denominator, and decimal BPM fields. Transport actions are not connected to the engine yet.
- `frontend/mixer/MixerPanel.qml` has a master and one strip per starter track with vertical volume sliders, M/S buttons, and Stereo/Mono selectors. These controls are visual only so far.
- `backend/session/project.cpp` implements UI-thread project track operations, audio clip add/remove, and clip-derived arrangement length. Track state includes kind, name, solo, mute, record-arm, and linear volume.
- `wrapper/session/tracklistmodel.*` exposes `Project` as a QML `QAbstractListModel`, including add/remove, per-track state, and audio clip data methods. Adding a track grows both the track list and timeline lanes; the left labels follow the timeline vertical scroll position.
- Each track row has a hover-only remove button. Removing a track deletes the track and its owned clips from the project.
- `frontend/common/HoverHint.qml` provides the shared dark, compact hover popup used by track controls.
- Track creation now opens a two-step chooser: Vocal or Instrument, followed by Guitar, Acoustic Guitar, Electric Guitar, Strings, or Drums for instruments. The selected track type is stored in the C++ project model and displayed under the track name.
- Each track card now places an FX button immediately after Rec On/Off. It opens the ten-band EQ panel. The close button is pinned to the far right of the controls row.
- Startup projects now contain zero tracks; the user adds tracks through the kind/type chooser. The chooser's internal row-anchor warning was fixed by using a plain `Item` for its left/right anchored labels.
- Electric Guitar clean-monitor slice: `JackAudioDevice` starts a JACK client and hands each JACK block to `AudioEngine`. The per-track input popup enumerates JACK audio source ports; startup prefers a source port whose name contains `inst` or `guitar`. The top bar lists system output devices from `pactl list sinks`, changes the default with `pactl set-default-sink`, and connects app JACK outputs to playback ports matching the selected output description. Arming an Electric Guitar track sends an RT-safe command that copies the mono input to both output channels at unity gain; disarming all Electric Guitar tracks sends silence. The status strip reports JACK startup or connection failures and the selected route. Port/device lists are captured at app startup; hotplug refresh and guitar effects/recording are not implemented.
- Each track now has an `IN` button between Mute and Rec. Its popup selects a per-track JACK input or resets to the top-bar default. The selected port is stored on the project `Track`; when an Electric Guitar track is armed, its input choice is routed to the clean monitor. Other track inputs are stored for future monitoring/recording support.
- Monitoring latency follow-up: `AudioEngine::start(128)` now requests a 128-frame JACK buffer through `jack_set_buffer_size` before activation, with a JACK buffer-size callback tracking the active size. The status strip reports frames and one-block duration and notes when JACK kept a different size. This changes the JACK/PipeWire audio quantum while the app starts; actual round-trip latency also includes interface and server graph latency.
- Monitoring gain and latency controls: the top bar now has a JACK buffer selector (32, 64, 128, 256, 512, 1024 frames) before the system output selector. Changing it calls `jack_set_buffer_size`; JACK may reject unsupported or busy settings. The Electric Guitar's track volume fader controls clean-monitor gain from 0–200%, default 100% (unity). The input popup now lists `pactl` capture sources matched to JACK capture ports and filters sink-monitor sources and unmatched virtual ports.
- First audio effect: `backend/plugins/eq/tenbandeq.*` implements a ten-band peaking EQ (31, 62, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz; ±12 dB) with atomic controls and preallocated biquad state. The track FX button opens its sliders and Reset control. Each track stores its own EQ values in the project model. When an Electric Guitar track is armed, its EQ feeds the clean live-monitor path; edits on other tracks are stored but do not affect audio yet because general per-track playback/routing is not implemented. Monitor baseline and new-track volume default are 100%.
- FX access/layout correction: clicking a track's FX button now opens a centered effects chooser with an explicit **Equalizer · 10-band graphic EQ** option. Choosing it opens the 10-band slider panel centered in the app window. The earlier direct-to-EQ behavior is replaced by this chooser-to-plugin flow.
- Hover hint placement fix: all buttons, sliders, transport controls, and clip resize handles now use Qt Quick Controls' attached `ToolTip` directly on the hovered control. Removed the custom shared tooltip component that was placing hints at the window origin.
- Tooltip theme pass: set the Qt Quick tooltip palette to the app's dark panel and light text colors. Use attached tooltips' supported API; they do not expose a per-tooltip `background` property. Build and offscreen startup succeeded.
- GuitarSuite Phase 1: added `backend/plugins/guitarsuite/` as a `Processor` chain container, standalone pedal modules in `backend/plugins/{overdrive,delay,reverb}/`, and shared ring-buffer/2x half-band DSP helpers in `backend/dsp/`. Overdrive has tanh shaping, tone, level, smoothed controls, and filtered 2x oversampling; Delay has smoothed interpolated time/feedback/mix; Reverb is a smoothed Freeverb-style comb/allpass design. Chain slot bypass crossfades and is atomic; structural changes compile a new `EngineSnapshot` off-thread and publish it through the existing snapshot handoff. The live electric guitar monitor now processes stereo through its current 10-band EQ and then the GuitarSuite chain.
- `wrapper/pluginWrap/` now has the shared pedal registry and `GuitarSuiteModel`; parameter UI is generated from the existing `Parameter` descriptor fields. The track FX chooser offers GuitarSuite. Its centered pedalboard lives in `frontend/plugins/guitarsuite/GuitarSuitePanel.qml` and can add/remove, bypass, dial-adjust, and drag-reorder pedals. Save/Load preset dialogs read/write versioned `.gspreset` JSON with pedal order, bypass state, and parameter values. Presets are standalone files; project-wide persistence remains unimplemented.
- Added `tests/plugins/guitarsuite_test.cpp`: offline rendering confirms overdrive/delay reordering changes the signal and bypass fades to dry. All 17 backend tests pass normally and with the realtime sanitizer. A temporary electric-guitar-track/Overdrive UI smoke launch opened the pedalboard without QML errors; temporary startup changes were reverted.

## Build check

Latest `cmake --build build --target appChaosMaker -j2` succeeds. The app stays running in an offscreen launch without QML errors. No tests were run.
After the first user launch exposed invalid `Text.letterSpacing` assignments, those were changed to `font.letterSpacing`. The app now builds and stays running under `QT_QPA_PLATFORM=offscreen timeout 3s ./build/appChaosMaker` (exit 124 is the expected timeout; no QML load errors were printed).

UI follow-up: the transport caption now reads `Time signature`. Mixer channel strips now start at 108 px and each right edge is a horizontal resize handle, with strip widths clamped between 82 px and 220 px. Rebuild and offscreen startup succeeded after this change.

Mixer layout follow-up: strips now sit edge-to-edge in a clipped horizontal `Flickable`. The panel shows as many strips as its width allows and exposes a horizontal scrollbar when the total strip width overflows. Shrinking the window clips the visible portion and allows scrolling. Build and offscreen startup succeeded.

Timeline/meter follow-up: fixed the grid stacking order so beat dividers draw over each track lane. Time signature is now editable as numerator/denominator, and both values determine bar and beat-unit widths. The then-current arrangement used a 1,000,000,000 px canvas; see the later dynamic-length correction below.

Timeline interaction follow-up: mouse-dragging the arrangement no longer pans the view or moves starter regions; it creates a persistent time-range selection across the tracks. Timeline Flickable mouse/touch panning is disabled and always-visible horizontal and vertical scrollbars navigate the edit canvas. Build and offscreen startup succeeded.

Ruler/time-signature follow-up: the numbered ruler at the top of the edit area now controls playhead position on click or drag; lane dragging is reserved for range selection. Time signature now uses custom-styled numeric fields for numerator and denominator, accepting any positive Qt `int` value instead of limiting denominator choices to powers of two. Grid rendering caps tick density at roughly one line per 8 px and bar-number density at 48 px so unusual meter values do not instantiate enormous delegate counts. Build and offscreen startup succeeded.

Transport field follow-up: BPM now uses a matching custom numeric field with decimal tempo entry (0.01–10000 BPM, up to two decimal places). The time-signature group is about half its previous width. The red playhead initializes exactly at bar 1 and ruler navigation clamps it to the left edge. Build and offscreen startup succeeded.

Track controls follow-up: each track card now has visibly checkable Solo, Mute, and Rec On/Off buttons with distinct active colors and hover tooltips, plus a horizontal volume slider and percentage readout at the bottom. Track card and arrangement lane heights are matched at 106 px. The latest step moved these values into the C++ project model; audio processing is still not connected. Build and offscreen startup succeeded.

Runtime correction: added `required property int index` to the `Repeater` track delegate, which uses the index for vertical placement and alternating row color. Build and offscreen launch now show no QML warnings.

Scroll/zoom follow-up: mouse-wheel scrolling over the left track panel now moves the shared vertical timeline position, keeping track labels aligned with lanes. Wheel scrolling over the edit area zooms around the cursor. Removed the zoom slider and its old 20–96 beat-width range; zoom now has no practical upper cap and only a near-zero numeric safety floor. Build and offscreen launch succeeded.

Timeline extent/track removal follow-up: removed the 1,000,000,000 px startup canvas and all placeholder region rectangles. The timeline now starts empty and its width follows `Project::contentEndBeats()` derived from actual `AudioClip` data, with at least one viewport visible and no fixed bar cap. Added a per-track × button (shown on hover) that removes the track and its owned clips. Added shared custom dark hover hints for track controls. Build and offscreen launch succeeded.

Clip creation follow-up: double-clicking an empty grid cell creates a full-bar clip, snapped to that numbered bar, via `TrackListModel::addClipAtBeat`; its duration follows the current time signature, and the backend converts musical beat positions to samples using the project tempo. The timeline displays model-owned clips and extends to the furthest clip. Double-clicking where the new bar would overlap an existing clip does not create a duplicate.

Clip resizing follow-up: a handle on the clip's right edge previews length changes while dragging and commits the new length to the project model on release. Resizes snap to one time-signature beat unit and cannot shrink below one unit. Build and offscreen startup succeeded. Real audio import/recording still needs to populate clips with media pool IDs.

Transport controls follow-up: reordered the bottom-bar controls to skip-to-start, skip-forward, record, play, pause, stop, repeat, using standard transport glyphs. Replaced default button styling with compact dark controls and added styled top-left-aligned hover hints for every transport button. Transport actions remain visual until wired to the engine. Build and offscreen startup succeeded.

Visual transport follow-up: transport actions are handled in QML. Play advances the red playhead at the current BPM, Pause freezes it, Record plays while extending a temporary visible recording tail, Stop returns to bar 1, and repeat toggles wrapping. Play/Record/Repeat buttons show active states. This is visual simulation only; it does not start audio or create recorded media. Build and offscreen launch succeeded.

Track-card layout follow-up: each card is now vertically ordered as track name, smaller track type, Solo/Mute/Rec/close buttons, then the volume slider. The close button is persistently visible instead of appearing only on hover. Build and offscreen launch succeeded.

Roadmap follow-up: documented ToneRig's locations (`backend/plugins/tonerig/`, reusable `backend/dsp/`, `wrapper/pluginWrap/`, and `frontend/plugins/tonerig/`) in `roadmap.txt`, `.claude/ROADMAP.md`, and the architecture map. Expanded Phase 9 with the guitar signal flow, dry-recording split, tuner read-only behavior, oversampling, gain staging, latency target, IR loading thread rules, and reuse of the existing compressor/EQ/reverb/delay work.

## Next steps

Previous checkpoint: **listen to GuitarSuite Phase 1 live before adding Phase 2 pedals**. The user subsequently requested Phase 2; see the implementation update below. The 2026-11-01 resumption rebuilt `appChaosMaker` and `backendtests` successfully (already up to date), ran both `build/backendtests` and `build-rt/backendtests` (17 tests each, zero failures), and ran an offscreen startup for four seconds (expected timeout 124, no QML diagnostics). Desktop audio access from the agent session failed (`pactl info`: Operation not permitted / Connection refused), so live routing, audible tone, and round-trip latency are **not verified**.

### Phase 1 live listening checklist

1. Connect the interface before launching; inputs/outputs are discovered at startup. From a desktop terminal in the project directory, run `./build/appChaosMaker`. Disable interface direct monitoring when comparing the processed sound and begin with low headphone/output volume.
2. Add one **Instrument → Electric Guitar** track. Select the interface output in the top bar, start at **128 frames**, and use the track's **IN** popup to select the guitar capture device. Only one Electric Guitar track should be armed for this test: the first armed Electric Guitar supplies the shared monitor input, gain, and EQ. Input channel selection is still pending; verify the chosen device is actually capturing the guitar jack.
3. Set track volume to **100%**, leave the separate ten-band EQ flat, and toggle **Rec On**. The monitor should sound dry with an empty pedalboard and go silent on **Rec Off**. Monitoring does not require transport Play or Record; transport recording remains visual simulation. Solo/Mute are not connected to this monitor path.
4. Open **FX → GuitarSuite**. Add Overdrive alone, compare **ON/BYP**, then add Delay and Reverb one at a time. Suggested starting values: Overdrive Drive 12 dB / Tone 0.62 / Level -6 dB; Delay Time 320 ms / Feedback 0.34 / Mix 0.25; Reverb Room 0.72 / Damping 0.28 / Mix 0.22. These are the existing defaults. Listen for control clicks, sustained-note instability, missing stereo output, and runaway tails.
5. Compare pedal orders, bypass each pedal, and save/load a `.gspreset`; confirm order, bypass, knob values, and sound return. Test UI drag-reordering explicitly: the offline order test verifies DSP ordering, not the drag gesture. The board currently controls one shared live monitor chain, not independent per-track chains. Save a preset before exiting; project persistence is not implemented.
6. After the 128-frame baseline, try **64 frames** and return to 128 if it crackles. Record the active buffer shown in the status strip, interface/input/output, tone observations, and any dropouts. Its milliseconds value is one JACK block, not measured round-trip latency. Disarming immediately silences the monitor, including effect tails.

Live listening result: **pending user desktop test**. The user explicitly authorized proceeding with Phase 2.

### Remaining work

1. Review/run the UI visually and tune spacing, scaling, colors, and resizing behavior.
2. Listen to the implemented GuitarSuite Phase 1 and Phase 2 pedals live. The 10-band EQ remains a separate track FX option; pitch effects and amp/cab are future batches.
3. Add hotplug refresh and input channel selection; the current JACK selectors enumerate ports once at app startup and route mono input to a stereo output pair.
4. Add realtime-safe input monitoring selection for other audio tracks, then recording to disk.
5. Amp/cab processing and EQ presets are implemented; listen to Amp → Cabinet IR with a real cabinet WAV next. Pitch effects remain a later batch.
6. Connect project track controls and mixer values to immutable engine snapshots or RT-safe engine commands when the audio graph supports routing/mixing.
7. Wire transport buttons, time display, tempo, and meter to the engine/controller.
8. Wire top-bar project actions, undo/redo, and metronome.

## Notes for the next session

- QML is registered by the `GLOB_RECURSE FRONTEND_QML` in `CMakeLists.txt`; adding files triggers CMake's automatic reconfigure.
- `Theme` is a singleton in `frontend/Theme.qml`.
- Existing `StatusStrip` still reports `EngineController.version/status` and should remain at the bottom.
- `EngineController` still only exposes engine version/status (`wrapper/engineWrap/enginecontroller.h`). `TrackListModel` owns the current UI-thread `Project`; its state is not yet passed to the mixer/audio engine.
- `Project` and its `Track`/`AudioClip` types are explicitly UI-thread document data. Keep mutations in the UI/model layer and route future audio graph updates through the engine command/snapshot path; never allocate or mutate `Project` from the audio callback.

## GuitarSuite Phase 2 implementation — 2026-11-01

- Added standalone `noisegate/`, `compressor/`, `wah/`, `chorus/`, and `tremolo/` processors, with shared `backend/dsp/pedalcontrols.h`. Gate uses hysteresis and attack/release; compressor uses linked stereo peak detection, ratio and makeup; wah has a manual Position dial; chorus uses modulated stereo delay; tremolo uses a sine LFO. Parameters are smoothed and processing uses preallocated storage.
- Added `tuner/pitchdetector.*` and `tuner/tuner.*`: fixed-storage YIN analysis decimated toward 12 kHz and spread across samples. The engine calls the chain's analysis hook on dry device input before track gain and EQ. Tuner position never changes its source or sound; bypass suspends analysis. The card displays note, cents, and Hz (A4 = 440), with stale display expiry after disarming. Intended for single-note guitar tuning, approximately 55–1200 Hz.
- Shared registry/presets now handle nine pedals. The pedal picker uses a dropdown; cards scroll vertically for the compressor's five controls. Drag release explicitly sends a drop and restores the card's position binding.
- Phase 2 listening: start with Noise Gate → Compressor → Wah → Overdrive → Chorus → Tremolo → Delay → Reverb, enabling one addition at a time. Add Tuner anywhere and play single notes. Test gate decay, compressor loud/soft response, wah Position sweep, stereo chorus, tremolo Rate/Depth, and preset recall. Live audio and round-trip latency remain unverified in this restricted session.
- Verification: app build succeeded; all **26 backend tests** passed in normal and realtime-sanitized builds, including stereo linking/spread and tuner checks at 44.1/48/96 kHz. A temporary Qt model harness passed nine-pedal preset order/bypass/parameter round-trip. A temporary all-pedals UI launch stayed running offscreen for five seconds without QML diagnostics (expected timeout 124); temporary startup changes were restored and the normal app rebuilt. Drag gesture behavior and audible quality still need desktop interaction.

## Live feedback fixes — Wah and Standard tuner

- User reported Wah making the other effects disappear. The original 100% band-pass replacement strongly attenuated content away from its sweep frequency. Wah now blends a resonant peak into the full signal, retaining lows/highs and upstream effect tails. Existing Position/Resonance/Mix settings and presets remain supported; the new response can be louder near resonance.
- Tuner now uses Standard guitar targets E2–A2–D3–G3–B3–E4 (strings 6–1, A4 = 440 Hz). Auto chooses the closest standard target; individual string buttons lock the target for badly detuned strings. The selected target is saved in `.gspreset` through the `string` parameter (0 = auto, 1..6 = low-to-high pitch).
- Tuner card shows target note, signed cents, LOW/FLAT → tune up or HIGH/SHARP → tune down, current/target Hz, and a centered ±50-cent meter. Within ±3 cents shows green “In tune”. Off-scale readings retain the full numeric offset. Bypassed/silent/stale readings show no needle.
- Verification: app built; all 28 tests passed normally and with realtime sanitizer. Added regressions for broadband preservation/upstream delay through Wah, and exact/flat/sharp standard targets. A Qt model harness fed all six strings at −25/0/+25 cents through the detector and verified wrapper direction/target results and saved string selection. All-pedals offscreen launch had no QML diagnostics; temporary startup changes were restored and the normal app rebuilt. Audible confirmation remains a desktop test.

## Tremolo BPM sync

- Added a `Sync to BPM` switch and beat-division selector to the Tremolo card. Divisions: whole, half, quarter, eighth, sixteenth, dotted eighth, and eighth triplet. Default remains free-running Hz for compatibility with existing presets; synced default division is a quarter note. Manual Rate is disabled while synced; Depth works in both modes.
- `Main.qml` binds the timeline/transport BPM to `EngineController.bpm`. The controller sends validated 0.01–10000 BPM to `AudioEngine` through a latest-value atomic; each live block passes this BPM in `ProcessContext` (including the analysis context). Rebuilding a pedal chain does not reset tempo. No full audio transport implementation is required.
- Tremolo derives pulses/second from BPM and the selected note length. It preserves oscillator phase and smooths pulse-rate changes when BPM, division, or mode changes. This matches pulse rate to tempo while monitoring, including when transport is stopped; it does not align oscillator phase to the visual playhead.
- Sync and division are normal parameter descriptors persisted in `.gspreset`; old presets retain manual-rate mode. At 120 BPM, quarter-note sync gives 2 pulses/second and eighth-note sync gives 4.
- Verification: all 31 backend tests pass normally and with realtime sanitizer. New tests cover division pulse counts at 60/120/180 BPM, manual-rate independence, block-size independence, smooth rate changes, extreme/invalid tempo, and engine tempo delivery across snapshot changes.
- UI/preset verification: a temporary Qt model harness passed sync/division/manual-rate preset round-trip and loading an older preset without sync fields. A temporary offscreen UI harness emitted bottom-bar BPM edits at 90 and 137.5, checked EngineController.bpm after each, toggled sync/division, and exited successfully via its completion timer. The audio remote was deliberately unavailable for this UI-only check. Temporary startup changes were restored and the normal app rebuilt. Live listening remains a desktop check.

## Input calibration foundation

- After “go next”, implemented the next step from the agreed GuitarSuite plan: input trim and level metering before amp/cab work.
- Pedalboard header now has −24 to +24 dB input trim, a 0 dB reset, Raw input / Pre-FX / Output L / Output R peak meters, and Clear clips. The white marker on Pre-FX is −12 dBFS. Play hard and adjust trim toward this marker; if Raw input clips, lower interface gain instead. Lower pedal levels if outputs clip.
- AudioEngine smooths trim over 20 ms before track EQ and the chain. Pre-FX measures after trim, track gain, and EQ; raw input and tuner analysis remain untouched. Trim is shared by the current single live monitor chain and saved as optional `inputTrimDb` in `.gspreset`; old presets default to unity. Invalid trim fields reject a preset before changing the model.
- `backend/dsp/peakmeter.h` accumulates block peaks atomically so transients between UI polls are retained. Near-full-scale clips latch at linear peak ≥0.999; clear resets the indicators and continued clipping relatches them. Controller polls at 20 Hz and decays the displayed level by 24 dB/second. Raw input meters even while disarmed; pedal input/output decay to silence when disarmed.
- Added `frontend/plugins/guitarsuite/LevelMeter.qml` and `tests/plugins/monitorlevels_test.cpp`. All 34 backend tests pass normally and with realtime sanitizer, including trim smoothing, separate raw/pre-FX/stereo outputs, unchanged dry analysis/input, retained transient peaks, clipping/reset, and invalid controls.
- Next implementation milestone remains amp and cabinet IR processing. Hardware listening and measured round-trip latency require the desktop session.
- UI/preset verification: temporary Qt model harness passed trim save/load, legacy default, invalid-preset rejection without changing trim, and change notifications. Offscreen all-pedals UI harness changed/reset trim, checked all four meter properties, cleared clips, and exited successfully via its completion timer with no QML diagnostics. Temporary startup edits were restored and the normal app rebuilt.

## GuitarSuite amp/cab stage and EQ presets

- User accepted the amp/cab stage and also requested equalizer factory presets and custom save/load. Both are implemented.
- Added `backend/plugins/amp/`: generic two-stage asymmetric shaping, DC blockers, input high-pass, mild envelope-driven gain reduction, filtered 4x oversampling (`backend/dsp/oversampler4x.h`), Bass/Mid/Treble/Presence, and Master. Parameters are smoothed. This is an initial generic amp, not a specific hardware model or neural amp. The oversampling filters add about 10.5 samples (~0.22 ms at 48 kHz); the processor reports 11 samples. No −60 dB alias-rejection claim or hardware listening result has been established.
- Added `backend/plugins/cabinet/` with Mix and Level and a zero-added-delay direct-head/FFT-tail convolver in `backend/dsp/partitionedconvolver.*`. Mono IRs are duplicated to both channels; stereo IR channels stay separate. The internal 128-frame partition size is independent of JACK buffer size. With no IR selected, Cabinet passes dry audio.
- `backend/audiofile/impulsewav.*` handles bounded RIFF WAV PCM16/24/32, IEEE float32/64 and supported extensible subformats, with windowed-sinc resampling, maximum 32 MB input file and first 200 ms of IR audio. Silent, nonfinite, malformed, unsupported and missing files are rejected.
- The registry now offers eleven modules. Cabinet cards have Load cabinet IR and Clear IR controls. Qt Concurrent workers read/resample/prepare IRs; the UI publishes a newly prepared slot through EngineSnapshot while the old chain keeps playing. The model exposes busy/error state and blocks structural edits during loading. Failed loads leave the active chain unchanged. Whole-chain presets use relative WAV paths; audio is not embedded. Resource-bearing preset loads prepare all candidate processors before a single model commit. Presets now use QSaveFile for atomic writes.
- Standalone EQ: added seven factory curves (Flat, Electric Guitar, Warm, Bright, Mid Scoop, Vocal Clarity, Bass Cut) in `backend/plugins/eq/eqpresets.h`. The EQ panel has a selector/Apply preset plus Save custom/Load custom dialogs for `.eqpreset`. All ten frequencies and ±12 dB gain values are validated before changing the selected track. Applying/loading on the armed guitar updates live monitoring. Presets can be transferred across tracks; other tracks remain unchanged. Project persistence remains separate.
- Build now links Qt Concurrent. Added a separate `wrappertests` executable for Qt model integration; backendtests and sanitizer builds remain Qt-free. Run `./build/wrappertests qtpresets` or CTest's `qtpresets` suite. README and architecture map are updated.
- Verification: all 44 backend cases pass normally and with realtime sanitizer. New cases cover amp controls/DC/master/block independence, FFT-vs-direct convolution including partition boundaries and long IRs, cabinet stereo/dry/mix behavior, factory EQ curves, PCM/float/extensible WAV decoding, resampling/capping and invalid-file rejection. Four retained Qt integration cases cover EQ save/load/isolation/invalid files and async IR loading, relative-reference recall and failure preservation. Address/undefined-behavior sanitizer tests also pass with leak detection disabled: LeakSanitizer itself cannot operate under this environment's tracing.
- An offscreen software-rendered UI harness opened the EQ and amp/cab panels, applied/saved/loaded EQ, loaded a generated test IR, recalled a resource-bearing GuitarSuite preset, captured previews and exited successfully with no QML diagnostics. Preview images are `/tmp/eq-presets-smoke.png` and `/tmp/ampcab-smoke.png`. Temporary startup edits were restored and the normal app rebuilt. Desktop audio access remains unavailable here.
- Live test next: keep output volume low, aim Pre-FX near −12 dBFS, add Amp then Cabinet IR, and load your own cabinet WAV. Start Amp Drive 14 dB / Master −12 dB and cabinet Mix 1 / Level 0 dB. Compare bypass, sweep tone controls, check output clip indicators, then put Delay/Reverb after the cabinet. Save/load the chain and verify the WAV reference resolves. Test EQ factory/custom recall independently through FX → Equalizer. Scroll vertically inside Amp to reach every control.

## User live-listening confirmation

- After the amp/cab stage and chain-order guidance, the user reported that the rig “works perfectly”. Hard strums occasionally push the output meter into yellow.
- Current meter colours: yellow above −12 dBFS; the main bar turns red at 0 dBFS, with a separate latched clip indicator at linear peak ≥0.999. Yellow alone is normal headroom use, not proof of clipping. If more output headroom is desired, lower Amp Master slightly rather than input trim/Drive, preserving distortion character.
- This confirms the user's subjective live-listening result. No numerical round-trip latency measurement was supplied. At that checkpoint, pitch effects were the next proposed GuitarSuite feature; they have since been requested and implemented below.

## Pitch pedals and BPM editing fix

- User requested the next pitch batch and reported being unable to change BPM. The backend tempo setter already existed; the previous check had bypassed the actual editor by invoking its signal. A retained Qt mouse/keyboard regression reproduced the old field failing to replace/commit BPM.
- Added reusable `frontend/transportbar/TempoEditor.qml`: entering the field selects its value, Enter or focus loss commits, −/+ buttons step by 1 BPM, decimal point/comma work, and empty/invalid/out-of-range values revert. Supported range remains 0.01–10000 BPM with two decimal places. There is no restrictive validator preventing temporary partial text while editing.
- Also identified the modal GuitarSuite popup as a blocker for bottom-bar interaction. GuitarSuite is now nonmodal and closes through its close button/Escape rather than outside clicks, so BPM can change while the board stays visible. A retained UI test opens the actual board, clicks/types in the actual BPM field, and checks the controller changes while the board stays open.
- EngineController.bpm is now the single tempo authority: bottom-bar edits write to it; Main.qml binds the timeline's BPM to it; Timeline updates document tempo. The controller's existing atomic backend setter supplies ProcessContext.bpm, including Tremolo. Full transport playback/recording is still separate.
- Added `backend/plugins/octaver/` (Dry, Octave down, Octave up, Level) and `backend/plugins/pitchshift/` (integer −12…+12 Semitones, Mix, Level). Octaver normalizes summed blend weights above unity. Shared `backend/dsp/pitchvoice.*` and `radix2fft.h` implement a fixed-storage phase-vocoder voice with 1024-sample FFT / 128-sample hop, smoothed controls, separate stereo channels, and no audio-thread allocation. Upward-shifted frequencies above Nyquist are omitted.
- Important audible limitation: shifted voices have about 1024 samples of processing delay (~21.3 ms at 48 kHz), though the dry path remains immediate. These are initial spectral effects, best with clear single notes; chords/transients may have artifacts. Pitch Shifter at zero semitones or Mix 0 passes dry audio at unity Level. These effects add wet-path latency beyond the original clean-monitor target; no new round-trip measurement is claimed.
- Registry/presets now handle thirteen modules. Semitones use 1-semitone dial increments. Both modules share bypass, ordering and `.gspreset` support; preset integration tests cover their transposition/blend values, order and bypass.
- Verification: all 51 backend cases pass normally and with realtime sanitizer; address/undefined-behavior checks also pass with leak detection disabled due the previously documented tracing limitation. Seven new pitch cases check transposition frequencies, low/high open strings, octave blends/dry output, reset/silence, block independence, finite extremes, stereo isolation and rejection above Nyquist. All five retained Qt preset integration cases pass.
- Added optional `transporttests` target when Qt Test is installed. Five actual UI regression methods pass (Qt Test also reports init/cleanup separately): click/type replacement, decimal point/comma, Enter/focus-loss/invalid handling, step buttons/range bounds, and BPM editing with the real pedalboard open. This target includes the production transport, board and controller and isolates JACK/Pulse from desktop audio. CTest entry: `transport_ui`.
- A temporary full-app harness sent mouse and keyboard events to the bottom-bar field with the actual pedalboard already open, checked both EngineController and timeline at 90 and 137.5 BPM, captured `/tmp/pitch-tempo-smoke.png`, and exited successfully. Temporary Main.qml/main.cpp instrumentation was restored and the normal app rebuilt. Real interface listening to the new pitch pedals remains pending.
- Live test next: reopen the rebuilt app; leave GuitarSuite open, click BPM and enter a value or use −/+. Enable Tremolo Sync to BPM to hear the tempo change. Start with one pitch pedal before Overdrive/Amp, use single notes, and compare Dry/Mix and bypass before combining effects. The broader next DAW milestone remains real dry-input recording/playback.

## Crunch and Distortion for live rock/metal monitoring

- User requested separate Crunch and Distortion pedals before recording, and asked for related rock/metal pedal recommendations. Implemented these two modules; further recommendations are not additional authorized implementation work.
- `backend/plugins/crunch/`: lighter asymmetric arctangent clipping, 70 Hz input high-pass, oversampled DC removal, Tone, Body and Level. Defaults: Drive 12 dB, Tone 0.6, Body +1 dB, Level −6 dB. Its picking-level response is deliberately less compressed than Distortion.
- `backend/plugins/distortion/`: stronger tanh compression into harder clipping, Tight (70–250 Hz pre-clipping high-pass), Bass/Mids/Treble ±12 dB, Mid frequency 300–1600 Hz and Level. Defaults: Drive 30 dB, Tight 0.6, EQ neutral, Mid frequency 750 Hz, Level −10 dB. Post-gain EQ helps shape rhythm/lead tones without changing input clipping.
- Both use existing filtered 4x oversampling, shared new `backend/dsp/dcblocker.h`, 20 ms smoothed controls, independent stereo state and roughly 11 samples of filter delay. These are generic DSP voicings, not named hardware models or claims of measured alias rejection. No recording engine changes were needed: adding/arming an Electric Guitar track already feeds the live snapshot chain.
- Registry now offers fifteen modules; controls are descriptor-driven and share bypass, ordering and `.gspreset` persistence. Added a scroll hint and always-visible vertical scrollbar when a card overflows so the seven-control Distortion card's lower controls are discoverable.
- Seven retained cases in `tests/plugins/drives_test.cpp` verify distinct dynamics, Tight low-end attenuation, mid gain/frequency response, Crunch tone/body, output level/smoothing, block independence, finite extremes at 44.1/48/96 kHz, silent stereo isolation, DC/silence clearing and the engine's monitor/bypass/disarm path without recording. A new Qt preset case checks ordering, bypass and saved controls for both modules.
- Verification: all 58 backend cases pass normally and with realtime sanitizer; all six Qt preset integration cases pass. Existing transport UI tests pass. The offscreen software-rendered app opened both new cards and saved/loaded their preset successfully; temporary startup changes were restored and the normal app rebuilt. Preview: `/tmp/metal-smoke.png`. Live tone confirmation remains with the user's interface.
- Listening start: Noise Gate → Crunch OR Distortion → Amp → Cabinet IR; start with one drive enabled, a modest Amp Drive, and use pedal Level/Amp Master to keep output peaks below clipping. These effects work with Rec On monitoring without transport Record. Scroll inside Distortion for Mids, Mid frequency, Treble and Level.
- Recommendations to discuss separately: a movable parametric EQ after Amp/Cab for final mid shaping; Fuzz for a rougher alternative; Phaser for rock movement. Existing gate, overdrive and EQ are useful before adding more. Reference reading used: BOSS metal guide (https://articles.boss.info/multi-effects-genre-guide-metal/), FZ-1W overview (https://www.boss.info/us/products/fz-1w/), PH-3 overview (https://www.boss.info/us/products/ph-3/). None of those additional pedals was implemented in this turn.

## Movable Parametric EQ, Fuzz and Phaser

- User explicitly requested all three previous recommendations. Added these modules to the existing live-monitor chain; registry now exposes eighteen modules. Their controls, order and bypass save/load through `.gspreset` without new recording support.
- `backend/plugins/parametriceq/`: three serial peaking bands with independent frequency/gain/Q and output Level. Ranges: Low 80–500 Hz, Mid 200–2500 Hz, High 1500–10000 Hz, gain ±12 dB, Q 0.3–4, Level ±12 dB. Starts flat. This is a movable pedal, intended as a post-amp/cab option, distinct from the separate ten-band track EQ and its `.eqpreset` files. Filter frequencies/gains/Q/output are smoothed over 20 ms; no block buffering.
- `backend/plugins/fuzz/`: two biased tanh saturation stages with filtered 4x oversampling and oversampled DC removal; a 25 Hz input high-pass retains more low end than the tighter Distortion. Controls: Fuzz 0–36 dB (default 24), Sustain 0–1 (0.65), Tone 0–1 (0.5), Level −24…+6 dB (−10). Sustain controls extra stage gain/compression, not a synthetic tail. Filter delay is approximately 11 samples. Generic original DSP voicing, not a hardware-emulation claim.
- `backend/plugins/phaser/`: six swept all-pass stages, feedback normalization, Depth/Feedback/Mix/Level, and manual Rate 0.05–12 Hz (default 0.4) or Sync to BPM. Default Depth 0.7, Feedback 0.35, Mix 0.5, Level 0, sync off; synced default division is a half note. The same seven straight/dotted/triplet divisions as Tremolo are available. Shared `backend/dsp/tempodivision.h` now computes note frequencies for both processors. Phaser matches tempo rate, not playhead phase, and adds no block buffering.
- Reused the modulation sync controls for Phaser, disabled its manual Rate while synced, and exposed descriptive object names for interaction tests.
- Fixed an existing shared control issue found during UI checks: parameter-role updates rebuilt the knob Repeater, which could disrupt focus/grabs during editing. Cards now snapshot descriptor metadata once and fetch live values separately; readouts and custom tuner/sync controls still update from current parameters. A retained UI test verifies the same dial survives a parameter update, displays the new value, and can update the model from keyboard input.
- Eight new backend cases cover EQ flatness and exact center gains, Q bandwidth/output level, Fuzz sustain/tone/level, Phaser dry/moving/static behavior, sync/manual equivalence across divisions/BPM, block independence, finite min/max settings at 44.1/48/96 kHz, stereo isolation, post-gain ordering, smooth level updates and live bypass fading. All 66 backend tests pass normally and with realtime sanitizer; original Tremolo tests also pass after sharing the division helper.
- All seven Qt preset integration cases pass, including recall of the new modules' controls, order and bypass. The transport/UI target now has seven actual interaction cases (plus Qt init/cleanup): prior BPM tests, Phaser sync/manual-rate/beat-selector controls, and stable dial updates. The completed UI suite passes. New modules still require subjective live listening on the user's interface.
- Suggested starting order: Noise Gate → Fuzz (or another drive) → Amp → Cabinet IR → Parametric EQ → Phaser → Delay → Reverb. Add these one at a time and keep an eye on the existing output meters when boosting EQ or gain.
- Final UI/preset smoke: the app opened all three new cards, enabled Phaser sync, saved/loaded their chain preset, captured `/tmp/extra-fx-smoke.png`, and exited successfully without QML diagnostics. Temporary startup edits were restored and the normal app rebuilt. Modulation sync labels use the dark-theme text colour; the inactive free-rate control is labeled manual while synced.

## Djent and Jazz factory presets

- User requested exactly two GuitarSuite presets (Djent/Jazz), then explicitly asked to test only changed behavior rather than rerunning existing pedal suites. No pedal DSP was changed or retested in this work.
- Added original version-1 factory chain JSON at `resources/presets/guitarsuite/Djent.gspreset` and `Jazz.gspreset`, embedded through CMake for the app and Qt test executables. Model API: `factoryPresetNames` and `applyFactoryPreset(name)`; the panel has a two-entry Factory Preset picker and explicit Apply preset button. Applying replaces the chain, and user edits still save/load with existing custom .gspreset support.
- Djent: Noise Gate (−42 dB threshold, 0.3 ms attack, 35 ms release) → Distortion (28 dB Drive, Tight 0.9, mildly scooped mids, Level −12 dB) → Amp (Drive 4 dB, Master −8 dB) → Cabinet IR → Parametric EQ (low/low-mid cuts, a small high-mid lift, Level −1 dB). Intended as a tight rhythm starting point.
- Jazz: Compressor (−20 dB threshold, 1.6:1, 25 ms attack, 180 ms release, +1.5 dB makeup) → Amp (Drive 0 dB, warmer EQ, Master −5 dB) → Cabinet IR → Parametric EQ (gentle warm shaping) → Reverb (Room 0.35, Damping 0.65, Mix 0.12). No gate or distortion pedal; intended as a warm clean starting point.
- Factory application preserves current input trim, BPM, and the separate track EQ. It reuses the first loaded cabinet IR and its Mix/Level settings without rereading or resetting the cached processor at the same sample rate. A new slot uses the preset's enabled/bypass state, so an existing bypassed cabinet becomes enabled. If no IR is loaded, the factory's Cabinet slot passes dry until the user supplies one; no external audio is bundled. A sample-rate mismatch rebuilds through the existing worker loader.
- Factored the existing custom parser into `loadPresetData` so factory/custom validation and snapshot publication share one path. Custom file loads continue to apply their saved trim/IR references; factory application leaves the current custom-save path alone. Invalid factory names leave the current chain intact.
- Targeted verification: two new Qt factory cases pass, checking exact declared types/order/parameter values, one publication per apply, trim/path preservation, invalid names, reuse of a cached IR even after removing its source file, and custom-save/edit/reload of Jazz. One new actual UI picker interaction test passes for selecting and applying Djent/Jazz. The initial `factory_` filter also matched one existing EQ factory-preset check; no full backend or realtime-sanitizer suite was rerun. Use the narrower `./build/wrappertests qtpresets.factory_` filter for just the two new model cases.
- The app builds successfully; no subjective audio/genre-quality claim is made without the user's listening test. Reopen the rebuilt app, choose Djent or Jazz at the top of GuitarSuite, then Apply preset. Keep the existing cabinet loaded for the intended amp/cab tone and save any changes as a custom preset.

## Real dry recording/playback, startup restore and sustain Tremolo — 2026-11-01

- User authorized the record/playback milestone and additionally requested optional startup-session
  restoration and Tremolo that enters on sustained notes. They reported their own changes to
  distortion.h/.cpp. Both Distortion files were read and preserved byte-for-byte (SHA-256 verified).
- AudioEngine now handles Play/Pause/Stop/Seek/Repeat and Record on a sample clock. UI playhead/time
  no longer use a visual simulation timer. Recording captures the mono JACK input before trim,
  EQ, GuitarSuite or fader. TakeRecorder owns a bounded SPSC ring, a background float32 WAV writer
  and an immutable RAM playback result. No disk work, allocation or waiting occurs in audio.
- Stop finalizes a take and returns to zero; Pause finalizes and preserves position; toggling Record
  stops recording while transport continues. Disarming ends a take. Record requires exactly one
  armed Audio/Vocal/Electric Guitar track and connected I/O. Repeat is disabled for recording;
  seeking and removing/changing the recording track's input are prevented until finalization.
  Normal window close quiesces JACK, drains/finalizes the take, attaches its clip and saves recovery.
- Completed takes become actual timeline clips. Playback runs with no live input and applies track
  volume/mute/solo; a second snapshot slot keeps document playback separate from GuitarSuite.
  Integration testing found an old pending snapshot could briefly hide a new clip on immediate
  Play. Playback now atomically replaces unconsumed snapshots, deleting discarded ones on the UI
  thread, so the next audio block sees the newest edit. Monitor volume is now after FX, allowing
  mute/solo to silence effect tails too. Stereo output has a finite-value clamp at the final sum.
- Current limitation: playback of recorded clips is DRY. GuitarSuite remains one shared live
  monitoring chain. Per-track insert/EQ playback routing, general WAV import, recording-latency
  compensation, portable project save/open and device hotplug remain future work. There is no
  claim of a measured live latency or audible quality test. WAVs are normally in
  ~/.local/share/ChaosMaker/ChaosMaker/Recordings; takes cap at one hour or 1 GB (RAM playback).
- Settings → Restore session on startup defaults off. When enabled, changes debounce to an atomic
  last-session.json in the application-data folder and save on normal exit. It embeds the document
  and GuitarSuite state, including calibration, all pedal controls/order/bypass and absolute IR
  references. Tracks, takes, tempo/meter, volume/mute/solo, input assignments and track EQ return.
  Startup stays stopped/disarmed. Missing media references remain in the document; malformed
  sessions and failed pedal/IR restoration are reported and protected against autosave overwrite.
  Startup WAV decoding currently runs on the UI thread, so a large session can take time to open.
- Tremolo adds Sustain only (off by default), Attack delay 20–1000 ms (180), Fade in 20–1500 ms
  (250), and Note threshold −80…−20 dB (−55). A fast envelope and a slower follower detect a new
  attack, delaying and fading in LFO depth; repicks reset it. Both manual and BPM-synced rates work.
  Old presets default to continuous tremolo. This is amplitude-based onset detection, not pitch or
  musical-intent detection; soft legato/compressed input can fail to retrigger. Put before delay/
  reverb. The checkbox was moved to the top after an actual mouse test exposed poor visibility.
- Validation: six focused recording backend cases, two sustain cases, three existing affected
  Tremolo sync cases, and monitor-level regression cases pass. Recording and sustain cases pass
  realtime and address/undefined-behavior sanitizers (ASan leak detection disabled because of the
  environment's tracing limitation). No full unrelated pedal suite was rerun; an initial loose
  recording filter incidentally included one drive-monitor test, so subsequent commands use a dot.
- Qt model checks cover media/control/preset roundtrip and missing-file handling. Two new offline
  device integration cases exercise controller Record → Stop → clip → Play → close/reopen/play,
  close while recording, and corrupt-file preservation. All pass. Eleven actual UI interaction
  methods pass (13 including Qt init/cleanup), covering the new sustain checkbox/startup setting/
  unavailable-audio behavior plus the existing affected transport/board controls.
- Full production app offscreen smoke restored and autosaved a seeded track/WAV, 97.5 BPM, 7/8
  meter and sustain Tremolo setting. It stayed running for four seconds with no QML diagnostics.
  Test data/settings were isolated under /tmp; normal startup source was not instrumented.
- Next user check: reopen ./build/appChaosMaker; arm one Electric Guitar track, select I/O, Record
  a short phrase, Stop, disarm, then Play. Expect dry playback. Enable Settings → Restore session
  on startup, close/reopen and confirm the clip/chain return. Try Tremolo Sustain only with isolated
  held notes, then repicks, adjusting threshold/delay/fade for the guitar.
