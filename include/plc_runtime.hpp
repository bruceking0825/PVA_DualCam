#pragma once

#include "models.hpp"
#include <QHash>
#include <QString>
#include <array>
#include <optional>
#include <utility>

namespace pva
{
    struct PlcRois
    {
        // PLC 参数已除以 100，坐标均对应当前视图中的原始图像像素。
        std::optional<std::array<double, 8>> diameter;
        std::optional<std::array<double, 6>> melt;
        std::optional<std::array<double, 3>> dip;
    };

    struct PlcRuntimeState
    {
        MeasurementStage stage{MeasurementStage::Neck};
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

    bool setPlcRoi(PlcRois &rois, const QString &name,
                   const std::vector<double> &values, QString *error = nullptr);
    MeasurementStage diameterStage(bool pointFitSelected, const PlcRois &rois,
                                   double bodyRadiusThreshold);
    MeasurementStage stageForPlcCommand(const QString &name, MeasurementStage current,
                                        bool pointFitSelected, const PlcRois &rois,
                                        double bodyRadiusThreshold);
    void appendPlcRoiOverlays(const PlcRois &rois, MeasurementStage stage,
                              double diameterRectHeight,
                              std::vector<OverlayElement> &camera1,
                              std::vector<OverlayElement> &camera2);

    struct PlcGrayStats { double average{}, maximum{}, minimum{}; };
    std::optional<PlcGrayStats> meltRoiStats(const cv::Mat &image,
                                            const std::array<double, 6> &values,
                                            int camera);
    std::optional<double> dipLineMean(const cv::Mat &image,
                                      const std::array<double, 3> &values);
}
