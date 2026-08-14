#pragma once

#include "pva/config.hpp"
#include "pva/models.hpp"
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

    private:
        MeasurementConfig config_;
        MeasurementState state_;

        std::pair<bool, std::string> processNeck(const cv::Mat &, const cv::Mat &, MeasurementResult &);
        std::pair<bool, std::string> processCrown(const cv::Mat &, const cv::Mat &, MeasurementResult &);
        std::pair<bool, std::string> processBody(const cv::Mat &, const cv::Mat &, MeasurementResult &);
        std::pair<bool, std::string> processEndcone(const cv::Mat &, const cv::Mat &, MeasurementResult &);
    };

} // namespace pva
