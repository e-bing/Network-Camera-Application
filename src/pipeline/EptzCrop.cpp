#include "EptzCrop.hpp"

#include <algorithm>
#include <cmath>

CropRect calculate_crop(int width, int height, const PtzState &ptz)
{
    const int crop_width = static_cast<int>(std::floor(width / ptz.zoom)) & ~1;
    const int crop_height = static_cast<int>(std::floor(height / ptz.zoom)) & ~1;

    const int max_left = width - crop_width;
    const int max_top = height - crop_height;

    int left = static_cast<int>(std::round((ptz.pan + 1.0) * 0.5 * max_left)) & ~1;
    int top = static_cast<int>(std::round((ptz.tilt + 1.0) * 0.5 * max_top)) & ~1;

    CropRect rect;

    rect.left = std::max(0, std::min(left, max_left));
    rect.right = max_left - rect.left;

    rect.top = std::max(0, std::min(top, max_top));
    rect.bottom = max_top - rect.top;

    return rect;
}