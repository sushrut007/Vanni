#include "Utils/WhisperModelPaths.h"

#include "Models/VaaniModelCatalog.h"
#include "Models/VaaniModelDownloader.h"

#include "Utils/AppSettingsPath.h"

#include <QFile>
#include <QSettings>

namespace Vaani {

QString selectedModelId()
{
    QSettings settings(Vaani::appSettingsIniPath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Models"));
    const QString id =
        settings.value(QStringLiteral("SelectedId"), defaultModelOffer().id).toString();
    settings.endGroup();
    return id;
}

ModelPathResult resolveInstalledModels()
{
    ModelPathResult result;
    const QString id = selectedModelId();
    QString dir;
    if (!isModelInstalled(id, &dir)) {
        result.errorMessage =
            QStringLiteral("Speech model not installed. Complete setup to download from Hugging Face.");
        return result;
    }
    const ModelOffer offer = offerById(id);
    result.ok = true;
    result.modelsDirectory = dir;
    result.whisperModelFile = dir + QLatin1Char('/') + offer.whisperFileName;
    result.sileroVadFile = dir + QLatin1Char('/') + sileroVadFileName();
  if (!QFile::exists(result.whisperModelFile) || !QFile::exists(result.sileroVadFile)) {
        result.ok = false;
        result.errorMessage = QStringLiteral("Model files are incomplete in %1").arg(dir);
    }
    return result;
}

} // namespace Vaani