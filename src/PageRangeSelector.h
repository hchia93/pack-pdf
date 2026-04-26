#pragma once

#include "Segment.h"

namespace packpdf
{
    // Inline [Mode ▼] [first] - [last]. Int fields hide when mode == All.
    void PageRangeSelector(Segment& seg);

    // Row width with the int-pair slot reserved even when hidden.
    float PageRangeSelectorWidth(const Segment& seg);
}
