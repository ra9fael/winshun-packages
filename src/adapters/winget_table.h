// winget_table.h — parse WinGet's fixed-width console tables.
//
// WinGet 1.29.380 does not support `--output` on `list`, `search` or
// `upgrade` (verified against the real tool: "Argument name was not recognized
// for the current command: '--output'"), so the only evidence available is the
// console table. That table is therefore parsed conservatively:
//
//   * the header row defines the column starts; the rule is that the header and
//     a following all-dash line exist, nothing else;
//   * column names are matched against a per-locale signature table — an
//     unknown locale is Unsupported, never "probably the same columns";
//   * a parsed id that contains whitespace proves the geometry did not hold, and
//     the whole output becomes Unsupported rather than being guessed at.
//
// Pure text logic: no OS headers, so fixtures from any version/locale are L1
// test data.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace pkg::winget {

struct TableRow {
    std::vector<std::string> cells; // trimmed; aligned by index with Table::headers
};

struct ParseFailure {
    bool ok = false;
    std::string reason; // "no header", "no separator", "missing column: Id", ...
};

struct Table {
    ParseFailure failure;
    std::vector<std::string> headers;
    std::vector<TableRow> rows;
    // Rows whose fields could not be split predictably, e.g. because a name was
    // wider than its column and swallowed the next one. They are counted and
    // skipped, never guessed at, so the caller can report an incomplete read.
    std::size_t unreadableRows = 0;

    // Value of one column in one row, or an empty string when the column is not
    // present (search omits Available, for example).
    std::string value(std::size_t row, const std::string& header) const;
    bool hasColumn(const std::string& header) const;
};

// Locale-specific column names WinGet is known to print. Kept explicit so a new
// locale is a deliberate addition with a fixture, not an accident.
struct ColumnSignature {
    std::string locale;    // "en", "zh-CN", ...
    std::string name;      // "Name"
    std::string id;        // "Id"
    std::string version;   // "Version"
    std::string available; // "Available" (may be empty when the command omits it)
    std::string match;     // "Match" (search only)
    std::string source;    // "Source" (search only, when shown)
};

const std::vector<ColumnSignature>& knownSignatures();

// Locates the given titles, in that order, in a header line. Titles that do not
// appear are skipped; the order of what is found must increase, otherwise the
// header is ambiguous and the result is empty. Exposed because `pin list` prints
// columns (Pinned/Blocking) that are not part of the list/search signature.
struct ColumnPosition {
    std::string title;
    std::size_t start = 0;
};
std::vector<ColumnPosition> locateColumns(const std::string& headerLine, const std::vector<std::string>& titles);

// Parses one table out of `text`. Stops at the first blank line after the rows
// (WinGet prints trailing notices such as "N upgrades available." after a blank
// line). Returns failure with a specific reason whenever anything is ambiguous.
Table parseTable(const std::string& text);

// Parses a table whose columns are the given titles rather than one of the
// list/search signatures. Used for `pin list`, whose columns differ.
Table parseTableWithColumns(const std::string& text, const std::vector<std::string>& titles);

// Whether an id looks like the WinGet output we can act on. `ARP\...` entries are
// registry entries WinGet enumerated: they are real installed software but
// WinGet does not manage them, so they must be reported as observed, not
// managed, and never removed through WinGet.
bool isArpId(const std::string& id);

// Whether the id is a WinGet package id we could pass to a mutation.
bool isWingetPackageId(const std::string& id);

// Splits a multi-line capture into lines, dropping \r.
std::vector<std::string> splitLines(const std::string& text);

// `winget show <id>` prints one "Field: value" per line, groups a few fields
// under a label ("Tags:", "Installer:"), and indents the grouped ones. This
// parses it into a flat map so the details pane can show the backend's own
// description instead of guessing. Keys are lowercased and spaces removed:
// "Publisher Url" becomes "publisherurl", "Installer SHA256" becomes
// "installersha256".
//
// The shape is stable across versions because it is what the user sees; a line
// without a colon is a continuation of the previous value (a wrapped
// description) and is appended to it.
std::map<std::string, std::string> parseShowOutput(const std::string& text);

// The package's real homepage from a parsed `winget show`. Empty when the output
// did not carry one, so the caller can fall back rather than open a wrong URL.
std::string homepageFromShow(const std::map<std::string, std::string>& fields);

} // namespace pkg::winget
