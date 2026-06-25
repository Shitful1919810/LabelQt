#include "ui/DialogWindowUtils.h"

#include <QDialog>

namespace labelqt::ui {

void configureLargeDialogWindow(QDialog& dialog, QSize defaultSize, bool openMaximized)
{
    dialog.setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    dialog.setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    dialog.resize(defaultSize);
    if (openMaximized) {
        dialog.setWindowState(dialog.windowState() | Qt::WindowMaximized);
    }
}

} // namespace labelqt::ui
