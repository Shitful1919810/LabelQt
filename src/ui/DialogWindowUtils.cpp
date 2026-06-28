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

void configureBusyDialogWindow(QDialog& dialog, bool allowCloseButton)
{
    Qt::WindowFlags flags = Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                            Qt::MSWindowsFixedSizeDialogHint;
    if (allowCloseButton) {
        flags |= Qt::WindowCloseButtonHint;
    }
    dialog.setWindowFlags(flags);
}

} // namespace labelqt::ui
