#include "core/AppPreferences.h"
#include "core/AtomicFileWriter.h"
#include "services/preferences/PreferenceStore.h"
#include "ui/preferences/PreferenceDialog.h"

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <barrier>
#include <thread>

using labelqt::core::AppPreferences;
using labelqt::core::AtomicFileWriter;
using labelqt::services::PreferenceStore;

namespace {
QJsonDocument document(int value)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("value"), value}});
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QPushButton* applyButton(PreferenceDialog& dialog)
{
    auto* box = dialog.findChild<QDialogButtonBox*>();
    for (auto* button : box->buttons()) {
        if (box->buttonRole(button) == QDialogButtonBox::ApplyRole)
            return qobject_cast<QPushButton*>(button);
    }
    return nullptr;
}

void changeDraft(PreferenceDialog& dialog)
{
    auto* spin = dialog.findChild<QSpinBox*>();
    spin->setValue(spin->value() + 1);
}

QAction* reloadAction(PreferenceDialog& dialog)
{
    for (auto* action : dialog.findChildren<QAction*>()) {
        if (action->text() == PreferenceDialog::tr("Reload"))
            return action;
    }
    return nullptr;
}

bool captureWidget(QWidget& widget, const QString& name)
{
    const auto path = qEnvironmentVariable("LABELQT_TEST_SCREENSHOTS");
    if (path.isEmpty())
        return true;
    QCoreApplication::processEvents();
    return QDir().mkpath(path) && widget.grab().save(QDir(path).filePath(name + QStringLiteral(".png")));
}

void answerPrompt(QMessageBox::StandardButton answer)
{
    QTimer::singleShot(0, qApp, [answer] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt == nullptr)
            return;
        prompt->button(answer)->click();
    });
}

void applyFromPrompt()
{
    QTimer::singleShot(0, qApp, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt == nullptr)
            return;
        for (auto* button : prompt->buttons()) {
            if (prompt->buttonRole(button) == QMessageBox::AcceptRole) {
                button->click();
                return;
            }
        }
    });
}
} // namespace

class PreferenceStoreTests final : public QObject {
    Q_OBJECT

private slots:
    void requiresBaselineAndValidObject();
    void boundedDocuments_data();
    void boundedDocuments();
    void staleDraftCannotOverwrite();
    void simultaneousWritersCommitOnlyOneDraft();
    void detectsCreationDeletionAndSameSizeEdits();
    void lockFailureRetainsBaselineAndDisk();
    void failedLoadRetainsBaseline();
    void failedSaveCanBeRetried();
    void missingParentIsCreated();
    void dialogFailedSaveNeverApplies();
    void dialogOversizedLoadAndSaveKeepDraft();
    void dialogApplyOnCloseSurvivesWriteFailure();
    void dialogLoadsDiskInsteadOfStaleApplicationState();
    void dialogReloadKeepsRuntimePendingUntilApply();
    void dialogCloseConfirmsUnappliedDiskDraft();
    void dialogDiskDirtyCanMatchRuntime();
    void dialogCloseAndReloadProtectDraft();
    void dialogSearchAndOptionalJson();
    void dialogOpacitySlidersPreservePercentValues();
};

void PreferenceStoreTests::requiresBaselineAndValidObject()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PreferenceStore store(dir.filePath(QStringLiteral("preference.json")));
    const auto uninitialized = store.save(document(1));
    QVERIFY(!uninitialized);
    QCOMPARE(uninitialized.error().code, PreferenceStore::ErrorCode::MissingBaseline);
    QVERIFY(!store.compareExternalChanges());
    const auto snapshot = store.load();
    QVERIFY(snapshot);
    QVERIFY(!snapshot->exists);
    const auto invalid = store.save(QJsonDocument{});
    QVERIFY(!invalid);
    QCOMPARE(invalid.error().code, PreferenceStore::ErrorCode::InvalidDocument);
    QVERIFY(!QFile::exists(store.path()));
    QVERIFY(store.save(document(1)));
    QVERIFY(!*store.compareExternalChanges());
    QVERIFY(store.save(document(2)));
    QCOMPARE(QJsonDocument::fromJson(readBytes(store.path())), document(2));
}

