// tests/filemanager/behavior/drag_drop_right_button.cpp
// Role: Drag/drop behavior cases for right-button menu semantics.

#include "internal.h"

using namespace filemanager_behavior_internal;

namespace {

struct DispatchResult {
  bool handled = false;
  bool accepted = false;
  Qt::DropAction action = Qt::IgnoreAction;
  QByteArray performed_drop_effect;
  QByteArray logical_performed_drop_effect;
};

DispatchResult dispatch_local_file_drop_raw(z7::ui::filemanager::MainWindow* window,
                                            QObject* target,
                                            const QStringList& paths,
                                            Qt::MouseButtons buttons = Qt::LeftButton,
                                            Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                                            QPointF pos = QPointF(2000.0, 2000.0),
                                            Qt::DropActions possible_actions =
                                                Qt::CopyAction | Qt::MoveAction) {
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
  delete mime;
  return {handled,
          accepted,
          action,
          performed_drop_effect,
          logical_performed_drop_effect};
}

bool dispatch_local_file_drop(z7::ui::filemanager::MainWindow* window,
                              QObject* target,
                              const QStringList& paths,
                              Qt::MouseButtons buttons = Qt::LeftButton,
                              Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                              QPointF pos = QPointF(2000.0, 2000.0),
                              Qt::DropActions possible_actions =
                                  Qt::CopyAction | Qt::MoveAction) {
  const DispatchResult result =
      dispatch_local_file_drop_raw(window,
                                   target,
                                   paths,
                                   buttons,
                                   modifiers,
                                   pos,
                                   possible_actions);
  return result.handled && result.accepted;
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

void FileManagerBehaviorTest::rightDropExternalFilesOnWindowTargetUsesActivePanelArchiveMode() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("window-archive-right.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("window target archive right drop");
    }

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    window.active_panel_controller().archive.type_hint = QStringLiteral("7z");
    window.setProperty("z7.fm.drop.command.override", QStringLiteral("copy_to_archive"));
    window.setProperty("z7.fm.drop.copy_to_archive.confirm.override",
                       QStringLiteral("yes"));

    QWidget* drop_target = window.centralWidget();
    QVERIFY(drop_target != nullptr);
    QVERIFY(dispatch_local_file_drop(
        &window, drop_target, QStringList{source_file}, Qt::RightButton));

    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(
        row_by_name(window, QStringLiteral("window-archive-right.txt")) >= 0,
        20000);
}

void FileManagerBehaviorTest::rightDropExternalFilesCanCopyIntoTargetDirectory() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("copy-me.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("copy by right drop");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.command.override", QStringLiteral("copy"));

    bool sevenzip_launched = false;
    window.external_command_launcher_ =
        [&sevenzip_launched](const QString&,
                             const QStringList&,
                             const QString&,
                             qint64*) {
          sevenzip_launched = true;
          return true;
        };

    QVERIFY(window.panels_[0].ui.details_view != nullptr);
    QVERIFY(window.panels_[0].ui.details_view->viewport() != nullptr);
    QVERIFY(dispatch_local_file_drop(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::RightButton));

    const QString copied_file = QDir(root.path()).filePath(QStringLiteral("copy-me.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(copied_file), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(QFileInfo::exists(source_file));
    QVERIFY(!sevenzip_launched);
}

void FileManagerBehaviorTest::rightDropExternalFilesCanMoveIntoTargetDirectory() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("move-me.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("move by right drop");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.command.override", QStringLiteral("move"));

    bool sevenzip_launched = false;
    window.external_command_launcher_ =
        [&sevenzip_launched](const QString&,
                             const QStringList&,
                             const QString&,
                             qint64*) {
          sevenzip_launched = true;
          return true;
        };

    QVERIFY(window.panels_[0].ui.details_view != nullptr);
    QVERIFY(window.panels_[0].ui.details_view->viewport() != nullptr);
    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::RightButton);
    QVERIFY(result.handled);
    QVERIFY(result.accepted);
    // Parity with original PanelDrag::Drop(): external sources should not
    // receive move effect to avoid source-side duplicate deletion.
    QCOMPARE(result.action, Qt::CopyAction);
    QCOMPARE(decode_dword_le(result.performed_drop_effect), 1u);
    QCOMPARE(decode_dword_le(result.logical_performed_drop_effect), 1u);

