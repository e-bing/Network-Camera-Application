#pragma once

#include "IEptzController.hpp"

struct CropRect
{
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;
};

// Preconditions:
// - width and height are positive.
// - ptz values are already validated and finite.
//
// Postconditions:
// - All crop values are even.
// - left + right < width.
// - top + bottom < height.
CropRect calculate_crop(int width, int height, const PtzState &ptz);