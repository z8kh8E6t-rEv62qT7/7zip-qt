// tests/filemanager/behavior/drag_drop.cpp
// Role: Drag/drop bridge behavior cases.

#include "internal.h"

using namespace filemanager_behavior_internal;

namespace {

struct DispatchResult {
  bool handled = false;
  bool accepted = false;
};

DispatchResult dispatch_local_file_drop_raw(z7::ui::filemanager::MainWindow* window,
                                            QObject* target,
                                            const QStringList& paths,
                                            Qt::MouseButtons buttons = Qt::LeftButton,
                                            Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                                            QPointF pos = QPointF(2000.0, 2000.0)) {
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

  QDropEvent positioned_event(pos,
                   Qt::CopyAction | Qt::MoveAction,
                   mime,
                   buttons,
                   modifiers);
  const bool handled = window->eventFilter(target, &positioned_event);
  const bool accepted = positioned_event.isAccepted();
  delete mime;
  return {handled, accepted};
}

bool dispatch_local_file_drop(z7::ui::filemanager::MainWindow* window,
                              QObject* target,
                              const QStringList& paths,
                              Qt::MouseButtons buttons = Qt::LeftButton,
                              Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                              QPointF pos = QPointF(2000.0, 2000.0)) {
  const DispatchResult result =
      dispatch_local_file_drop_raw(window, target, paths, buttons, modifiers, pos);
  return result.handled && result.accepted;
}

}  // namespace

void FileManagerBehaviorTest::dropExternalFilesInFilesystemPanelStartsAddDialogBridge() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("payload.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("drop payload");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    QVERIFY(window.panels_[0].ui.details_view != nullptr);
    QVERIFY(window.panels_[0].ui.details_view->viewport() != nullptr);
    reset_bridge_segments_for_test();
    QVERIFY(dispatch_local_file_drop(
        &window, window.panels_[0].ui.details_view->viewport(), QStringList{source_file}));

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
    QVERIFY(payload.archive_path.startsWith(QDir::fromNativeSeparators(root.path()) +
                                            QLatin1Char('/')));
}

void FileManagerBehaviorTest::dropExternalFilesInArchiveViewUpdatesCurrentArchive() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("drop-me.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("update archive via drop");
    }

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    window.active_panel_controller().archive.type_hint = QStringLiteral("7z");
    window.setProperty("z7.fm.drop.copy_to_archive.confirm.override",
                       QStringLiteral("yes"));

    QVERIFY(window.panels_[0].ui.details_view != nullptr);
    QVERIFY(window.panels_[0].ui.details_view->viewport() != nullptr);
    QVERIFY(dispatch_local_file_drop(
        &window, window.panels_[0].ui.details_view->viewport(), QStringList{source_file}));

    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(row_by_name(window, QStringLiteral("drop-me.txt")) >= 0, 20000);
}

void FileManagerBehaviorTest::dropExternalFilesOnWindowTargetUsesActivePanelFilesystemMode() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("window-drop.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("window target filesystem drop");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    QWidget* drop_target = window.centralWidget();
    QVERIFY(drop_target != nullptr);
    reset_bridge_segments_for_test();
    QVERIFY(dispatch_local_file_drop(
        &window, drop_target, QStringList{source_file}));

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
    QVERIFY(payload.archive_path.startsWith(
        QDir::fromNativeSeparators(root.path()) + QLatin1Char('/')));
}

void FileManagerBehaviorTest::dropExternalFilesOnWindowTargetUsesActivePanelArchiveMode() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("window-archive.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("window target archive drop");
    }

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    QWidget* drop_target = window.centralWidget();
    QVERIFY(drop_target != nullptr);
    reset_bridge_segments_for_test();
    QVERIFY(dispatch_local_file_drop(
        &window, drop_target, QStringList{source_file}));

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
    QVERIFY(payload.archive_path.startsWith(
        QDir::fromNativeSeparators(root.path()) + QLatin1Char('/')));
}
