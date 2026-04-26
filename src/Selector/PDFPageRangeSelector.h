#pragma once

#include "File/TimelineRow.h"

namespace packpdf
{
    // Inline [Mode ▼] [first] - [last]. Int fields hide when mode == All.
    void PDFPageRangeSelector(PDFOptions& opts);

    // Row width with the int-pair slot reserved even when hidden.
    float PDFPageRangeSelectorWidth(const PDFOptions& opts);
}
