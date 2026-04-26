#include "Selector/PDFPageRangeSelector.h"

#include "App/AppUI.h"

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

    float PDFPageRangeSelectorWidth(const PDFOptions& /*opts*/)
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

    void PDFPageRangeSelector(PDFOptions& opts)
    {
        const ImGuiStyle& s = ImGui::GetStyle();

        int modeIdx = static_cast<int>(opts.pageSelection);
        ImGui::SetNextItemWidth(UiSize::RowDropdownWidth());
        if (ImGui::Combo("##prsmode", &modeIdx, kModeLabels, IM_ARRAYSIZE(kModeLabels)))
        {
            opts.pageSelection = static_cast<PageSelection>(modeIdx);
        }

        const float intW   = IntFieldWidth();
        const float frameH = ImGui::GetFrameHeight();

        if (opts.pageSelection == PageSelection::All)
        {
            // Pad to the same width Range/Exclude consume so the trailing
            // button column lines up across rows regardless of mode.
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
        ImGui::InputInt("##prsfirst", &opts.rangeFirst, 0, 0);
        if (opts.rangeFirst < 1)
        {
            opts.rangeFirst = 1;
        }

        ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("-");

        ImGui::SameLine(0.0f, s.ItemInnerSpacing.x);
        ImGui::SetNextItemWidth(intW);
        ImGui::InputInt("##prslast", &opts.rangeLast, 0, 0);
        if (opts.rangeLast < opts.rangeFirst)
        {
            opts.rangeLast = opts.rangeFirst;
        }
    }
}
