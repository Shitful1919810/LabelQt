#pragma once

#include "ui/widgets/BusyTaskDialog.h"
#include <QCoreApplication>
#include <QFutureWatcher>
#include <functional>
#include <optional>
#include <utility>

namespace labelqt::ui {

// Rejection is authoritative even if the worker finished before its notification was delivered.
template <typename T>
std::optional<T> waitForBackgroundTask(QFuture<T> future, BusyTaskDialog& dialog, std::function<void()> discard = {})
{
    auto* watcher = new QFutureWatcher<T>(QCoreApplication::instance());
    QObject::connect(watcher, &QFutureWatcher<T>::finished, &dialog, &QDialog::accept);
    watcher->setFuture(std::move(future));
    const bool accepted = dialog.exec() == QDialog::Accepted;
    QObject::disconnect(watcher, nullptr, &dialog, nullptr);
    if (!accepted) {
        const auto cleanup = [watcher, discard = std::move(discard)] {
            if (discard)
                discard();
            watcher->deleteLater();
        };
        if (watcher->isFinished())
            cleanup();
        else
            QObject::connect(watcher, &QFutureWatcher<T>::finished, watcher, cleanup);
        return std::nullopt;
    }
    watcher->deleteLater();
    return watcher->result();
}

} // namespace labelqt::ui
