// tests/filemanager/behavior/archive_flow_navigation.cpp
// Role: In-archive navigation and parent-link behavior cases.

#include "internal.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace filemanager_behavior_internal;

namespace {

class ScopedEnvVar final {
 public:
  ScopedEnvVar(const QByteArray& key, const QByteArray& value)
      : key_(key),
        old_value_(qgetenv(key.constData())) {
    qputenv(key_.constData(), value);
  }

  ~ScopedEnvVar() {
    if (old_value_.isNull()) {
      qunsetenv(key_.constData());
    } else {
      qputenv(key_.constData(), old_value_);
    }
  }

 private:
  QByteArray key_;
  QByteArray old_value_;
};

QString fake_launcher_dir() {
  return QStringLiteral(Z7_FAKE_LAUNCHER_BIN_DIR);
}

QByteArray prepend_to_path(const QString& dir_path) {
  const QByteArray encoded_dir = QFile::encodeName(dir_path);
  const QByteArray current_path = qgetenv("PATH");
#ifdef Q_OS_WIN
  const QByteArray separator(";");
#else
  const QByteArray separator(":");
#endif
  if (current_path.isEmpty()) {
    return encoded_dir;
  }
  return encoded_dir + separator + current_path;
}

QJsonObject read_tracker_log(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QJsonObject{};
  }

  QJsonParseError error{};
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return QJsonObject{};
  }
  return document.object();
}

bool tracker_log_contains_sample_entry(const QJsonObject& log) {
  const QJsonArray args = log.value(QStringLiteral("args")).toArray();
  for (const QJsonValue& value : args) {
    const QString arg = value.toString();
    if (QFileInfo(arg).fileName() == QStringLiteral("sample.txt")) {
      return true;
    }
  }
  return false;
}

int row_by_name_for_panel(const z7::ui::filemanager::MainWindow& window,
                          int panel_index,
                          const QString& name) {
  const auto& panel = window.panels_[panel_index];
  const QAbstractItemModel* model =
      panel.ui.details_view != nullptr ? panel.ui.details_view->model()
                                       : nullptr;
  if (model == nullptr) {
    return -1;
  }
  for (int row = 0; row < model->rowCount(); ++row) {
    const QModelIndex index = model->index(row, 0);
    if (model->data(index, Qt::DisplayRole).toString() == name) {
      return row;
    }
  }
  return -1;
}

bool set_current_row_by_name_for_panel(
    z7::ui::filemanager::MainWindow* window,
    int panel_index,
    const QString& name) {
  if (window == nullptr) {
    return false;
  }
  auto& panel = window->panels_[panel_index];
  if (panel.ui.details_view == nullptr ||
      panel.ui.details_view->selectionModel() == nullptr ||
      panel.ui.details_view->model() == nullptr) {
    return false;
  }
  const int row = row_by_name_for_panel(*window, panel_index, name);
  if (row < 0) {
    return false;
  }
  const QModelIndex index = panel.ui.details_view->model()->index(row, 0);
  if (!index.isValid()) {
    return false;
  }
  panel.ui.details_view->selectionModel()->setCurrentIndex(
      index,
      QItemSelectionModel::NoUpdate);
  window->refresh_action_states();
  return true;
}

}  // namespace

