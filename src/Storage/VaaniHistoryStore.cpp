#include "Storage/VaaniHistoryStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

VaaniHistoryStore::VaaniHistoryStore(QObject* parent)
    : QObject(parent)
{
    load();
}

QString VaaniHistoryStore::filePath() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("history.json"));
}

QList<VaaniHistoryItem> VaaniHistoryStore::items() const
{
    return m_items;
}

void VaaniHistoryStore::addItem(const QString& text, const QString& languageCode)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;

    VaaniHistoryItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.timestamp = QDateTime::currentDateTime();
    item.text = trimmed;
    item.languageCode = languageCode;

    m_items.prepend(item);
    while (m_items.size() > MaxHistoryItems) {
        m_items.removeLast();
    }

    save();
    emit historyChanged();
}

void VaaniHistoryStore::removeItem(const QString& id)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id) {
            m_items.removeAt(i);
            save();
            emit historyChanged();
            return;
        }
    }
}

void VaaniHistoryStore::clear()
{
    if (m_items.isEmpty())
        return;

    m_items.clear();
    save();
    emit historyChanged();
}

void VaaniHistoryStore::load()
{
    m_items.clear();
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return;

    const QJsonArray arr = doc.array();
    for (const auto& val : arr) {
        if (!val.isObject())
            continue;
        const QJsonObject obj = val.toObject();
        VaaniHistoryItem item;
        item.id = obj.value(QStringLiteral("id")).toString();
        item.timestamp = QDateTime::fromString(obj.value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
        item.text = obj.value(QStringLiteral("text")).toString();
        item.languageCode = obj.value(QStringLiteral("languageCode")).toString();

        if (!item.id.isEmpty() && !item.text.isEmpty()) {
            m_items.append(item);
        }
    }
}

void VaaniHistoryStore::save()
{
    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray arr;
    for (const auto& item : m_items) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), item.id);
        obj.insert(QStringLiteral("timestamp"), item.timestamp.toString(Qt::ISODate));
        obj.insert(QStringLiteral("text"), item.text);
        obj.insert(QStringLiteral("languageCode"), item.languageCode);
        arr.append(obj);
    }

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }
}
