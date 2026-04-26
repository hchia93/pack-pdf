#include "PageRangeSelector.h"

#include "UiStyle.h"

#include <imgui.h>

namespace packpdf
{
    namespace
    {
        constexpr const char* kModeLabels[] = { "All", "Range", "Exclude" };

        float IntFieldWidth()
        {
            const ImGuiStyle& s = ImGui::GetStyle();
            return ImGui::CalcTextSize("0000").x + s.FramePadding.x * 2.0f;
        }

        float DashWidth()
        {
            return ImGui::CalcTextSize("-").x;
        }
    }

    float PageRangeSelectorWidth(const Segment& /*seg*/)
    {
        // Reserve room for the int pair on every row so the combo column
        // lines up across rows even when the current mode is All.
        const float gap        = ImGui::GetStyle().ItemInnerSpacing.x;
        const float comboW     = UiSize::RowDropdownWidth();
        const float intFieldW  = IntFieldWidth();
        const float dashW      = DashWidth();
        const float rangeBlock = intFieldW + gap + dashW + gap + intFieldW;
        return comboW + gap + rangeBlock;
    }

    void PageRangeSelector(Segment& seg)
    {
        const ImGuiStyle& s = ImGui::GetStyle();

        int modeIdx = static_cast<int>(seg.pageSelection);
        ImGui::SetNextItemWidth(UiSize::RowDropdownWidth());
        if (ImGui::Combo("##prsmode", &modeIdx, kModeLabels, IM_ARRAYSIZE(kModeLabels)))
        {
            seg.pageSelection = static_cast<PageSelection>(modeIdx);
        }

        const float intW   = IntFieldWidth();
        const float frameH = ImGui::GetFrameHeight();

        if (seg.pageSelection == PageSelection::All)
        {
            // Pad to the same horizontal extent Range/Exclude consume so the
            // trailing button column lines up across rows regardless of the
            // current mode. Same pattern as ImageOptionsSelector uses to
            // reserve the "Merge" slot.
            ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
            ImGui::Dummy(ImVec2(intW, frameH));
            ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
            ImGui::Dummy(ImVec2(DashWidth(), frameH));
            ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
            ImGui::Dummy(ImVec2(intW, frameH));
            return;
        }

        ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(intW);
        ImGui::InputInt("##prsfirst", &seg.rangeFirst, 0, 0);
        if (seg.rangeFirst < 1)
        {
            seg.rangeFirst = 1;
        }

        ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("-");

        ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(intW);
        ImGui::InputInt("##prslast", &seg.rangeLast, 0, 0);
        if (seg.rangeLast < seg.rangeFirst)
        {
            seg.rangeLast = seg.rangeFirst;
        }
    }
}
