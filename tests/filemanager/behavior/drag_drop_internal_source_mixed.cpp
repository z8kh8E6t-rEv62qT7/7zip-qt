// tests/filemanager/behavior/drag_drop_internal_source_mixed.cpp
// Role: Mixed-volume batch drag/drop behavior for internal filesystem source semantics.

#include "internal.h"

using namespace filemanager_behavior_internal;

namespace {

struct DispatchResult {
  bool handled = false;
  bool accepted = false;
  Qt::DropAction action = Qt::IgnoreAction;
};

DispatchResult dispatch_local_file_drop_raw(
    z7::ui::filemanager::MainWindow* window,
    QObject* target,
    const QStringList& paths,
    Qt::MouseButtons buttons = Qt::LeftButton,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier,
    QPointF pos = QPointF(2000.0, 2000.0),
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

DispatchResult dispatch_local_file_drag_enter_raw(
    z7::ui::filemanager::MainWindow* window,
    QObject* target,
    const QStringList& paths,
    Qt::MouseButtons buttons = Qt::LeftButton,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier,
    QPoint pos = QPoint(2000, 2000),
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

  QDragEnterEvent positioned_event(pos,
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

void FileManagerBehaviorTest::leftDropInternalFsSourceUsesCopyByDefaultOnMixedVolumeBatch() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file_a = QDir(source_dir).filePath(QStringLiteral("mixed-copy-a.txt"));
    const QString source_file_b = QDir(source_dir).filePath(QStringLiteral("mixed-copy-b.txt"));
    {
      QFile f(source_file_a);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("mixed batch source a");
    }
    {
      QFile f(source_file_b);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("mixed batch source b");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.source.internal_fs.override", true);
    window.setProperty("z7.fm.drop.source.volume_relation.override",
                       QStringLiteral("mixed"));

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
    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file_a, source_file_b},
        Qt::LeftButton,
        Qt::NoModifier,
        row_center_in_viewport(window.panels_[0].ui.details_view, target_row));
    QVERIFY(result.handled);
    QVERIFY(result.accepted);
    QCOMPARE(result.action, Qt::CopyAction);

    const QString copied_a = QDir(target_dir).filePath(QStringLiteral("mixed-copy-a.txt"));
    const QString copied_b = QDir(target_dir).filePath(QStringLiteral("mixed-copy-b.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(copied_a), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(copied_b), 20000);
    QVERIFY(QFileInfo::exists(source_file_a));
    QVERIFY(QFileInfo::exists(source_file_b));
    QVERIFY(!sevenzip_launched);
}

void FileManagerBehaviorTest::dragEnterRightButtonMixedVolumeBatchPrefersCopyPreview() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("mixed-preview-copy.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("mixed batch right button preview");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.source.volume_relation.override",
                       QStringLiteral("mixed"));

    const int target_row = row_by_name(window, QStringLiteral("target"));
    QVERIFY(target_row >= 0);
    const DispatchResult result = dispatch_local_file_drag_enter_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::RightButton,
        Qt::NoModifier,
        row_center_in_viewport(window.panels_[0].ui.details_view, target_row).toPoint());
    QVERIFY(result.handled);
    QVERIFY(result.accepted);
    QCOMPARE(result.action, Qt::CopyAction);
    QVERIFY(QFileInfo::exists(source_file));
}

void FileManagerBehaviorTest::leftDropInternalFsSourceMixedVolumeMoveOnlyIsRejectedByDefault() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("incoming"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(source_dir));
    QVERIFY(QDir().mkpath(target_dir));
    const QString source_file = QDir(source_dir).filePath(QStringLiteral("mixed-move-only.txt"));
    {
      QFile f(source_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("mixed batch move only rejects default");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.setProperty("z7.fm.drop.source.internal_fs.override", true);
    window.setProperty("z7.fm.drop.source.volume_relation.override",
                       QStringLiteral("mixed"));

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
    const DispatchResult result = dispatch_local_file_drop_raw(
        &window,
        window.panels_[0].ui.details_view->viewport(),
        QStringList{source_file},
        Qt::LeftButton,
        Qt::NoModifier,
        row_center_in_viewport(window.panels_[0].ui.details_view, target_row),
        Qt::MoveAction);
    QVERIFY(result.handled);
    QVERIFY(!result.accepted);
    QCOMPARE(result.action, Qt::IgnoreAction);
    QVERIFY(QFileInfo::exists(source_file));
    QVERIFY(!QFileInfo::exists(QDir(target_dir).filePath(QStringLiteral("mixed-move-only.txt"))));
    QVERIFY(!sevenzip_launched);
}
