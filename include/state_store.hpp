#pragma once

#include "models.hpp"
#include <QString>

namespace pva
{
    class StateStore
    {
    public:
        explicit StateStore(QString path) : path_(std::move(path)) {}
        [[nodiscard]] MeasurementState load(QString *warning = nullptr) const;
        bool save(const MeasurementState &state, QString *error = nullptr) const;

    private:
        QString path_;
    };
}
