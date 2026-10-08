#include "EptzController.hpp"

#include <gst/gst.h>

#include <cmath>

EptzController::EptzController(int width, int height)
    : width_(width),
      height_(height)
{
}

EptzController::~EptzController()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (crop_ != nullptr)
    {
        gst_object_unref(crop_);
        crop_ = nullptr;
    }
}

PtzResult EptzController::setPtz(const PtzState &ptz)
{
    if (!std::isfinite(ptz.pan) ||
        !std::isfinite(ptz.tilt) ||
        !std::isfinite(ptz.zoom))
    {
        return PtzResult::InvalidArgument;
    }

    if (ptz.pan < kPanTiltMin || ptz.pan > kPanTiltMax ||
        ptz.tilt < kPanTiltMin || ptz.tilt > kPanTiltMax ||
        ptz.zoom < kZoomMin || ptz.zoom > kZoomMax)
    {
        return PtzResult::InvalidArgument;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    state_ = ptz;
    applyLocked();

    return PtzResult::Ok;
}

PtzState EptzController::getPtz() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void EptzController::attach(GstElement *crop)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (crop_ != nullptr)
    {
        gst_object_unref(crop_);
        crop_ = nullptr;
    }

    if (crop == nullptr)
        return;

    crop_ = GST_ELEMENT(gst_object_ref(crop));
    applied_ = {};

    applyLocked();
}

void EptzController::detach()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (crop_ != nullptr)
    {
        gst_object_unref(crop_);
        crop_ = nullptr;
        applied_ = {};
    }
}

void EptzController::applyLocked()
{
    if (crop_ == nullptr)
        return;

    const CropRect rect = calculate_crop(width_, height_, state_);

    const bool left_first =
        (rect.left + applied_.right) <=
        (applied_.left + rect.right);

    if (left_first)
    {
        g_object_set(crop_, "left", rect.left, nullptr);
        g_object_set(crop_, "right", rect.right, nullptr);
    }
    else
    {
        g_object_set(crop_, "right", rect.right, nullptr);
        g_object_set(crop_, "left", rect.left, nullptr);
    }

    const bool top_first =
        (rect.top + applied_.bottom) <=
        (applied_.top + rect.bottom);

    if (top_first)
    {
        g_object_set(crop_, "top", rect.top, nullptr);
        g_object_set(crop_, "bottom", rect.bottom, nullptr);
    }
    else
    {
        g_object_set(crop_, "bottom", rect.bottom, nullptr);
        g_object_set(crop_, "top", rect.top, nullptr);
    }

    applied_ = rect;
}