#pragma once

#include <cstdint>

namespace packpdf
{
    enum class PageSelection : uint8_t
    {
        All,     // every page
        Range,   // keep [first..last] (1-indexed, inclusive)
        Exclude  // drop [first..last], keep the rest
    };

    enum class ImageOrientation : uint8_t
    {
        Portrait,   // image rotated to portrait (default)
        Landscape   // image rotated to landscape
    };

    enum class ImageScaleMode : uint8_t
    {
        Original,   // small images keep native size; A4 padding is the surplus
        FitPage     // small images scale up to fill the page minus padding
    };
}
