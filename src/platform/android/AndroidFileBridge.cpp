#include "AndroidFileBridge.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <utility>

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QMutex>
#include <QMutexLocker>
#endif

namespace labelqt::platform {
namespace {
// Wire values must match LabelQtActivity. No Java exception details cross this boundary.
enum Error {
    Unavailable = 1,
    Busy = 2,
    InvalidInput = 3,
    TooLarge = 4,
    PermissionDenied = 5,
    CopyFailed = 6,
    PickerFailed = 7,
    ShareFailed = 8,
    UnsupportedType = 9,
    Interrupted = 10,
    Configuration = 11
};

#ifdef Q_OS_ANDROID
constexpr auto ActivityClass = "org/labelqt/mobile/LabelQtActivity";
QMutex receiverMutex;
QPointer<AndroidFileBridge> activeReceiver;
qint64 activeToken = 0;
qint64 nextToken = 0;

bool clearJniException()
{
    QJniEnvironment env;
    return env.checkAndClearExceptions(QJniEnvironment::OutputMode::Silent);
}
#endif
} // namespace

AndroidFileBridge::AndroidFileBridge(QObject* parent) : QObject(parent)
{
    Q_ASSERT(QCoreApplication::instance());
    Q_ASSERT(thread() == QCoreApplication::instance()->thread());
    QTimer::singleShot(0, this, &AndroidFileBridge::initialize);
}

AndroidFileBridge::~AndroidFileBridge()
{
#ifdef Q_OS_ANDROID
    {
        // The JNI callback holds this lock while posting. Clear before QObject teardown so
        // it cannot enqueue through a dying receiver. Qt removes already queued calls.
        QMutexLocker lock(&receiverMutex);
        if (activeReceiver == this) {
            activeReceiver.clear();
            activeToken = 0;
        }
    }
    if (m_receiver != 0) {
        QJniObject::callStaticMethod<jboolean>(ActivityClass, "detachNative", "(J)Z", jlong(m_receiver));
        clearJniException();
    }
#endif
}

void AndroidFileBridge::initialize()
{
#ifdef Q_OS_ANDROID
    if (m_receiver != 0)
        return;
    if (!QNativeInterface::QAndroidApplication::context().isValid()) {
        emit failure(errorMessage(Unavailable));
        return;
    }

    QJniEnvironment env;
    const JNINativeMethod methods[] = {{const_cast<char*>("nativeResult"),
                                        const_cast<char*>("(JJLjava/lang/String;Ljava/lang/String;I)V"),
                                        reinterpret_cast<void*>(+[](JNIEnv*, jclass, jlong receiver, jlong event,
                                                                    jstring path, jstring name, jint error) {
                                            dispatch(receiver, event, path ? QJniObject(path).toString() : QString{},
                                                     name ? QJniObject(name).toString() : QString{}, error);
                                        })}};
    const bool registered = env.registerNativeMethods(ActivityClass, methods, 1);
    const bool exception = clearJniException();
    if (!registered || exception) {
        emit failure(errorMessage(Configuration));
        return;
    }

    bool occupied = false;
    {
        QMutexLocker lock(&receiverMutex);
        occupied = !activeReceiver.isNull();
        if (!occupied) {
            m_receiver = ++nextToken;
            activeToken = m_receiver;
            activeReceiver = this;
        }
    }
    if (occupied) {
        emit failure(errorMessage(Busy));
        return;
    }
    const bool attached =
        QJniObject::callStaticMethod<jboolean>(ActivityClass, "attachNative", "(J)Z", jlong(m_receiver));
    const bool attachException = clearJniException();
    if (!attached || attachException) {
        {
            QMutexLocker lock(&receiverMutex);
            activeReceiver.clear();
            activeToken = 0;
            m_receiver = 0;
        }
        emit failure(errorMessage(Configuration));
    }
#endif
}

void AndroidFileBridge::chooseFile()
{
    Q_ASSERT(QThread::currentThread() == thread());
#ifdef Q_OS_ANDROID
    if (m_receiver == 0) {
        emit failure(errorMessage(Unavailable));
        return;
    }
    const bool posted = QJniObject::callStaticMethod<jboolean>(ActivityClass, "chooseFile", "(J)Z", jlong(m_receiver));
    const bool exception = clearJniException();
    if (!posted || exception)
        emit failure(errorMessage(Configuration));
#else
    emit failure(errorMessage(Unavailable));
#endif
}

void AndroidFileBridge::shareFile(QString localFilePath)
{
    Q_ASSERT(QThread::currentThread() == thread());
#ifdef Q_OS_ANDROID
    if (m_receiver == 0) {
        emit failure(errorMessage(Unavailable));
        return;
    }
    const auto path = QJniObject::fromString(localFilePath);
    const bool posted = QJniObject::callStaticMethod<jboolean>(ActivityClass, "shareFile", "(JLjava/lang/String;)Z",
                                                               jlong(m_receiver), path.object<jstring>());
    const bool exception = clearJniException();
    if (!posted || exception)
        emit failure(errorMessage(Configuration));
#else
    Q_UNUSED(localFilePath);
    emit failure(errorMessage(Unavailable));
#endif
}

void AndroidFileBridge::dispatch(qint64 receiver, qint64 eventId, QString path, QString displayName, int error)
{
#ifdef Q_OS_ANDROID
    // Only this guarded posting step runs on Java's thread. No document or UI pointers
    // cross JNI, and the QPointer is dereferenced only on its owner's Qt thread.
    QMutexLocker lock(&receiverMutex);
    if (!activeReceiver || activeToken != receiver)
        return;
    const QPointer<AndroidFileBridge> target = activeReceiver;
    QMetaObject::invokeMethod(
        target.data(),
        [target, receiver, eventId, path = std::move(path), displayName = std::move(displayName), error] {
            if (target && target->m_receiver == receiver)
                target->receive(eventId, path, displayName, error);
        },
        Qt::QueuedConnection);
#else
    Q_UNUSED(receiver);
    Q_UNUSED(eventId);
    Q_UNUSED(path);
    Q_UNUSED(displayName);
    Q_UNUSED(error);
#endif
}

void AndroidFileBridge::receive(qint64 eventId, const QString& path, const QString& displayName, int error)
{
#ifdef Q_OS_ANDROID
    // Acknowledge on the owner thread, not when JNI merely queues a callback. If the
    // bridge dies before dispatch, Java retains the event for the next receiver.
    const bool posted = QJniObject::callStaticMethod<jboolean>(ActivityClass, "acknowledge", "(JJ)Z", jlong(m_receiver),
                                                               jlong(eventId));
    const bool exception = clearJniException();
    if (!posted || exception) {
        emit failure(errorMessage(Configuration));
        return;
    }
#else
    Q_UNUSED(eventId);
#endif
    if (error != 0)
        emit failure(errorMessage(error));
    else if (!path.isEmpty())
        emit fileReceived(path, displayName);
    else
        emit failure(errorMessage(CopyFailed));
}

QString AndroidFileBridge::errorMessage(int error)
{
    switch (error) {
    case Unavailable:
        return tr("Android file access is not available here.");
    case Busy:
        return tr("The file inbox or system file chooser is busy. Finish a pending import and try again.");
    case InvalidInput:
        return tr("Select one content URI for a ZIP or TXT file. Conflicting or local file URIs are not accepted.");
    case TooLarge:
        return tr("The file or pending inbox exceeds the 2 GiB limit.");
    case PermissionDenied:
        return tr("Permission to read the selected file was lost. Select the file again.");
    case CopyFailed:
        return tr("The file could not be staged. Check available storage and select it again.");
    case PickerFailed:
        return tr("The Android system file chooser could not be opened.");
    case ShareFailed:
        return tr("The Android share chooser could not be opened. The exported file has been kept.");
    case UnsupportedType:
        return tr("Only ZIP and TXT files are accepted by this file bridge.");
    case Interrupted:
        return tr("An interrupted file import was discarded. Select the source file again.");
    default:
        return tr("The Android file bridge is not configured correctly.");
    }
}

} // namespace labelqt::platform
