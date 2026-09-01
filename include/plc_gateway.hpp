#pragma once
#include "models.hpp"
#include <string>

namespace pva
{
    class PlcGateway
    {
    public:
        virtual ~PlcGateway() = default;
        virtual bool connect(std::string &error) = 0;
        virtual MeasurementStage readStage(std::string &error) = 0;
        virtual bool publish(const MeasurementResult &result, std::string &error) = 0;
        virtual void disconnect() = 0;
    };
} // namespace pva
