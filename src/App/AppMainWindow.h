#pragma once

#include <string>
#include <unordered_map>

#include "App/AppTheme.h"
#include "File/ImageCache.h"
#include "File/TimelineRow.h"

namespace packpdf
{
    class AppMainWindow
    {
    public:
        AppMainWindow();

        void Render();
        void ApplyImGuiStyle();

        // Must be called after ImGui::CreateContext() and before the first NewFrame().
        static void LoadFonts();

        // Drag-drop entry from GLFW callback.
        void OnFilesDropped(const char** paths, int count);

    private:
        void RenderOutputPanel();
        void RenderNoticePopup();
        void Compose();
        void BrowseFolder();
        void LoadConfig();
        void SaveConfig();
        void ShowMessageDialog(std::string msg, std::string buttonLabel = "OK", std::string openPath = {});

        TimelineContainer m_Rows;
        int               m_SelectedIndex = -1;

        char        m_OutputDir[1024]  = {};
        char        m_OutputFile[256]  = {};

        std::string m_NoticeMessage;
        std::string m_NoticeButton     = "OK";
        std::string m_NoticeOpenPath;          // empty → no Open button
        bool        m_NoticeRequested  = false;

        ThemeId     m_Theme            = ThemeId::PhotoshopDark;

        ImageCache  m_ImageCache;

        // PDF page-count cache so the timeline number column doesn't re-parse every frame.
        std::unordered_map<std::string, int> m_PdfPageCounts;
    };
}
