#include "App/AppMainWindow.h"

#include "App/AppUI.h"
#include "App/Cli.h"
#include "File/Composer.h"
#include "File/FileTypes.h"
#include "File/ImageHelpers.h"
#include "Selector/ImageOptionsSelector.h"
#include "Selector/PDFPageRangeSelector.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <variant>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
  #include <shlobj.h>
  #include <shellapi.h>
#endif

namespace packpdf
{
    namespace
    {
        // Build the variant-tagged row for the dragged-in path. Caller already
        // verified the extension via FileExtensionFromPath.
        TimelineRow MakeRowFromPath(std::string utf8Path, FileExtension ext)
        {
            TimelineRow row;
            row.path = std::move(utf8Path);
            if (ext == FileExtension::PDF)
            {
                row.options = PDFOptions{};
            }
            else
            {
                ImageOptions img;
                img.format = ImageFormatFromExtension(ext);
                row.options = img;
            }
            return row;
        }

        ImU32 FileTypeColor(FileExtension k)
        {
            switch (k)
            {
                case FileExtension::PDF:  return IM_COL32(196,  64,  64, 255); // red
                case FileExtension::JPEG: return IM_COL32( 64, 112, 196, 255); // blue
                case FileExtension::PNG:  return IM_COL32(220, 134,  48, 255); // orange
            }
            return IM_COL32(110, 110, 110, 255);
        }

        const char* FileTypeLabel(FileExtension k)
        {
            switch (k)
            {
                case FileExtension::PDF:  return "PDF";
                case FileExtension::JPEG: return "JPG";
                case FileExtension::PNG:  return "PNG";
            }
            return "?";
        }

        // The badge needs a single tri-state at the row level. Map the row
        // variant (and image format) back to the FileExtension enum so the
        // existing color / label tables stay one switch each.
        FileExtension RowFileExtension(const TimelineRow& row)
        {
            if (std::holds_alternative<PDFOptions>(row.options))
            {
                return FileExtension::PDF;
            }
            const auto& img = std::get<ImageOptions>(row.options);
            return (img.format == ImageFormat::PNG) ? FileExtension::PNG
                                                    : FileExtension::JPEG;
        }

        // Two-section pill: dark page-number slot left, file-type slot right.
        // Width is fixed so badges line up. pageNumber=0 blanks the number slot.
        void DrawFileTypeIcon(FileExtension k, int pageNumber)
        {
            const char* typeLabel = FileTypeLabel(k);
            const ImU32 typeBg    = FileTypeColor(k);
            const float h         = ImGui::GetFrameHeight();

            // Both sections sized to their widest expected text so the badge
            // is the same width on every row.
            const float numSecW  = ImGui::CalcTextSize("999").x + h * 0.4f;
            const float typeSecW = ImGui::CalcTextSize("PNG").x + h * 0.6f;
            const float totalW   = numSecW + typeSecW;

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList* dl   = ImGui::GetWindowDrawList();

            // Full pill in type color, then darken the left section with a
            // translucent black overlay to carve out the number area.
            dl->AddRectFilled(pos, ImVec2(pos.x + totalW, pos.y + h), typeBg, h * 0.25f);
            dl->AddRectFilled(pos, ImVec2(pos.x + numSecW, pos.y + h), IM_COL32(0, 0, 0, 110), h * 0.25f, ImDrawFlags_RoundCornersLeft);

            if (pageNumber > 0)
            {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", pageNumber);
                const ImVec2 nts = ImGui::CalcTextSize(buf);
                dl->AddText(ImVec2(pos.x + (numSecW - nts.x) * 0.5f, pos.y + (h - nts.y) * 0.5f), IM_COL32_WHITE, buf);
            }

            const float typeX = pos.x + numSecW;
            const ImVec2 tts  = ImGui::CalcTextSize(typeLabel);
            dl->AddText(ImVec2(typeX + (typeSecW - tts.x) * 0.5f, pos.y + (h - tts.y) * 0.5f), IM_COL32_WHITE, typeLabel);

            ImGui::Dummy(ImVec2(totalW, h));
        }

        bool XButton(const char* str_id, ImVec2 size)
        {
            return Ui::IconButton(str_id, size, [](ImDrawList* dl, ImVec2 pos, ImVec2 sz, ImU32 /*frameBg*/)
            {
                const ImU32 fg    = ImGui::GetColorU32(ImGuiCol_Text);
                const float pad   = std::min(sz.x, sz.y) * 0.30f;
                const float thick = std::max(1.0f, ImGui::GetFontSize() * 0.12f);
                dl->AddLine(ImVec2(pos.x + pad, pos.y + pad), ImVec2(pos.x + sz.x - pad, pos.y + sz.y - pad), fg, thick);
                dl->AddLine(ImVec2(pos.x + sz.x - pad, pos.y + pad), ImVec2(pos.x + pad, pos.y + sz.y - pad), fg, thick);
            });
        }

