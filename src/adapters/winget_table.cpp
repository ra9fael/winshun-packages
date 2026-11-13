// winget_table.cpp — see winget_table.h.
//
// How the real output looks (captured from WinGet v1.29.380, zh-CN):
//
//   Name                                    Id                                          Version        Available
//   -----------------------------------------------------------------------------------------------------------
//   7-Zip 26.05 (x64 edition)               7zip.7zip                                   26.05.00.0     26.06
//   AMD Ryzen Master                        ARP\Machine\X64\AMD Ryzen Master            2.14.1.3286
//
// Two shapes break naive column parsing and both appear in that capture:
//   1. a name wider than its column spills across the gap, so splitting on gaps
//      yields more fields than columns;
//   2. ARP registry ids legitimately contain spaces, so an id cannot be assumed
//      to be a single token.
//
// The parser therefore tries every reading of a row (merge the first k fields,
// pad the tail) and accepts the first whose values have the shape each column
// really has. When no reading fits, the row is counted as unreadable and skipped
// — reported, never guessed.
#include "winget_table.h"

#include <algorithm>
#include <cctype>

namespace pkg::winget {
namespace {

std::string trim(const std::string& s)
{
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

bool isAllDashes(const std::string& s)
{
    bool any = false;
    for (const char c : s) {
        if (c == '-') {
            any = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)))
            continue;
        return false;
    }
    return any;
}

bool blank(const std::string& s) { return trim(s).empty(); }

// Finds the start of `needle` as a whole column title (surrounded by spaces or
// the string edges), at or after `from`.
std::size_t findTitle(const std::string& header, const std::string& needle, std::size_t from)
{
    if (needle.empty())
        return std::string::npos;
    std::size_t at = from;
    while ((at = header.find(needle, at)) != std::string::npos) {
        const bool leftOk = at == 0 || header[at - 1] == ' ';
        const std::size_t end = at + needle.size();
        const bool rightOk = end >= header.size() || header[end] == ' ';
        if (leftOk && rightOk)
            return at;
        at = end;
    }
    return std::string::npos;
}

std::size_t gapAt(const std::string& line, std::size_t from)
{
    for (std::size_t i = from; i + 1 < line.size(); ++i)
        if (line[i] == ' ' && line[i + 1] == ' ')
            return i;
    return std::string::npos;
}

std::vector<std::string> gapSplit(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ')
            ++i;
        if (i >= line.size())
            break;
        const std::size_t start = i;
        std::size_t end = gapAt(line, i);
        if (end == std::string::npos)
            end = line.size();
        fields.push_back(trim(line.substr(start, end - start)));
        i = end;
    }
    return fields;
}

// The first non-blank line whose successor is a rule of dashes. WinGet prints a
// banner before the table, and the banner has no rule line under it.
//
// A capture may hold a SECOND table after a blank line (for example "packages
// that require explicit targeting") whose columns are separated by single
// spaces. That one cannot be parsed reliably, so only the first table is read and
// the rest is left alone rather than guessed at.
std::size_t findFirstHeader(const std::vector<std::string>& lines)
{
    for (std::size_t i = 0; i + 1 < lines.size(); ++i)
        if (!blank(lines[i]) && isAllDashes(lines[i + 1]))
            return i;
    return std::string::npos;
}

std::vector<std::size_t> titleStarts(const std::string& headerLine, const std::vector<std::string>& titles)
{
    std::vector<std::size_t> starts;
    std::size_t cursor = 0;
    for (const auto& title : titles) {
        if (title.empty()) {
            starts.push_back(std::string::npos);
            continue;
        }
        const auto at = findTitle(headerLine, title, cursor);
        starts.push_back(at);
        if (at != std::string::npos)
            cursor = at + title.size();
    }
    return starts;
}

// A plain token: no whitespace.
bool plainToken(const std::string& s)
{
    if (s.empty())
        return false;
    for (const char c : s)
        if (std::isspace(static_cast<unsigned char>(c)))
            return false;
    return true;
}

// A version column value: starts with a digit (optionally after a leading 'v'),
// contains no space. "< nightly" style ranges are handled by the caller.
bool versionShaped(const std::string& s)
{
    if (s.empty() || s.find(' ') != std::string::npos)
        return false;
    std::size_t i = 0;
    if ((s[0] == 'v' || s[0] == 'V') && s.size() > 1)
        i = 1;
    return std::isdigit(static_cast<unsigned char>(s[i])) != 0;
}

