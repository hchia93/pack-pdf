#include "ImageOptionsSelector.h"

#include "UiStyle.h"

#include <imgui.h>

#include <cmath>

namespace packpdf
{
    namespace
    {
        constexpr const char* kOrientationLabels[] = { "Portrait", "Landscape" };
        constexpr const char* kScaleLabels[]       = { "Original size", "Fit page" };

        // Self-drawn 8-tooth gear icon button. Same Button/Hovered/Active
        // background contract as XButton in AppMainWindow.cpp; if hasMark is
        // true a small accent dot is stamped on the top-right corner so the
        // user knows the popup carries non-default settings without opening it.
        bool GearButton(const char* str_id, bool hasMark)
        {
            const float  sz   = ImGui::GetFrameHeight();
            const ImVec2 size(sz, sz);
            const ImVec2 pos  = ImGui::GetCursorScreenPos();

            const bool clicked = ImGui::InvisibleButton(str_id, size);
            const bool hovered = ImGui::IsItemHovered();
            const bool held    = ImGui::IsItemActive();

            const ImU32 frameBg = ImGui::GetColorU32(
                  held    ? ImGuiCol_ButtonActive
                : hovered ? ImGuiCol_ButtonHovered
                          : ImGuiCol_Button);
            const ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos,
                              ImVec2(pos.x + size.x, pos.y + size.y),
                              frameBg, ImGui::GetStyle().FrameRounding);

            const float cx = pos.x + size.x * 0.5f;
            const float cy = pos.y + size.y * 0.5f;

            // 8 teeth radiating from a hub. Each tooth is a tangential quad
            // straddling the perimeter; teeth + hub merge into a single gear
            // outline once the central hole is punched out below.
            const float bodyR    = sz * 0.26f;
            const float toothIn  = bodyR;
            const float toothOut = bodyR + sz * 0.10f;
            const float halfW    = sz * 0.06f;
            for (int i = 0; i < 8; ++i)
            {
                const float ang = i * (3.14159265f / 4.0f);
                const float c   = std::cos(ang);
                const float s   = std::sin(ang);
                const float tx  = -s; // perpendicular unit vector
                const float ty  =  c;
                const ImVec2 q[4] = {
                    { cx + c * toothIn  + tx * halfW, cy + s * toothIn  + ty * halfW },
                    { cx + c * toothOut + tx * halfW, cy + s * toothOut + ty * halfW },
                    { cx + c * toothOut - tx * halfW, cy + s * toothOut - ty * halfW },
                    { cx + c * toothIn  - tx * halfW, cy + s * toothIn  - ty * halfW },
                };
                dl->AddConvexPolyFilled(q, 4, fg);
            }
            dl->AddCircleFilled(ImVec2(cx, cy), bodyR, fg, 24);
            dl->AddCircleFilled(ImVec2(cx, cy), sz * 0.10f, frameBg, 12);

            if (hasMark)
            {
                const float dotR = sz * 0.11f;
                dl->AddCircleFilled(ImVec2(pos.x + size.x - dotR - 1.0f,
                                           pos.y + dotR + 1.0f),
                                    dotR,
                                    ImGui::GetColorU32(ImGuiCol_CheckMark), 12);
            }

            return clicked;
        }

        // True when the segment carries any image option different from its
        // default state, so the gear picks up a "has settings" mark.
        bool HasNonDefaultOptions(const Segment& seg, bool mergeForcedByPrev)
        {
            return seg.reverse180
                || seg.addPadding
                || seg.autoMerge
                || seg.scaleMode != ImageScaleMode::FitPage
                || mergeForcedByPrev;
        }
    }

    float ImageOptionsSelectorWidth(const Segment& /*seg*/)
    {
        const ImGuiStyle& s = ImGui::GetStyle();
        return UiSize::RowDropdownWidth()
             + s.ItemInnerSpacing.x + ImGui::GetFrameHeight();
    }

    void ImageOptionsSelector(Segment& seg, bool mergeForcedByPrev)
    {
        const ImGuiStyle& s = ImGui::GetStyle();

        int orientIdx = static_cast<int>(seg.orientation);
        ImGui::SetNextItemWidth(UiSize::RowDropdownWidth());
        if (ImGui::Combo("##iosorient", &orientIdx,
                         kOrientationLabels, IM_ARRAYSIZE(kOrientationLabels)))
        {
            seg.orientation = static_cast<ImageOrientation>(orientIdx);
            // AutoMerge only makes sense in landscape; clear it when leaving
            // so persisted state stays consistent.
            if (!IsLandscapeMode(seg.orientation))
            {
                seg.autoMerge = false;
            }
        }

        ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
        if (GearButton("##iosgear", HasNonDefaultOptions(seg, mergeForcedByPrev)))
        {
            ImGui::OpenPopup("##ios_adv");
        }

        if (ImGui::BeginPopup("##ios_adv"))
        {
            ImGui::Checkbox("Reverse 180\xC2\xB0", &seg.reverse180);
            ImGui::Checkbox("Padding", &seg.addPadding);

            int scaleIdx = static_cast<int>(seg.scaleMode);
            ImGui::SetNextItemWidth(
                ImGui::CalcTextSize("Original size").x
                + ImGui::GetFrameHeight()
                + s.FramePadding.x * 2.0f);
            if (ImGui::Combo("Scale", &scaleIdx,
                             kScaleLabels, IM_ARRAYSIZE(kScaleLabels)))
                seg.scaleMode = static_cast<ImageScaleMode>(scaleIdx);

            if (mergeForcedByPrev)
            {
                // Forced partner of the previous segment's merge: shown
                // checked + disabled, but do not touch seg.autoMerge so the
                // real value returns if the pairing breaks.
                ImGui::BeginDisabled();
                bool checked = true;
                ImGui::Checkbox("Auto Merge", &checked);
                ImGui::EndDisabled();
            }
            else
            {
                const bool canMerge = IsLandscapeMode(seg.orientation);
                if (!canMerge)
                {
                    ImGui::BeginDisabled();
                }
                ImGui::Checkbox("Auto Merge", &seg.autoMerge);
                if (!canMerge)
                {
                    ImGui::EndDisabled();
                }
            }

            ImGui::EndPopup();
        }
    }
}
