#pragma once

// Valid range is inclusive.
// - pan/tilt: [-1.0, 1.0]
// - zoom: [1.0, 2.0]
// NaN and infinity are rejected as InvalidArgument.

// 2.0x is the maximum zoom without upscaling:
// 1280x720 capture -> minimum 640x360 crop -> 640x360 output.
inline constexpr double kPanTiltMin = -1.0;
inline constexpr double kPanTiltMax = 1.0;

inline constexpr double kZoomMin = 1.0;
inline constexpr double kZoomMax = 2.0;

enum class PtzResult
{
    Ok,
    InvalidArgument
};

struct PtzState
{
    double pan = 0.0;
    double tilt = 0.0;
    double zoom = 1.0;
};

class IEptzController
{
public:
    virtual ~IEptzController() = default;

    // setPtz:
    // - Absolute position setting.
    // - On validation failure, the current state is not modified.
    // - Returns Ok even if the crop element is not attached yet.
    //   The requested state is applied when attach() is called.
    // - getPtz() returns the last accepted request, not the actual crop.
    //   The actual crop may differ slightly due to even-value adjustment.
    [[nodiscard]] virtual PtzResult setPtz(const PtzState &) = 0;
    virtual PtzState getPtz() const = 0;
};