#include "Selector/ImageOptionsSelector.h"

#include "App/AppUI.h"
#include "File/ImageHelpers.h"

#include <imgui.h>

#include <cmath>

namespace packpdf
{
    namespace
    {
        constexpr const char* kOrientationLabels[] = { "Portrait", "Landscape" };
        constexpr const char* kScaleLabels[]       = { "Original size", "Fit page" };

        // Gear icon button. hasMark stamps a corner dot for non-default state.
        bool GearButton(const char* str_id, bool hasMark)
        {
            const float sz = ImGui::GetFrameHeight();
            return Ui::IconButton(str_id, ImVec2(sz, sz), [hasMark, sz](ImDrawList* dl, ImVec2 pos, ImVec2 size, ImU32 frameBg)
            {
                const ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);
                const float cx = pos.x + size.x * 0.5f;
                const float cy = pos.y + size.y * 0.5f;

                // 8 teeth + hub form one gear outline once the center hole is punched out.
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
                    dl->AddCircleFilled(ImVec2(pos.x + size.x - dotR - 1.0f, pos.y + dotR + 1.0f), dotR, ImGui::GetColorU32(ImGuiCol_CheckMark), 12);
                }
            });
        }

        // True when the row carries any image option different from its default state, so the gear picks up a "has settings" mark.
        bool HasNonDefaultOptions(const ImageOptions& opts, bool mergeForcedByPrev)
        {
            return opts.reverse180
                || opts.addPadding
                || opts.autoMerge
                || opts.scaleMode != ImageScaleMode::FitPage
                || mergeForcedByPrev;
        }
    }

    float ImageOptionsSelectorWidth(const ImageOptions& /*opts*/)
    {
        const ImGuiStyle& s = ImGui::GetStyle();
        return UiSize::RowDropdownWidth() + s.ItemInnerSpacing.x + ImGui::GetFrameHeight();
    }

    void ImageOptionsSelector(ImageOptions& opts, bool mergeForcedByPrev)
    {
        const ImGuiStyle& s = ImGui::GetStyle();

        int orientIdx = static_cast<int>(opts.orientation);
        ImGui::SetNextItemWidth(UiSize::RowDropdownWidth());
        if (ImGui::Combo("##iosorient", &orientIdx, kOrientationLabels, IM_ARRAYSIZE(kOrientationLabels)))
        {
            opts.orientation = static_cast<ImageOrientation>(orientIdx);
            if (!IsLandscapeMode(opts.orientation))
            {
                opts.autoMerge = false;
            }
        }

        ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
        if (GearButton("##iosgear", HasNonDefaultOptions(opts, mergeForcedByPrev)))
        {
            ImGui::OpenPopup("##ios_adv");
        }

        if (ImGui::BeginPopup("##ios_adv"))
        {
            ImGui::Checkbox("Reverse 180\xC2\xB0", &opts.reverse180);
            ImGui::Checkbox("Padding", &opts.addPadding);

            int scaleIdx = static_cast<int>(opts.scaleMode);
            const float scaleComboW = ImGui::CalcTextSize("Original size").x
                                    + ImGui::GetFrameHeight()
                                    + s.FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(scaleComboW);
            if (ImGui::Combo("Scale", &scaleIdx, kScaleLabels, IM_ARRAYSIZE(kScaleLabels)))
            {
                opts.scaleMode = static_cast<ImageScaleMode>(scaleIdx);
            }

            if (mergeForcedByPrev)
            {
                // Forced partner of the previous segment's merge by show check and disabled uncheck merge
                ImGui::BeginDisabled();
                bool checked = true;
                ImGui::Checkbox("Auto Merge", &checked);
                ImGui::EndDisabled();
            }
            else
            {
                const bool canMerge = IsLandscapeMode(opts.orientation);
                if (!canMerge)
                {
                    ImGui::BeginDisabled();
                }
                ImGui::Checkbox("Auto Merge", &opts.autoMerge);
                if (!canMerge)
                {
                    ImGui::EndDisabled();
                }
            }

            ImGui::EndPopup();
        }
    }
}
