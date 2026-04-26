#pragma once

#include "File/TimelineRow.h"

#include <filesystem>
#include <string>

namespace packpdf
{
    struct ComposeResult
    {
        bool        ok           = false;
        int         pagesWritten = 0;
        std::string errorMessage;
    };

    // UTF-8 → path via wide chars; std::string overload would mojibake CJK on Windows.
    std::filesystem::path Utf8ToPath(const std::string& utf8);

    // Write all rows into one output PDF. row paths are UTF-8.
    // PDFium inits lazily on first call, never destroyed.
    ComposeResult ComposeToFile(const TimelineContainer& rows, const std::filesystem::path& outputPath);

    // Image placement on its destination page in PDF points (Y-up).
    // Page is always A4 portrait; image is rotated, scaled down to fit, centered.
    struct ImagePageLayout
    {
        double pageW, pageH;       // page size in PDF points (A4 portrait)
        double imgX,  imgY;        // image bbox bottom-left in page coords
        double imgW,  imgH;        // image bbox displayed size (post-rotation)
        int    rotation;           // clockwise degrees (0 / 90 / 180 / 270)
    };

    // Shared by the compose pass and the GUI preview so both render identically.
    ImagePageLayout ComputeImagePageLayout(int pxW, int pxH, const ImageOptions& opts);

    // Page count of a PDF, or -1 on open / parse failure. Lazy-inits PDFium.
    int GetPdfPageCount(const std::filesystem::path& pdfPath);
}
