#pragma once

#include "measurement_geometry.hpp"
#include <QHash>
#include <QString>
#include <array>
#include <optional>
#include <utility>

namespace pva
{
    struct PlcRuntimeState
    {
        MeasurementStage stage{MeasurementStage::Melt};
        bool pointFitSelected{false};
        bool acquisitionEnabled{true};
        bool relativeThreshold{false};
        double refreshRate{1.0};
        QHash<QString, std::vector<double>> parameters;
    };

    class PlcRuntimeStore
    {
    public:
        explicit PlcRuntimeStore(QString path) : path_(std::move(path)) {}
        bool load(PlcRuntimeState *state, QString *error = nullptr) const;
        bool save(const PlcRuntimeState &state, QString *error = nullptr) const;
    private:
        QString path_;
    };

    bool setPlcRoi(MeasurementRois &rois, const QString &name,
                   const std::vector<double> &values, QString *error = nullptr);
    MeasurementStage diameterStage(bool pointFitSelected, const MeasurementRois &rois,
                                   double bodyRadiusThreshold);
    MeasurementStage stageForPlcCommand(const QString &name, MeasurementStage current,
                                        bool pointFitSelected, const MeasurementRois &rois,
                                        double bodyRadiusThreshold);
    void appendPlcRoiOverlays(const MeasurementRois &rois, MeasurementStage stage,
                              double diameterRectHeight,
                              std::vector<OverlayElement> &camera1,
                              std::vector<OverlayElement> &camera2);


}
