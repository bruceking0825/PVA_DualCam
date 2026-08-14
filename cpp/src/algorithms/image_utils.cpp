#include "pva/algorithms/detectors.hpp"
#include <opencv2/imgproc.hpp>

// 图像格式归一化实现保留为独立编译单元，供后续相机适配器复用。
namespace pva::algorithms
{
    cv::Mat normalizeGray8(const cv::Mat &source)
    {
        if (source.empty())
            return {};
        cv::Mat gray;
        if (source.channels() == 1)
            gray = source;
        else
            cv::cvtColor(source, gray, source.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
        if (gray.depth() == CV_8U)
            return gray.clone();
        double low = 0, high = 0;
        cv::minMaxLoc(gray, &low, &high);
        if (high <= low)
            return cv::Mat::zeros(gray.size(), CV_8U);
        cv::Mat result;
        gray.convertTo(result, CV_8U, 255.0 / (high - low), -low * 255.0 / (high - low));
        return result;
    }
}