void PreferenceStoreTests::boundedDocuments_data()
{
    QTest::addColumn<QJsonDocument>("accepted");
    QTest::addColumn<QJsonDocument>("rejected");

    QJsonObject bytes{{QStringLiteral("value"), QString()}};
    const auto overhead = QJsonDocument(bytes).toJson().size();
    bytes.insert(QStringLiteral("value"), QString(PreferenceStore::StorageLimits.bytes - overhead, QLatin1Char('x')));
    const QJsonDocument byteLimit(bytes);
    bytes.insert(QStringLiteral("value"), bytes.value(QStringLiteral("value")).toString() + QLatin1Char('x'));
    QTest::newRow("bytes") << byteLimit << QJsonDocument(bytes);

    QJsonArray items;
    for (qsizetype i = 0; i < PreferenceStore::StorageLimits.items - 2; ++i)
        items.append(0);
    const QJsonDocument itemLimit(QJsonObject{{QStringLiteral("values"), items}});
    items.append(0);
    QTest::newRow("items") << itemLimit << QJsonDocument(QJsonObject{{QStringLiteral("values"), items}});

    QJsonObject depth{{QStringLiteral("value"), 0}};
    for (int level = 2; level < PreferenceStore::StorageLimits.depth; ++level)
        depth = QJsonObject{{QStringLiteral("value"), depth}};
    QTest::newRow("depth") << QJsonDocument(depth) << QJsonDocument(QJsonObject{{QStringLiteral("value"), depth}});
}

void PreferenceStoreTests::boundedDocuments()
{
    QFETCH(QJsonDocument, accepted);
    QFETCH(QJsonDocument, rejected);
    QTemporaryDir dir;
    PreferenceStore store(dir.filePath(QStringLiteral("preference.json")));
    QVERIFY(store.load());
    QVERIFY(store.save(accepted));
    const QByteArray committed = readBytes(store.path());
    const auto snapshot = store.load();
    QVERIFY(snapshot);
    QCOMPARE(snapshot->document, accepted);

    const auto failedSave = store.save(rejected);
    QVERIFY(!failedSave);
    QCOMPARE(failedSave.error().code, PreferenceStore::ErrorCode::InvalidDocument);
    QCOMPARE(readBytes(store.path()), committed);
    const auto unchanged = store.compareExternalChanges();
    QVERIFY(unchanged);
    QVERIFY(!*unchanged);

    const QByteArray invalidBytes = rejected.toJson();
    QVERIFY(AtomicFileWriter::write(store.path(), invalidBytes));
    QVERIFY(!store.load());
    QVERIFY(store.hasBaseline());
    QVERIFY(!store.save(document(2)));
    QCOMPARE(readBytes(store.path()), invalidBytes);
    PreferenceStore fresh(store.path());
    QVERIFY(!fresh.load());
    QVERIFY(!fresh.hasBaseline());
    QCOMPARE(fresh.save(document(2)).error().code, PreferenceStore::ErrorCode::MissingBaseline);

    QVERIFY(AtomicFileWriter::write(store.path(), committed));
    const auto restored = store.compareExternalChanges();
    QVERIFY(restored);
    QVERIFY(!*restored);
    QVERIFY(store.save(document(2)));
}

void PreferenceStoreTests::staleDraftCannotOverwrite()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("preference.json"));
    QVERIFY(AtomicFileWriter::write(path, document(0).toJson()));
    PreferenceStore first(path);
    PreferenceStore second(path);
    QVERIFY(first.load());
    QVERIFY(second.load());
    QVERIFY(first.save(document(1)));
    const QByteArray committed = readBytes(path);
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto stale = second.save(document(2));
        QVERIFY(!stale);
        QCOMPARE(stale.error().code, PreferenceStore::ErrorCode::ExternalChanges);
        QCOMPARE(readBytes(path), committed);
        QVERIFY(*second.compareExternalChanges());
    }
    QCOMPARE(second.load()->document, document(1));
    QVERIFY(second.save(document(2)));
    QVERIFY(*first.compareExternalChanges());
}

