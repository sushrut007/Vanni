#include "Models/VaaniModelDownloader.h"

#include "Utils/AppSettingsPath.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace Vaani {

QString modelsRootDirectory()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString root = base + QStringLiteral("/models");
    QDir().mkpath(root);
    return root;
}

bool isModelInstalled(const QString& modelId, QString* outDirectory)
{
    const QString dir = modelsRootDirectory() + QLatin1Char('/') + modelId;
    const ModelOffer offer = offerById(modelId);
    const QString whisper = dir + QLatin1Char('/') + offer.whisperFileName;
    const QString silero = dir + QLatin1Char('/') + sileroVadFileName();
    if (!QFile::exists(whisper) || !QFile::exists(silero))
        return false;
    if (outDirectory)
        *outDirectory = dir;
    return true;
}

VaaniModelDownloader::VaaniModelDownloader(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

VaaniModelDownloader::~VaaniModelDownloader()
{
    cancel();
}

QString VaaniModelDownloader::installDirectory() const
{
    return m_installDir;
}

void VaaniModelDownloader::cancel()
{
    for (QNetworkReply* reply : m_activeReplies)
        reply->abort();
    m_activeReplies.clear();
}

void VaaniModelDownloader::startDownload(const ModelOffer& whisperOffer)
{
    cancel();
    m_whisperOffer = whisperOffer;
    m_whisperDone = false;
    m_sileroDone = false;
    m_failed = false;
    m_bytesReceived = 0;
    m_bytesTotal = whisperOffer.approximateBytes + sileroVadApproximateBytes();

    m_installDir = modelsRootDirectory() + QLatin1Char('/') + whisperOffer.id;
    QDir().mkpath(m_installDir);

    emit statusChanged(tr("Downloading %1…").arg(whisperOffer.title));

    const QString whisperDest = m_installDir + QLatin1Char('/') + whisperOffer.whisperFileName;
    downloadFile(
        QUrl(whisperOffer.huggingFacePath), whisperDest,
        [this]() {
            m_whisperDone = true;
            emit statusChanged(tr("Downloading voice-activity model (Silero)…"));
            const QString sileroDest = m_installDir + QLatin1Char('/') + sileroVadFileName();
            downloadFile(QUrl(sileroHuggingFacePath()), sileroDest, [this]() {
                m_sileroDone = true;
                tryFinish();
            });
        });
}

void VaaniModelDownloader::downloadFile(const QUrl& url, const QString& destPath,
                                        std::function<void()> onSuccess)
{
    const QString tempPath = destPath + QStringLiteral(".part");
    QFile::remove(tempPath);

    auto* file = new QFile(tempPath);
    if (!file->open(QIODevice::WriteOnly)) {
        fail(tr("Cannot write to %1").arg(tempPath));
        delete file;
        return;
    }

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "Vaani/0.2 (+https://vaani)");
    req.setRawHeader("Accept", "*/*");

    QNetworkReply* reply = m_nam->get(req);
    m_activeReplies.insert(reply);
    const qint64 offsetBefore = m_bytesReceived;

    connect(reply, &QNetworkReply::readyRead, this, [reply, file]() {
        file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, offsetBefore](qint64 received, qint64 total) {
                if (total > 0)
                    m_bytesReceived = offsetBefore + received;
                emit progressChanged(m_bytesReceived, m_bytesTotal);
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, destPath, onSuccess]() {
                m_activeReplies.remove(reply);
                if (m_failed) {
                    reply->deleteLater();
                    file->close();
                    delete file;
                    return;
                }
                if (reply->error() != QNetworkReply::NoError) {
                    const int httpStatus =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    QString detail = reply->errorString();
                    if (httpStatus > 0)
                        detail = QStringLiteral("HTTP %1 — %2").arg(httpStatus).arg(detail);
                    fail(tr("Download failed for %1\n%2").arg(destPath, detail));
                    reply->deleteLater();
                    file->close();
                    delete file;
                    return;
                }
                file->write(reply->readAll());
                file->close();
                delete file;
                QFile::remove(destPath);
                if (!QFile::rename(destPath + QStringLiteral(".part"), destPath)) {
                    fail(tr("Failed to finalize %1").arg(destPath));
                    reply->deleteLater();
                    return;
                }
                reply->deleteLater();
                if (!m_failed)
                    onSuccess();
            });
}

void VaaniModelDownloader::tryFinish()
{
    if (m_failed || !m_whisperDone || !m_sileroDone)
        return;

    QSettings settings(Vaani::appSettingsIniPath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Models"));
    settings.setValue(QStringLiteral("SelectedId"), m_whisperOffer.id);
    settings.endGroup();
    settings.sync();

    emit statusChanged(tr("Download complete"));
    emit progressChanged(m_bytesTotal, m_bytesTotal);
    emit finishedSuccess(m_installDir);
}

void VaaniModelDownloader::fail(const QString& message)
{
    if (m_failed)
        return;
    m_failed = true;
    cancel();
    emit failed(message);
}

} // namespace Vaani