void FileManagerBehaviorTest::archivePreviewColumnsToggleBetweenArchiveAndFilesystem() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    QVERIFY(window.panels_[0].ui.details_view != nullptr);
    auto* details = window.panels_[0].ui.details_view;

    QVERIFY(details->isColumnHidden(2));   // Packed size
    QVERIFY(details->isColumnHidden(5));   // Accessed
    QVERIFY(details->isColumnHidden(6));   // Attributes
    QVERIFY(details->isColumnHidden(10));  // Method
    QVERIFY(details->isColumnHidden(15));  // Offset

    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    QVERIFY(!details->isColumnHidden(2));   // Packed size
    QVERIFY(!details->isColumnHidden(5));   // Accessed
    QVERIFY(!details->isColumnHidden(6));   // Attributes
    QVERIFY(!details->isColumnHidden(10));  // Method
    QVERIFY(!details->isColumnHidden(15));  // Offset

    const QAbstractItemModel* model = details->model();
    QVERIFY(model != nullptr);
    QCOMPARE(model->headerData(2, Qt::Horizontal, Qt::DisplayRole).toString(),
             z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1008)));
    QCOMPARE(model->headerData(5, Qt::Horizontal, Qt::DisplayRole).toString(),
             z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1011)));
    QCOMPARE(model->headerData(15, Qt::Horizontal, Qt::DisplayRole).toString(),
             z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(1036)));

    const int sample_row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(sample_row >= 0);
    const QModelIndex sample_size = model->index(sample_row, 1);
    const QModelIndex sample_modified = model->index(sample_row, 3);
    QVERIFY(sample_size.isValid());
    QVERIFY(sample_modified.isValid());
    select_rows_in_active_panel(&window, {sample_row});
    QVERIFY(window.panels_[0].ui.status_selected_count != nullptr);
    QVERIFY(window.panels_[0].ui.status_selected_size != nullptr);
    QVERIFY(window.panels_[0].ui.status_focused_size != nullptr);
    QVERIFY(window.panels_[0].ui.status_focused_modified != nullptr);
    QCOMPARE(window.panels_[0].ui.status_selected_count->text(),
             z7::ui::runtime_support::LF(3002, {QStringLiteral("1 / 1")}));
    QCOMPARE(window.panels_[0].ui.status_selected_size->text(),
             model->data(sample_size, Qt::DisplayRole).toString());
    QCOMPARE(window.panels_[0].ui.status_focused_size->text(),
             model->data(sample_size, Qt::DisplayRole).toString());
    QCOMPARE(window.panels_[0].ui.status_focused_modified->text(),
             model->data(sample_modified, Qt::DisplayRole).toString());

    window.on_open_parent_requested();
    QTRY_VERIFY_WITH_TIMEOUT(!window.in_archive_view(), 20000);
    QVERIFY(details->isColumnHidden(2));
    QVERIFY(details->isColumnHidden(5));
    QVERIFY(details->isColumnHidden(6));
    QVERIFY(details->isColumnHidden(10));
    QVERIFY(details->isColumnHidden(15));
  }

void FileManagerBehaviorTest::archiveVirtualNavigationAndParentExit() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_nested_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare nested archive");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir, QString());

    const int top_row = row_by_name(window, QStringLiteral("top"));
    QVERIFY(top_row >= 0);
    select_rows_in_active_panel(&window, {top_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("top"));
    QVERIFY(row_by_name(window, QStringLiteral("inner")) >= 0);

    window.on_open_parent_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    window.on_open_parent_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(!window.in_archive_view());
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QFileInfo(archive_path).absolutePath());
  }

