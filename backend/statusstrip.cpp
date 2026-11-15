#include "statusstrip.h"

#include "common/core/valueformat.h"
#include "common/engine/enginestatus.h"

#include <cctype>

namespace {
const std::string dash = "\xe2\x80\x94"; // "—"
}

std::string statusstrip::buffer(int frames, double hz)
{
    if (frames <= 0 || !enginestatus::validRate(hz)) return dash;
    return std::to_string(frames) + " smp \xc2\xb7 " + valueformat::withUnit(1000.0 * frames / hz, 1, "ms");
}

std::string statusstrip::bufferTooltip(int frames, int requestedFrames, double hz)
{
    if (frames <= 0 || !enginestatus::validRate(hz)) return "Audio device not running";
    std::string text = "Buffer " + std::to_string(frames) + " frames = "
        + valueformat::withUnit(1000.0 * frames / hz, 2, "ms") + " at " + enginestatus::sampleRate(hz);
    if (requestedFrames > 0 && requestedFrames != frames)
        text += " (" + std::to_string(requestedFrames) + " requested)";
    return text;
}

std::string statusstrip::xruns(std::uint64_t count)
{
    return std::to_string(count) + (count == 1 ? " xrun" : " xruns");
}

std::string statusstrip::xrunTooltip(std::uint64_t count)
{
    if (count == 0) return "Audio dropouts (xruns): none";
    return "Audio dropouts (xruns) since start or the last reset \xc2\xb7 click to reset";
}

std::string statusstrip::message(std::string_view transportMessage, std::string_view version, std::string_view status)
{
    const std::string text = !transportMessage.empty()
        ? std::string(transportMessage)
        : "Engine " + std::string(version) + "  \xc2\xb7  " + std::string(status);
    std::string line;
    for (std::size_t i = 0; i < text.size();) {
        if (!std::isspace(static_cast<unsigned char>(text[i]))) { line += text[i++]; continue; }
        std::size_t end = i;
        bool newline = false;
        while (end < text.size() && std::isspace(static_cast<unsigned char>(text[end]))) newline |= text[end++] == '\n';
        line += newline ? std::string(" \xc2\xb7 ") : text.substr(i, end - i);
        i = end;
    }
    return line;
}
