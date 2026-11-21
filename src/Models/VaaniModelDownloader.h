#pragma once

#include "Models/VaaniModelCatalog.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace Vaani {

class VaaniModelDownloader : public QObject
{
    Q_OBJECT

public:
    explicit VaaniModelDownloader(QObject* parent = nullptr);
    ~VaaniModelDownloader() override;

    void startDownload(const ModelOffer& whisperOffer);
    void cancel();

    QString installDirectory() const;

signals:
    void statusChanged(const QString& text);
    void progressChanged(qint64 received, qint64 total);
    void finishedSuccess(const QString& modelsDirectory);
    void failed(const QString& message);

private:
    void downloadFile(const QUrl& url, const QString& destPath, std::function<void()> onSuccess);
    void fail(const QString& message);
    void tryFinish();

    QNetworkAccessManager* m_nam = nullptr;
    QSet<QNetworkReply*> m_activeReplies;
    QString m_installDir;
    ModelOffer m_whisperOffer;
    bool m_whisperDone = false;
    bool m_sileroDone = false;
    bool m_failed = false;
    qint64 m_bytesTotal = 0;
    qint64 m_bytesReceived = 0;
};

QString modelsRootDirectory();
bool isModelInstalled(const QString& modelId, QString* outDirectory = nullptr);

} // namespace Vaani