void FileManagerBehaviorTest::crossPanelAltUpBindsOppositePanelToSameArchiveFolder() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_nested_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare nested archive");
    const QString right_dir = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(right_dir));

    z7::ui::filemanager::MainWindow window;
    if (!window.two_panels_visible_) {
      window.two_panels_action_->trigger();
    }
    window.set_current_directory_for_panel(1, right_dir);
    window.set_active_panel(0);
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view_for_panel(0));

    const int top_row = row_by_name(window, QStringLiteral("top"));
    QVERIFY(top_row >= 0);
    select_rows_in_active_panel(&window, {top_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(window.panels_[0].archive.virtual_dir, QStringLiteral("top"));

    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Up,
                    Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY(window.in_archive_view_for_panel(1));
    QCOMPARE(window.panels_[1].archive.virtual_dir, QStringLiteral("top"));
    QCOMPARE(window.panels_[1].archive.current_token,
             window.panels_[0].archive.current_token);
    QCOMPARE(window.panels_[1].archive.parent_stack.size(),
             window.panels_[0].archive.parent_stack.size());
    QCOMPARE(window.active_panel_index_, 0);
  }

void FileManagerBehaviorTest::crossPanelAltLeftRightBindOppositePanelToArchiveFocusedFolder() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_nested_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare nested archive");
    const QString right_dir = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(right_dir));

    z7::ui::filemanager::MainWindow window;
    if (!window.two_panels_visible_) {
      window.two_panels_action_->trigger();
    }
    window.set_current_directory_for_panel(1, right_dir);
    window.set_active_panel(0);
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    const int top_row = row_by_name(window, QStringLiteral("top"));
    QVERIFY(top_row >= 0);
    select_rows_in_active_panel(&window, {top_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(window.panels_[0].archive.virtual_dir, QStringLiteral("top"));
    QVERIFY(set_current_row_by_name_for_panel(&window, 0, QStringLiteral("inner")));

    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Left,
                    Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY(window.in_archive_view_for_panel(1));
    QCOMPARE(window.panels_[1].archive.virtual_dir,
             QStringLiteral("top/inner"));
    QCOMPARE(window.panels_[0].archive.virtual_dir, QStringLiteral("top"));
    QCOMPARE(window.panels_[1].archive.current_token,
             window.panels_[0].archive.current_token);
    QCOMPARE(window.active_panel_index_, 0);
  }

void FileManagerBehaviorTest::crossPanelAltNavigationUsesArchiveParentTargets() {
    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/ShowDots"), true);
      settings.sync();
    }

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_nested_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare nested archive");
    const QString right_dir = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(right_dir));

    z7::ui::filemanager::MainWindow window;
    if (!window.two_panels_visible_) {
      window.two_panels_action_->trigger();
    }
    window.set_current_directory_for_panel(1, right_dir);
    window.set_active_panel(0);
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    const int top_row = row_by_name(window, QStringLiteral("top"));
    QVERIFY(top_row >= 0);
    select_rows_in_active_panel(&window, {top_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(window.panels_[0].archive.virtual_dir, QStringLiteral("top"));
    QVERIFY(set_current_row_by_name_for_panel(&window, 0, QStringLiteral("..")));

    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Right,
                    Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY(window.in_archive_view_for_panel(1));
    QCOMPARE(window.panels_[1].archive.virtual_dir, QString());
    QCOMPARE(window.panels_[0].archive.virtual_dir, QStringLiteral("top"));

    window.set_current_directory_for_panel(0, QString());
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(window.panels_[0].archive.virtual_dir, QString());
    QVERIFY(set_current_row_by_name_for_panel(&window, 0, QStringLiteral("..")));
    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Right,
                    Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY(!window.in_archive_view_for_panel(1));
    QCOMPARE(QDir(window.current_directory_for_panel(1)).absolutePath(),
             QFileInfo(archive_path).absolutePath());

    const QString embedded_archive =
        create_archive_with_embedded_archive_in_folder(root);
    QVERIFY2(!embedded_archive.isEmpty(),
             "failed to create embedded archive fixture");
    window.open_archive_inside(embedded_archive);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(window.panels_[0].archive.virtual_dir, QString());

    const int pack_row = row_by_name(window, QStringLiteral("pack"));
    QVERIFY(pack_row >= 0);
    select_rows_in_active_panel(&window, {pack_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(window.panels_[0].archive.virtual_dir, QStringLiteral("pack"));

    const int child_row = row_by_name(window, QStringLiteral("child2.7z"));
    QVERIFY(child_row >= 0);
    select_rows_in_active_panel(&window, {child_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 20000);
    QCOMPARE(window.panels_[0].archive.parent_stack.size(), 1);
    QVERIFY(set_current_row_by_name_for_panel(&window, 0, QStringLiteral("..")));

    const z7::app::ArchiveSessionToken parent_token =
        window.panels_[0].archive.parent_stack.back().session_token;
    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Right,
                    Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY(window.in_archive_view_for_panel(1));
    QCOMPARE(window.panels_[1].archive.virtual_dir, QStringLiteral("pack"));
    QCOMPARE(window.panels_[1].archive.parent_stack.size(), 0);
    QCOMPARE(window.panels_[1].archive.current_token, parent_token);
    QVERIFY(row_by_name_for_panel(window, 1, QStringLiteral("child2.7z")) >= 0);
    QCOMPARE(window.panels_[0].archive.parent_stack.size(), 1);
    QCOMPARE(window.active_panel_index_, 0);
  }

void FileManagerBehaviorTest::crossPanelAltNavigationSharesArchiveSessionState() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");
    const QString right_dir = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(right_dir));

    z7::ui::filemanager::MainWindow window;
    if (!window.two_panels_visible_) {
      window.two_panels_action_->trigger();
    }
    window.set_current_directory_for_panel(1, right_dir);
    window.set_active_panel(0);
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view_for_panel(0));

    const int sample_row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(sample_row >= 0);
    select_rows_in_active_panel(&window, {sample_row});
    schedule_copy_move_dialog_submit(QStringLiteral("sample-copy.txt"));
    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_F5,
                    Qt::ShiftModifier);
    QTRY_VERIFY_WITH_TIMEOUT(row_by_name(window, QStringLiteral("sample-copy.txt")) >= 0,
                             30000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr,
                             30000);

    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Up,
                    Qt::AltModifier);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr,
                             20000);
    QTRY_VERIFY(window.in_archive_view_for_panel(1));
    QCOMPARE(window.panels_[1].archive.current_token,
             window.panels_[0].archive.current_token);
    QTRY_VERIFY(row_by_name_for_panel(window, 1, QStringLiteral("sample-copy.txt")) >= 0);
    QCOMPARE(window.active_panel_index_, 0);
  }

