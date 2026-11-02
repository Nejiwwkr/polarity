#pragma once

#include <vector>

namespace polarity::dsp
{

struct CurvePoint
{
    double freqHz = 20.0;
    double amount = 0.0;
};

/**
    A per-channel inversion-amount curve A(f) in [0, 2], interpolated with
    PCHIP in log10(frequency) so the curve never overshoots its control
    points.  The 20 Hz and 20 kHz endpoints are pinned to zero.
*/
class CurveModel
{
public:
    static constexpr double minFreq = 20.0;
    static constexpr double maxFreq = 20000.0;
    static constexpr double maxAmount = 2.0;

    CurveModel();
    explicit CurveModel (std::vector<CurvePoint> points);

    void setPoints (std::vector<CurvePoint> points);
    const std::vector<CurvePoint>& getPoints() const noexcept { return points; }

    /** Returns A(f), clamped to [0, 2]. */
    double amountAt (double freqHz) const noexcept;

private:
    std::vector<CurvePoint> points;
    std::vector<double> logFreqs;
    std::vector<double> tangents;

    void rebuild();
    void rebuildTangents();
};

} // namespace polarity::dsp