    const QString moved_file = QDir(root.path()).filePath(QStringLiteral("move-me.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(moved_file), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(!QFileInfo::exists(source_file));
    QVERIFY(!sevenzip_launched);
}

void FileManagerBehaviorTest::rightDropMoveOverrideFallsBackToCopyWhenMoveNotAllowed() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("move-override-fallback.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("move override fallback to copy");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.command.override", QStringLiteral("move"));

    bool sevenzip_launched = false;
    window.external_command_launcher_ =
        [&sevenzip_launched](const QString&,
                             const QStringList&,
                             const QString&,
                             qint64*) {
          sevenzip_launched = true;
          return true;
        };

    QVERIFY(window.panels_[0].ui.details_view != nullptr);
    QVERIFY(window.panels_[0].ui.details_view->viewport() != nullptr);
    QVERIFY(dispatch_local_file_drop(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::RightButton,
        Qt::NoModifier,
        QPointF(2000.0, 2000.0),
        Qt::CopyAction));

    const QString copied_file = QDir(root.path()).filePath(
        QStringLiteral("move-override-fallback.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(copied_file), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(QFileInfo::exists(source_file));
    QVERIFY(!sevenzip_launched);
}

void FileManagerBehaviorTest::rightDropMenuSelectionRespectsPossibleActions() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("menu-actions-filter.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("right drop menu action filtering");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.menu.selection.override", QStringLiteral("move"));

    bool sevenzip_launched = false;
    window.external_command_launcher_ =
        [&sevenzip_launched](const QString&,
                             const QStringList&,
                             const QString&,
                             qint64*) {
          sevenzip_launched = true;
          return true;
        };

    const DispatchResult unavailable_move = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::RightButton,
        Qt::NoModifier,
        QPointF(2000.0, 2000.0),
        Qt::CopyAction);
    QVERIFY(unavailable_move.handled);
    QVERIFY(!unavailable_move.accepted);
    QVERIFY(QFileInfo::exists(source_file));
    QVERIFY(!QFileInfo::exists(QDir(root.path()).filePath(
        QStringLiteral("menu-actions-filter.txt"))));
    QVERIFY(!sevenzip_launched);

    window.setProperty("z7.fm.drop.menu.selection.override", QStringLiteral("copy"));
    QVERIFY(dispatch_local_file_drop(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::RightButton,
        Qt::NoModifier,
        QPointF(2000.0, 2000.0),
        Qt::CopyAction));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(
                                 QDir(root.path()).filePath(
                                     QStringLiteral("menu-actions-filter.txt"))),
                             20000);
    QVERIFY(QFileInfo::exists(source_file));
}

void FileManagerBehaviorTest::rightDropCopyIntoHoveredFolderRow() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("row-copy.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("copy into hovered folder row");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.command.override", QStringLiteral("copy"));

    bool sevenzip_launched = false;
    window.external_command_launcher_ =
        [&sevenzip_launched](const QString&,
                             const QStringList&,
                             const QString&,
                             qint64*) {
          sevenzip_launched = true;
          return true;
        };

    const int target_row = row_by_name(window, QStringLiteral("target"));
    QVERIFY(target_row >= 0);
    const QPointF drop_pos = row_center_in_viewport(window.panels_[0].ui.details_view, target_row);
    QVERIFY(dispatch_local_file_drop(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::RightButton,
        Qt::NoModifier,
        drop_pos));

    const QString copied_file = QDir(target_dir).filePath(QStringLiteral("row-copy.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(copied_file), 20000);
    QVERIFY(QFileInfo::exists(source_file));
    QVERIFY(!sevenzip_launched);
}

void FileManagerBehaviorTest::rightDropAddToArchiveOnFileRowUsesBridge() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("row-add.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("file-row add bridge");
    }
    const QString existing_file = QDir(root.path()).filePath(QStringLiteral("existing.txt"));
    {
      QFile f(existing_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("existing file row");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.command.override",
                       QStringLiteral("add_to_archive"));
    window.resize(900, 600);
    window.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    const int file_row = row_by_name(window, QStringLiteral("existing.txt"));
    QVERIFY(file_row >= 0);
    const QModelIndex file_index =
        window.panels_[0].ui.details_view->model()->index(file_row, 0);
    QVERIFY(file_index.isValid());
    window.panels_[0].ui.details_view->scrollTo(file_index);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const QPointF drop_pos = row_center_in_viewport(window.panels_[0].ui.details_view, file_row);
    QCOMPARE(window.panels_[0].ui.details_view->indexAt(drop_pos.toPoint()).row(),
             file_row);
    reset_bridge_segments_for_test();
    QString warning_title;
    QString warning_text;
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           &warning_title,
                                           &warning_text);
    QVERIFY(dispatch_local_file_drop(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::RightButton,
        Qt::NoModifier,
        drop_pos));

    z7::ui::gui::BridgeTaskPayload payload;
    QString payload_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 z7::ui::gui::BridgeCommandKind::kAdd,
                 &payload,
                 &payload_error),
             qPrintable(QStringLiteral("%1 %2 %3")
                            .arg(payload_error, warning_title, warning_text)));
    QCOMPARE(payload.command, z7::ui::gui::BridgeCommandKind::kAdd);
    QVERIFY(payload.show_dialog);
    QVERIFY(payload.refresh_after_finish);
    QVERIFY(payload.input_paths.contains(source_file));
    QVERIFY(QFileInfo::exists(source_file));
}

void FileManagerBehaviorTest::rightDropToArchiveRequiresConfirmBeforeUpdate() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("archive-drop.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("archive right drop");
    }

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    window.active_panel_controller().archive.type_hint = QStringLiteral("7z");
    window.setProperty("z7.fm.drop.command.override", QStringLiteral("copy_to_archive"));
    window.setProperty("z7.fm.drop.copy_to_archive.confirm.override",
                       QStringLiteral("no"));

    const DispatchResult canceled_drop = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::RightButton);
    QVERIFY(canceled_drop.handled);
    QVERIFY(!canceled_drop.accepted);

    window.setProperty("z7.fm.drop.copy_to_archive.confirm.override",
                       QStringLiteral("yes"));
    QVERIFY(dispatch_local_file_drop(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::RightButton));
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(
        row_by_name(window, QStringLiteral("archive-drop.txt")) >= 0,
        20000);
}
