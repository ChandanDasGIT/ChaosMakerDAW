#include "instruments/sfz/sfzparser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace sfz {

const Opcode* Region::find(std::string_view name) const noexcept
{
    for (auto it = opcodes.rbegin(); it != opcodes.rend(); ++it)
        if (it->name == name) return &*it;
    return nullptr;
}

namespace {

bool nameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; }
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }

std::string trim(std::string_view s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && space(s[a])) ++a;
    while (b > a && space(s[b - 1])) --b;
    return std::string(s.substr(a, b - a));
}

enum class Level { None, Control, Global, Master, Group, Region, Curve, Ignored };

class Parser {
public:
    Parser(Document& doc, const FileReader& reader) : m_doc(doc), m_reader(reader) {}

    void file(std::string_view text, const std::string& name, int depth)
    {
        const std::string clean = stripBlockComments(text, name);
        std::size_t start = 0;
        int line = 0;
        while (start <= clean.size()) {
            const std::size_t end = std::min(clean.find('\n', start), clean.size());
            ++line;
            lineOf(std::string_view(clean).substr(start, end - start), name, line, depth);
            if (end == clean.size()) break;
            start = end + 1;
        }
    }

    void finish()
    {
        closeRegion();
        closeCurve();
    }

private:
    void report(const std::string& file, int line, std::string message)
    {
        m_doc.diagnostics.push_back({file, line, std::move(message)});
    }

    // Block comments become spaces, keeping the line breaks so line numbers stay right.
    std::string stripBlockComments(std::string_view text, const std::string& name)
    {
        std::string out(text);
        std::size_t p = 0;
        while ((p = out.find("/*", p)) != std::string::npos) {
            const std::size_t lineComment = out.rfind("//", p);
            const std::size_t lineStart = out.rfind('\n', p);
            if (lineComment != std::string::npos && (lineStart == std::string::npos || lineComment > lineStart)) {
                p += 2;                                            // inside a // comment
                continue;
            }
            std::size_t e = out.find("*/", p + 2);
            const bool closed = e != std::string::npos;
            if (!closed) {
                report(name, 1 + int(std::count(out.begin(), out.begin() + std::ptrdiff_t(p), '\n')),
                       "unclosed /* comment runs to the end of the file");
                e = out.size();
            } else {
                e += 2;
            }
            for (std::size_t i = p; i < e; ++i) if (out[i] != '\n') out[i] = ' ';
            p = e;
        }
        return out;
    }