void FileManagerBehaviorTest::archiveFolderHistoryRecordsVirtualDirsLikeOriginal() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_nested_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare nested archive");

    z7::ui::filemanager::MainWindow window;
    const QString root_dir = QDir(root.path()).absolutePath();
    const QString archive_root_history = QFileInfo(archive_path).absoluteFilePath();
    const QString archive_top_history =
        QDir(archive_root_history).filePath(QStringLiteral("top"));
    window.set_current_directory(root_dir);
    const QStringList history_before_archive = window.folder_history_;
    QVERIFY(!history_before_archive.isEmpty());
    QCOMPARE(history_before_archive.front(), root_dir);

    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.folder_history_.front(), archive_root_history);
    QVERIFY(window.folder_history_.contains(root_dir));

    const int top_row = row_by_name(window, QStringLiteral("top"));
    QVERIFY(top_row >= 0);
    select_rows_in_active_panel(&window, {top_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("top"));
    QCOMPARE(window.folder_history_.front(), archive_top_history);
    QVERIFY(window.folder_history_.contains(archive_root_history));
    QVERIFY(window.folder_history_.contains(root_dir));
  }

void FileManagerBehaviorTest::archiveFolderHistorySelectionLeavesArchiveView() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_nested_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare nested archive");

    const QString root_dir = QDir(root.path()).absolutePath();
    const QString alternate_dir = QDir(root.path()).filePath(QStringLiteral("history-target"));
    QVERIFY(QDir().mkpath(alternate_dir));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root_dir);
    window.set_current_directory(alternate_dir);
    window.set_current_directory(root_dir);
    QVERIFY(window.folder_history_.contains(root_dir));
    QVERIFY(window.folder_history_.contains(alternate_dir));

    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int top_row = row_by_name(window, QStringLiteral("top"));
    QVERIFY(top_row >= 0);
    select_rows_in_active_panel(&window, {top_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("top"));

    bool saw_history_dialog = false;
    bool selected_history_entry = false;
    schedule_folders_history_dialog_interaction([&](QDialog* dialog) {
      saw_history_dialog = true;
      auto* list =
          dialog->findChild<QListWidget*>(QStringLiteral("foldersHistoryList"));
      auto* buttons =
          dialog->findChild<QDialogButtonBox*>(QStringLiteral("foldersHistoryButtons"));
      if (list == nullptr || buttons == nullptr) {
        dialog->reject();
        return;
      }
      for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->text() == alternate_dir) {
          list->setCurrentRow(row);
          selected_history_entry = true;
          break;
        }
      }
      if (!selected_history_entry) {
        dialog->reject();
        return;
      }
      if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok)) {
        ok->click();
      } else {
        dialog->reject();
      }
    });
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F12,
                    Qt::AltModifier);

    QVERIFY(saw_history_dialog);
    QVERIFY(selected_history_entry);
    QTRY_VERIFY(!window.in_archive_view());
    QTRY_COMPARE(QDir(window.current_directory()).absolutePath(),
                 QDir(alternate_dir).absolutePath());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir, QString());
    QVERIFY(QDir(window.current_directory()).absolutePath() !=
            QFileInfo(archive_path).absolutePath());
  }

