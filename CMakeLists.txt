cmake_minimum_required(VERSION 3.21)

project(ChaosMaker VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)     # DSP in a debug build is too slow to judge
endif()

# The sanitizer builds turn this off, so they need only the engine and the tests.
option(DAW_BUILD_APP "Build the Qt app as well as the engine and tests" ON)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)
find_package(nlohmann_json 3 REQUIRED)

# ---------------------------------------------------------------- the engine (no Qt)
# Everything under backend/ is plain C++20. It is built as its own library and does not link to Qt,
# so a Qt include in there fails to compile.
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/backend/*.cpp")

add_library(dawengine STATIC ${ENGINE_SOURCES})
target_include_directories(dawengine PUBLIC "${CMAKE_CURRENT_LIST_DIR}/backend")
target_compile_definitions(dawengine PUBLIC DAW_VERSION="${PROJECT_VERSION}")
# PUBLIC: the wrapper and tests must compile the backend's inline headers the same way.
target_compile_options(dawengine PUBLIC -march=x86-64-v3 -fno-math-errno)
target_compile_options(dawengine PRIVATE -Wall -Wextra)
target_link_libraries(dawengine PUBLIC Threads::Threads PkgConfig::JACK)
# PRIVATE: only backend .cpp files can see it. Keep its include inside backend/persistence/.
target_link_libraries(dawengine PRIVATE nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------- the app
if(DAW_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Quick Concurrent)
    qt_standard_project_setup(REQUIRES 6.10)

    # wrapper/  : QObject classes that connect the engine to QML (any subfolders)
    # frontend/ : the QML files (any subfolders). File names must start with a capital letter and be
    #             unique across all subfolders.
    file(GLOB_RECURSE WRAPPER_SOURCES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/wrapper/*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/wrapper/*.h")

    file(GLOB_RECURSE FRONTEND_QML CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_LIST_DIR}"
        "${CMAKE_CURRENT_LIST_DIR}/frontend/*.qml")

    set_source_files_properties(frontend/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    set_source_files_properties(frontend/icons/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

    qt_add_executable(appChaosMaker
        main.cpp
    )

    qt_add_qml_module(appChaosMaker
        URI ChaosMaker
        QML_FILES
            Main.qml
            ${FRONTEND_QML}
        SOURCES
            ${WRAPPER_SOURCES}
    )

    target_include_directories(appChaosMaker PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

    # Qt's generated registration code includes the wrapper headers by bare file name,
    # so every folder under wrapper/ has to be on the include path.
    set(WRAPPER_DIRS "")
    foreach(f ${WRAPPER_SOURCES})
        get_filename_component(d "${CMAKE_CURRENT_LIST_DIR}/${f}" DIRECTORY)
        list(APPEND WRAPPER_DIRS "${d}")
    endforeach()
    list(REMOVE_DUPLICATES WRAPPER_DIRS)
    target_include_directories(appChaosMaker PRIVATE ${WRAPPER_DIRS})

    target_link_libraries(appChaosMaker PRIVATE Qt6::Quick Qt6::Concurrent dawengine)
endif()

# ---------------------------------------------------------------- tools (no Qt)
# fxrender: runs a dry WAV through guitar suite pedals offline (listening tests, plan 6.3).
add_executable(fxrender tools/fxrender/fxrender.cpp)
target_compile_options(fxrender PRIVATE -Wall -Wextra)
target_link_libraries(fxrender PRIVATE dawengine)
# chordrender: the Chords track's autoplay patterns on the grand piano, offline (restMap G4).
add_executable(chordrender tools/chordrender/chordrender.cpp)
target_compile_options(chordrender PRIVATE -Wall -Wextra)
target_link_libraries(chordrender PRIVATE dawengine)
# latencytest: measures the interface's round trip through JACK with a loopback cable (restMap F3).
add_executable(latencytest tools/latencytest/latencytest.cpp)
target_compile_options(latencytest PRIVATE -Wall -Wextra)
target_link_libraries(latencytest PRIVATE dawengine)
# drumbench: scores the drum pattern analyzer against the MIDI a drummer played (restMap G20).
add_executable(drumbench tools/drumbench/drumbench.cpp)
target_compile_options(drumbench PRIVATE -Wall -Wextra)
target_link_libraries(drumbench PRIVATE dawengine)

# ---------------------------------------------------------------- the tests (no Qt)
# tests/abc/abc_test.cpp is found by itself, and ctest gets one entry named abc.
enable_testing()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/tests/*_test.cpp")

add_executable(backendtests tests/backendtests.cpp ${TEST_SOURCES})
target_include_directories(backendtests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/tests")
target_compile_options(backendtests PRIVATE -Wall -Wextra)
target_link_libraries(backendtests PRIVATE dawengine)

foreach(file ${TEST_SOURCES})
    get_filename_component(stem "${file}" NAME_WE)
    string(REGEX REPLACE "_test$" "" suite "${stem}")
    add_test(NAME ${suite} COMMAND backendtests ${suite})
endforeach()

# Qt model integration tests are separate from the Qt-free realtime test binary.
if(DAW_BUILD_APP)
    add_executable(wrappertests
        tests/backendtests.cpp tests/wrapper/presets_qtcheck.cpp tests/wrapper/recording_qtcheck.cpp tests/wrapper/clipediting_qtcheck.cpp
        tests/wrapper/audioengine_qtcheck.cpp tests/wrapper/pianoroll_qtcheck.cpp tests/wrapper/harmonium_qtcheck.cpp tests/wrapper/drumkit_qtcheck.cpp tests/wrapper/electrodrums_qtcheck.cpp tests/wrapper/piano_qtcheck.cpp tests/wrapper/virtualkeyboard_qtcheck.cpp tests/wrapper/drumgroove_qtcheck.cpp tests/wrapper/effectchain_qtcheck.cpp tests/wrapper/chords_qtcheck.cpp tests/wrapper/volumeenvelope_qtcheck.cpp tests/wrapper/mixeffect_qtcheck.cpp tests/wrapper/importexport_qtcheck.cpp tests/wrapper/fileswrapper_qtcheck.cpp tests/wrapper/shortcuts_qtcheck.cpp tests/wrapper/denoise_qtcheck.cpp tests/wrapper/pitchmonitor_qtcheck.cpp tests/wrapper/synth_qtcheck.cpp tests/wrapper/bass_qtcheck.cpp tests/wrapper/drumanalyzer_qtcheck.cpp tests/wrapper/chordanalyzer_qtcheck.cpp
        wrapper/files/fileswrap/fileswrapper.cpp wrapper/files/fileswrap/fileswrapper.h
        wrapper/plugins/denoise/denoisewrap/denoisewrapper.cpp wrapper/plugins/denoise/denoisewrap/denoisewrapper.h
        wrapper/plugins/pitchmonitor/pitchmonitorwrap/pitchmonitorwrapper.cpp wrapper/plugins/pitchmonitor/pitchmonitorwrap/pitchmonitorwrapper.h
        wrapper/settings/shortcutswrap/shortcutswrapper.cpp wrapper/settings/shortcutswrap/shortcutswrapper.h
        wrapper/pianorollwrap/pianorollwrapper.cpp wrapper/pianorollwrap/pianorollwrapper.h
        wrapper/pianorollwrap/pianorollnotemodel.cpp wrapper/pianorollwrap/pianorollnotemodel.h
        wrapper/engineWrap/enginecontroller.cpp wrapper/engineWrap/enginecontroller.h
        wrapper/session/tracklistwrap/tracklistwrapper.cpp wrapper/session/tracklistwrap/tracklistwrapper.h
        wrapper/instruments/guitar/guitarsuiteeditorwrap/guitarsuiteeditorwrapper.cpp wrapper/instruments/guitar/guitarsuiteeditorwrap/guitarsuiteeditorwrapper.h
        wrapper/recording/recordingcontrollerwrap/recordingcontrollerwrapper.cpp wrapper/recording/recordingcontrollerwrap/recordingcontrollerwrapper.h
        wrapper/session/projectsessionwrap/projectsessionwrapper.cpp wrapper/session/projectsessionwrap/projectsessionwrapper.h
        wrapper/session/projectactionswrap/projectactionswrapper.cpp wrapper/session/projectactionswrap/projectactionswrapper.h
        wrapper/settings/quicksettingswrap/quicksettingswrapper.cpp wrapper/settings/quicksettingswrap/quicksettingswrapper.h
        wrapper/audiodevice/devicesettingswrap/devicesettingswrapper.cpp wrapper/audiodevice/devicesettingswrap/devicesettingswrapper.h
        wrapper/settings/appsettingswrap/appsettingswrapper.cpp wrapper/settings/appsettingswrap/appsettingswrapper.h
        wrapper/engine/playbackcontrolswrap/playbackcontrolswrapper.cpp wrapper/engine/playbackcontrolswrap/playbackcontrolswrapper.h
        wrapper/engine/audioenginewrap/audioenginewrapper.cpp wrapper/engine/audioenginewrap/audioenginewrapper.h
        wrapper/plugins/chaoseq/chaoseqwrap/chaoseqwrapper.cpp wrapper/plugins/chaoseq/chaoseqwrap/chaoseqwrapper.h
        wrapper/plugins/mixfx/mixfxwrap/mixeffectwrapper.cpp wrapper/plugins/mixfx/mixfxwrap/mixeffectwrapper.h
        wrapper/session/importexportwrap/importexportwrapper.cpp wrapper/session/importexportwrap/importexportwrapper.h
        wrapper/instruments/harmonium/harmoniumwrap/harmoniumwrapper.cpp wrapper/instruments/harmonium/harmoniumwrap/harmoniumwrapper.h
        wrapper/instruments/synth/synthwrap/synthwrapper.cpp wrapper/instruments/synth/synthwrap/synthwrapper.h
        wrapper/instruments/bass/basswrap/basswrapper.cpp wrapper/instruments/bass/basswrap/basswrapper.h
        wrapper/analysis/drumanalyzerwrap/drumanalyzerwrapper.cpp wrapper/analysis/drumanalyzerwrap/drumanalyzerwrapper.h
        wrapper/analysis/chordanalyzerwrap/chordanalyzerwrapper.cpp wrapper/analysis/chordanalyzerwrap/chordanalyzerwrapper.h
        wrapper/instruments/drums/drumkitwrap/drumkitwrapper.cpp wrapper/instruments/drums/drumkitwrap/drumkitwrapper.h
        wrapper/instruments/drums/groovewrap/drumgroovewrapper.cpp wrapper/instruments/drums/groovewrap/drumgroovewrapper.h
        wrapper/plugins/chordplayer/chordswrap/chordswrapper.cpp wrapper/plugins/chordplayer/chordswrap/chordswrapper.h
        wrapper/instruments/electrodrums/electrodrumswrap/electrodrumswrapper.cpp wrapper/instruments/electrodrums/electrodrumswrap/electrodrumswrapper.h
        wrapper/instruments/piano/pianowrap/pianowrapper.cpp wrapper/instruments/piano/pianowrap/pianowrapper.h
        wrapper/midi/virtualkeyboardwrap/virtualkeyboardwrapper.cpp wrapper/midi/virtualkeyboardwrap/virtualkeyboardwrapper.h)
    target_include_directories(wrappertests PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/tests" ${WRAPPER_DIRS})
    target_compile_options(wrappertests PRIVATE -Wall -Wextra)
    target_link_libraries(wrappertests PRIVATE Qt6::Qml Qt6::Gui Qt6::Concurrent dawengine)
    add_test(NAME qtpresets COMMAND wrappertests qtpresets)
    add_test(NAME qtshortcuts COMMAND wrappertests qtshortcuts)
    add_test(NAME qtdenoise COMMAND wrappertests qtdenoise)
    add_test(NAME qtpitchmonitor COMMAND wrappertests qtpitchmonitor)
    add_test(NAME qtrecording COMMAND wrappertests qtrecording)
    add_test(NAME qtmultirecording COMMAND wrappertests qtmultirecording)
    add_test(NAME qtclipediting COMMAND wrappertests qtclipediting)
    add_test(NAME qtprojectfiles COMMAND wrappertests qtprojectfiles)
    add_test(NAME qttracknames COMMAND wrappertests qttracknames)
    add_test(NAME qttrackcolours COMMAND wrappertests qttrackcolours)
    add_test(NAME qttrackeffects COMMAND wrappertests qttrackeffects)
    add_test(NAME qtaudioengine COMMAND wrappertests qtaudioengine)
    add_test(NAME qtpianoroll COMMAND wrappertests qtpianoroll)
    add_test(NAME qtharmonium COMMAND wrappertests qtharmonium)
    add_test(NAME qtsynth COMMAND wrappertests qtsynth)
    add_test(NAME qtbass COMMAND wrappertests qtbass)
    add_test(NAME qtdrumanalyzer COMMAND wrappertests qtdrumanalyzer)
    add_test(NAME qtchordanalyzer COMMAND wrappertests qtchordanalyzer)
    add_test(NAME qtdrumkit COMMAND wrappertests qtdrumkit)
    add_test(NAME qtdrumgroove COMMAND wrappertests qtdrumgroove)
    add_test(NAME qteffectchain COMMAND wrappertests qteffectchain)
    add_test(NAME qtchords COMMAND wrappertests qtchords)
    add_test(NAME qtelectrodrums COMMAND wrappertests qtelectrodrums)
    add_test(NAME qtpiano COMMAND wrappertests qtpiano)
    add_test(NAME qtvirtualkeyboard COMMAND wrappertests qtvirtualkeyboard)
    add_test(NAME qtvolumeenvelope COMMAND wrappertests qtvolumeenvelope)
    add_test(NAME qtmixeffect COMMAND wrappertests qtmixeffect)
    add_test(NAME qtimportexport COMMAND wrappertests qtimportexport)
    add_test(NAME qtfiles COMMAND wrappertests qtfiles)
endif()

# Exercise actual mouse/keyboard tempo editing when Qt Test is available.
if(DAW_BUILD_APP)
    find_package(Qt6 QUIET COMPONENTS Test)
    if(TARGET Qt6::Test)
        qt_add_executable(transporttests tests/ui/transport_qtcheck.cpp)
        qt_add_qml_module(transporttests URI TransportTests
            QML_FILES frontend/theme/Theme.qml frontend/theme/PianoRollColors.qml frontend/theme/TrackListColors.qml frontend/theme/TimelineColors.qml frontend/theme/MixerColors.qml frontend/theme/TransportColors.qml
                      frontend/theme/GuitarSuiteColors.qml frontend/theme/EffectsColors.qml frontend/theme/ChaosEqColors.qml
                      frontend/icons/Icons.qml frontend/window/TopBar.qml frontend/transportbar/TransportBar.qml
                      frontend/window/EditMenu.qml frontend/window/UndoHistory.qml frontend/window/ViewMenu.qml frontend/window/ToolsMenu.qml frontend/window/TapTempoDialog.qml frontend/tools/DrumAnalyzerDialog.qml frontend/tools/ChordAnalyzerDialog.qml frontend/window/PanelTabs.qml
                      frontend/window/StatusStrip.qml
                      frontend/common/ThemedMenuItem.qml frontend/common/ThemedMenu.qml frontend/common/ThemedMenuSeparator.qml frontend/common/ToggleChip.qml frontend/common/PanelButton.qml frontend/common/ThemedComboBox.qml frontend/common/CloseButton.qml frontend/common/ThemedSlider.qml frontend/common/HardwareSlider.qml frontend/common/ThemedDialog.qml frontend/common/ThemedMenuButton.qml frontend/common/ToolWindow.qml
                      frontend/engine/PlaybackControls.qml
                      frontend/session/ProjectActions.qml frontend/session/ExportDialog.qml frontend/dialogs/FilePicker.qml frontend/audiodevice/DeviceSettings.qml
                      frontend/settings/AppSettings.qml frontend/settings/QuickSettings.qml frontend/settings/ShortcutEditor.qml
                      frontend/tempo/TempoEditor.qml frontend/tempo/MeterEditor.qml
                      frontend/icons/MetronomeIcon.qml
                      frontend/instruments/guitar/GuitarSuitePanel.qml
                      frontend/instruments/guitar/GuitarSuiteEditor.qml
                      frontend/instruments/harmonium/HarmoniumEditor.qml
                      frontend/instruments/synth/SynthEditor.qml
                      frontend/instruments/bass/BassEditor.qml
                      frontend/instruments/drums/DrumKitEditor.qml
                      frontend/instruments/drums/DrumKitPicture.qml frontend/instruments/drums/GroovePanel.qml
                      frontend/instruments/electrodrums/ElectroDrumsEditor.qml
                      frontend/instruments/electrodrums/ElectroDrumsPicture.qml
                      frontend/instruments/piano/PianoEditor.qml
                      frontend/instruments/piano/PianoPicture.qml
                      frontend/transportbar/VirtualKeyboard.qml
                      frontend/plugins/TrackEffectsPanel.qml frontend/plugins/denoise/NoiseReductionEditor.qml frontend/plugins/pitchmonitor/PitchMonitorEditor.qml frontend/plugins/eq/Tenbandeq.qml frontend/plugins/chaoseq/ChaosEqEditor.qml
                      frontend/plugins/chordplayer/ChordsEditor.qml frontend/plugins/chordplayer/ChordStrips.qml
                      frontend/session/TrackList.qml frontend/timeline/Timeline.qml frontend/timeline/VolumeEnvelopeLane.qml frontend/plugins/mixfx/MixEffectEditor.qml
                      frontend/mixer/MixerPanel.qml
                      frontend/dsp/StereoLevelMeter.qml
                      frontend/dsp/LevelMeter.qml
                      frontend/pianoroll/PianoRollView.qml frontend/pianoroll/PianoKeys.qml
                      frontend/pianoroll/PianoRollComboBox.qml frontend/pianoroll/VelocityLane.qml
                      tests/ui/TransportHarness.qml Main.qml
            SOURCES ${WRAPPER_SOURCES})
        target_include_directories(transporttests PRIVATE "${CMAKE_CURRENT_LIST_DIR}" ${WRAPPER_DIRS})
        # Source-tree checks (e.g. icons live only in frontend/icons/Icons.qml).
        target_compile_definitions(transporttests PRIVATE CHAOSMAKER_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}")
        target_link_libraries(transporttests PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent dawengine)
        add_test(NAME transport_ui COMMAND transporttests)
        set_tests_properties(transport_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PIPEWIRE_REMOTE=codex-test-no-server;PULSE_SERVER=unix:/tmp/chaosmaker-test-no-server")
    endif()
endif()

# The two factory chains are embedded in every executable that uses GuitarSuiteModel.
if(DAW_BUILD_APP)
    foreach(preset_target appChaosMaker wrappertests transporttests)
        if(TARGET ${preset_target})
            qt_add_resources(${preset_target} guitarsuite_factory_presets
                PREFIX "/GuitarSuitePresets"
                BASE "${CMAKE_CURRENT_LIST_DIR}/resources/presets/guitarsuite"
                FILES resources/presets/guitarsuite/Djent.gspreset
                      resources/presets/guitarsuite/MetalSolo.gspreset
                      resources/presets/guitarsuite/RockChords.gspreset
                      resources/presets/guitarsuite/RockSolo.gspreset
                      resources/presets/guitarsuite/Jazz.gspreset)
        endif()
    endforeach()
endif()
