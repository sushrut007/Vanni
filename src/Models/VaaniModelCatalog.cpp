#include "Models/VaaniModelCatalog.h"

namespace Vaani {

QVector<ModelOffer> modelCatalog()
{
    return {
        { QStringLiteral("tiny-q8"),
          QStringLiteral("Faster / lighter"),
          QStringLiteral("Lower CPU use; shorter phrases only"),
          QStringLiteral("ggml-tiny-q8_0.bin"),
          QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny-q8_0.bin"),
          45 * 1024 * 1024LL },
        { QStringLiteral("base-q8"),
          QStringLiteral("Balanced (recommended)"),
          QStringLiteral("Best speed/accuracy for desktop dictation"),
          QStringLiteral("ggml-base-q8_0.bin"),
          QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base-q8_0.bin"),
          150 * 1024 * 1024LL },
        { QStringLiteral("small-q8"),
          QStringLiteral("More accurate"),
          QStringLiteral("Heavier CPU; better for accents"),
          QStringLiteral("ggml-small-q8_0.bin"),
          QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q8_0.bin"),
          260 * 1024 * 1024LL },
    };
}

ModelOffer defaultModelOffer()
{
    return offerById(QStringLiteral("base-q8"));
}

QString formatModelChoiceLabel(const ModelOffer& offer)
{
    const double mb = offer.approximateBytes > 0 ? offer.approximateBytes / (1024.0 * 1024.0) : 0;
    const QString size =
        mb > 0 ? QStringLiteral("~%1 MB").arg(QString::number(mb, 'f', 0)) : QStringLiteral("—");
    return QStringLiteral("%1 — %2 (%3)")
        .arg(offer.title, offer.whisperFileName, size);
}

ModelOffer offerById(const QString& id)
{
    for (const ModelOffer& o : modelCatalog()) {
        if (o.id == id)
            return o;
    }
    return defaultModelOffer();
}

} // namespace Vaani