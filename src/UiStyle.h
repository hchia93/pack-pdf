#pragma once

#include <imgui.h>

#include <algorithm>

namespace packpdf
{
    // Standardized button widths so the toolbar reads as one coherent set.
    // Height = 0 → ImGui resolves to GetFrameHeight() at draw time.
    namespace UiSize
    {
        // Auxiliary toolbar buttons (Browse, Open-folder, Clear).
        inline const ImVec2 InterfaceButtonSmall  = ImVec2( 64.0f, 0.0f);

        // Primary action buttons (Compose, Output's Open).
        inline const ImVec2 InterfaceButtonMedium = ImVec2( 96.0f, 0.0f);

        // Buttons inside modal dialogs (Notice popup OK / Open).
        inline const ImVec2 DialogButton          = ImVec2(108.0f, 0.0f);

        // Per-row dropdown width, shared by PDF and image selectors so the
        // column lines up. Fits the widest label across both selectors.
        inline float RowDropdownWidth()
        {
            const ImGuiStyle& s = ImGui::GetStyle();
            const float widest = std::max(
                ImGui::CalcTextSize("Landscape").x, // image orientations
                ImGui::CalcTextSize("Exclude").x     // PDF page modes
            );
            return widest + ImGui::GetFrameHeight() + s.FramePadding.x * 2.0f;
        }
    }

    // ImGui boilerplate wrappers — named call > raw math at the use site.
    namespace Ui
    {
        // Center the next widget of width `itemW` in the current window.
        inline void CenterCursorX(float itemW)
        {
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - itemW) * 0.5f);
        }

        // Right-align the next widget of width `itemW` to the content edge.
        inline void RightAlignCursorX(float itemW)
        {
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - itemW);
        }

        // ArrowButton when visible, else FrameHeight² Dummy so layout is stable.
        inline bool OptionalArrowButton(const char* id, ImGuiDir dir, bool visible)
        {
            if (visible)
            {
                return ImGui::ArrowButton(id, dir);
            }
            const float h = ImGui::GetFrameHeight();
            ImGui::Dummy(ImVec2(h, h));
            return false;
        }
    }
}
