#pragma once

#include "Segment.h"

namespace packpdf
{
    // Inline [Orientation ▼] [☐ Merge] [☐ Padding]. Merge slot reserved even
    // when hidden. mergeForcedByPrev shows checked+disabled without mutating seg.
    void ImageOptionsSelector(Segment& seg, bool mergeForcedByPrev = false);

    // Constant row width so the surrounding layout reserves a stable column.
    float ImageOptionsSelectorWidth(const Segment& seg);
}
