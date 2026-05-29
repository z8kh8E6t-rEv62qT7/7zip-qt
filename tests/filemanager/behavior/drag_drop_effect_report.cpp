// tests/filemanager/behavior/drag_drop_effect_report.cpp
// Role: Drop-action reporting parity between internal and external drag sources.

#include "internal.h"

using namespace filemanager_behavior_internal;

namespace {

constexpr const char* kMimeTypeZ7FmFsSource =
    "application/x-z7-filemanager-fs-source";

struct DispatchResult {
  bool handled = false;
  bool accepted = false;
  Qt::DropAction action = Qt::IgnoreAction;
  QByteArray performed_drop_effect;
  QByteArray logical_performed_drop_effect;
  QByteArray performed_drop_effect_unquoted;
  QByteArray logical_performed_drop_effect_unquoted;
};

DispatchResult dispatch_local_file_drop_raw(
    z7::ui::filemanager::MainWindow* window,
    QObject* target,
    const QStringList& paths,
    Qt::MouseButtons buttons = Qt::LeftButton,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier,
    QPointF pos = QPointF(2000.0, 2000.0),
    const QByteArray& internal_fs_source_marker = QByteArray(),
    Qt::DropActions possible_actions = Qt::CopyAction | Qt::MoveAction) {
  if (window == nullptr || target == nullptr || paths.isEmpty()) {
    return {};
  }

  auto* mime = new QMimeData();
  QList<QUrl> urls;
  urls.reserve(paths.size());
  for (const QString& path : paths) {
    urls << QUrl::fromLocalFile(path);
  }
  mime->setUrls(urls);
  if (!internal_fs_source_marker.isEmpty() && !urls.isEmpty()) {
    mime->setData(QString::fromLatin1(kMimeTypeZ7FmFsSource),
                  internal_fs_source_marker);
  }

  QDropEvent positioned_event(pos,
                              possible_actions,
                              mime,
                              buttons,
                              modifiers);
  const bool handled = window->eventFilter(target, &positioned_event);
  const bool accepted = positioned_event.isAccepted();
  const Qt::DropAction action = positioned_event.dropAction();
  const QByteArray performed_drop_effect =
      mime->data(QString::fromLatin1(kWinMimePerformedDropEffect));
  const QByteArray logical_performed_drop_effect =
      mime->data(QString::fromLatin1(kWinMimeLogicalPerformedDropEffect));
  const QByteArray performed_drop_effect_unquoted =
      mime->data(QString::fromLatin1(kWinMimePerformedDropEffectUnquoted));
  const QByteArray logical_performed_drop_effect_unquoted =
      mime->data(QString::fromLatin1(kWinMimeLogicalPerformedDropEffectUnquoted));
  delete mime;
  return {handled,
          accepted,
          action,
          performed_drop_effect,
          logical_performed_drop_effect,
          performed_drop_effect_unquoted,
          logical_performed_drop_effect_unquoted};
}

QPointF row_center_in_viewport(QAbstractItemView* view, int row) {
  if (view == nullptr || view->model() == nullptr || row < 0) {
    return QPointF(2000.0, 2000.0);
  }
  const QModelIndex index = view->model()->index(row, 0);
  if (!index.isValid()) {
    return QPointF(2000.0, 2000.0);
  }

  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  const QRect rect = view->visualRect(index);
  if (!rect.isValid()) {
    const int fallback_y = 10 + row * 24;
    return QPointF(16.0, static_cast<qreal>(fallback_y));
  }
  return QPointF(rect.center());
}

}  // namespace

void FileManagerBehaviorTest::forcedMoveDropReportsCopyActionForExternalSource() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("external-forced-move.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("external forced move");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.command.override", QStringLiteral("move"));

    const int target_row = row_by_name(window, QStringLiteral("target"));
    QVERIFY(target_row >= 0);
    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::LeftButton,
        Qt::NoModifier,
        row_center_in_viewport(window.panels_[0].ui.details_view, target_row));
    QVERIFY(result.handled);
    QVERIFY(result.accepted);
    QCOMPARE(result.action, Qt::CopyAction);
    QCOMPARE(decode_dword_le(result.performed_drop_effect), 1u);
    QCOMPARE(decode_dword_le(result.logical_performed_drop_effect), 1u);
    QCOMPARE(decode_dword_le(result.performed_drop_effect_unquoted), 1u);
    QCOMPARE(decode_dword_le(result.logical_performed_drop_effect_unquoted), 1u);

    const QString moved_file =
        QDir(target_dir).filePath(QStringLiteral("external-forced-move.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(moved_file), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(source_file), 20000);
}

void FileManagerBehaviorTest::forcedMoveDropReportsMoveActionForInternalSource() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("internal-forced-move.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("internal forced move");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.command.override", QStringLiteral("move"));
    window.setProperty("z7.fm.drop.source.internal_fs.override", true);
    window.setProperty("z7.fm.drop.source.same_volume.override", true);
    window.setProperty("z7.fm.drop.source.trusted_internal.override", true);

    const int target_row = row_by_name(window, QStringLiteral("target"));
    QVERIFY(target_row >= 0);
    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::LeftButton,
        Qt::NoModifier,
        row_center_in_viewport(window.panels_[0].ui.details_view, target_row));
    QVERIFY(result.handled);
    QVERIFY(result.accepted);
    QCOMPARE(result.action, Qt::MoveAction);
    QCOMPARE(decode_dword_le(result.performed_drop_effect), 1u);
    QCOMPARE(decode_dword_le(result.logical_performed_drop_effect), 2u);

    const QString moved_file =
        QDir(target_dir).filePath(QStringLiteral("internal-forced-move.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(moved_file), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(source_file), 20000);
}

void FileManagerBehaviorTest::canceledDropReportsNonePerformedEffects() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("drop-cancelled.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("cancelled drop should report none effects");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.command.override", QStringLiteral("cancel"));

    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::LeftButton,
        Qt::NoModifier,
        QPointF(2000.0, 2000.0));
    QVERIFY(result.handled);
    QVERIFY(!result.accepted);
    QCOMPARE(result.action, Qt::IgnoreAction);
    QCOMPARE(decode_dword_le(result.performed_drop_effect), 0u);
    QCOMPARE(decode_dword_le(result.logical_performed_drop_effect), 0u);
    QCOMPARE(decode_dword_le(result.performed_drop_effect_unquoted), 0u);
    QCOMPARE(decode_dword_le(result.logical_performed_drop_effect_unquoted), 0u);
    QVERIFY(QFileInfo::exists(source_file));
}

