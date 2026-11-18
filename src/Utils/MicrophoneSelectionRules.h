#pragma once

#include <QComboBox>
#include <QtCore/qnamespace.h>

namespace MicrophoneSelectionRules {

constexpr int kPlaceholderRole = Qt::UserRole + 2;

inline void markPlaceholder(QComboBox* combo, int index)
{
    if (combo && index >= 0 && index < combo->count())
        combo->setItemData(index, true, kPlaceholderRole);
}

inline bool isPlaceholder(const QComboBox* combo, int index)
{
    return combo && index >= 0 && index < combo->count()
           && combo->itemData(index, kPlaceholderRole).toBool();
}

} // namespace MicrophoneSelectionRules