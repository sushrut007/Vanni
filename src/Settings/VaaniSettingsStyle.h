#pragma once

class QWidget;

namespace Vaani {
enum class UiTheme {
    System,
    Light,
    Dark,
};

void applySettingsStyle(QWidget* root, UiTheme theme);
UiTheme effectiveTheme(UiTheme preference);
} // namespace Vaani