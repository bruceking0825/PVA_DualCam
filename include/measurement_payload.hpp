#pragma once
#include "models.hpp"
namespace pva
{
    // 仅协议适配层决定字段顺序；数值单位保持历史协议语义。
    std::vector<double> measurementPayloadValues(const MeasurementResult &result);
}