// Whether `value` has the shape the named column has in real output.
bool columnPlausible(const std::string& title, const std::string& value)
{
    if (title == "Name")
        return !value.empty();
    if (title == "Id") {
        if (value.empty() || value.front() == '(')   // '(' is a misread version
            return false;
        // A plain package id has no spaces; from disk, some unusual installs use
        // an id containing a brace; anything that is not ARP must be one token.
        return isArpId(value) ? value.size() > 4 : plainToken(value);
    }
    if (title == "Version") {
        // Real output contains "< 2026.3.1" and "< nightly" here: WinGet knows a
        // version exists but cannot name an orderable one.
        if (!value.empty() && value.rfind("<", 0) == 0)
            return true;
        return versionShaped(value) || value == "Unknown" || value == "unknown";
    }
    if (title == "Available")
        return value.empty() || versionShaped(value) || value.rfind("<", 0) == 0;
    return true;   // Match / Source / Pinned Version / Blocking: free text
}

// Reads `fields` as `count` columns by merging the first `k` into one and padding
// the tail, which covers both "last column empty" and "name spilled over".
bool tryReading(const std::vector<std::string>& fields, std::size_t k, std::size_t count,
                std::vector<std::string>& out)
{
    if (k == 0 || k > fields.size())
        return false;
    std::vector<std::string> merged;
    std::string joined = fields[0];
    for (std::size_t i = 1; i < k; ++i)
        joined += " " + fields[i];
    merged.push_back(std::move(joined));
    for (std::size_t i = k; i < fields.size(); ++i)
        merged.push_back(fields[i]);
    if (merged.size() > count)
        return false;
    while (merged.size() < count)
        merged.emplace_back();
    out = std::move(merged);
    return true;
}

Table parseWith(const std::vector<std::string>& titles, const std::vector<std::size_t>& starts,
                const std::string& text)
{
    Table table;
    const auto lines = splitLines(text);
    const std::size_t headerIndex = findFirstHeader(lines);
    if (headerIndex == std::string::npos) {
        table.failure.reason = "no table header followed by a rule line";
        return table;
    }
    if (starts.size() != titles.size()) {
        table.failure.reason = "internal column mismatch";
        return table;
    }

    std::vector<std::string> keptTitles;
    for (std::size_t i = 0; i < titles.size(); ++i)
        if (starts[i] != std::string::npos)
            keptTitles.push_back(titles[i]);
    if (keptTitles.size() < 2) {
        table.failure.reason = "fewer than two columns could be located in the header";
        return table;
    }
    table.headers = keptTitles;

    for (std::size_t i = headerIndex + 2; i < lines.size(); ++i) {
        if (blank(lines[i]))
            break;   // end of this table; anything after it is a separate section
        const auto fields = gapSplit(lines[i]);
        if (fields.empty())
            continue;

        std::vector<std::string> chosen;
        bool decided = false;
        for (std::size_t k = 1; k <= fields.size() && !decided; ++k) {
            std::vector<std::string> candidate;
            if (!tryReading(fields, k, keptTitles.size(), candidate))
                continue;
            bool ok = true;
            for (std::size_t c = 0; c < keptTitles.size() && ok; ++c)
                ok = columnPlausible(keptTitles[c], candidate[c]);
            if (ok) {
                chosen = std::move(candidate);
                decided = true;
            }
        }
        if (!decided) {
            // No reading fits: count it and move on, so the caller can report an
            // incomplete read instead of a guess.
            ++table.unreadableRows;
            continue;
        }
        table.rows.push_back({std::move(chosen)});
    }

    table.failure.ok = true;
    return table;
}

} // namespace

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    if (!lines.empty() && lines.back().empty())
        lines.pop_back();
    return lines;
}

const std::vector<ColumnSignature>& knownSignatures()
{
    // Only locales with a captured fixture appear here. Anything else is
    // Unsupported until someone adds a fixture and a signature.
    static const std::vector<ColumnSignature> table = {
        {"en", "Name", "Id", "Version", "Available", "Match", "Source"},
        {"zh-CN", "Name", "Id", "Version", "Available", "Match", "Source"},
    };
    return table;
}

std::vector<ColumnPosition> locateColumns(const std::string& headerLine, const std::vector<std::string>& titles)
{
    std::vector<ColumnPosition> out;
    const auto starts = titleStarts(headerLine, titles);
    for (std::size_t i = 0; i < titles.size(); ++i)
        if (starts[i] != std::string::npos)
            out.push_back({titles[i], starts[i]});
    return out;
}

Table parseTableWithColumns(const std::string& text, const std::vector<std::string>& titles)
{
    const auto lines = splitLines(text);
    const std::size_t headerIndex = findFirstHeader(lines);
    if (headerIndex == std::string::npos) {
        Table table;
        table.failure.reason = "no table header followed by a rule line";
        return table;
    }
    return parseWith(titles, titleStarts(lines[headerIndex], titles), text);
}

