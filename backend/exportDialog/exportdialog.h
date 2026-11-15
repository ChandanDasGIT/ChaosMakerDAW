#pragma once

#include "exportDialog/songrender.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

// What the export dialog decides: file names, which formats exist, the message when an export
// cannot start, and the job that renders the song and writes the file.
namespace exportdialog {

// "wav16", "wav24", "wav32f" or "mp3".
bool isFormat(std::string_view format);

// The project file's name without its extension; "Untitled" when there is no project file.
std::string baseName(std::string_view projectPath);
// The project's folder, or `fallbackFolder` when there is no project file.
std::string folder(std::string_view projectPath, std::string_view fallbackFolder);
// `path` with its extension replaced (or added): "song.wav" + "mp3" -> "song.mp3".
std::string withExtension(std::string_view path, std::string_view extension);

// Why Export did not start: an empty song, or a recording / export already running.
std::string cannotStartMessage(bool nothingToExport);

struct FileOptions {
    std::string path;
    std::string format;          // see isFormat
    int mp3Kbps = 320;
    bool normalise = false;
    float normalisePeakDb = 0.0f;
};

struct Result {
    bool ok = false;
    std::string message;         // "Exported out.wav.", "Export cancelled.", "Export failed: ..."
};

// Renders the song and writes the file. `progress` gets 0..1 of the render now and then;
// `cancelled` is asked during and after the render. Runs on a worker thread; the snapshot's
// processors must not be used meanwhile.
Result exportSong(std::unique_ptr<EngineSnapshot> snapshot, const songrender::Options& render,
                  const FileOptions& file, const std::function<void(double)>& progress,
                  const std::function<bool()>& cancelled);

} // namespace exportdialog
