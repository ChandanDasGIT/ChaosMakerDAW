#include "namedialog.h"

#include <cctype>

std::string namedialog::trimmed(std::string_view name)
{
    std::size_t begin = 0, end = name.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(name[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(name[end - 1]))) --end;
    return std::string(name.substr(begin, end - begin));
}

namedialog::Step namedialog::nextStep(bool renaming, std::string_view name, bool taken, bool confirmedReplace)
{
    if (trimmed(name).empty()) return Step::Nothing;
    if (renaming) return Step::Rename;
    if (taken && !confirmedReplace) return Step::AskReplace;
    return Step::Save;
}