void FileManagerBehaviorTest::archiveParentLinkVisibilityFollowsShowDotsSetting() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_nested_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare nested archive");

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/ShowDots"), false);
      settings.sync();
    }

    z7::ui::filemanager::MainWindow hidden_window;
    hidden_window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(hidden_window) == nullptr, 20000);
    QVERIFY(hidden_window.in_archive_view());
    QCOMPARE(row_by_name(hidden_window, QStringLiteral("..")), -1);

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/ShowDots"), true);
      settings.sync();
    }

    z7::ui::filemanager::MainWindow shown_window;
    shown_window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(shown_window) == nullptr, 20000);
    QVERIFY(shown_window.in_archive_view());
    QVERIFY(row_by_name(shown_window, QStringLiteral("..")) >= 0);
  }

void FileManagerBehaviorTest::archiveParentLinkVisibilityRefreshesOnApplyRuntimeSettings() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_nested_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare nested archive");

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/ShowDots"), false);
      settings.sync();
    }

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(row_by_name(window, QStringLiteral("..")), -1);

    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(row_by_name(window, QStringLiteral("..")) >= 0, 20000);

    window.display_settings_.show_dots = false;
    window.apply_runtime_settings();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(row_by_name(window, QStringLiteral("..")), -1);
  }

void FileManagerBehaviorTest::archiveParentLinkEntryNavigatesToParent() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_nested_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare nested archive");

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/ShowDots"), true);
      settings.sync();
    }

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QVERIFY(row_by_name(window, QStringLiteral("..")) >= 0);

    const int top_row = row_by_name(window, QStringLiteral("top"));
    QVERIFY(top_row >= 0);
    select_rows_in_active_panel(&window, {top_row});
    window.on_open_requested();
    QVERIFY(filemanager_behavior_internal::current_runner(window) != nullptr);
    QVERIFY(filemanager_behavior_internal::current_progress_dialog(window) == nullptr ||
            !filemanager_behavior_internal::current_progress_dialog(window)->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("top"));
    int up_row = row_by_name(window, QStringLiteral(".."));
    QVERIFY(up_row >= 0);

    select_rows_in_active_panel(&window, {up_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir, QString());
    QCOMPARE(window.selected_filesystem_paths_including_parent_link(),
             QStringList{QStringLiteral("top")});

    up_row = row_by_name(window, QStringLiteral(".."));
    QVERIFY(up_row >= 0);
    select_rows_in_active_panel(&window, {up_row});
    window.activate_panel_selection(Qt::NoModifier);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(!window.in_archive_view());
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QFileInfo(archive_path).absolutePath());
    QCOMPARE(window.selected_filesystem_paths_including_parent_link(),
             QStringList{QFileInfo(archive_path).absoluteFilePath()});
  }

void FileManagerBehaviorTest::openOutsideFromArchiveViewExtractsAndLaunchesEntry() {
#if defined(Q_OS_WIN)
    QSKIP("Path-based fake launcher covers the current tracked open-outside path on non-Windows.");
#endif

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString tracker_log_path =
        QDir(root.path()).filePath(QStringLiteral("open-outside-log.json"));
    ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                          prepend_to_path(fake_launcher_dir()));
    ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                         tracker_log_path.toUtf8());
    ScopedEnvVar fail_mode_env(QByteArrayLiteral("Z7_FAKE_TRACKER_FAIL_MODE"),
                               QByteArrayLiteral("normal"));
    ScopedEnvVar sleep_env(QByteArrayLiteral("Z7_FAKE_TRACKER_SLEEP_MS"),
                           QByteArrayLiteral("0"));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int file_row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(file_row >= 0);
    select_rows_in_active_panel(&window, {file_row});

    window.on_open_outside_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(!window.archive_temp_sessions_.isEmpty(), 3000);

    QJsonObject tracker_log;
    QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                              !tracker_log.isEmpty()),
                             5000);
    QVERIFY(tracker_log_contains_sample_entry(tracker_log));
    QVERIFY(window.in_archive_view());
    QVERIFY(window.close());
    QTRY_VERIFY_WITH_TIMEOUT(window.archive_temp_sessions_.isEmpty(), 3000);
  }
