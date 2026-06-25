#pragma once

#include <QSize>

class QDialog;

namespace labelqt::ui {

void configureLargeDialogWindow(QDialog& dialog, QSize defaultSize, bool openMaximized = true);

} // namespace labelqt::ui