void PreferenceStoreTests::detectsCreationDeletionAndSameSizeEdits()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("preference.json"));
    PreferenceStore store(path);
    QVERIFY(store.load());
    QVERIFY(AtomicFileWriter::write(path, document(1).toJson()));
    QCOMPARE(store.save(document(2)).error().code, PreferenceStore::ErrorCode::ExternalChanges);
    QVERIFY(store.load());
    const auto modified = QFileInfo(path).lastModified();
    QVERIFY(AtomicFileWriter::write(path, document(3).toJson()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
    file.close();
    QVERIFY(*store.compareExternalChanges());
    QCOMPARE(store.save(document(2)).error().code, PreferenceStore::ErrorCode::ExternalChanges);
    QVERIFY(store.load());
    QVERIFY(QFile::remove(path));
    QCOMPARE(store.save(document(2)).error().code, PreferenceStore::ErrorCode::ExternalChanges);
    QVERIFY(!QFile::exists(path));
}

void PreferenceStoreTests::simultaneousWritersCommitOnlyOneDraft()
{
    QTemporaryDir dir;
    for (int attempt = 0; attempt < 10; ++attempt) {
        const QString path = dir.filePath(QString::number(attempt) + QStringLiteral(".json"));
        PreferenceStore first(path);
        PreferenceStore second(path);
        QVERIFY(first.load());
        QVERIFY(second.load());
        std::barrier start(3);
        std::expected<void, PreferenceStore::Error> firstResult;
        std::expected<void, PreferenceStore::Error> secondResult;
        std::thread firstWriter([&] {
            start.arrive_and_wait();
            firstResult = first.save(document(1));
        });
        std::thread secondWriter([&] {
            start.arrive_and_wait();
            secondResult = second.save(document(2));
        });
        start.arrive_and_wait();
        firstWriter.join();
        secondWriter.join();
        QVERIFY(firstResult.has_value() != secondResult.has_value());
        const auto error = firstResult ? secondResult.error().code : firstResult.error().code;
        QVERIFY(error == PreferenceStore::ErrorCode::Locked || error == PreferenceStore::ErrorCode::ExternalChanges);
        QCOMPARE(QJsonDocument::fromJson(readBytes(path)), document(firstResult ? 1 : 2));
    }
}

void PreferenceStoreTests::lockFailureRetainsBaselineAndDisk()
{
    QTemporaryDir dir;
    PreferenceStore store(dir.filePath(QStringLiteral("preference.json")));
    QVERIFY(store.load());
    QVERIFY(store.save(document(1)));
    QLockFile lock(store.lockPath());
    QVERIFY(lock.tryLock());
    const auto failed = store.save(document(2));
    QVERIFY(!failed);
    QCOMPARE(failed.error().code, PreferenceStore::ErrorCode::Locked);
    QCOMPARE(QJsonDocument::fromJson(readBytes(store.path())), document(1));
    QVERIFY(!*store.compareExternalChanges());
    lock.unlock();
    QVERIFY(store.save(document(2)));
}

void PreferenceStoreTests::failedLoadRetainsBaseline()
{
    QTemporaryDir dir;
    PreferenceStore store(dir.filePath(QStringLiteral("preference.json")));
    QVERIFY(store.load());
    QVERIFY(store.save(document(1)));
    QVERIFY(AtomicFileWriter::write(store.path(), QByteArrayLiteral("{broken")));
    const auto invalid = store.load();
    QVERIFY(!invalid);
    QCOMPARE(invalid.error().code, PreferenceStore::ErrorCode::InvalidDocument);
    QCOMPARE(store.save(document(2)).error().code, PreferenceStore::ErrorCode::ExternalChanges);
    QCOMPARE(readBytes(store.path()), QByteArrayLiteral("{broken"));
    QVERIFY(QFile::remove(store.path()));
    QVERIFY(QDir().mkdir(store.path()));
    QVERIFY(!store.load());
    QVERIFY(!store.save(document(2)));
    QVERIFY(QDir().rmdir(store.path()));
    QVERIFY(AtomicFileWriter::write(store.path(), document(1).toJson()));
    QVERIFY(store.save(document(2)));
}

void PreferenceStoreTests::failedSaveCanBeRetried()
{
    QTemporaryDir dir;
    const QString parent = dir.filePath(QStringLiteral("settings"));
    QVERIFY(QDir().mkdir(parent));
    PreferenceStore store(QDir(parent).filePath(QStringLiteral("preference.json")));
    QVERIFY(store.load());
    QVERIFY(QDir().rmdir(parent));
    QVERIFY(AtomicFileWriter::write(parent, QByteArrayLiteral("blocked")));
    const auto failed = store.save(document(1));
    QVERIFY(!failed);
    QCOMPARE(failed.error().code, PreferenceStore::ErrorCode::WriteFailed);
    QCOMPARE(readBytes(parent), QByteArrayLiteral("blocked"));
    QVERIFY(QFile::remove(parent));
    QVERIFY(store.save(document(1)));
}

void PreferenceStoreTests::missingParentIsCreated()
{
    QTemporaryDir dir;
    PreferenceStore store(dir.filePath(QStringLiteral("a/b/preference.json")));
    QVERIFY(store.load());
    QVERIFY(store.save(document(1)));
    QVERIFY(!QFile::exists(store.lockPath()));
    QCOMPARE(QJsonDocument::fromJson(readBytes(store.path())), document(1));
}

void PreferenceStoreTests::dialogFailedSaveNeverApplies()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("preference.json"));
    const AppPreferences current;
    QVERIFY(AtomicFileWriter::write(path, current.toJsonDocument().toJson()));
    PreferenceDialog dialog(path, current, {});
    QSignalSpy applied(&dialog, &PreferenceDialog::preferencesApplied);
    QVERIFY(!dialog.hasUnsavedChanges());
    QVERIFY(!applyButton(dialog)->isEnabled());
    changeDraft(dialog);
    QVERIFY(dialog.hasUnsavedChanges());
    QLockFile lock(PreferenceStore(path).lockPath());
    QVERIFY(lock.tryLock());
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 0);
    QVERIFY(dialog.hasUnsavedChanges());
    lock.unlock();
    const QByteArray external = document(42).toJson();
    QVERIFY(AtomicFileWriter::write(path, external));
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 0);
    QVERIFY(dialog.hasUnsavedChanges());
    QCOMPARE(readBytes(path), external);
    QVERIFY(AtomicFileWriter::write(path, current.toJsonDocument().toJson()));
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 1);
    QVERIFY(!dialog.hasUnsavedChanges());
    QVERIFY(!applyButton(dialog)->isEnabled());
}

