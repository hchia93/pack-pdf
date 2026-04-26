#pragma once

#include "File/FileOptionsTypes.h"

namespace packpdf
{
    inline bool IsLandscapeMode(ImageOrientation o)
    {
        return o == ImageOrientation::Landscape;
    }

    // Clockwise degrees (0/90/180/270) to rotate the image into the requested
    // orientation; reverse180 adds a 180° flip for head-down photos.
    inline int RotationFor(int pxW, int pxH, ImageOrientation orient, bool reverse180)
    {
        const bool naturallyPortrait = (pxH >= pxW);
        const bool wantPortrait      = (orient == ImageOrientation::Portrait);
        int rot = (naturallyPortrait != wantPortrait) ? 270 : 0;
        if (reverse180)
        {
            rot = (rot + 180) % 360;
        }
        return rot;
    }
}
