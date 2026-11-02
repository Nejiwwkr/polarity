#include "CurveModel.h"

#include <algorithm>
#include <cmath>

namespace polarity::dsp
{

CurveModel::CurveModel()
{
    setPoints ({ { minFreq, 0.0 }, { maxFreq, 0.0 } });
}

CurveModel::CurveModel (std::vector<CurvePoint> pointsIn)
{
    setPoints (std::move (pointsIn));
}

void CurveModel::setPoints (std::vector<CurvePoint> pointsIn)
{
    std::vector<CurvePoint> normalised;
    normalised.reserve (pointsIn.size() + 2);

    for (auto point : pointsIn)
    {
        point.freqHz = std::clamp (point.freqHz, minFreq, maxFreq);
        point.amount = std::clamp (point.amount, 0.0, maxAmount);
        normalised.push_back (point);
    }

    std::sort (normalised.begin(), normalised.end(),
               [] (const CurvePoint& a, const CurvePoint& b)
               {
                   return a.freqHz < b.freqHz;
               });

    std::vector<CurvePoint> deduped;
    deduped.reserve (normalised.size());

    for (const auto& point : normalised)
    {
        if (! deduped.empty()
            && std::abs (point.freqHz - deduped.back().freqHz) < 1.0e-6)
            deduped.back() = point;
        else
            deduped.push_back (point);
    }

    if (deduped.empty() || deduped.front().freqHz > minFreq)
        deduped.insert (deduped.begin(), { minFreq, 0.0 });
    else
        deduped.front().amount = 0.0;

    if (deduped.back().freqHz < maxFreq)
        deduped.push_back ({ maxFreq, 0.0 });
    else
        deduped.back().amount = 0.0;

    if (deduped.size() < 2)
        deduped.push_back ({ maxFreq, 0.0 });

    points = std::move (deduped);
    rebuild();
}

double CurveModel::amountAt (double freqHz) const noexcept
{
    const auto n = points.size();

    if (n == 0)
        return 0.0;
    if (n == 1)
        return points.front().amount;

    const double x = std::log10 (std::clamp (freqHz, minFreq, maxFreq));

    if (x <= logFreqs.front())
        return points.front().amount;
    if (x >= logFreqs.back())
        return points.back().amount;

    const auto upper = std::upper_bound (logFreqs.begin(), logFreqs.end(), x);
    const auto index = static_cast<std::size_t> (upper - logFreqs.begin() - 1);

    const double x0 = logFreqs[index];
    const double x1 = logFreqs[index + 1];
    const double h = x1 - x0;

    if (h <= 0.0)
        return points[index].amount;

    const double t = (x - x0) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;

    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;

    const double y = h00 * points[index].amount
                   + h10 * h * tangents[index]
                   + h01 * points[index + 1].amount
                   + h11 * h * tangents[index + 1];

    return std::clamp (y, 0.0, maxAmount);
}

void CurveModel::rebuild()
{
    logFreqs.clear();
    logFreqs.reserve (points.size());

    for (const auto& point : points)
        logFreqs.push_back (std::log10 (point.freqHz));

    rebuildTangents();
}

void CurveModel::rebuildTangents()
{
    const auto n = points.size();
    tangents.assign (n, 0.0);

    if (n < 2)
        return;

    std::vector<double> interval (n - 1);
    std::vector<double> slope (n - 1);

    for (std::size_t k = 0; k + 1 < n; ++k)
    {
        interval[k] = logFreqs[k + 1] - logFreqs[k];
        slope[k] = interval[k] > 0.0
                       ? (points[k + 1].amount - points[k].amount) / interval[k]
                       : 0.0;
    }

    if (n == 2)
    {
        tangents[0] = tangents[1] = slope[0];
        return;
    }

    tangents.front() = slope.front();
    tangents.back() = slope.back();

    for (std::size_t k = 1; k + 1 < n; ++k)
    {
        const double d0 = slope[k - 1];
        const double d1 = slope[k];

        if (d0 * d1 <= 0.0)
        {
            tangents[k] = 0.0;
            continue;
        }

        const double h0 = interval[k - 1];
        const double h1 = interval[k];
        const double w1 = 2.0 * h1 + h0;
        const double w2 = h1 + 2.0 * h0;

        tangents[k] = (w1 + w2) / (w1 / d0 + w2 / d1);
    }
}

} // namespace polarity::dsp