void PreferenceStoreTests::dialogOversizedLoadAndSaveKeepDraft()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("preference.json"));
    const AppPreferences current;
    const QByteArray committed = current.toJsonDocument().toJson();
    QVERIFY(AtomicFileWriter::write(path, committed));
    PreferenceDialog dialog(path, current, {});
    QSignalSpy applied(&dialog, &PreferenceDialog::preferencesApplied);
    QLineEdit* backupPath = nullptr;
    for (auto* edit : dialog.findChildren<QLineEdit*>()) {
        if (edit->text() == current.backupPath())
            backupPath = edit;
    }
    QVERIFY(backupPath);
    const QString oversized(PreferenceStore::StorageLimits.bytes + 1, QLatin1Char('x'));
    backupPath->setMaxLength(static_cast<int>(oversized.size()));
    backupPath->setText(oversized);
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 0);
    QVERIFY(dialog.hasUnsavedChanges());
    QCOMPARE(backupPath->text(), oversized);
    QCOMPARE(readBytes(path), committed);

    const QByteArray external = QJsonDocument(QJsonObject{{QStringLiteral("value"), oversized}}).toJson();
    QVERIFY(AtomicFileWriter::write(path, external));
    QAction* reload = nullptr;
    for (auto* action : dialog.findChildren<QAction*>()) {
        if (action->text() == PreferenceDialog::tr("Reload"))
            reload = action;
    }
    QVERIFY(reload);
    answerPrompt(QMessageBox::Discard);
    reload->trigger();
    QCOMPARE(applied.count(), 0);
    QVERIFY(dialog.hasUnsavedChanges());
    QCOMPARE(backupPath->text(), oversized);
    QCOMPARE(readBytes(path), external);

    QVERIFY(AtomicFileWriter::write(path, committed));
    backupPath->setText(QStringLiteral("custom-bak"));
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 1);
    QVERIFY(!dialog.hasUnsavedChanges());
    QCOMPARE(AppPreferences::loadFromFile(path).preferences.backupPath(), QStringLiteral("custom-bak"));
}

