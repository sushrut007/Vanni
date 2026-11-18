#pragma once

#include <QString>
#include <QVector>

namespace Vaani {

struct ModelOffer {
    QString id;
    QString title;
    QString hint;
    QString whisperFileName;
    QString huggingFacePath;
    qint64 approximateBytes = 0;
};

QVector<ModelOffer> modelCatalog();
ModelOffer defaultModelOffer();
ModelOffer offerById(const QString& id);
QString formatModelChoiceLabel(const ModelOffer& offer);

inline QString sileroVadFileName() { return QStringLiteral("ggml-silero-v6.2.0.bin"); }
inline QString sileroHuggingFacePath() {
    return QStringLiteral(
        "https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin");
}

inline qint64 sileroVadApproximateBytes() { return 900 * 1024LL; }

} // namespace Vaani