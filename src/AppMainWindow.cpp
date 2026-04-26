#include "AppMainWindow.h"

#include "Composer.h"
#include "ImageOptionsSelector.h"
#include "PageRangeSelector.h"
#include "UiStyle.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

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
        // Returns nullopt for any extension PackPDF cannot process (e.g.
        // .mp4, .docx, no extension at all). Callers drop those silently
        // instead of letting them slip in as bogus PDF segments.
        std::optional<FileExtension> FileTypeFromExtension(const std::string& path)
        {
            std::filesystem::path p(path);
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext == ".pdf")
            {
                return FileExtension::PDF;
            }
            if (ext == ".jpg" || ext == ".jpeg")
            {
                return FileExtension::JPEG;
            }
            if (ext == ".png")
            {
                return FileExtension::PNG;
            }
            return std::nullopt;
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

        // Renders a two-section colored pill: dark number section on the left
        // (the page-number "overlay"), bright file-type section on the right.
        // Total width is fixed across all calls so the badge column lines up
        // across rows regardless of digit count or whether the row is a
        // merge partner. `pageNumber == 0` leaves the number slot empty
        // (used for merge-partner rows that inherit the leader's number).
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
            dl->AddRectFilled(pos, ImVec2(pos.x + totalW, pos.y + h),
                              typeBg, h * 0.25f);
            dl->AddRectFilled(pos, ImVec2(pos.x + numSecW, pos.y + h),
                              IM_COL32(0, 0, 0, 110), h * 0.25f,
                              ImDrawFlags_RoundCornersLeft);

            if (pageNumber > 0)
            {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%d", pageNumber);
                const ImVec2 nts = ImGui::CalcTextSize(buf);
                dl->AddText(ImVec2(pos.x + (numSecW - nts.x) * 0.5f,
                                   pos.y + (h - nts.y) * 0.5f),
                            IM_COL32_WHITE, buf);
            }

            const float typeX = pos.x + numSecW;
            const ImVec2 tts  = ImGui::CalcTextSize(typeLabel);
            dl->AddText(ImVec2(typeX + (typeSecW - tts.x) * 0.5f,
                               pos.y + (h - tts.y) * 0.5f),
                        IM_COL32_WHITE, typeLabel);

            ImGui::Dummy(ImVec2(totalW, h));
        }

        // Square close button. Picks up Button / ButtonHovered / ButtonActive
        // from the active theme as the background, then draws two diagonal
        // lines in the Text color forming an X. Returns true on click,
        // matching ImGui::Button's contract.
        bool XButton(const char* str_id, ImVec2 size)
        {
            const ImVec2 pos     = ImGui::GetCursorScreenPos();
            const bool   clicked = ImGui::InvisibleButton(str_id, size);
            const bool   hovered = ImGui::IsItemHovered();
            const bool   held    = ImGui::IsItemActive();

            const ImU32 bg = ImGui::GetColorU32(
                  held    ? ImGuiCol_ButtonActive
                : hovered ? ImGuiCol_ButtonHovered
                          : ImGuiCol_Button);
            const ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos,
                              ImVec2(pos.x + size.x, pos.y + size.y),
                              bg, ImGui::GetStyle().FrameRounding);

            // Inset the cross so the X reads as an icon rather than filling
            // the whole button face.
            const float pad   = std::min(size.x, size.y) * 0.30f;
            const float thick = std::max(1.0f, ImGui::GetFontSize() * 0.12f);
            dl->AddLine(ImVec2(pos.x + pad,           pos.y + pad           ),
                        ImVec2(pos.x + size.x - pad,  pos.y + size.y - pad  ),
                        fg, thick);
            dl->AddLine(ImVec2(pos.x + size.x - pad,  pos.y + pad           ),
                        ImVec2(pos.x + pad,           pos.y + size.y - pad  ),
                        fg, thick);

            return clicked;
        }

        bool FileExists(const char* path)
        {
            std::error_code ec;
            return std::filesystem::exists(path, ec);
        }

        // Extract the trailing filename from a UTF-8 path. Walks bytes
        // directly rather than going through std::filesystem::path so we
        // bypass the std::string → ANSI codepage conversion that would
        // mojibake CJK characters on Windows.
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

        // Renders the image preview tooltip for a JPG/PNG segment. Pulls the
        // exact same A4 page-placement math the actual PDF compose pass uses
        // (Composer::ComputeImagePageLayout), so the preview shows the
        // rendered page with whitespace / padding exactly where it will land
        // in the output file.
        void DrawHoverPreview(const ImageCache::Entry& entry, const Segment& seg)
        {
            if (entry.w <= 0 || entry.h <= 0)
            {
                return;
            }

            const ImagePageLayout L = ComputeImagePageLayout(entry.w, entry.h, seg);
            if (L.pageW <= 0.0 || L.pageH <= 0.0
                || L.imgW <= 0.0 || L.imgH <= 0.0) return;

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

            // UV mapping per CCW rotation; corners listed clockwise from
            // top-left (matching AddImageQuad's expected vertex order).
            ImVec2 uvA, uvB, uvC, uvD;
            switch (((L.rotationCcw % 360) + 360) % 360)
            {
                default:
                case 0:   uvA = {0,0}; uvB = {1,0}; uvC = {1,1}; uvD = {0,1}; break;
                case 90:  uvA = {1,0}; uvB = {1,1}; uvC = {0,1}; uvD = {0,0}; break;
                case 180: uvA = {1,1}; uvB = {0,1}; uvC = {0,0}; uvD = {1,0}; break;
                case 270: uvA = {0,1}; uvB = {0,0}; uvC = {1,0}; uvD = {1,1}; break;
            }

            ImGui::BeginTooltip();

            // Center both rows on the wider of (path text, page rect).
            const float pathW    = ImGui::CalcTextSize(seg.path.c_str()).x;
            const float contentW = std::max(pathW, pageW);
            const float baseX    = ImGui::GetCursorPosX();

            if (pathW < contentW)
            {
                ImGui::SetCursorPosX(baseX + (contentW - pathW) * 0.5f);
            }
            ImGui::TextUnformatted(seg.path.c_str());

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
            dl->AddImageQuad(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(entry.tex)),
                             a, b, c, d, uvA, uvB, uvC, uvD);

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
            int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                        static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                static_cast<int>(s.size()), w.data(), n);
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
            HRESULT hrInit = CoInitializeEx(nullptr,
                                            COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            IFileOpenDialog* dlg = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                           CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
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
                        if (SUCCEEDED(SHCreateItemFromParsingName(
                                wInit.c_str(), nullptr, IID_PPV_ARGS(&item))))
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
            HINSTANCE r = ShellExecuteW(nullptr, L"open", w.c_str(),
                                        nullptr, nullptr, SW_SHOWNORMAL);
            return reinterpret_cast<INT_PTR>(r) > 32;
        }
