#include "offline_image_source.hpp"
#include <QFile>
#include <QFileInfo>
#include <opencv2/imgcodecs.hpp>
#include <stdexcept>
namespace pva {
std::pair<cv::Mat, cv::Mat> OfflineImageSource::load(const QString &path)
{
    const QFileInfo info(path);
    if (path != path_ || info.size() != size_ || info.lastModified() != modified_ || composite_.empty()) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
        const auto bytes = file.readAll();
        if (bytes.isEmpty()) throw std::runtime_error("Offline image is empty");
        const cv::Mat buffer(1, bytes.size(), CV_8U, const_cast<char *>(bytes.constData()));
        auto decoded = cv::imdecode(buffer, cv::IMREAD_UNCHANGED);
        if (decoded.empty() || decoded.rows < 2 || decoded.rows % 2)
            throw std::runtime_error("Offline image must contain two equal-height camera images");
        composite_ = std::move(decoded); path_ = path; size_ = info.size(); modified_ = info.lastModified();
    }
    // 保持原图方向，ROI 引用计数保证缓存更换后旧任务仍然有效。
    return {composite_.rowRange(0, composite_.rows / 2), composite_.rowRange(composite_.rows / 2, composite_.rows)};
}
}
