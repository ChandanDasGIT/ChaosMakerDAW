#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Reads SFZ text into regions. Text only: no samples are opened and nothing is checked
// against what the sampler can play (the loader does that).
//
//   comments    // to the end of the line, and /* ... */
//   #define     $NAME value; later lines use the definition in force when they are read, so
//               a file may redefine a name between #includes
//   #include    "path", relative to the folder of the main .sfz file (as in SFZ players)
//   headers     <control> <global> <master> <group> <region> <curve>; a new <global> clears
//               master and group, a new <master> clears group. A region holds the opcodes of
//               its global, master and group, then its own; a later one of the same name wins.
//   values      run to the next "name=" or the end of the line, so sample=FF B-1.wav keeps its
//               space
//
// Problems never stop the parse; each one is reported with its file and line.
namespace sfz {

struct Opcode {
    std::string name, value;
    std::string file;
    int line = 0;
};

struct Region {
    std::vector<Opcode> opcodes;          // inherited first, its own last
    std::string defaultPath;              // <control> default_path in force for this region
    std::string file;
    int line = 0;

    // The value in force for `name` (the last one given), or nothing.
    const Opcode* find(std::string_view name) const noexcept;
};

struct Curve {
    std::vector<Opcode> opcodes;          // curve_index, v000..v127
    std::string file;
    int line = 0;
};

struct Diagnostic {
    std::string file;
    int line = 0;
    std::string message;
};

struct Document {
    std::string path;                     // the main file
    std::string folder;                   // its folder: includes and samples are found from here
    std::vector<Opcode> control;          // every <control> opcode, in order
    std::vector<Region> regions;
    std::vector<Curve> curves;
    std::vector<Diagnostic> diagnostics;
};

// Gives the text of a file, or nothing when it cannot be read.
using FileReader = std::function<std::optional<std::string>(const std::string& path)>;

constexpr int MaxIncludeDepth = 16;

Document parseFile(const std::string& path);
// `path` names the main file (its folder resolves includes); `text` is its content.
Document parseText(std::string_view text, const std::string& path, const FileReader& reader);

} // namespace sfz
