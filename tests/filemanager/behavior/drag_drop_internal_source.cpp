// tests/filemanager/behavior/drag_drop_internal_source.cpp
// Role: Drag/drop behavior cases for internal filesystem source semantics.

#include "internal.h"

using namespace filemanager_behavior_internal;

namespace {

constexpr const char* kMimeTypeZ7FmFsSource =
    "application/x-z7-filemanager-fs-source";

struct DispatchResult {
  bool handled = false;
  bool accepted = false;
  Qt::DropAction action = Qt::IgnoreAction;
};

DispatchResult dispatch_local_file_drop_raw(z7::ui::filemanager::MainWindow* window,
                                            QObject* target,
                                            const QStringList& paths,
                                            Qt::MouseButtons buttons = Qt::LeftButton,
                                            Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                                            QPointF pos = QPointF(2000.0, 2000.0),
                                            bool tag_internal_fs_source = false,
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
  if (tag_internal_fs_source && !urls.isEmpty()) {
    mime->setData(QString::fromLatin1(kMimeTypeZ7FmFsSource),
                  QByteArrayLiteral("1"));
  }

  QDropEvent positioned_event(pos,
                              possible_actions,
                              mime,
                              buttons,
                              modifiers);
  const bool handled = window->eventFilter(target, &positioned_event);
  const bool accepted = positioned_event.isAccepted();
  const Qt::DropAction action = positioned_event.dropAction();
  delete mime;
  return {handled, accepted, action};
}

bool dispatch_local_file_drop(z7::ui::filemanager::MainWindow* window,
                              QObject* target,
                              const QStringList& paths,
                              Qt::MouseButtons buttons = Qt::LeftButton,
                              Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                              QPointF pos = QPointF(2000.0, 2000.0),
                              bool tag_internal_fs_source = false,
                              Qt::DropActions possible_actions =
                                  Qt::CopyAction | Qt::MoveAction) {
  const DispatchResult result =
      dispatch_local_file_drop_raw(window,
                                   target,
                                   paths,
                                   buttons,
                                   modifiers,
                                   pos,
                                   tag_internal_fs_source,
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

void FileManagerBehaviorTest::dropInternalFsSourceOnSamePanelIsIgnored() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("same-panel-ignore.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("same panel drop should be ignored");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.source.panel_index.override", 0);
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

    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::LeftButton);
    QVERIFY(result.handled);
    QVERIFY(!result.accepted);
    QVERIFY(QFileInfo::exists(source_file));
    QVERIFY(!QFileInfo::exists(QDir(root.path()).filePath(QStringLiteral("same-panel-ignore.txt"))));
    QVERIFY(!sevenzip_launched);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
}

void FileManagerBehaviorTest::leftDropInternalFsSourceUsesMoveByDefaultOnSameVolume() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("left-move-default.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("left drop internal fs default move");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.source.internal_fs.override", true);
    window.setProperty("z7.fm.drop.source.trusted_internal.override", true);
    window.setProperty("z7.fm.drop.source.same_volume.override", true);

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
    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::LeftButton,
        Qt::NoModifier,
        drop_pos);
    QVERIFY(result.handled);
    QVERIFY(result.accepted);
    QCOMPARE(result.action, Qt::MoveAction);

    const QString moved_file = QDir(target_dir).filePath(QStringLiteral("left-move-default.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(moved_file), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(source_file), 20000);
    QVERIFY(!sevenzip_launched);
}

void FileManagerBehaviorTest::leftDropInternalFsSourceUsesCopyWhenCtrlPressed() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("left-copy-ctrl.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("left drop internal fs ctrl copy");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.source.internal_fs.override", true);
    window.setProperty("z7.fm.drop.source.same_volume.override", true);

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
    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::LeftButton,
        Qt::ControlModifier,
        drop_pos);
    QVERIFY(result.handled);
    QVERIFY(result.accepted);
    QCOMPARE(result.action, Qt::CopyAction);

    const QString copied_file = QDir(target_dir).filePath(QStringLiteral("left-copy-ctrl.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(copied_file), 20000);
    QVERIFY(QFileInfo::exists(source_file));
    QVERIFY(!sevenzip_launched);
}

void FileManagerBehaviorTest::leftDropInternalFsSourceUsesCopyByDefaultOnDifferentVolume() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("left-copy-default.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("left drop internal fs default copy");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.source.internal_fs.override", true);
    window.setProperty("z7.fm.drop.source.same_volume.override", false);

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
        Qt::LeftButton,
        Qt::NoModifier,
        drop_pos));

    const QString copied_file = QDir(target_dir).filePath(QStringLiteral("left-copy-default.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(copied_file), 20000);
    QVERIFY(QFileInfo::exists(source_file));
    QVERIFY(!sevenzip_launched);
}

void FileManagerBehaviorTest::leftDropInternalFsSourceCtrlShiftUsesDefaultMoveOnSameVolume() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("left-ctrl-shift.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("left drop internal fs ctrl+shift default");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.source.internal_fs.override", true);
    window.setProperty("z7.fm.drop.source.same_volume.override", true);

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
        Qt::LeftButton,
        Qt::ControlModifier | Qt::ShiftModifier,
        drop_pos));

    const QString moved_file = QDir(target_dir).filePath(QStringLiteral("left-ctrl-shift.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(moved_file), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(source_file), 20000);
    QVERIFY(!sevenzip_launched);
}

void FileManagerBehaviorTest::leftDropTaggedInternalFsSourceUsesMoveByDefaultOnSameVolume() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("left-tagged-default.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("left drop tagged internal fs default move");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

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
        Qt::LeftButton,
        Qt::NoModifier,
        drop_pos,
        true));

    const QString moved_file = QDir(target_dir).filePath(QStringLiteral("left-tagged-default.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(moved_file), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(source_file), 20000);
    QVERIFY(!sevenzip_launched);
}

void FileManagerBehaviorTest::filesystemModelMimeDataIncludesInternalSourceTag() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("mime-tagged-source.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("mime data internal source marker");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    auto* model = window.panels_[0].ui.details_view->model();
    QVERIFY(model != nullptr);

    const int file_row = row_by_name(window, QStringLiteral("mime-tagged-source.txt"));
    QVERIFY(file_row >= 0);

    const QModelIndex name_index = model->index(file_row, 0);
    const QModelIndex size_index = model->index(file_row, 1);
    QVERIFY(name_index.isValid());
    QVERIFY(size_index.isValid());
    QVERIFY(model->flags(name_index).testFlag(Qt::ItemIsDragEnabled));

    const QModelIndexList indexes{name_index, size_index};
    std::unique_ptr<QMimeData> mime(model->mimeData(indexes));
    QVERIFY(mime != nullptr);
    QVERIFY(mime->hasUrls());
    QCOMPARE(mime->urls().size(), 1);
    QCOMPARE(mime->urls().front().toLocalFile(), QFileInfo(source_file).absoluteFilePath());
    QVERIFY(mime->hasFormat(QString::fromLatin1(kMimeTypeZ7FmFsSource)));
    const QByteArray marker =
        mime->data(QString::fromLatin1(kMimeTypeZ7FmFsSource));
    QVERIFY(marker.startsWith(QByteArrayLiteral("v1;pid=")));
    QVERIFY(marker.contains(QByteArrayLiteral(";sid=")));
    const QString marker_text = QString::fromUtf8(marker);
    QVERIFY(marker_text.contains(
        QStringLiteral("pid=%1").arg(QCoreApplication::applicationPid())));
}

void FileManagerBehaviorTest::dropSamePanelSourceOnWindowTargetStillUsesAddDialogBridge() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("window-same-panel.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("same panel source on window target");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.source.panel_index.override", 0);
    window.setProperty("z7.fm.drop.source.internal_fs.override", true);
    window.setProperty("z7.fm.drop.source.same_volume.override", true);

    QWidget* drop_target = window.centralWidget();
    QVERIFY(drop_target != nullptr);
    reset_bridge_segments_for_test();
    QString warning_title;
    QString warning_text;
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           &warning_title,
                                           &warning_text);
    QVERIFY2(dispatch_local_file_drop(
                 &window, drop_target, QStringList{source_file}),
             qPrintable(QStringLiteral("%1: %2").arg(warning_title,
                                                      warning_text)));

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
}