void FileManagerBehaviorTest::addDialogLauncherFailureReportsNonePerformedEffects() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("add-launcher-fail.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("launcher failure should report none effects");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    reset_bridge_segments_for_test();
    QString warning_title;
    QString warning_text;
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           &warning_title,
                                           &warning_text);
    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::LeftButton,
        Qt::NoModifier,
        QPointF(2000.0, 2000.0));
    QVERIFY(result.handled);
    QVERIFY2(result.accepted,
             qPrintable(QStringLiteral("%1: %2").arg(warning_title,
                                                      warning_text)));
    QCOMPARE(result.action, Qt::CopyAction);
    QCOMPARE(decode_dword_le(result.performed_drop_effect), 1u);
    QCOMPARE(decode_dword_le(result.logical_performed_drop_effect), 1u);

    z7::ui::gui::BridgeTaskPayload payload;
    QString payload_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 z7::ui::gui::BridgeCommandKind::kAdd,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, z7::ui::gui::BridgeCommandKind::kAdd);
    QVERIFY(payload.show_dialog);
    QVERIFY(payload.refresh_after_finish);
    QVERIFY(payload.input_paths.contains(source_file));
    QVERIFY(QFileInfo::exists(source_file));
}

void FileManagerBehaviorTest::taggedInternalSourceMoveReportsCopyActionWhenNotTrusted() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file =
        QDir(source_dir).filePath(QStringLiteral("tagged-untrusted-move.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("tagged source move with untrusted report");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.source.same_volume.override", true);

    const int target_row = row_by_name(window, QStringLiteral("target"));
    QVERIFY(target_row >= 0);
    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::LeftButton,
        Qt::NoModifier,
        row_center_in_viewport(window.panels_[0].ui.details_view, target_row),
        QByteArrayLiteral("1"));
    QVERIFY(result.handled);
    QVERIFY(result.accepted);
    QCOMPARE(result.action, Qt::CopyAction);
    QCOMPARE(decode_dword_le(result.performed_drop_effect), 1u);
    QCOMPARE(decode_dword_le(result.logical_performed_drop_effect), 1u);

    const QString moved_file =
        QDir(target_dir).filePath(QStringLiteral("tagged-untrusted-move.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(moved_file), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(source_file), 20000);
}

void FileManagerBehaviorTest::taggedTrustedInternalSourceMoveReportsMoveAction() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file =
        QDir(source_dir).filePath(QStringLiteral("tagged-trusted-move.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("tagged source move with trusted marker");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.source.same_volume.override", true);

    const QVariant previous_instance =
        QCoreApplication::instance()->property("z7.fm.drag.source.instance_id");
    const QString instance_id = QStringLiteral("test-trusted-instance");
    QCoreApplication::instance()->setProperty("z7.fm.drag.source.instance_id",
                                              instance_id);
    const QByteArray trusted_marker = QStringLiteral("v1;pid=%1;sid=%2")
                                          .arg(QCoreApplication::applicationPid())
                                          .arg(instance_id)
                                          .toUtf8();

    const int target_row = row_by_name(window, QStringLiteral("target"));
    QVERIFY(target_row >= 0);
    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::LeftButton,
        Qt::NoModifier,
        row_center_in_viewport(window.panels_[0].ui.details_view, target_row),
        trusted_marker);
    QCoreApplication::instance()->setProperty("z7.fm.drag.source.instance_id",
                                              previous_instance);

    QVERIFY(result.handled);
    QVERIFY(result.accepted);
    QCOMPARE(result.action, Qt::MoveAction);
    QCOMPARE(decode_dword_le(result.performed_drop_effect), 1u);
    QCOMPARE(decode_dword_le(result.logical_performed_drop_effect), 2u);

    const QString moved_file =
        QDir(target_dir).filePath(QStringLiteral("tagged-trusted-move.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(moved_file), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(source_file), 20000);
}
