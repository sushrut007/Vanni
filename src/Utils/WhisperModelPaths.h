#pragma once

#include <QString>

namespace Vaani {

struct ModelPathResult {
    bool ok = false;
    QString modelsDirectory;
    QString whisperModelFile;
    QString sileroVadFile;
    QString errorMessage;
};

// Resolves installed HF models under %LOCALAPPDATA%/Vaani/models/<id>/ — no bundled copy.
ModelPathResult resolveInstalledModels();

QString selectedModelId();

} // namespace Vaani