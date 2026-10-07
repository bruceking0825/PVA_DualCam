#include "measurement_payload.hpp"
namespace pva
{
    std::vector<double> measurementPayloadValues(const MeasurementResult &r)
    {
        if (!r.valid) return {};
        const auto &a = r.data.cameras[0];
        const auto &b = r.data.cameras[1];
        if (r.data.hasDip) return {r.data.dipAverage};
        if (r.data.hasMelt)
            return {r.data.meltCount, a.average, a.maximum, a.minimum, b.average, b.maximum, b.minimum};
        if (r.data.hasDiameter)
            return {a.diameter, b.diameter, a.boundaryX, b.boundaryX,
                    a.average, a.maximum, a.minimum, b.average, b.maximum, b.minimum,
                    a.center.x, a.center.y, b.center.x, b.center.y};
        return {};
    }
}