    // $NAME -> its definition; the longest defined name wins ($DIR before $D).
    std::string expand(std::string_view text, const std::string& file, int line)
    {
        std::string out;
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] != '$') { out += text[i++]; continue; }
            std::size_t j = i + 1;
            while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) ++j;
            bool done = false;
            for (std::size_t k = j; k > i + 1 && !done; --k) {
                const auto it = m_defines.find(std::string(text.substr(i, k - i)));
                if (it != m_defines.end()) { out += it->second; i = k; done = true; }
            }
            if (!done) {
                if (j > i + 1) report(file, line, "undefined variable " + std::string(text.substr(i, j - i)));
                out += text.substr(i, j - i);
                i = j;
            }
        }
        return out;
    }

    void lineOf(std::string_view raw, const std::string& file, int line, int depth)
    {
        if (const auto c = raw.find("//"); c != std::string_view::npos) raw = raw.substr(0, c);
        const std::string trimmed = trim(raw);
        if (trimmed.empty()) return;

        if (trimmed[0] == '#') { directive(trimmed, file, line, depth); return; }
        const std::string text = expand(trimmed, file, line);

        std::size_t i = 0;
        while (i < text.size()) {
            if (space(text[i])) { ++i; continue; }
            if (text[i] == '<') {
                const std::size_t close = text.find('>', i);
                if (close == std::string::npos) { report(file, line, "unclosed header: " + text.substr(i)); return; }
                header(text.substr(i + 1, close - i - 1), file, line);
                i = close + 1;
                continue;
            }
            std::size_t eq = i;
            while (eq < text.size() && nameChar(text[eq])) ++eq;
            if (eq == i || eq >= text.size() || text[eq] != '=') {
                std::size_t stop = i;
                while (stop < text.size() && !space(text[stop])) ++stop;
                report(file, line, "not an opcode: " + text.substr(i, stop - i));
                i = stop;
                continue;
            }
            // The value runs to the next "name=" after a space, a header, or the line's end.
            std::size_t end = eq + 1, valueEnd = text.size();
            while (end < text.size()) {
                if (text[end] == '<') { valueEnd = end; break; }
                if (space(text[end])) {
                    std::size_t n = end;
                    while (n < text.size() && space(text[n])) ++n;
                    std::size_t m = n;
                    while (m < text.size() && nameChar(text[m])) ++m;
                    if (m > n && m < text.size() && text[m] == '=') { valueEnd = end; break; }
                    if (n < text.size() && text[n] == '<') { valueEnd = end; break; }
                }
                ++end;
            }
            opcode({text.substr(i, eq - i), trim(std::string_view(text).substr(eq + 1, valueEnd - eq - 1)), file, line});
            i = valueEnd;
        }
    }

    void directive(const std::string& text, const std::string& file, int line, int depth)
    {
        if (text.rfind("#define", 0) == 0) {
            std::istringstream in(text.substr(7));
            std::string name, value;
            in >> name >> value;
            if (name.size() < 2 || name[0] != '$' || value.empty()) { report(file, line, "bad #define: " + text); return; }
            m_defines[name] = expand(value, file, line);
        } else if (text.rfind("#include", 0) == 0) {
            const std::string rest = expand(trim(std::string_view(text).substr(8)), file, line);
            const std::size_t a = rest.find('"'), b = a == std::string::npos ? a : rest.find('"', a + 1);
            if (b == std::string::npos) { report(file, line, "bad #include: " + text); return; }
            const std::string relative = rest.substr(a + 1, b - a - 1);
            if (depth + 1 > MaxIncludeDepth) { report(file, line, "#include nested too deep: " + relative); return; }
            const std::string path = (std::filesystem::path(m_doc.folder) / relative).lexically_normal().string();
            const auto content = m_reader ? m_reader(path) : std::nullopt;
            if (!content) { report(file, line, "cannot read #include \"" + relative + "\""); return; }
            this->file(*content, path, depth + 1);
        } else {
            report(file, line, "unknown directive: " + text);
        }
    }

    void header(const std::string& name, const std::string& file, int line)
    {
        closeRegion();
        closeCurve();
        if (name == "control") { m_level = Level::Control; return; }
        if (name == "global") { m_global.clear(); m_master.clear(); m_group.clear(); m_level = Level::Global; return; }
        if (name == "master") { m_master.clear(); m_group.clear(); m_level = Level::Master; return; }
        if (name == "group") { m_group.clear(); m_level = Level::Group; return; }
        if (name == "region") {
            m_level = Level::Region;
            m_region = Region{};
            m_region.defaultPath = m_defaultPath;
            m_region.file = file;
            m_region.line = line;
            for (const auto* set : {&m_global, &m_master, &m_group})
                m_region.opcodes.insert(m_region.opcodes.end(), set->begin(), set->end());
            m_inRegion = true;
            return;
        }
        if (name == "curve") { m_level = Level::Curve; m_curve = Curve{{}, file, line}; m_inCurve = true; return; }
        m_level = Level::Ignored;
        report(file, line, "unsupported header <" + name + ">; its opcodes are skipped");
    }

    void opcode(Opcode op)
    {
        switch (m_level) {
        case Level::None: report(op.file, op.line, "opcode before any header: " + op.name); break;
        case Level::Control:
            if (op.name == "default_path") m_defaultPath = op.value;
            m_doc.control.push_back(std::move(op));
            break;
        case Level::Global: m_global.push_back(std::move(op)); break;
        case Level::Master: m_master.push_back(std::move(op)); break;
        case Level::Group: m_group.push_back(std::move(op)); break;
        case Level::Region: m_region.opcodes.push_back(std::move(op)); break;
        case Level::Curve: m_curve.opcodes.push_back(std::move(op)); break;
        case Level::Ignored: break;
        }
    }

    void closeRegion()
    {
        if (m_inRegion) m_doc.regions.push_back(std::move(m_region));
        m_inRegion = false;
    }

    void closeCurve()
    {
        if (m_inCurve) m_doc.curves.push_back(std::move(m_curve));
        m_inCurve = false;
    }

    Document& m_doc;
    const FileReader& m_reader;
    std::map<std::string, std::string> m_defines;
    Level m_level = Level::None;
    std::vector<Opcode> m_global, m_master, m_group;
    std::string m_defaultPath;
    Region m_region;
    Curve m_curve;
    bool m_inRegion = false, m_inCurve = false;
};

std::optional<std::string> readText(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

Document parseText(std::string_view text, const std::string& path, const FileReader& reader)
{
    Document doc;
    doc.path = path;
    doc.folder = std::filesystem::path(path).parent_path().string();
    Parser parser(doc, reader);
    parser.file(text, path, 0);
    parser.finish();
    return doc;
}

Document parseFile(const std::string& path)
{
    const auto text = readText(path);
    if (!text) {
        Document doc;
        doc.path = path;
        doc.folder = std::filesystem::path(path).parent_path().string();
        doc.diagnostics.push_back({path, 0, "cannot read the SFZ file"});
        return doc;
    }
    return parseText(*text, path, readText);
}

} // namespace sfz
