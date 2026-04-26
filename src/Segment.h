#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace packpdf
{
    enum class FileExtension : uint8_t
    {
        PDF,
        JPEG,
        PNG
    };

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

    inline bool IsLandscapeMode(ImageOrientation o)
    {
        return o == ImageOrientation::Landscape;
    }

    // CCW degrees (multiple of 90) to rotate the image into the requested
    // orientation; reverse180 adds a 180° flip for head-down photos.
    inline int RotationCcwFor(int pxW, int pxH, ImageOrientation orient,
                              bool reverse180)
    {
        const bool naturallyPortrait = (pxH >= pxW);
        const bool wantPortrait      = (orient == ImageOrientation::Portrait);
        int rot = (naturallyPortrait != wantPortrait) ? 90 : 0;
        if (reverse180)
        {
            rot = (rot + 180) % 360;
        }
        return rot;
    }

    struct Segment
    {
        FileExtension    fileType = FileExtension::PDF;
        std::string      path;

        // PDF only
        PageSelection    pageSelection = PageSelection::All;
        int              rangeFirst    = 1;                   // 1-indexed, inclusive
        int              rangeLast     = 1;                   // 1-indexed, inclusive

        // Image only (JPEG / PNG)
        ImageOrientation orientation   = ImageOrientation::Portrait;
        bool             reverse180    = false;               // flip head-down photos
        ImageScaleMode   scaleMode     = ImageScaleMode::FitPage;
        bool             autoMerge     = false;               // Landscape only: pair with next
        bool             addPadding    = false;               // add white margin around content
    };

    using SegmentList = std::vector<Segment>;
}
