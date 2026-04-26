#pragma once

#include "File/TimelineRow.h"

#include <string>
#include <vector>

namespace packpdf
{
    // Round-trips through ParseToken. Syntax reference: `pack-pdf compose --help`.
    std::string RowToToken(const TimelineRow& row);

    // Parse one token into a TimelineRow. The path's extension picks PDF vs
    // image semantics (and JPEG vs PNG decode hint). Returns false with `err`
    // populated on syntax error.
    bool ParseToken(const std::string& token, TimelineRow& out, std::string& err);

    // `compose` subcommand entry. args = `<token>... -o <output.pdf>`.
    // Returns 0 ok, 1 usage, 2 parse, 3 compose. Diagnostics → stderr.
    int RunComposeCli(const std::vector<std::string>& args);

    // GUI helper: build the argv (token list + "-o" + output path) the GUI
    // hands to its own exe in compose mode. Output is one UTF-8 string per
    // argv slot; the caller is responsible for Win32 quoting.
    std::vector<std::string> BuildComposeArgs(const TimelineContainer& rows, const std::string& outputUtf8);
}