Table parseTable(const std::string& text)
{
    const auto lines = splitLines(text);
    const std::size_t headerIndex = findFirstHeader(lines);
    if (headerIndex == std::string::npos) {
        Table table;
        table.failure.reason = "no table header followed by a rule line";
        return table;
    }

    const ColumnSignature* signature = nullptr;
    for (const auto& candidate : knownSignatures()) {
        // Locate the three mandatory titles as whole words in the header. A split
        // on runs of spaces cannot be used: WinGet separates "Version" and "Match"
        // with a single space in real output.
        const auto located = locateColumns(lines[headerIndex], {candidate.name, candidate.id, candidate.version});
        if (located.size() == 3) {
            signature = &candidate;
            break;
        }
    }
    if (!signature) {
        Table table;
        table.failure.reason = "header columns do not match a known locale signature";
        return table;
    }

    const std::vector<std::string> ordered = {signature->name, signature->id, signature->version,
                                             signature->available, signature->match, signature->source};
    return parseWith(ordered, titleStarts(lines[headerIndex], ordered), text);
}

std::string Table::value(std::size_t row, const std::string& header) const
{
    if (row >= rows.size())
        return {};
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == header) {
            const auto& cells = rows[row].cells;
            return i < cells.size() ? cells[i] : std::string();
        }
    }
    return {};
}

bool Table::hasColumn(const std::string& header) const
{
    return std::find(headers.begin(), headers.end(), header) != headers.end();
}

bool isArpId(const std::string& id)
{
    // WinGet enumerates uninstall-registry entries as "ARP\<hive>\<arch>\<name>".
    return id.rfind("ARP\\", 0) == 0 || id.rfind("ARP/", 0) == 0;
}

bool isWingetPackageId(const std::string& id)
{
    if (id.empty() || isArpId(id))
        return false;
    return plainToken(id);
}

std::map<std::string, std::string> parseShowOutput(const std::string& text)
{
    // "Publisher Url" → "publisherurl": the key the details pane and
    // homepageFromShow() look up. Lowercase, no spaces, no trailing colon.
    auto normalizedKey = [](const std::string& raw) {
        std::string key;
        for (const char ch : raw) {
            const auto c = static_cast<unsigned char>(ch);
            if (c == ' ' || c == '\t' || c == ':' || c == '\r')
                continue;
            key.push_back(static_cast<char>(std::tolower(c)));
        }
        return key;
    };
    auto trimBoth = [](const std::string& raw) {
        std::size_t from = 0;
        while (from < raw.size() && (raw[from] == ' ' || raw[from] == '\t' || raw[from] == '\r'))
            ++from;
        std::size_t to = raw.size();
        while (to > from && (raw[to - 1] == ' ' || raw[to - 1] == '\t' || raw[to - 1] == '\r'))
            --to;
        return raw.substr(from, to - from);
    };

    std::map<std::string, std::string> fields;
    std::string lastKey;
    for (const auto& raw : splitLines(text)) {
        // Trim only trailing whitespace, so an indented grouped value keeps its
        // meaning (the indent is what distinguishes it from a top-level field).
        std::string line = raw;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (line.empty()) {
            lastKey.clear();
            continue;
        }
        const bool indented = line.front() == ' ' || line.front() == '\t';
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            // A continuation of the previous value: a description wraps at the
            // console width and the second line has no colon.
            if (!lastKey.empty()) {
                std::string& previous = fields[lastKey];
                if (!previous.empty())
                    previous += ' ';
                previous += trim(line);
            }
            continue;
        }
        if (indented && !lastKey.empty()) {
            // A grouped field ("Installer Type" under "Installer:"). Kept under
            // its own key so "homepage" can never be shadowed by a nested one.
            std::string key = normalizedKey(line.substr(0, colon));
            if (!key.empty())
                fields[key] = trim(line.substr(colon + 1));
            continue;
        }
        // A top-level field. "Found Docker Desktop [Docker.DockerDesktop]" is the
        // header line and has no colon before the bracket, so it is skipped by the
        // caller when it looks for "found".
        std::string key = normalizedKey(line.substr(0, colon));
        if (key.empty())
            continue;
        const std::string value = trim(line.substr(colon + 1));
        if (value.empty()) {
            // A group label such as "Tags:" or "Installer:". Later indented lines
            // belong to it, so remember it as the continuation target.
            lastKey = key;
            fields[key] = std::string();
            continue;
        }
        lastKey = key;
        fields[key] = value;
    }
    return fields;
}

std::string homepageFromShow(const std::map<std::string, std::string>& fields)
{
    const auto it = fields.find("homepage");
    if (it == fields.end())
        return {};
    const std::string& url = it->second;
    // Only an absolute http(s) URL is opened: the field comes from a package
    // manifest, and a manifest is third-party data.
    if (url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0)
        return url;
    return {};
}

} // namespace pkg::winget
