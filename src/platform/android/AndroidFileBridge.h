#pragma once

#include <QObject>
#include <QString>

namespace labelqt::platform {

// One application-owned instance, constructed/called/destroyed on the Qt application thread.
// Connect signals before returning to the event loop: attachment to Java is deferred.
class AndroidFileBridge final : public QObject {
    Q_OBJECT

public:
    explicit AndroidFileBridge(QObject* parent = nullptr);
    ~AndroidFileBridge() override;

    // Android system picker, supported project/archive formats only. Cancellation is silent.
    Q_INVOKABLE void chooseFile();
    // Absolute path to an immutable ZIP or PDF inside Context.getFilesDir()/outgoing/<UUID>/.
    // The export service owns its lifetime (at least seven days); this never deletes it.
    Q_INVOKABLE void shareFile(QString localFilePath);

signals:
    // Unvalidated input, NOT a published workspace. Consume/delete after import or rejection
    // to release the inbox quota. Interrupted process recovery can redeliver a staged file.
    void fileReceived(QString stagedLocalPath, QString displayName);
    // Translated in C++; Java sends only numeric error codes, never provider exception text.
    void failure(QString message);

private:
    void initialize();
    void receive(qint64 eventId, const QString& path, const QString& displayName, int error);
    static void dispatch(qint64 receiver, qint64 eventId, QString path, QString displayName, int error);
    static QString errorMessage(int error);

    qint64 m_receiver = 0;
};

} // namespace labelqt::platform