#else
        std::filesystem::path ConfigPath()
        {
            return std::filesystem::current_path() / "userdata" / "config.ini";
        }
        std::string DesktopPathUtf8() { return {}; }
        std::string PickFolderDialog(const std::string&) { return {}; }
        bool OpenInExplorer(const std::string&) { return false; }
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
            std::optional<FileExtension> ft = FileTypeFromExtension(paths[i]);
            if (!ft) continue; // unsupported extension, drop silently
            Segment s{};
            s.path = paths[i];
            s.fileType = *ft;
            m_Segments.push_back(std::move(s));
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

    void AppMainWindow::ShowMessageDialog(std::string msg,
                                          std::string buttonLabel,
                                          std::string openPath)
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
                m_Segments.clear();
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
        if (m_Segments.empty())
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
            const int last = static_cast<int>(m_Segments.size()) - 1;

            // Pre-pass: mark each segment that is the auto-merge partner of
            // the previous segment. Pairs consume two indices; the third
            // landscape (if any) starts a fresh group.
            std::vector<bool> mergeForced(m_Segments.size(), false);
            for (size_t k = 0; k < m_Segments.size(); )
            {
                const auto& cur = m_Segments[k];
                if (cur.fileType == FileExtension::JPEG
                    && IsLandscapeMode(cur.orientation)
                    && cur.autoMerge
                    && k + 1 < m_Segments.size())
                {
                    const auto& nxt = m_Segments[k + 1];
                    if (nxt.fileType == FileExtension::JPEG
                        && IsLandscapeMode(nxt.orientation))
                    {
                        mergeForced[k + 1] = true;
                        k += 2;
                        continue;
                    }
                }
                ++k;
            }

            // Pre-pass: starting output page number for each segment.
            // Auto-merge partners share the leader's page (no advance).
            std::vector<int> startPage(m_Segments.size(), 0);
            {
                int next = 1;
                for (size_t k = 0; k < m_Segments.size(); ++k)
                {
                    const auto& cur = m_Segments[k];
                    if (mergeForced[k] && k > 0)
                    {
                        startPage[k] = startPage[k - 1];  // no advance
                        continue;
                    }
                    startPage[k] = next;

                    int pages = 0;
                    if (cur.fileType == FileExtension::PDF)
                    {
                        auto it = m_PdfPageCounts.find(cur.path);
                        if (it == m_PdfPageCounts.end())
                        {
                            it = m_PdfPageCounts.emplace(
                                cur.path,
                                GetPdfPageCount(Utf8ToPath(cur.path))).first;
                        }
                        pages = PdfSelectedPageCount(it->second,
                                                     cur.pageSelection,
                                                     cur.rangeFirst,
                                                     cur.rangeLast);
                    }
                    else
                    {
                        pages = 1;  // image: one page per row (or per pair)
                    }
                    next += pages;
                }
            }

            for (size_t i = 0; i < m_Segments.size(); ++i)
            {
                const int idx = static_cast<int>(i);
                ImGui::PushID(idx);
                auto& seg = m_Segments[i];
                bool selected = (m_SelectedIndex == idx);

                // Page number is overlaid on the badge's dark left section.
                // Merge partners pass 0 → leader carries the number for the
                // pair, partner's slot stays blank.
                const int pageNum = mergeForced[i] ? 0 : startPage[i];
                DrawFileTypeIcon(seg.fileType, pageNum);
                ImGui::SameLine();

                // Per-file-type options sit between the path and the
                // up/down/remove buttons. Reserve the WIDER of the two
                // selector kinds for every row so the trailing column lines
                // up across PDF and image rows alike. Whichever selector is
                // narrower simply leaves whitespace inside the slot.
                const float selectorW = std::max(PageRangeSelectorWidth(seg),
                                                 ImageOptionsSelectorWidth(seg))
                                      + spacing;

                float availW = ImGui::GetContentRegionAvail().x - trailingW - selectorW;
                if (availW < 1.0f)
                {
                    availW = 1.0f;
                }
                const std::string filename = FilenameFromPath(seg.path);
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
                if (ImGui::Selectable(filename.c_str(), selected, 0,
                                      ImVec2(availW, rowH)))
                {
                    m_SelectedIndex = idx;
                }
                const bool pathHovered = ImGui::IsItemHovered();
                ImGui::PopStyleVar();

                if (pathHovered)
                {
                    if (seg.fileType == FileExtension::JPEG
                     || seg.fileType == FileExtension::PNG)
                    {
                        if (const auto* entry = m_ImageCache.Get(seg.path))
                        {
                            DrawHoverPreview(*entry, seg);
                        }
                        else
                        {
                            ImGui::SetTooltip("%s", seg.path.c_str());
                        }
                    }
                    else
                    {
                        ImGui::SetTooltip("%s", seg.path.c_str());
                    }
                }

                ImGui::SameLine();
                if (seg.fileType == FileExtension::PDF)
                {
                    PageRangeSelector(seg);
                }
                else
                {
                    ImageOptionsSelector(seg, mergeForced[i]);
                }

                // Force trailing buttons to a fixed right-anchored column,
                // regardless of how much the selector actually consumed.
                // trailingW reserves "leading gap + 3 buttons + 2 inter-gaps";
                // the visible trailing GROUP itself spans (trailingW - spacing).
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
                std::swap(m_Segments[moveUpIdx], m_Segments[moveUpIdx - 1]);
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
                std::swap(m_Segments[moveDownIdx], m_Segments[moveDownIdx + 1]);
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
                m_Segments.erase(m_Segments.begin() + removeIdx);
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
        if (m_Segments.empty())
        {
            ShowMessageDialog("Timeline is empty. Drop PDF / PNG / JPG files first.");
            return;
        }

        // Go through Utf8ToPath so non-ASCII output dirs / filenames survive
        // the std::string → std::filesystem::path conversion on Windows
        // (which would otherwise be interpreted via the ANSI code page).
        std::filesystem::path dirPath = Utf8ToPath(dir);

        std::error_code ec;
        if (!std::filesystem::exists(dirPath, ec))
        {
            std::filesystem::create_directories(dirPath, ec);
            if (ec)
            {
                ShowMessageDialog("Cannot create folder: " + ec.message());
                return;
            }
        }
        else if (!std::filesystem::is_directory(dirPath, ec))
        {
            ShowMessageDialog("Path exists but is not a folder.");
            return;
        }

        std::filesystem::path outPath = dirPath / Utf8ToPath(file);

        ComposeResult result = ComposeToFile(m_Segments, outPath);
        if (result.ok)
        {
            ShowMessageDialog(outPath.filename().generic_string() + " created.",
                              "Confirm",
                              outPath.generic_string());
        }
        else
        {
            ShowMessageDialog("Compose failed: " + result.errorMessage);
        }
    }
}
