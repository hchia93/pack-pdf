#include "App/Cli.h"

#include "File/Composer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
#else
  #include <cstdio>
#endif

namespace packpdf
{
    namespace
    {
        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // Split a token at the trailing "{...}" option block. The block must
        // end at the very last byte; everything before its matching '{' is
        // the path. Tokens without a trailing '}' have no option block.
        bool SplitToken(const std::string& tok, std::string& path, std::string& opts, bool& hasOpts, std::string& err)
        {
            hasOpts = false;
            if (tok.empty())
            {
                err = "empty token";
                return false;
            }
            if (tok.back() != '}')
            {
                path = tok;
                return true;
            }
            const size_t open = tok.find_last_of('{');
            if (open == std::string::npos)
            {
                err = "unmatched '}' in token: " + tok;
                return false;
            }
            path    = tok.substr(0, open);
            opts    = tok.substr(open + 1, tok.size() - open - 2);
            hasOpts = true;
            if (path.empty())
            {
                err = "missing path before '{...}': " + tok;
                return false;
            }
            return true;
        }

        bool ParseInt(std::string_view s, int& out)
        {
            if (s.empty()) return false;
            int v = 0;
            for (char c : s)
            {
                if (c < '0' || c > '9') return false;
                v = v * 10 + (c - '0');
                if (v > 1'000'000'000) return false;
            }
            out = v;
            return true;
        }

        // Page spec inside braces: "5" / "1-7" → Range, "!5" / "!1-7" → Exclude.
        bool ParsePDFSpec(const std::string& spec, PDFOptions& out, std::string& err)
        {
            std::string_view s = spec;
            const bool excl = !s.empty() && s.front() == '!';
            if (excl) s.remove_prefix(1);

            if (s.empty())
            {
                err = "empty page spec";
                return false;
            }

            int first = 0, last = 0;
            const size_t dash = s.find('-');
            if (dash == std::string_view::npos)
            {
                if (!ParseInt(s, first))
                {
                    err = "invalid page number: " + spec;
                    return false;
                }
                last = first;
            }
            else
            {
                if (!ParseInt(s.substr(0, dash), first) || !ParseInt(s.substr(dash + 1), last))
                {
                    err = "invalid page range: " + spec;
                    return false;
                }
            }
            if (first < 1 || last < first)
            {
                err = "page range out of order or below 1: " + spec;
                return false;
            }

            out.pageSelection = excl ? PageSelection::Exclude : PageSelection::Range;
            out.rangeFirst    = first;
            out.rangeLast     = last;
            return true;
        }

        bool ParseImageOpts(const std::string& opts, ImageOptions& out, std::string& err)
        {
            std::stringstream ss(opts);
            std::string tok;
            while (std::getline(ss, tok, ','))
            {
                // Trim ASCII whitespace.
                while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.front()))) tok.erase(tok.begin());
                while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.back())))  tok.pop_back();
                if (tok.empty()) continue;

                const std::string k = ToLower(tok);
                if      (k == "portrait")  out.orientation = ImageOrientation::Portrait;
                else if (k == "landscape") out.orientation = ImageOrientation::Landscape;
                else if (k == "flip")      out.reverse180  = true;
                else if (k == "fit")       out.scaleMode   = ImageScaleMode::FitPage;
                else if (k == "orig")      out.scaleMode   = ImageScaleMode::Original;
                else if (k == "merge")     out.autoMerge   = true;
                else if (k == "pad")       out.addPadding  = true;
                else
                {
                    err = "unknown image option: " + tok;
                    return false;
                }
            }
            // `merge` only takes effect on landscape images. Reject explicit
            // misuse instead of silently dropping it at compose time.
            if (out.autoMerge && out.orientation != ImageOrientation::Landscape)
            {
                err = "'merge' requires 'landscape'";
                return false;
            }
            return true;
        }