        bool FileExists(const char* path)
        {
            std::error_code ec;
            return std::filesystem::exists(path, ec);
        }

        // Byte-wise basename so CJK paths survive on Windows (std::filesystem
        // would round-trip through the ANSI codepage and mojibake them).
        std::string FilenameFromPath(const std::string& utf8Path)
        {
            const size_t cut = utf8Path.find_last_of("/\\");
            return (cut == std::string::npos) ? utf8Path
                                              : utf8Path.substr(cut + 1);
        }

        // Pages a PDF range/exclude selection resolves to, given the source's
        // total page count. Mirrors Composer::BuildImportSpec's clamping.
        int PdfSelectedPageCount(int total, PageSelection sel, int first, int last)
        {
            if (total <= 0)
            {
                return 0;
            }
            if (sel == PageSelection::All)
            {
                return total;
            }
            const int a = std::max(1, first);
            const int b = std::min(total, last);
            const int spanLen = std::max(0, b - a + 1);
            if (sel == PageSelection::Range)
            {
                return spanLen;
            }
            return std::max(0, total - spanLen);  // Exclude
        }

        // Image preview tooltip — uses the same A4 placement math as the
        // compose pass so what you see is what gets written.
        void DrawHoverPreview(const ImageCache::Entry& entry, const std::string& path, const ImageOptions& opts)
        {
            if (entry.w <= 0 || entry.h <= 0)
            {
                return;
            }

            const ImagePageLayout L = ComputeImagePageLayout(entry.w, entry.h, opts);
            if (L.pageW <= 0.0 || L.pageH <= 0.0 || L.imgW <= 0.0 || L.imgH <= 0.0) return;

            // Cap the long axis to ~280px. Page is always A4 portrait so the
            // preview is portrait too.
            const float kPreviewLongPx = 280.0f;
            const float scale = static_cast<float>(kPreviewLongPx / L.pageH);
            const float pageW = static_cast<float>(L.pageW) * scale;
            const float pageH = static_cast<float>(L.pageH) * scale;
            const float imgW  = static_cast<float>(L.imgW)  * scale;
            const float imgH  = static_cast<float>(L.imgH)  * scale;

            // PDF coords are Y-up (origin = bottom-left); ImGui is Y-down
            // (origin = top-left). Flip Y when mapping into the preview.
            const float imgX  = static_cast<float>(L.imgX) * scale;
            const float imgY  = pageH - static_cast<float>(L.imgY) * scale - imgH;

            // UV mapping per CW rotation; corners clockwise from top-left.
            ImVec2 uvA, uvB, uvC, uvD;
            switch (((L.rotation % 360) + 360) % 360)
            {
                default:
                case 0:   uvA = {0,0}; uvB = {1,0}; uvC = {1,1}; uvD = {0,1}; break;
                case 90:  uvA = {0,1}; uvB = {0,0}; uvC = {1,0}; uvD = {1,1}; break;
                case 180: uvA = {1,1}; uvB = {0,1}; uvC = {0,0}; uvD = {1,0}; break;
                case 270: uvA = {1,0}; uvB = {1,1}; uvC = {0,1}; uvD = {0,0}; break;
            }

            ImGui::BeginTooltip();

            // Center both rows on the wider of (path text, page rect).
            const float pathW    = ImGui::CalcTextSize(path.c_str()).x;
            const float contentW = std::max(pathW, pageW);
            const float baseX    = ImGui::GetCursorPosX();

            if (pathW < contentW)
            {
                ImGui::SetCursorPosX(baseX + (contentW - pathW) * 0.5f);
            }
            ImGui::TextUnformatted(path.c_str());

            if (pageW < contentW)
            {
                ImGui::SetCursorPosX(baseX + (contentW - pageW) * 0.5f);
            }

            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // White A4 page + thin border so empty padding around small
            // images reads as paper rather than tooltip background.
            const ImVec2 pageTL = p;
            const ImVec2 pageBR = ImVec2(p.x + pageW, p.y + pageH);
            dl->AddRectFilled(pageTL, pageBR, IM_COL32(245, 245, 245, 255));
            dl->AddRect      (pageTL, pageBR, IM_COL32( 90,  90,  90, 200));

            const ImVec2 a{ p.x + imgX,        p.y + imgY        };
            const ImVec2 b{ p.x + imgX + imgW, p.y + imgY        };
            const ImVec2 c{ p.x + imgX + imgW, p.y + imgY + imgH };
            const ImVec2 d{ p.x + imgX,        p.y + imgY + imgH };
            dl->AddImageQuad(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(entry.tex)), a, b, c, d, uvA, uvB, uvC, uvD);