void PreferenceStoreTests::dialogLoadsDiskInsteadOfStaleApplicationState()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("preference.json"));
    const AppPreferences stale;
    auto values = stale.values();
    values.backupMaximumCount += 3;
    const auto disk = AppPreferences::fromValues(values);
    QVERIFY(AtomicFileWriter::write(path, disk.toJsonDocument().toJson()));
    bool constructorPrompted = false;
    QTimer promptGuard;
    promptGuard.setSingleShot(true);
    connect(&promptGuard, &QTimer::timeout, this, [&] {
        if (auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            constructorPrompted = true;
            prompt->reject();
        }
    });
    promptGuard.start(0);
    PreferenceDialog dialog(path, stale, {});
    promptGuard.stop();
    QVERIFY(!constructorPrompted);
    QSignalSpy applied(&dialog, &PreferenceDialog::preferencesApplied);
    QVERIFY(!dialog.hasUnsavedChanges());
    QVERIFY(dialog.hasUnappliedChanges());
    QVERIFY(applyButton(dialog)->isEnabled());
    QCOMPARE(applied.count(), 0);
    dialog.show();
    QVERIFY(captureWidget(dialog, QStringLiteral("preferences-pending-apply")));
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 1);
    QVERIFY(!dialog.hasUnsavedChanges());
    QVERIFY(!dialog.hasUnappliedChanges());
    QVERIFY(!applyButton(dialog)->isEnabled());
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 1);
    QCOMPARE(AppPreferences::loadFromFile(path).preferences.backupMaximumCount(), disk.backupMaximumCount());
}

void PreferenceStoreTests::dialogReloadKeepsRuntimePendingUntilApply()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("preference.json"));
    const AppPreferences runtime;
    QVERIFY(AtomicFileWriter::write(path, runtime.toJsonDocument().toJson()));
    PreferenceDialog dialog(path, runtime, {});
    QSignalSpy applied(&dialog, &PreferenceDialog::preferencesApplied);
    auto values = runtime.values();
    values.backupMaximumCount += 3;
    const auto disk = AppPreferences::fromValues(values);
    QVERIFY(AtomicFileWriter::write(path, disk.toJsonDocument().toJson()));
    auto* reload = reloadAction(dialog);
    QVERIFY(reload);
    reload->trigger();
    QVERIFY(!dialog.hasUnsavedChanges());
    QVERIFY(dialog.hasUnappliedChanges());
    QVERIFY(applyButton(dialog)->isEnabled());
    QCOMPARE(applied.count(), 0);

    // A second reload must not confuse an unapplied disk draft with unsaved local edits.
    bool reloadPrompted = false;
    QTimer promptGuard;
    promptGuard.setSingleShot(true);
    connect(&promptGuard, &QTimer::timeout, this, [&] {
        if (auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            reloadPrompted = true;
            prompt->reject();
        }
    });
    promptGuard.start(0);
    reload->trigger();
    promptGuard.stop();
    QVERIFY(!reloadPrompted);
    QVERIFY(!dialog.hasUnsavedChanges());
    QVERIFY(dialog.hasUnappliedChanges());

    QLockFile lock(PreferenceStore(path).lockPath());
    QVERIFY(lock.tryLock());
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 0);
    QVERIFY(!dialog.hasUnsavedChanges());
    QVERIFY(dialog.hasUnappliedChanges());
    lock.unlock();
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 1);
    const auto result = qvariant_cast<labelqt::core::AppPreferencesLoadResult>(applied.first().first());
    QCOMPARE(result.preferences.backupMaximumCount(), disk.backupMaximumCount());
    QVERIFY(!dialog.hasUnsavedChanges());
    QVERIFY(!dialog.hasUnappliedChanges());
    QVERIFY(!applyButton(dialog)->isEnabled());
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 1);
}

