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
# PRIVATE: only backend .cpp files can see it.
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

    # The window icon, from the app's resources (":/icons/icon.svg").
    qt_add_resources(appChaosMaker app_icon PREFIX "/icons" BASE "${CMAKE_CURRENT_LIST_DIR}/resources/icons"
                     FILES resources/icons/icon.svg)

    # Installed layout (the AppImage and the tarball are made from it: packaging/linux).
    include(GNUInstallDirs)
    set(APP_ID io.github.chandandasgit.ChaosMaker)
    install(TARGETS appChaosMaker RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/${APP_ID}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/${APP_ID}.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES resources/icons/icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
            RENAME ${APP_ID}.svg)
endif()

# ---------------------------------------------------------------- tools (no Qt)
# fxrender: runs a dry WAV through guitar suite pedals offline (listening tests, plan 6.3).
add_executable(fxrender tests/tools/fxrender/fxrender.cpp)
target_compile_options(fxrender PRIVATE -Wall -Wextra)
target_link_libraries(fxrender PRIVATE dawengine)
# chordrender: the Chords track's autoplay patterns on the grand piano, offline (restMap G4).
add_executable(chordrender tests/tools/chordrender/chordrender.cpp)
target_compile_options(chordrender PRIVATE -Wall -Wextra)
target_link_libraries(chordrender PRIVATE dawengine)
# latencytest: measures the interface's round trip through JACK with a loopback cable (restMap F3).
add_executable(latencytest tests/tools/latencytest/latencytest.cpp)
target_compile_options(latencytest PRIVATE -Wall -Wextra)
target_link_libraries(latencytest PRIVATE dawengine)
# drumbench: scores the drum pattern analyzer against the MIDI a drummer played (restMap G20).
add_executable(drumbench tests/tools/drumbench/drumbench.cpp)
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
        wrapper/common/filePickerWrap.cpp wrapper/common/filePickerWrap.h
        wrapper/common/trackEffectsPanel/plugins/denoiseWrap.cpp wrapper/common/trackEffectsPanel/plugins/denoiseWrap.h
        wrapper/common/trackEffectsPanel/plugins/pitchMonitorWrap.cpp wrapper/common/trackEffectsPanel/plugins/pitchMonitorWrap.h
        wrapper/common/shortcutsWrap.cpp wrapper/common/shortcutsWrap.h
        wrapper/pianoRoll/pianoRollWrap.cpp wrapper/pianoRoll/pianoRollWrap.h
        wrapper/pianoRoll/pianoRollNoteModel.cpp wrapper/pianoRoll/pianoRollNoteModel.h
        wrapper/common/engine/engineController.cpp wrapper/common/engine/engineController.h
        wrapper/common/trackListModelWrap.cpp wrapper/common/trackListModelWrap.h
        wrapper/common/trackEffectsPanel/instruments/guitar/electric/electricWrap.cpp wrapper/common/trackEffectsPanel/instruments/guitar/electric/electricWrap.h
        wrapper/topBar/projectActions/projectActionsWrap.cpp wrapper/topBar/projectActions/projectActionsWrap.h
        wrapper/transportBar/quickSettingsWrap.cpp wrapper/transportBar/quickSettingsWrap.h
        wrapper/topBar/appSettings/deviceSettingsWrap.cpp wrapper/topBar/appSettings/deviceSettingsWrap.h
        wrapper/topBar/appSettings/appSettingsWrap.cpp wrapper/topBar/appSettings/appSettingsWrap.h
        wrapper/transportBar/playbackControlsWrap.cpp wrapper/transportBar/playbackControlsWrap.h
        wrapper/common/engine/audioEngineWrap.cpp wrapper/common/engine/audioEngineWrap.h
        wrapper/common/trackEffectsPanel/plugins/chaosEqWrap.cpp wrapper/common/trackEffectsPanel/plugins/chaosEqWrap.h
        wrapper/common/mixEffectWrap.cpp wrapper/common/mixEffectWrap.h
        wrapper/topBar/projectActions/importExportWrap.cpp wrapper/topBar/projectActions/importExportWrap.h
        wrapper/exportDialogWrap.cpp wrapper/exportDialogWrap.h
        wrapper/common/trackEffectsPanel/instruments/harmonium/harmoniumWrap.cpp wrapper/common/trackEffectsPanel/instruments/harmonium/harmoniumWrap.h
        wrapper/common/trackEffectsPanel/instruments/synth/synthWrap.cpp wrapper/common/trackEffectsPanel/instruments/synth/synthWrap.h
        wrapper/common/trackEffectsPanel/instruments/guitar/bass/bassWrap.cpp wrapper/common/trackEffectsPanel/instruments/guitar/bass/bassWrap.h
        wrapper/topBar/toolsMenu/drumAnalyzerWrap.cpp wrapper/topBar/toolsMenu/drumAnalyzerWrap.h
        wrapper/topBar/toolsMenu/chordAnalyzerWrap.cpp wrapper/topBar/toolsMenu/chordAnalyzerWrap.h
        wrapper/common/trackEffectsPanel/instruments/drums/drumsWrap.cpp wrapper/common/trackEffectsPanel/instruments/drums/drumsWrap.h
        wrapper/common/groovePanelWrap.cpp wrapper/common/groovePanelWrap.h
        wrapper/common/trackEffectsPanel/plugins/chordPlayer/chordPlayerWrap.cpp wrapper/common/trackEffectsPanel/plugins/chordPlayer/chordPlayerWrap.h
        wrapper/common/trackEffectsPanel/instruments/electrodrums/electrodrumsWrap.cpp wrapper/common/trackEffectsPanel/instruments/electrodrums/electrodrumsWrap.h
        wrapper/common/trackEffectsPanel/instruments/piano/pianoWrap.cpp wrapper/common/trackEffectsPanel/instruments/piano/pianoWrap.h
        wrapper/transportBar/virtualKeyboardWrap.cpp wrapper/transportBar/virtualKeyboardWrap.h)
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
                      frontend/icons/Icons.qml frontend/topBar/TopBar.qml frontend/topBar/DeviceLine.qml frontend/transportBar/TransportBar.qml frontend/transportBar/TypingKey.qml
                      frontend/topBar/editMenu/EditMenu.qml frontend/topBar/editMenu/EditHistoryWindow.qml frontend/topBar/ViewMenu.qml frontend/topBar/toolsMenu/ToolsMenu.qml frontend/topBar/toolsMenu/TapTempoDialog.qml frontend/topBar/toolsMenu/DrumAnalyzerDialog.qml frontend/topBar/toolsMenu/ChordAnalyzerDialog.qml frontend/topBar/PanelTabs.qml
                      frontend/StatusStrip.qml
                      frontend/common/themed/ThemedMenuItem.qml frontend/common/themed/ThemedMenu.qml frontend/common/themed/ThemedMenuSeparator.qml frontend/common/ToggleChip.qml frontend/common/PanelButton.qml frontend/common/themed/ThemedComboBox.qml frontend/common/CloseButton.qml frontend/common/themed/ThemedSlider.qml frontend/common/HardwareSlider.qml frontend/common/themed/ThemedDialog.qml frontend/common/themed/ThemedMenuButton.qml frontend/common/ToolWindow.qml frontend/common/themed/ThemedCheckBox.qml frontend/common/themed/ThemedRadioButton.qml frontend/common/themed/ThemedSwitch.qml
                      frontend/transportBar/PlaybackControls.qml
                      frontend/topBar/projectActions/ProjectActions.qml frontend/topBar/projectActions/ImportExport.qml frontend/topBar/projectActions/RecentProjects.qml frontend/ExportDialog.qml frontend/common/FilePicker.qml frontend/topBar/appSettings/DeviceSettings.qml
                      frontend/topBar/appSettings/AppSettings.qml frontend/transportBar/QuickSettings.qml frontend/topBar/appSettings/ShortcutEditor.qml
                      frontend/transportBar/TempoEditor.qml frontend/transportBar/MeterEditor.qml
                      frontend/icons/MetronomeIcon.qml frontend/icons/MagnetIcon.qml
                      frontend/common/trackEffectsPanel/instruments/guitar/electric/GuitarSuiteEditor.qml frontend/common/trackEffectsPanel/instruments/guitar/electric/Tuner.qml frontend/common/trackEffectsPanel/instruments/guitar/electric/SignalChainBar.qml
                      frontend/common/trackEffectsPanel/instruments/harmonium/HarmoniumEditor.qml
                      frontend/common/trackEffectsPanel/instruments/synth/SynthEditor.qml
                      frontend/common/trackEffectsPanel/instruments/guitar/bass/BassEditor.qml
                      frontend/common/trackEffectsPanel/instruments/drums/DrumKitEditor.qml
                      frontend/common/trackEffectsPanel/instruments/drums/DrumKitPicture.qml frontend/common/GroovePanel.qml frontend/common/NameDialog.qml
                      frontend/common/trackEffectsPanel/instruments/electrodrums/ElectroDrumsEditor.qml
                      frontend/common/trackEffectsPanel/instruments/electrodrums/ElectroDrumsPicture.qml
                      frontend/common/trackEffectsPanel/instruments/piano/PianoEditor.qml
                      frontend/common/trackEffectsPanel/instruments/piano/PianoPicture.qml
                      frontend/transportBar/VirtualKeyboard.qml
                      frontend/common/trackEffectsPanel/TrackEffectsPanel.qml frontend/common/trackEffectsPanel/plugins/NoiseReductionEditor.qml frontend/common/trackEffectsPanel/plugins/PitchMonitorEditor.qml frontend/common/trackEffectsPanel/plugins/Tenbandeq.qml frontend/common/trackEffectsPanel/plugins/ChaosEqEditor.qml
                      frontend/common/trackEffectsPanel/plugins/chordPlayer/ChordsEditor.qml frontend/common/trackEffectsPanel/plugins/chordPlayer/ChordStrips.qml
                      frontend/TrackList.qml frontend/timeline/Timeline.qml frontend/timeline/VolumeEnvelopeLane.qml frontend/common/trackEffectsPanel/plugins/MixEffectEditor.qml
                      frontend/mixerPanel/MixerPanel.qml
                      frontend/mixerPanel/StereoLevelMeter.qml
                      frontend/common/trackEffectsPanel/instruments/guitar/electric/LevelMeter.qml
                      frontend/pianoRoll/PianoRollView.qml frontend/pianoRoll/PianoKeys.qml
                      frontend/pianoRoll/PianoRollComboBox.qml frontend/pianoRoll/VelocityLane.qml
                      tests/ui/TransportHarness.qml Main.qml
            SOURCES ${WRAPPER_SOURCES})
        target_include_directories(transporttests PRIVATE "${CMAKE_CURRENT_LIST_DIR}" ${WRAPPER_DIRS})
        # Source-tree checks (e.g. icons live only in frontend/icons/Icons.qml).
        target_compile_definitions(transporttests PRIVATE CHAOSMAKER_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}")
        target_link_libraries(transporttests PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent dawengine)
        add_test(NAME transport_ui COMMAND transporttests)
        set_tests_properties(transport_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PIPEWIRE_REMOTE=chaosmaker-test-no-server;PULSE_SERVER=unix:/tmp/chaosmaker-test-no-server")

        # uishots: pictures of the app's views, offscreen with throwaway settings (UI work).
        qt_add_executable(uishots tests/tools/uishots/uishots.cpp)
        qt_add_qml_module(uishots URI UiShots
            QML_FILES Main.qml ${FRONTEND_QML}
            SOURCES ${WRAPPER_SOURCES})
        target_include_directories(uishots PRIVATE "${CMAKE_CURRENT_LIST_DIR}" ${WRAPPER_DIRS})
        target_compile_definitions(uishots PRIVATE CHAOSMAKER_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}")
        target_link_libraries(uishots PRIVATE Qt6::Quick Qt6::Test Qt6::Concurrent dawengine)
    endif()
endif()

# The two factory chains are embedded in every executable that uses GuitarSuiteModel.
if(DAW_BUILD_APP)
    foreach(preset_target appChaosMaker wrappertests transporttests uishots)
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
