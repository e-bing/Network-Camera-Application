#pragma once

#include "IEptzController.hpp"

#include <mutex>

struct _GstElement;
using GstElement = struct _GstElement;

class EptzController final : public IEptzController
{
public:
    EptzController(int width, int height);
    ~EptzController() override;

    EptzController(const EptzController &) = delete;
    EptzController &operator=(const EptzController &) = delete;

    [[nodiscard]] PtzResult setPtz(const PtzState &ptz) override;
    PtzState getPtz() const override;

    void attach(GstElement *crop);
    void detach();

private:
    void applyLocked();

    const int width_;
    const int height_;

    mutable std::mutex mutex_;
    PtzState state_;
    GstElement *crop_ = nullptr;
};