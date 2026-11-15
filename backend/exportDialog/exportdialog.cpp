#include "exportdialog.h"

#include "common/audiofile/mp3writer.h"
#include "common/audiofile/wavwriter.h"

#include <exception>
#include <filesystem>

bool exportdialog::isFormat(std::string_view format)
{
    return format == "wav16" || format == "wav24" || format == "wav32f" || format == "mp3";
}

std::string exportdialog::baseName(std::string_view projectPath)
{
    const auto slash = projectPath.find_last_of('/');
    const auto name = slash == std::string_view::npos ? projectPath : projectPath.substr(slash + 1);
    if (name.empty()) return "Untitled";
    const auto dot = name.find_last_of('.');
    return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
}

std::string exportdialog::folder(std::string_view projectPath, std::string_view fallbackFolder)
{
    if (projectPath.empty()) return std::string(fallbackFolder);
    const auto slash = projectPath.find_last_of('/');
    return slash == std::string_view::npos ? std::string() : std::string(projectPath.substr(0, slash));
}

std::string exportdialog::withExtension(std::string_view path, std::string_view extension)
{
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    const bool hasDot = dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash);
    return std::string(hasDot ? path.substr(0, dot) : path) + "." + std::string(extension);
}

std::string exportdialog::cannotStartMessage(bool nothingToExport)
{
    return nothingToExport ? "There is nothing to export yet: the song has no clips."
                           : "Export can't start now (recording, or an export is already running).";
}

exportdialog::Result exportdialog::exportSong(std::unique_ptr<EngineSnapshot> snapshot,
                                              const songrender::Options& render, const FileOptions& file,
                                              const std::function<void(double)>& progress,
                                              const std::function<bool()>& cancelled)
{
    Result result;
    try {
        auto channels = songrender::render(std::move(snapshot), render, [&](double done) {
            progress(done);
            return !cancelled();
        });
        if (cancelled() || channels[0].empty()) {
            result.message = "Export cancelled.";
            return result;
        }
        if (file.normalise) songrender::normalise(channels, file.normalisePeakDb);
        if (file.format == "mp3") {
            audiofile::Mp3Options mp3;
            mp3.bitrateKbps = file.mp3Kbps;
            audiofile::writeMp3(file.path, channels, render.sampleRate, mp3);
        } else {
            audiofile::writeWav(file.path, channels, render.sampleRate,
                                file.format == "wav16"   ? audiofile::WavFormat::Int16
                                : file.format == "wav24" ? audiofile::WavFormat::Int24
                                                         : audiofile::WavFormat::Float32);
        }
        result.ok = true;
        result.message = "Exported " + std::filesystem::path(file.path).filename().string() + ".";
    } catch (const std::exception& e) {
        result.message = std::string("Export failed: ") + e.what();
    }
    return result;
}