void PreferenceStoreTests::dialogCloseConfirmsUnappliedDiskDraft()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("preference.json"));
    const AppPreferences runtime;
    auto values = runtime.values();
    values.backupMaximumCount += 3;
    QVERIFY(AtomicFileWriter::write(path, AppPreferences::fromValues(values).toJsonDocument().toJson()));
    PreferenceDialog dialog(path, runtime, {});
    QSignalSpy applied(&dialog, &PreferenceDialog::preferencesApplied);
    dialog.show();
    QVERIFY(!dialog.hasUnsavedChanges());
    QVERIFY(dialog.hasUnappliedChanges());
    answerPrompt(QMessageBox::Cancel);
    dialog.close();
    QVERIFY(dialog.isVisible());
    QCOMPARE(applied.count(), 0);
    QVERIFY(dialog.hasUnappliedChanges());
    applyFromPrompt();
    dialog.close();
    QVERIFY(!dialog.isVisible());
    QCOMPARE(applied.count(), 1);
    QVERIFY(!dialog.hasUnsavedChanges());
    QVERIFY(!dialog.hasUnappliedChanges());
}

void PreferenceStoreTests::dialogDiskDirtyCanMatchRuntime()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("preference.json"));
    const AppPreferences runtime;
    auto values = runtime.values();
    values.backupPath = QStringLiteral("external-backups");
    QVERIFY(AtomicFileWriter::write(path, AppPreferences::fromValues(values).toJsonDocument().toJson()));
    PreferenceDialog dialog(path, runtime, {});
    QSignalSpy applied(&dialog, &PreferenceDialog::preferencesApplied);
    QLineEdit* backupPath = nullptr;
    for (auto* edit : dialog.findChildren<QLineEdit*>()) {
        if (edit->text() == values.backupPath)
            backupPath = edit;
    }
    QVERIFY(backupPath);
    backupPath->setText(runtime.backupPath());
    QVERIFY(dialog.hasUnsavedChanges());
    QVERIFY(!dialog.hasUnappliedChanges());
    QVERIFY(applyButton(dialog)->isEnabled());
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 1);
    QCOMPARE(AppPreferences::loadFromFile(path).preferences.backupPath(), runtime.backupPath());
    QVERIFY(!dialog.hasUnsavedChanges());
    QVERIFY(!dialog.hasUnappliedChanges());
    QVERIFY(!applyButton(dialog)->isEnabled());
}

void PreferenceStoreTests::dialogApplyOnCloseSurvivesWriteFailure()
{
    QTemporaryDir dir;
    const QString directory = dir.filePath(QStringLiteral("settings"));
    const QString path = QDir(directory).filePath(QStringLiteral("preference.json"));
    PreferenceDialog dialog(path, AppPreferences{}, {});
    QSignalSpy applied(&dialog, &PreferenceDialog::preferencesApplied);
    dialog.show();
    changeDraft(dialog);
    QVERIFY(AtomicFileWriter::write(directory, QByteArrayLiteral("blocked")));
    applyFromPrompt();
    dialog.close();
    QVERIFY(dialog.isVisible());
    QVERIFY(dialog.hasUnsavedChanges());
    QCOMPARE(applied.count(), 0);
    QCOMPARE(readBytes(directory), QByteArrayLiteral("blocked"));
    QVERIFY(QFile::remove(directory));
    applyFromPrompt();
    dialog.close();
    QVERIFY(!dialog.isVisible());
    QVERIFY(!dialog.hasUnsavedChanges());
    QCOMPARE(applied.count(), 1);
    QVERIFY(QFile::exists(path));
}

void PreferenceStoreTests::dialogCloseAndReloadProtectDraft()
{
    QTemporaryDir dir;
    PreferenceDialog dialog(dir.filePath(QStringLiteral("preference.json")), AppPreferences{}, {});
    dialog.show();
    changeDraft(dialog);
    answerPrompt(QMessageBox::Cancel);
    dialog.close();
    QVERIFY(dialog.isVisible());
    QVERIFY(dialog.hasUnsavedChanges());
    QAction* reload = nullptr;
    for (auto* action : dialog.findChildren<QAction*>()) {
        if (action->text() == PreferenceDialog::tr("Reload"))
            reload = action;
    }
    QVERIFY(reload);
    answerPrompt(QMessageBox::Cancel);
    reload->trigger();
    QVERIFY(dialog.hasUnsavedChanges());
    answerPrompt(QMessageBox::Discard);
    reload->trigger();
    QVERIFY(!dialog.hasUnsavedChanges());
    changeDraft(dialog);
    answerPrompt(QMessageBox::Discard);
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QVERIFY(!dialog.isVisible());
}

