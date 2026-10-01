#pragma once

#include "config.hpp"
#include "models.hpp"
#include "plc_runtime.hpp"
#include <utility>

namespace pva
{

    class MeasurementEngine
    {
    public:
        explicit MeasurementEngine(MeasurementConfig config, MeasurementState state = {});

        MeasurementResult process(const cv::Mat &camera1, const cv::Mat &camera2, MeasurementStage stage);
        [[nodiscard]] const MeasurementState &state() const { return state_; }
        void setConfig(MeasurementConfig config) { config_ = std::move(config); }
        void setPlcRois(PlcRois rois) { plcRois_ = std::move(rois); }

    private:
        MeasurementConfig config_;
        MeasurementState state_;
        PlcRois plcRois_;

        std::pair<bool, std::string> processMelt(const cv::Mat &, const cv::Mat &, MeasurementResult &);
        std::pair<bool, std::string> processDip(const cv::Mat &, MeasurementResult &);
        std::pair<bool, std::string> processNeck(const cv::Mat &, const cv::Mat &, MeasurementResult &);
        std::pair<bool, std::string> processCrown(const cv::Mat &, const cv::Mat &, MeasurementResult &);
        std::pair<bool, std::string> processBody(const cv::Mat &, const cv::Mat &, MeasurementResult &);
        std::pair<bool, std::string> processEndcone(const cv::Mat &, const cv::Mat &, MeasurementResult &);
    };

} // namespace pva