            ImGui::Dummy(ImVec2(pageW, pageH));
            ImGui::EndTooltip();
        }

#ifdef _WIN32
        std::wstring Utf8ToWide(const std::string& s)
        {
            if (s.empty())
            {
                return {};
            }
            int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        std::string WideToUtf8(const wchar_t* w)
        {
            if (!w || !*w)
            {
                return {};
            }
            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (n <= 1)
            {
                return {};
            }
            std::string s(static_cast<size_t>(n - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n - 1, nullptr, nullptr);
            return s;
        }

        std::filesystem::path ExeDir()
        {
            wchar_t buf[MAX_PATH];
            DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
            if (n == 0 || n == MAX_PATH)
            {
                return std::filesystem::current_path();
            }
            return std::filesystem::path(std::wstring(buf, n)).parent_path();
        }

        std::filesystem::path ConfigPath()
        {
            return ExeDir() / L"userdata" / L"config.ini";
        }

        std::string DesktopPathUtf8()
        {
            PWSTR p = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p)))
            {
                std::string r = WideToUtf8(p);
                CoTaskMemFree(p);
                return r;
            }
            return {};
        }

        std::string PickFolderDialog(const std::string& initialDirUtf8)
        {
            std::string result;
            HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            IFileOpenDialog* dlg = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
            {
                DWORD opts = 0;
                dlg->GetOptions(&opts);
                dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

                if (!initialDirUtf8.empty())
                {
                    std::error_code ec;
                    if (std::filesystem::exists(initialDirUtf8, ec))
                    {
                        std::wstring wInit = Utf8ToWide(initialDirUtf8);
                        IShellItem* item = nullptr;
                        if (SUCCEEDED(SHCreateItemFromParsingName(wInit.c_str(), nullptr, IID_PPV_ARGS(&item))))
                        {
                            dlg->SetFolder(item);
                            item->Release();
                        }
                    }
                }

                if (SUCCEEDED(dlg->Show(nullptr)))
                {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(dlg->GetResult(&item)))
                    {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                        {
                            result = WideToUtf8(path);
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                dlg->Release();
            }
            if (SUCCEEDED(hrInit))
            {
                CoUninitialize();
            }
            return result;
        }

        bool OpenInExplorer(const std::string& pathUtf8)
        {
            std::wstring w = Utf8ToWide(pathUtf8);
            HINSTANCE r = ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return reinterpret_cast<INT_PTR>(r) > 32;
        }

        // Path of the currently running pack-pdf.exe; the GUI re-spawns this
        // exact binary with the `compose` subcommand so the CLI and GUI share
        // a single PDFium engine.
        std::wstring SelfExePath()
        {
            wchar_t buf[MAX_PATH];
            DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
            if (n == 0 || n == MAX_PATH) return {};
            return std::wstring(buf, n);
        }

        // CommandLineToArgvW's exact inverse — quotes + backslash-doubling.
        // Doesn't escape cmd.exe metacharacters; we feed CreateProcessW directly.
        std::wstring WinQuote(const std::wstring& a)
        {
            if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos)
            {
                return a;
            }
            std::wstring out = L"\"";
            int slashes = 0;
            for (wchar_t c : a)
            {
                if (c == L'\\')
                {
                    ++slashes;
                    out += c;
                }
                else if (c == L'"')
                {
                    out.append(static_cast<size_t>(slashes), L'\\');
                    out += L'\\';
                    out += L'"';
                    slashes = 0;
                }
                else
                {
                    slashes = 0;
                    out += c;
                }
            }
            out.append(static_cast<size_t>(slashes), L'\\');
            out += L'"';
            return out;
        }

        struct ChildResult
        {
            bool        launched = false;
            DWORD       exitCode = 0;
            std::string stderrText;
        };

        // Spawn pack-pdf.exe in compose mode, capture its stderr (and stdout
        // — both go to the same pipe), wait for exit. The child is run with
        // CREATE_NO_WINDOW so no console window flashes on Pack.
        ChildResult RunSelfCompose(const std::vector<std::string>& utf8Args)
        {
            ChildResult r;

            const std::wstring exe = SelfExePath();
            if (exe.empty())
            {
                r.stderrText = "cannot resolve own exe path";
                return r;
            }

            std::wstring cmd = WinQuote(exe);
            cmd += L" compose";
            for (const auto& a : utf8Args)
            {
                cmd += L' ';
                cmd += WinQuote(Utf8ToWide(a));
            }

            SECURITY_ATTRIBUTES sa{};
            sa.nLength        = sizeof(sa);
            sa.bInheritHandle = TRUE;

            HANDLE hReadErr = nullptr;
            HANDLE hWriteErr = nullptr;
            if (!::CreatePipe(&hReadErr, &hWriteErr, &sa, 0))
            {
                r.stderrText = "CreatePipe failed";
                return r;
            }
            // Keep the read end private to the parent so the child does not
            // also inherit it (would leave the pipe open after the child
            // exits and stall ReadFile in the loop below).
            ::SetHandleInformation(hReadErr, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOW si{};
            si.cb         = sizeof(si);
            si.dwFlags    = STARTF_USESTDHANDLES;
            si.hStdInput  = nullptr;
            si.hStdOutput = hWriteErr;
            si.hStdError  = hWriteErr;

            PROCESS_INFORMATION pi{};
            std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
            mutableCmd.push_back(L'\0');

            BOOL ok = ::CreateProcessW(exe.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

            // Drop the parent's copy of the write end immediately; otherwise
            // ReadFile keeps blocking even after the child closes its end.
            ::CloseHandle(hWriteErr);

            if (!ok)
            {
                ::CloseHandle(hReadErr);
                r.stderrText = "CreateProcess failed";
                return r;
            }
            r.launched = true;

            char buf[4096];
            DWORD got = 0;
            while (::ReadFile(hReadErr, buf, sizeof(buf), &got, nullptr) && got > 0)
            {
                r.stderrText.append(buf, got);
            }
            ::CloseHandle(hReadErr);

            ::WaitForSingleObject(pi.hProcess, INFINITE);
            ::GetExitCodeProcess(pi.hProcess, &r.exitCode);
            ::CloseHandle(pi.hProcess);
            ::CloseHandle(pi.hThread);
            return r;
        }
#else
        std::filesystem::path ConfigPath()
        {
            return std::filesystem::current_path() / "userdata" / "config.ini";
        }
        std::string DesktopPathUtf8() { return {}; }
        std::string PickFolderDialog(const std::string&) { return {}; }
        bool OpenInExplorer(const std::string&) { return false; }

        struct ChildResult { bool launched = false; int exitCode = 0; std::string stderrText; };
        ChildResult RunSelfCompose(const std::vector<std::string>&)
        {
            ChildResult r;
            r.stderrText = "subprocess compose only supported on Windows";
            return r;
        }
#endif
    }

    void AppMainWindow::LoadFonts()
    {
        ImGuiIO& io = ImGui::GetIO();

        // Glyph ranges held in static storage: ImGui retains the pointer until
        // the font atlas is built on first NewFrame.
        static ImVector<ImWchar> ranges;
        ranges.clear();

        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
        builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
        builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
        builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
        builder.AddRanges(io.Fonts->GetGlyphRangesGreek());
        builder.AddRanges(io.Fonts->GetGlyphRangesThai());
        builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
        builder.BuildRanges(&ranges);

        const float fontSize = 16.0f;

        // Primary: Microsoft YaHei. Covers Latin + Simplified Chinese + CJK Han
        // + Cyrillic + Greek. OversampleH=1 keeps the atlas under 8192px even
        // with the full Chinese range loaded.
        const char* primaryPath = "C:/Windows/Fonts/msyh.ttc";
        ImFont* primary = nullptr;
        if (FileExists(primaryPath))
        {
            ImFontConfig cfg;
            cfg.OversampleH = 1;
            cfg.OversampleV = 1;
            cfg.PixelSnapH  = true;
            primary = io.Fonts->AddFontFromFileTTF(primaryPath, fontSize, &cfg, ranges.Data);
        }

        if (!primary)
        {
            io.Fonts->AddFontDefault();
            return;
        }

        // Merge Korean Hangul glyphs (YaHei does not ship Jamo blocks).
        const char* koreanPath = "C:/Windows/Fonts/malgun.ttf";
        if (FileExists(koreanPath))
        {
            ImFontConfig mergeCfg;
            mergeCfg.MergeMode  = true;
            mergeCfg.OversampleH = 1;
            mergeCfg.OversampleV = 1;
            mergeCfg.PixelSnapH  = true;
            io.Fonts->AddFontFromFileTTF(koreanPath, fontSize, &mergeCfg, ranges.Data);
        }
    }

    void AppMainWindow::ApplyImGuiStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding   = 4.0f;
        style.FrameRounding    = 3.0f;
        style.GrabRounding     = 3.0f;
        style.WindowPadding    = ImVec2(10, 10);
        style.FramePadding     = ImVec2(8, 4);
        style.ItemSpacing      = ImVec2(8, 6);

        // Colors come from the theme persisted in config.ini and loaded by
        // the constructor before this call.
        ApplyTheme(m_Theme);
    }

    void AppMainWindow::OnFilesDropped(const char** paths, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            std::optional<FileExtension> ft = FileExtensionFromPath(paths[i]);
            if (!ft) continue; // unsupported extension, drop silently
            m_Rows.push_back(MakeRowFromPath(paths[i], *ft));
        }
    }

    AppMainWindow::AppMainWindow()
    {
        std::strncpy(m_OutputFile, "packed.pdf", sizeof(m_OutputFile) - 1);
        LoadConfig();
    }

    void AppMainWindow::LoadConfig()
    {
        bool gotFolder = false;

        const auto userPath    = ConfigPath();
        std::error_code ec;
        const bool fileExisted = std::filesystem::exists(userPath, ec);

        std::ifstream f(userPath);
        if (f)
        {
            std::string line;
            while (std::getline(f, line))
            {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                {
                    line.pop_back();
                }

                auto match = [&](const char* key) -> const char* {
                    const size_t klen = std::strlen(key);
                    if (line.size() >= klen && line.compare(0, klen, key) == 0)
                    {
                        return line.c_str() + klen;
                    }
                    return nullptr;
                };

                if (const char* val = match("folder="))
                {
                    // Empty `folder=` in the shipped default → fall through to
                    // the Desktop fallback below.
                    if (*val != '\0')
                    {
                        std::strncpy(m_OutputDir, val, sizeof(m_OutputDir) - 1);
                        m_OutputDir[sizeof(m_OutputDir) - 1] = '\0';
                        gotFolder = true;
                    }
                }
                else if (const char* val = match("theme="))
                {
                    m_Theme = ThemeFromKey(val);
                }
            }
        }

        if (!gotFolder)
        {
            // No config or no folder entry: fall back to user's Desktop.
            std::string desk = DesktopPathUtf8();
            if (desk.empty())
            {
                std::error_code fallbackEc;
                desk = std::filesystem::current_path(fallbackEc).generic_string();
            }
            std::strncpy(m_OutputDir, desk.c_str(), sizeof(m_OutputDir) - 1);
            m_OutputDir[sizeof(m_OutputDir) - 1] = '\0';
        }

        // First-launch defaults flush to disk so userdata/config.ini stays
        // the single source of truth.
        if (!fileExisted)
        {
            SaveConfig();
        }
    }

    void AppMainWindow::SaveConfig()
    {
        const std::filesystem::path cfg = ConfigPath();
        std::error_code ec;
        std::filesystem::create_directories(cfg.parent_path(), ec);

        std::ofstream f(cfg, std::ios::trunc);
        if (!f)
        {
            return;
        }
        f << "folder=" << m_OutputDir << '\n';
        f << "theme="  << ThemeKey(m_Theme) << '\n';
    }

    void AppMainWindow::BrowseFolder()
    {
        std::string picked = PickFolderDialog(std::string(m_OutputDir));
        if (picked.empty())
        {
            return;
        }
        std::strncpy(m_OutputDir, picked.c_str(), sizeof(m_OutputDir) - 1);
        m_OutputDir[sizeof(m_OutputDir) - 1] = '\0';
        SaveConfig();
    }

    void AppMainWindow::ShowMessageDialog(std::string msg, std::string buttonLabel, std::string openPath)
    {
        m_NoticeMessage   = std::move(msg);
        m_NoticeButton    = std::move(buttonLabel);
        m_NoticeOpenPath  = std::move(openPath);
        m_NoticeRequested = true;
    }

    void AppMainWindow::Render()
    {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                               | ImGuiWindowFlags_NoResize
                               | ImGuiWindowFlags_NoMove
                               | ImGuiWindowFlags_NoCollapse
                               | ImGuiWindowFlags_NoBringToFrontOnFocus
                               | ImGuiWindowFlags_MenuBar;

        ImGui::Begin("PackPDF", nullptr, flags);

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("Theme"))
            {
                for (int i = 0; i < kThemeCount; ++i)
                {
                    const bool selected = (m_Theme == kThemes[i].id);
                    if (ImGui::MenuItem(kThemes[i].displayName, nullptr, selected))
                    {
                        m_Theme = kThemes[i].id;
                        ApplyTheme(m_Theme);
                        SaveConfig();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        {
            const float clearW = UiSize::InterfaceButtonSmall.x;
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Drop PDF / PNG / JPG files anywhere on this window. Drag to reorder.");
            ImGui::SameLine();
            Ui::RightAlignCursorX(clearW);
            if (ImGui::Button("Clear", UiSize::InterfaceButtonSmall))
            {
                m_Rows.clear();
                m_SelectedIndex = -1;
            }
        }
        ImGui::Separator();

        // Footer = separator + "Output" header + 2 input rows + small padding + button row.
        // Sized tightly so the button row lands at the window bottom (RenderOutputPanel
        // anchors it explicitly as a safety net against layout drift).
        const ImGuiStyle& s = ImGui::GetStyle();
        const float footerH = s.ItemSpacing.y
                            + ImGui::GetTextLineHeightWithSpacing()
                            + ImGui::GetFrameHeightWithSpacing() * 2
                            + s.ItemSpacing.y * 2
                            + ImGui::GetFrameHeight();

        ImGui::BeginChild("Timeline", ImVec2(0, -footerH), true);
        if (m_Rows.empty())
        {
            ImGui::TextDisabled("(Empty)");
        }
        else
        {
            const float rowH    = ImGui::GetFrameHeight();
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float btnW    = rowH; // square trailing buttons

            // Reserve fixed slots for [Up][Down][Remove] so the column lines
            // up across rows even when the up/down arrows are hidden on the
            // edges of the list.
            const float trailingW = btnW * 3 + spacing * 3;

            int moveUpIdx   = -1;
            int moveDownIdx = -1;
            int removeIdx   = -1;
            const int last = static_cast<int>(m_Rows.size()) - 1;

            // Pre-pass: mark each row that is the auto-merge partner of the
            // previous row. Pairs consume two indices; the third landscape (if
            // any) starts a fresh group.
            std::vector<bool> mergeForced(m_Rows.size(), false);
            for (size_t k = 0; k < m_Rows.size(); )
            {
                const auto* curImg = std::get_if<ImageOptions>(&m_Rows[k].options);
                if (curImg && IsLandscapeMode(curImg->orientation) && curImg->autoMerge
                    && k + 1 < m_Rows.size())
                {
                    const auto* nxtImg = std::get_if<ImageOptions>(&m_Rows[k + 1].options);
                    if (nxtImg && IsLandscapeMode(nxtImg->orientation))
                    {
                        mergeForced[k + 1] = true;
                        k += 2;
                        continue;
                    }
                }
                ++k;
            }

            // Pre-pass: starting output page number for each row. Auto-merge
            // partners share the leader's page (no advance).
            std::vector<int> startPage(m_Rows.size(), 0);
            {
                int next = 1;
                for (size_t k = 0; k < m_Rows.size(); ++k)
                {
                    if (mergeForced[k] && k > 0)
                    {
                        startPage[k] = startPage[k - 1];  // no advance
                        continue;
                    }
                    startPage[k] = next;

                    int pages = 0;
                    if (const auto* pdf = std::get_if<PDFOptions>(&m_Rows[k].options))
                    {
                        const std::string& path = m_Rows[k].path;
                        auto it = m_PdfPageCounts.find(path);
                        if (it == m_PdfPageCounts.end())
                        {
                            it = m_PdfPageCounts.emplace(path, GetPdfPageCount(Utf8ToPath(path))).first;
                        }
                        pages = PdfSelectedPageCount(it->second, pdf->pageSelection, pdf->rangeFirst, pdf->rangeLast);
                    }
                    else
                    {
                        pages = 1;  // image: one page per row (or per pair)
                    }
                    next += pages;
                }
            }

            for (size_t i = 0; i < m_Rows.size(); ++i)
            {
                const int idx = static_cast<int>(i);
                ImGui::PushID(idx);
                auto& row = m_Rows[i];
                bool selected = (m_SelectedIndex == idx);

                // Page number is overlaid on the badge's dark left section.
                // Merge partners pass 0 → leader carries the number for the
                // pair, partner's slot stays blank.
                const int pageNum = mergeForced[i] ? 0 : startPage[i];
                const FileExtension ext = RowFileExtension(row);
                DrawFileTypeIcon(ext, pageNum);
                ImGui::SameLine();

                // Reserve the wider of the two selector widths so the trailing
                // button column lines up across PDF and image rows alike.
                float selectorW = 0.0f;
                if (const auto* pdf = std::get_if<PDFOptions>(&row.options))
                {
                    selectorW = std::max(PDFPageRangeSelectorWidth(*pdf), ImageOptionsSelectorWidth(ImageOptions{})) + spacing;
                }
                else
                {
                    const auto& img = std::get<ImageOptions>(row.options);
                    selectorW = std::max(PDFPageRangeSelectorWidth(PDFOptions{}), ImageOptionsSelectorWidth(img)) + spacing;
                }

                float availW = ImGui::GetContentRegionAvail().x - trailingW - selectorW;
                if (availW < 1.0f)
                {
                    availW = 1.0f;
                }
                const std::string filename = FilenameFromPath(row.path);
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
                if (ImGui::Selectable(filename.c_str(), selected, 0, ImVec2(availW, rowH)))
                {
                    m_SelectedIndex = idx;
                }
                const bool pathHovered = ImGui::IsItemHovered();
                ImGui::PopStyleVar();

                if (pathHovered)
                {
                    if (const auto* img = std::get_if<ImageOptions>(&row.options))
                    {
                        if (const auto* entry = m_ImageCache.Get(row.path))
                        {
                            DrawHoverPreview(*entry, row.path, *img);
                        }
                        else
                        {
                            ImGui::SetTooltip("%s", row.path.c_str());
                        }
                    }
                    else
                    {
                        ImGui::SetTooltip("%s", row.path.c_str());
                    }
                }

                ImGui::SameLine();
                if (auto* pdf = std::get_if<PDFOptions>(&row.options))
                {
                    PDFPageRangeSelector(*pdf);
                }
                else
                {
                    auto& img = std::get<ImageOptions>(row.options);
                    ImageOptionsSelector(img, mergeForced[i]);
                }

                // Right-anchor the trailing button column to a fixed width,
                // independent of selector column actual width.
                ImGui::SameLine();
                Ui::RightAlignCursorX(trailingW - spacing);

                if (Ui::OptionalArrowButton("##up", ImGuiDir_Up, idx > 0))
                {
                    moveUpIdx = idx;
                }
                ImGui::SameLine();
                if (Ui::OptionalArrowButton("##down", ImGuiDir_Down, idx < last))
                {
                    moveDownIdx = idx;
                }

                ImGui::SameLine();
                if (XButton("##remove", ImVec2(btnW, rowH)))
                {
                    removeIdx = idx;
                }

                ImGui::PopID();
            }

            // At most one of the three actions fires per frame (each is a
            // distinct button click). Mutually exclusive handling so a stale
            // index from a different action cannot bite us.
            if (moveUpIdx >= 0)
            {
                std::swap(m_Rows[moveUpIdx], m_Rows[moveUpIdx - 1]);
                if (m_SelectedIndex == moveUpIdx)
                {
                    m_SelectedIndex = moveUpIdx - 1;
                }
                else if (m_SelectedIndex == moveUpIdx - 1)
                {
                    m_SelectedIndex = moveUpIdx;
                }
            }
            else if (moveDownIdx >= 0)
            {
                std::swap(m_Rows[moveDownIdx], m_Rows[moveDownIdx + 1]);
                if (m_SelectedIndex == moveDownIdx)
                {
                    m_SelectedIndex = moveDownIdx + 1;
                }
                else if (m_SelectedIndex == moveDownIdx + 1)
                {
                    m_SelectedIndex = moveDownIdx;
                }
            }
            else if (removeIdx >= 0)
            {
                m_Rows.erase(m_Rows.begin() + removeIdx);
                if (m_SelectedIndex == removeIdx)
                {
                    m_SelectedIndex = -1;
                }
                else if (m_SelectedIndex > removeIdx)
                {
                    --m_SelectedIndex;
                }
            }
        }
        ImGui::EndChild();

        RenderOutputPanel();
        RenderNoticePopup();

        ImGui::End();
    }

    void AppMainWindow::RenderNoticePopup()
    {
        const char* id = "Notice##packpdf";
        if (m_NoticeRequested)
        {
            ImGui::OpenPopup(id);
            m_NoticeRequested = false;
        }

        const ImGuiStyle& s = ImGui::GetStyle();
        const bool  hasOpen  = !m_NoticeOpenPath.empty();
        const float btnW     = UiSize::DialogButton.x;
        const float rowW     = hasOpen ? (btnW * 2 + s.ItemSpacing.x) : btnW;
        const float textW    = ImGui::CalcTextSize(m_NoticeMessage.c_str()).x;
        // Lock the popup width to whichever line is wider so both message and
        // button row can be centered with the same `(winW - itemW) * 0.5` math.
        const float contentW = std::max(textW, rowW);
        const float popupW   = contentW + s.WindowPadding.x * 2.0f;

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        // Width fixed; height = 0 → auto-fit content.
        ImGui::SetNextWindowSize(ImVec2(popupW, 0.0f));

        if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoSavedSettings))
        {
            Ui::CenterCursorX(textW);
            ImGui::TextUnformatted(m_NoticeMessage.c_str());
            ImGui::Dummy(ImVec2(0, s.ItemSpacing.y));

            Ui::CenterCursorX(rowW);

            if (ImGui::Button(m_NoticeButton.c_str(), UiSize::DialogButton)
                || ImGui::IsKeyPressed(ImGuiKey_Enter)
                || ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                ImGui::CloseCurrentPopup();
            }

            if (hasOpen)
            {
                ImGui::SameLine();
                if (ImGui::Button("Open", UiSize::DialogButton))
                {
                    OpenInExplorer(m_NoticeOpenPath);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }

    void AppMainWindow::RenderOutputPanel()
    {
        const ImGuiStyle& s = ImGui::GetStyle();

        ImGui::Separator();
        ImGui::TextDisabled("Output");

        const float labelW  = ImGui::CalcTextSize("Filename").x + s.ItemSpacing.x * 2;
        const float trailW  = UiSize::InterfaceButtonSmall.x + s.ItemSpacing.x;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Folder");
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-trailW);
        if (ImGui::InputText("##outdir", m_OutputDir, sizeof(m_OutputDir)))
        {
            SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse", UiSize::InterfaceButtonSmall))
        {
            BrowseFolder();
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Filename");
        ImGui::SameLine(labelW);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##outfile", m_OutputFile, sizeof(m_OutputFile));

        // Anchor the button row at the very bottom of the window so the
        // [Compose][Open] pair sits flush with the window edge.
        const float btnRowY = ImGui::GetWindowHeight() - s.WindowPadding.y - ImGui::GetFrameHeight();
        if (ImGui::GetCursorPosY() < btnRowY)
        {
            ImGui::SetCursorPosY(btnRowY);
        }

        // Resolve the would-be output file and check whether it already exists
        // on disk. Open lights up only when there is something to open.
        std::filesystem::path targetPath;
        bool targetExists = false;
        {
            std::string dir(m_OutputDir);
            std::string file(m_OutputFile);
            if (!dir.empty() && !file.empty())
            {
                targetPath = Utf8ToPath(dir) / Utf8ToPath(file);
                std::error_code ec;
                targetExists = std::filesystem::exists(targetPath, ec)
                            && !std::filesystem::is_directory(targetPath, ec);
            }
        }

        const float pairW = UiSize::InterfaceButtonMedium.x * 2.0f + s.ItemSpacing.x;
        Ui::CenterCursorX(pairW);

        if (ImGui::Button("Pack", UiSize::InterfaceButtonMedium))
        {
            Compose();
        }
        ImGui::SameLine();

        ImGui::BeginDisabled(!targetExists);
        if (ImGui::Button("Open", UiSize::InterfaceButtonMedium) && targetExists)
        {
            OpenInExplorer(targetPath.generic_string());
        }
        ImGui::EndDisabled();
    }

    void AppMainWindow::Compose()
    {
        std::string dir(m_OutputDir);
        std::string file(m_OutputFile);

        if (dir.empty() || file.empty())
        {
            ShowMessageDialog("Folder and filename are required.");
            return;
        }
        if (m_Rows.empty())
        {
            ShowMessageDialog("Timeline is empty. Drop PDF / PNG / JPG files first.");
            return;
        }

        // Build the output path as a UTF-8 string so it survives intact when
        // the CLI subprocess parses argv (CommandLineToArgvW + UTF-8 decode).
        // The CLI creates the parent directory itself if missing.
        std::string outPathUtf8 = dir;
        if (!outPathUtf8.empty()
            && outPathUtf8.back() != '/' && outPathUtf8.back() != '\\')
        {
            outPathUtf8 += '/';
        }
        outPathUtf8 += file;
        std::filesystem::path outPath = Utf8ToPath(outPathUtf8);

        // Same path CLI / AI agents take — fold the timeline into argv and spawn
        // self in compose mode. GUI never touches PDFium directly.
        std::vector<std::string> args = BuildComposeArgs(m_Rows, outPathUtf8);
        ChildResult cr = RunSelfCompose(args);

        if (!cr.launched)
        {
            ShowMessageDialog("Pack failed: " + cr.stderrText);
            return;
        }
        if (cr.exitCode != 0)
        {
            std::string msg = cr.stderrText.empty()
                ? "Pack failed (exit " + std::to_string(cr.exitCode) + ")."
                : cr.stderrText;
            // Trim a single trailing newline so the message dialog does not
            // grow an extra blank line.
            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r'))
            {
                msg.pop_back();
            }
            ShowMessageDialog(msg);
            return;
        }

        ShowMessageDialog(outPath.filename().generic_string() + " created.", "Confirm", outPath.generic_string());
    }
}
