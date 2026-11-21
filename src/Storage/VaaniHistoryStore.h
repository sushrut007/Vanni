#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

struct VaaniHistoryItem {
    QString id;
    QDateTime timestamp;
    QString text;
    QString languageCode;
};

class VaaniHistoryStore : public QObject
{
    Q_OBJECT

public:
    explicit VaaniHistoryStore(QObject* parent = nullptr);

    QList<VaaniHistoryItem> items() const;
    void addItem(const QString& text, const QString& languageCode);
    void removeItem(const QString& id);
    void clear();

signals:
    void historyChanged();

private:
    void load();
    void save();
    QString filePath() const;

    QList<VaaniHistoryItem> m_items;
    static constexpr int MaxHistoryItems = 100;
};