void PreferenceStoreTests::dialogSearchAndOptionalJson()
{
    QTemporaryDir dir;
    PreferenceDialog dialog(dir.filePath(QStringLiteral("preference.json")), AppPreferences{}, {});
    auto* categories = dialog.findChild<QListWidget*>();
    QCOMPARE(categories->count(), 8);
    QVERIFY(!dialog.findChild<QPlainTextEdit*>());
    dialog.show();
    QVERIFY(captureWidget(dialog, QStringLiteral("preferences")));
    categories->setCurrentRow(2);
    QVERIFY(captureWidget(dialog, QStringLiteral("preferences-label-display")));
    categories->setCurrentRow(0);
    QLineEdit* search = nullptr;
    for (auto* edit : dialog.findChildren<QLineEdit*>()) {
        if (edit->placeholderText() == PreferenceDialog::tr("Search..."))
            search = edit;
    }
    QVERIFY(search);
    search->setText(QStringLiteral("backup"));
    QVERIFY(!categories->item(0)->isHidden());
    search->setText(QStringLiteral("there-is-no-such-setting"));
    QVERIFY(dialog.findChild<QStackedWidget*>()->isHidden());
    search->clear();
    QVERIFY(!dialog.findChild<QStackedWidget*>()->isHidden());
    for (auto* action : dialog.findChildren<QAction*>()) {
        if (action->text() == PreferenceDialog::tr("JSON preview")) {
            action->trigger();
            break;
        }
    }
    auto* preview = dialog.findChild<QPlainTextEdit*>();
    QVERIFY(preview);
    QVERIFY(preview->isReadOnly());
    const QString before = preview->toPlainText();
    changeDraft(dialog);
    QVERIFY(before != preview->toPlainText());
    auto* previewDialog = qobject_cast<QDialog*>(preview->parentWidget());
    QVERIFY(previewDialog);
    previewDialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    changeDraft(dialog);
}

void PreferenceStoreTests::dialogOpacitySlidersPreservePercentValues()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("preference.json"));
    const AppPreferences runtime;
    PreferenceDialog dialog(path, runtime, {});
    QSignalSpy applied(&dialog, &PreferenceDialog::preferencesApplied);
    const auto sliders = dialog.findChildren<QSlider*>();
    QCOMPARE(sliders.size(), 2);
    QCOMPARE(sliders[0]->value(), qRound(runtime.markerTextBubbleOpacity() * 100.0));
    QCOMPARE(sliders[1]->value(), qRound(runtime.canvasLabelTextEditorOpacity() * 100.0));
    for (auto* slider : sliders) {
        QCOMPARE(slider->orientation(), Qt::Horizontal);
        QCOMPARE(slider->minimum(), 0);
        QCOMPARE(slider->maximum(), 100);
        QCOMPARE(slider->singleStep(), 5);
        QCOMPARE(slider->pageStep(), 10);
        auto* label = slider->parentWidget()->findChild<QLabel*>();
        QVERIFY(label);
        QCOMPARE(label->minimumWidth(), label->fontMetrics().horizontalAdvance(QStringLiteral("100%")));
        slider->setValue(0);
        QCOMPARE(label->text(), PreferenceDialog::tr("%1%").arg(0));
        slider->setValue(100);
        QCOMPARE(label->text(), PreferenceDialog::tr("%1%").arg(100));
    }
    sliders[0]->setValue(35);
    sliders[1]->setValue(60);
    QVERIFY(dialog.hasUnsavedChanges());
    QVERIFY(applyButton(dialog)->isEnabled());
    applyButton(dialog)->click();
    QCOMPARE(applied.count(), 1);
    const auto saved = AppPreferences::loadFromFile(path).preferences;
    QCOMPARE(saved.markerTextBubbleOpacity(), 0.35);
    QCOMPARE(saved.canvasLabelTextEditorOpacity(), 0.60);
    PreferenceDialog reopened(path, saved, {});
    const auto reloadedSliders = reopened.findChildren<QSlider*>();
    QCOMPARE(reloadedSliders.size(), 2);
    QCOMPARE(reloadedSliders[0]->value(), 35);
    QCOMPARE(reloadedSliders[1]->value(), 60);
}

QTEST_MAIN(PreferenceStoreTests)
#include "PreferenceStoreTests.moc"
