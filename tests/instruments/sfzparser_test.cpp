#include "testkit.h"
#include "instruments/sfz/sfzparser.h"

#include <map>
#include <string>

using sfz::parseText;

namespace {

// Files by path, standing in for the disk.
sfz::FileReader files(std::map<std::string, std::string> contents)
{
    return [contents = std::move(contents)](const std::string& path) -> std::optional<std::string> {
        const auto it = contents.find(path);
        if (it == contents.end()) return std::nullopt;
        return it->second;
    };
}

std::string value(const sfz::Region& r, const char* name)
{
    const auto* op = r.find(name);
    return op ? op->value : std::string("<none>");
}

bool reported(const sfz::Document& doc, const std::string& part, int line = -1)
{
    for (const auto& d : doc.diagnostics)
        if (d.message.find(part) != std::string::npos && (line < 0 || d.line == line)) return true;
    return false;
}

} // namespace

TEST(sfzparser, regions_inherit_global_master_and_group_and_their_own_wins)
{
    const auto doc = parseText(R"(
<global> loop_mode=one_shot volume=-3
<master> pan=50
<group> lokey=36 hikey=36 volume=-6
<region> sample=a.wav
<region> sample=b.wav volume=0
<group> lokey=38
<region> sample=c.wav
<master>
<region> sample=d.wav
<global>
<region> sample=e.wav
)", "/kit/main.sfz", files({}));
    CHECK(doc.diagnostics.empty());
    CHECK(doc.regions.size() == 5);
    if (doc.regions.size() != 5) return;
    const auto& a = doc.regions[0];
    CHECK(value(a, "loop_mode") == "one_shot" && value(a, "pan") == "50" && value(a, "volume") == "-6");
    CHECK(value(doc.regions[1], "volume") == "0");                    // its own wins
    CHECK(value(doc.regions[2], "lokey") == "38" && value(doc.regions[2], "hikey") == "<none>");   // new group
    CHECK(value(doc.regions[3], "pan") == "<none>" && value(doc.regions[3], "lokey") == "<none>");
    CHECK(value(doc.regions[3], "loop_mode") == "one_shot");
    CHECK(value(doc.regions[4], "loop_mode") == "<none>");            // a new global clears all
    CHECK(a.file == "/kit/main.sfz" && a.line == 5);
    CHECK(doc.folder == "/kit");
}

TEST(sfzparser, values_keep_spaces_and_end_at_the_next_opcode)
{
    const auto doc = parseText(
        "<region> region_label=01 lokey=021 sample=FF B-1.flac   pitch_keycenter=023\n"
        "<region>sample=My Piano/C 4.wav<region> sample=x.wav\n",
        "/p/piano.sfz", files({}));
    CHECK(doc.regions.size() == 3);
    if (doc.regions.size() != 3) return;
    CHECK(value(doc.regions[0], "sample") == "FF B-1.flac");
    CHECK(value(doc.regions[0], "pitch_keycenter") == "023");
    CHECK(value(doc.regions[1], "sample") == "My Piano/C 4.wav");
    CHECK(value(doc.regions[2], "sample") == "x.wav");
}

TEST(sfzparser, comments_are_skipped_and_lines_still_count)
{
    const auto doc = parseText(
        "// a line comment\n"
        "<group> lokey=40 // after an opcode\n"
        "/* a block\n"
        "   comment <region> sample=no.wav */ <region> sample=yes.wav\n"
        "<region> sample=z.wav /* inline */ volume=-2\n",
        "/k/c.sfz", files({}));
    CHECK(doc.diagnostics.empty());
    CHECK(doc.regions.size() == 2);
    if (doc.regions.size() != 2) return;
    CHECK(value(doc.regions[0], "sample") == "yes.wav" && doc.regions[0].line == 4);
    CHECK(value(doc.regions[0], "lokey") == "40");
    CHECK(value(doc.regions[1], "sample") == "z.wav" && value(doc.regions[1], "volume") == "-2");
}

