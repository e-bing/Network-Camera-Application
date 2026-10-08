#pragma once

#include "IEptzController.hpp"

struct CropRect
{
    int left;
    int right;
    int top;
    int bottom;
};

class EptzController : public IEptzController
{
    // ...

private:
    static CropRect calculate_crop(
        int width,
        int height,
        const PtzState &ptz);
};