#ifdef _WIN32
        std::wstring Utf8ToWide(const std::string& s)
        {
            if (s.empty()) return {};
            int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        // Console-less Windows-subsystem builds have no FILE* stderr bound.
        // Write directly to whatever Win32 handle the parent (or AttachConsole)
        // provided.
        void WriteHandle(DWORD which, const std::string& s)
        {
            HANDLE h = ::GetStdHandle(which);
            if (h == nullptr || h == INVALID_HANDLE_VALUE) return;
            DWORD written = 0;
            ::WriteFile(h, s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
        }
#endif

        void PrintErr(const std::string& s)
        {
#ifdef _WIN32
            WriteHandle(STD_ERROR_HANDLE, s);
#else
            std::fputs(s.c_str(), stderr);
#endif
        }

        void PrintOut(const std::string& s)
        {
#ifdef _WIN32
            WriteHandle(STD_OUTPUT_HANDLE, s);
#else
            std::fputs(s.c_str(), stdout);
#endif
        }

        constexpr const char* kUsage =
            "Usage: pack-pdf compose <row>... -o <output.pdf>\n"
            "\n"
            "Rows:\n"
            "  <path>                       file with default options\n"
            "  <path.pdf>{N}                page N (1-indexed)\n"
            "  <path.pdf>{N-M}              page range\n"
            "  <path.pdf>{!N} / {!N-M}      exclude pages\n"
            "  <path.img>{opt,opt,...}     image options\n"
            "\n"
            "Image options (comma-separated):\n"
            "  portrait | landscape   default: portrait\n"
            "  flip                   180 degree rotation\n"
            "  fit | orig             default: fit\n"
            "  merge                  auto-merge with next landscape image\n"
            "  pad                    add 0.5 inch white margin\n";
    }

    std::string RowToToken(const TimelineRow& row)
    {
        std::string out = row.path;
        std::visit([&](const auto& opts) 
        {
            using T = std::decay_t<decltype(opts)>;
            if constexpr (std::is_same_v<T, PDFOptions>)
            {
                if (opts.pageSelection == PageSelection::All) return;
                out += '{';
                if (opts.pageSelection == PageSelection::Exclude) out += '!';
                if (opts.rangeFirst == opts.rangeLast)
                {
                    out += std::to_string(opts.rangeFirst);
                }
                else
                {
                    out += std::to_string(opts.rangeFirst);
                    out += '-';
                    out += std::to_string(opts.rangeLast);
                }
                out += '}';
            }
            else if constexpr (std::is_same_v<T, ImageOptions>)
            {
                std::vector<const char*> bits;
                if (opts.orientation == ImageOrientation::Landscape) bits.push_back("landscape");
                if (opts.reverse180)                                 bits.push_back("flip");
                if (opts.scaleMode   == ImageScaleMode::Original)    bits.push_back("orig");
                if (opts.autoMerge)                                  bits.push_back("merge");
                if (opts.addPadding)                                 bits.push_back("pad");
                if (bits.empty()) return;
                out += '{';
                for (size_t i = 0; i < bits.size(); ++i)
                {
                    if (i) out += ',';
                    out += bits[i];
                }
                out += '}';
            }
        }, row.options);
        return out;
    }

    bool ParseToken(const std::string& token, TimelineRow& out, std::string& err)
    {
        std::string path, opts;
        bool hasOpts = false;
        if (!SplitToken(token, path, opts, hasOpts, err)) return false;

        auto ext = FileExtensionFromPath(path);
        if (!ext)
        {
            err = "unsupported file extension: " + path;
            return false;
        }

        out      = TimelineRow{};
        out.path = path;

        if (*ext == FileExtension::PDF)
        {
            PDFOptions p;
            if (hasOpts && !ParsePDFSpec(opts, p, err)) return false;
            out.options = p;
            return true;
        }

        ImageOptions img;
        img.format = ImageFormatFromExtension(*ext);
        if (hasOpts && !ParseImageOpts(opts, img, err)) return false;
        out.options = img;
        return true;
    }

    std::vector<std::string> BuildComposeArgs(const TimelineContainer& rows, const std::string& outputUtf8)
    {
        std::vector<std::string> argv;
        argv.reserve(rows.size() + 2);
        for (const auto& r : rows)
        {
            argv.push_back(RowToToken(r));
        }
        argv.emplace_back("-o");
        argv.push_back(outputUtf8);
        return argv;
    }

    int RunComposeCli(const std::vector<std::string>& args)
    {
        // `pack-pdf compose -h | --help | help` -> usage, exit 0.
        if (args.size() == 1 && (args[0] == "-h" || args[0] == "--help" || args[0] == "help"))
        {
            PrintOut(kUsage);
            return 0;
        }

        TimelineContainer rows;
        std::string outputUtf8;

        for (size_t i = 0; i < args.size(); ++i)
        {
            const std::string& a = args[i];
            if (a == "-o" || a == "--output")
            {
                if (i + 1 >= args.size())
                {
                    PrintErr("error: -o requires a path\n");
                    PrintErr(kUsage);
                    return 1;
                }
                outputUtf8 = args[++i];
                continue;
            }
            TimelineRow row;
            std::string err;
            if (!ParseToken(a, row, err))
            {
                PrintErr("parse error: " + err + "\n");
                return 2;
            }
            rows.push_back(std::move(row));
        }

        if (outputUtf8.empty())
        {
            PrintErr("error: -o <output.pdf> is required\n");
            PrintErr(kUsage);
            return 1;
        }
        if (rows.empty())
        {
            PrintErr("error: no input rows\n");
            return 1;
        }

        std::filesystem::path outPath = Utf8ToPath(outputUtf8);
        std::error_code ec;
        std::filesystem::path parent = outPath.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent, ec))
        {
            std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                PrintErr("error: cannot create output directory: " + ec.message() + "\n");
                return 3;
            }
        }

        ComposeResult r = ComposeToFile(rows, outPath);
        if (!r.ok)
        {
            PrintErr("compose failed: " + r.errorMessage + "\n");
            return 3;
        }
        return 0;
    }
}
