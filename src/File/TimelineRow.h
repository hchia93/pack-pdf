#pragma once

#include "File/FileOptionsTypes.h"
#include "File/FileTypes.h"

#include <string>
#include <variant>
#include <vector>

namespace packpdf
{
    struct PDFOptions
    {
        PageSelection pageSelection = PageSelection::All;
        int           rangeFirst    = 1;   // 1-indexed, inclusive
        int           rangeLast     = 1;   // 1-indexed, inclusive
    };

    struct ImageOptions
    {
        ImageFormat      format      = ImageFormat::JPEG;        // decode hint
        ImageOrientation orientation = ImageOrientation::Portrait;
        bool             reverse180  = false;                    // flip head-down photos
        ImageScaleMode   scaleMode   = ImageScaleMode::FitPage;
        bool             autoMerge   = false;                    // landscape only: pair with next
        bool             addPadding  = false;                    // 0.5 inch white margin
    };

    // sum-type of the only two row kinds the program understands. Compose, CLI
    // and Selectors all consume the variant directly via std::visit / get_if,
    // so an ImageOptions can never be passed where PDFOptions is expected.
    struct TimelineRow
    {
        std::string                            path;
        std::variant<PDFOptions, ImageOptions> options;
    };

    using TimelineContainer = std::vector<TimelineRow>;

    inline bool IsPDF(const TimelineRow& r)
    {
        return std::holds_alternative<PDFOptions>(r.options);
    }
    inline bool IsImage(const TimelineRow& r)
    {
        return std::holds_alternative<ImageOptions>(r.options);
    }
}