TEST(sfzparser, defines_apply_in_order_and_includes_come_from_the_main_folder)
{
    const auto reader = files({
        {"/lib/Data/PP.txt", "<region> sample=PP $NOTE.$EXT\n"},
        {"/lib/Data/FF.txt", "<region> sample=FF $NOTE.$EXT lokey=$KEY\n"},
    });
    const auto doc = parseText(R"(
#define $EXT flac
#define $DIR Data
#define $NOTE C4
#define $KEY 60
<group> lovel=1
#define $DYN PP
#include "$DIR/$DYN.txt"
<group> lovel=100
#define $DYN FF
#include "$DIR/$DYN.txt"
)", "/lib/Piano.sfz", reader);
    CHECK(doc.diagnostics.empty());
    CHECK(doc.regions.size() == 2);
    if (doc.regions.size() != 2) return;
    CHECK(value(doc.regions[0], "sample") == "PP C4.flac" && value(doc.regions[0], "lovel") == "1");
    CHECK(value(doc.regions[1], "sample") == "FF C4.flac" && value(doc.regions[1], "lokey") == "60");
    CHECK(doc.regions[1].file == "/lib/Data/FF.txt" && doc.regions[1].line == 1);
}

TEST(sfzparser, nested_includes_stay_relative_to_the_main_file)
{
    // As in the drum kit: Programs/Kit.sfz includes mappings/x.sfz, which names ../Samples.
    const auto reader = files({
        {"/kit/Programs/mappings/kick.sfz", "<region> sample=../Samples/kick.wav\n#include \"mappings/more.sfz\"\n"},
        {"/kit/Programs/mappings/more.sfz", "<region> sample=../Samples/kick2.wav\n"},
    });
    const auto doc = parseText("<master> group=1\n#include \"mappings/kick.sfz\"\n", "/kit/Programs/Kit.sfz", reader);
    CHECK(doc.diagnostics.empty());
    CHECK(doc.regions.size() == 2);
    if (doc.regions.size() == 2) CHECK(value(doc.regions[1], "group") == "1");
}

TEST(sfzparser, control_and_curves_are_kept_and_default_path_follows_the_regions)
{
    const auto doc = parseText(R"(
<control> default_path=Samples/ set_hdcc10=0.5 label_cc7=Vol
<region> sample=a.wav
<curve> curve_index=11 v000=0 v127=1
<control> default_path=Other/
<region> sample=b.wav
)", "/x/i.sfz", files({}));
    CHECK(doc.diagnostics.empty());
    CHECK(doc.control.size() == 4);
    CHECK(doc.curves.size() == 1 && doc.curves[0].opcodes.size() == 3);
    CHECK(doc.regions.size() == 2);
    if (doc.regions.size() != 2) return;
    CHECK(doc.regions[0].defaultPath == "Samples/" && doc.regions[1].defaultPath == "Other/");
    CHECK(doc.regions[0].find("curve_index") == nullptr);             // curves are not opcodes of regions
}

TEST(sfzparser, problems_are_reported_with_file_and_line_and_parsing_goes_on)
{
    const auto doc = parseText(
        "lokey=1\n"                                                   // 1: before any header
        "<effect> type=reverb\n"                                      // 2: unsupported, skipped
        "<region> sample=a.wav junk lokey=$NOPE\n"                    // 3: stray word, undefined name
        "#include \"missing.sfz\"\n"                                  // 4
        "#define broken\n"                                            // 5
        "#pragma x\n"                                                 // 6
        "<region\n"                                                   // 7
        "<region> sample=b.wav /* never closed\n",                    // 8
        "/d/bad.sfz", files({}));
    CHECK(reported(doc, "before any header", 1));
    CHECK(reported(doc, "<effect>", 2));
    CHECK(reported(doc, "undefined variable $NOPE", 3));
    CHECK(reported(doc, "cannot read #include \"missing.sfz\"", 4));
    CHECK(reported(doc, "bad #define", 5));
    CHECK(reported(doc, "unknown directive", 6));
    CHECK(reported(doc, "unclosed header", 7));
    CHECK(reported(doc, "unclosed /*", 8));
    CHECK(doc.regions.size() == 2);
    for (const auto& d : doc.diagnostics) CHECK(d.file == "/d/bad.sfz");
    if (!doc.regions.empty()) CHECK(value(doc.regions[0], "sample") == "a.wav junk");   // a value may hold words

    const auto loop = parseText("#include \"self.sfz\"\n", "/l/self.sfz", files({{"/l/self.sfz", "#include \"self.sfz\"\n"}}));
    CHECK(reported(loop, "nested too deep"));

    const auto none = sfz::parseFile("/nonexistent/chaosmaker/missing.sfz");
    CHECK(none.regions.empty() && reported(none, "cannot read the SFZ file"));
}
