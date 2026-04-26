#pragma once

#include "File/TimelineRow.h"

namespace packpdf
{
    // Inline [Orientation ▼] [☐ Merge] [☐ Padding]. Merge slot reserved even
    // when hidden. mergeForcedByPrev shows checked+disabled without mutating opts.
    void ImageOptionsSelector(ImageOptions& opts, bool mergeForcedByPrev = false);

    // Constant row width so the surrounding layout reserves a stable column.
    float ImageOptionsSelectorWidth(const ImageOptions& opts);
}
