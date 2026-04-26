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
            const float widest = std::max(ImGui::CalcTextSize("Landscape").x, ImGui::CalcTextSize("Exclude").x);
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

        // ImGui's default Button bg color, picked by interaction priority.
        inline ImU32 ButtonBgColor(bool isHeld, bool isHovered)
        {
            return ImGui::GetColorU32(isHeld ? ImGuiCol_ButtonActive : isHovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        }

        // Self-drawn button shell. drawIcon(dl, pos, size, frameBg) paints
        // the foreground; frameBg is forwarded so it can carve inner shapes.
        template <typename DrawIcon>
        bool IconButton(const char* str_id, ImVec2 size, DrawIcon&& drawIcon)
        {
            const ImVec2 pos       = ImGui::GetCursorScreenPos();
            const bool   isClicked = ImGui::InvisibleButton(str_id, size);
            const bool   isHovered = ImGui::IsItemHovered();
            const bool   isHeld    = ImGui::IsItemActive();
            const ImU32  frameBg   = ButtonBgColor(isHeld, isHovered);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 br(pos.x + size.x, pos.y + size.y);
            dl->AddRectFilled(pos, br, frameBg, ImGui::GetStyle().FrameRounding);
            drawIcon(dl, pos, size, frameBg);
            return isClicked;
        }
    }
}
