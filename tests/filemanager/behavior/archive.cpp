// tests/filemanager/behavior/archive.cpp
// Role: Archive action behavior cases.

#include "internal.h"

#include <vector>

#include "main_window/internal.h"
#include "main_window/model/model.h"
#include "structured_list_proxy.h"

using namespace filemanager_behavior_internal;

namespace {

bool write_archive_fixture_file(const QString& path, const QByteArray& contents) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(contents) == contents.size();
}

QByteArray read_file_bytes(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  return file.readAll();
}

QString create_same_folder_archive_fixture(const QTemporaryDir& root,
                                           QString* error) {
  if (error != nullptr) {
    error->clear();
  }

  const QDir root_dir(root.path());
  if (!write_archive_fixture_file(root_dir.filePath(QStringLiteral("selected.txt")),
                                  "selected payload") ||
      !write_archive_fixture_file(root_dir.filePath(QStringLiteral("focused.txt")),
                                  "focused payload") ||
      !write_archive_fixture_file(root_dir.filePath(QStringLiteral("move-me.txt")),
                                  "move payload") ||
      !write_archive_fixture_file(root_dir.filePath(QStringLiteral("existing.txt")),
                                  "existing payload") ||
      !root_dir.mkpath(QStringLiteral("folder")) ||
      !write_archive_fixture_file(
          root_dir.filePath(QStringLiteral("folder/nested.txt")),
          "nested payload")) {
    if (error != nullptr) {
      *error = QStringLiteral("failed to write archive fixture files");
    }
    return QString();
  }

  const QString archive_path =
      root_dir.filePath(QStringLiteral("same-folder.7z"));
  QString create_error;
  if (!create_archive_via_backend(root.path(),
                                  archive_path,
                                  QStringList{
                                      QStringLiteral("selected.txt"),
                                      QStringLiteral("focused.txt"),
                                      QStringLiteral("move-me.txt"),
                                      QStringLiteral("existing.txt"),
                                      QStringLiteral("folder"),
                                  },
                                  &create_error)) {
    if (error != nullptr) {
      *error = create_error;
    }
    return QString();
  }
  return archive_path;
}

bool set_active_current_row_by_name(z7::ui::filemanager::MainWindow* window,
                                    const QString& name) {
  if (window == nullptr ||
      window->active_panel_controller().ui.details_view == nullptr ||
      window->active_panel_controller().ui.details_view->selectionModel() ==
          nullptr) {
    return false;
  }

  const int row = row_by_name(*window, name);
  if (row < 0) {
    return false;
  }

  const QModelIndex index =
      window->active_panel_controller().ui.details_view->model()->index(row, 0);
  if (!index.isValid()) {
    return false;
  }
  window->active_panel_controller().ui.details_view->selectionModel()
      ->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
  window->refresh_action_states();
  return true;
}

int row_by_name_in_panel(const z7::ui::filemanager::MainWindow& window,
                         int panel_index,
                         const QString& name) {
  const auto& panel = window.panel_controller(panel_index);
  if (panel.ui.details_view == nullptr ||
      panel.ui.details_view->model() == nullptr) {
    return -1;
  }
  QAbstractItemModel* model = panel.ui.details_view->model();
  for (int row = 0; row < model->rowCount(); ++row) {
    const QModelIndex index = model->index(
        row,
        z7::ui::filemanager::DirectoryListModel::kNameColumn);
    if (index.isValid() && index.data().toString() == name) {
      return row;
    }
  }
  return -1;
}

QDialog* find_archive_add_sources_dialog() {
  if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
    if (dialog->objectName() == QStringLiteral("archiveAddSourcesDialog")) {
      return dialog;
    }
  }
  const QWidgetList top_levels = QApplication::topLevelWidgets();
  for (QWidget* widget : top_levels) {
    auto* dialog = qobject_cast<QDialog*>(widget);
    if (dialog != nullptr &&
        dialog->objectName() == QStringLiteral("archiveAddSourcesDialog")) {
      return dialog;
    }
  }
  return nullptr;
}

QModelIndex archive_add_sources_index_by_name(
    z7::ui::filemanager::DragAwareStructuredListView* view,
    const QString& name) {
  if (view == nullptr || view->model() == nullptr) {
    return {};
  }
  for (int row = 0; row < view->model()->rowCount(); ++row) {
    const QModelIndex index = view->model()->index(
        row,
        z7::ui::filemanager::DirectoryListModel::kNameColumn);
    if (index.isValid() && index.data(Qt::DisplayRole).toString() == name) {
      return index;
    }
  }
  return {};
}

bool set_archive_current_row_by_name(z7::ui::filemanager::MainWindow* window,
                                     const QString& name) {
  return set_active_current_row_by_name(window, name);
}

QLineEdit* visible_inline_rename_editor(
    z7::ui::filemanager::MainWindow& window) {
  QAbstractItemView* view =
      window.active_panel_controller().current_item_view();
  if (view == nullptr) {
    return nullptr;
  }

  const QList<QLineEdit*> editors = view->findChildren<QLineEdit*>();
  for (QLineEdit* editor : editors) {
    if (editor != nullptr && editor->isVisible()) {
      return editor;
    }
  }
  return nullptr;
}

QModelIndex active_source_name_index_for_name(
    z7::ui::filemanager::MainWindow& window,
    const QString& name) {
  auto& panel = window.active_panel_controller();
  if (panel.ui.details_view == nullptr ||
      panel.ui.details_view->model() == nullptr ||
      panel.model == nullptr) {
    return QModelIndex();
  }

  const int proxy_row = row_by_name(window, name);
  if (proxy_row < 0) {
    return QModelIndex();
  }

  const QModelIndex proxy_index = panel.ui.details_view->model()->index(
      proxy_row,
      z7::ui::filemanager::DirectoryListModel::kNameColumn);
  if (!proxy_index.isValid()) {
    return QModelIndex();
  }
  return panel.map_proxy_to_source(proxy_index);
}

bool submit_model_rename(z7::ui::filemanager::MainWindow& window,
                         const QString& item_name,
                         const QString& new_name) {
  auto& panel = window.active_panel_controller();
  if (panel.model == nullptr) {
    return false;
  }
  const QModelIndex source_index =
      active_source_name_index_for_name(window, item_name);
  if (!source_index.isValid()) {
    return false;
  }
  return panel.model->setData(source_index, new_name, Qt::EditRole);
}

bool source_name_index_is_editable(z7::ui::filemanager::MainWindow& window,
                                   const QString& item_name) {
  auto& panel = window.active_panel_controller();
  if (panel.model == nullptr) {
    return false;
  }
  const QModelIndex source_index =
      active_source_name_index_for_name(window, item_name);
  return source_index.isValid() &&
         (panel.model->flags(source_index) & Qt::ItemIsEditable) != 0;
}

void schedule_copy_move_dialog_submit_and_capture_initial(
    const QString& destination_path,
    QString* captured_initial,
    bool* seen_dialog) {
  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(10);
  QObject::connect(
      timer,
      &QTimer::timeout,
      [timer, destination_path, captured_initial, seen_dialog]() {
        QWidgetList candidates;
        if (QWidget* modal = QApplication::activeModalWidget()) {
          candidates << modal;
        }
        candidates << QApplication::topLevelWidgets();
        candidates << QApplication::allWidgets();
        for (QWidget* widget : candidates) {
          auto* dialog = qobject_cast<QDialog*>(widget);
          if (dialog == nullptr ||
              dialog->objectName() != QStringLiteral("copyMoveDialog")) {
            continue;
          }

          auto* combo = dialog->findChild<QComboBox*>(
              QStringLiteral("copyMoveDestinationCombo"));
          if (combo != nullptr) {
            if (captured_initial != nullptr) {
              *captured_initial =
                  QDir::fromNativeSeparators(combo->currentText().trimmed());
            }
            combo->setEditText(QDir::toNativeSeparators(destination_path));
          }
          if (seen_dialog != nullptr) {
            *seen_dialog = true;
          }
          dialog->accept();
          timer->stop();
          timer->deleteLater();
          return;
        }
      });
  timer->start();
  QTimer::singleShot(3000, timer, [timer]() {
    timer->stop();
    timer->deleteLater();
  });
}

void schedule_copy_move_dialog_capture_info_and_close(QString* captured_info,
                                                      bool* seen_dialog) {
  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(10);
  QObject::connect(
      timer,
      &QTimer::timeout,
      [timer, captured_info, seen_dialog]() {
        QWidgetList candidates;
        if (QWidget* modal = QApplication::activeModalWidget()) {
          candidates << modal;
        }
        candidates << QApplication::topLevelWidgets();
        candidates << QApplication::allWidgets();
        for (QWidget* widget : candidates) {
          auto* dialog = qobject_cast<QDialog*>(widget);
          if (dialog == nullptr ||
              dialog->objectName() != QStringLiteral("copyMoveDialog")) {
            continue;
          }
          if (auto* label = dialog->findChild<QLabel*>(
                  QStringLiteral("copyMoveInfoLabel"));
              label != nullptr && captured_info != nullptr) {
            *captured_info = label->text();
          }
          if (seen_dialog != nullptr) {
            *seen_dialog = true;
          }
          dialog->reject();
          timer->stop();
          timer->deleteLater();
          return;
        }
      });
  timer->start();
  QTimer::singleShot(3000, timer, [timer]() {
    timer->stop();
    timer->deleteLater();
  });
}

QString extract_archive_session_entry_text(z7::app::ArchiveSessionToken token,
                                           const QString& entry_name,
                                           const QString& output_root,
                                           bool* ok) {
  if (ok != nullptr) {
    *ok = false;
  }

  if (!token.is_valid()) {
    return QString();
  }

  z7::app::ExtractRequest request;
  request.session_token = token;
  request.output_dir = to_native_path_string(output_root);
  request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
  request.entries = std::vector<std::string>{to_native_path_string(entry_name)};

  z7::app::ArchiveRequest archive_request;
  archive_request.payload = request;
  const z7::app::OperationOutcome outcome =
      run_archive_request_and_await(archive_request);
  if (!outcome.ok) {
    return QString();
  }

  QFile file(QDir(output_root).filePath(QDir::fromNativeSeparators(entry_name)));
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  if (ok != nullptr) {
    *ok = true;
  }
  return QString::fromUtf8(file.readAll());
}

}  // namespace

void FileManagerBehaviorTest::addActionInArchiveViewShowsUnsupportedNotice() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int entry_row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(entry_row >= 0);
    select_rows_in_active_panel(&window, {entry_row});

    bool sevenzip_launched = false;
    window.external_command_launcher_ = [&sevenzip_launched](const QString&,
                                                     const QStringList&,
                                                     const QString&,
                                                     qint64*) {
      sevenzip_launched = true;
      return true;
    };

    bool dialog_seen = false;
    QString target_text;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, [&dialog_seen, &target_text, &timer]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr ||
            dialog->objectName() != QStringLiteral("archiveAddSourcesDialog")) {
          continue;
        }
        dialog_seen = true;
        if (auto* label = dialog->findChild<QLabel*>(
                QStringLiteral("archiveAddSourcesTargetLabel"))) {
          target_text = label->text();
        }
        if (auto* cancel = dialog->findChild<QPushButton*>(
                QStringLiteral("archiveAddSourcesCancelButton"))) {
          cancel->click();
        } else {
          dialog->reject();
        }
        timer.stop();
        return;
      }
    });
    timer.start();
    window.on_compress_requested();
    QTRY_VERIFY_WITH_TIMEOUT(dialog_seen, 5000);
    timer.stop();
    QVERIFY(target_text.contains(QStringLiteral("sample.txt")) ||
            !target_text.isEmpty());
    QVERIFY(!sevenzip_launched);
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
  }

void FileManagerBehaviorTest::archiveAddSourcesDialogUsesFileManagerPickerUi() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    auto& catalog = z7::ui::runtime_support::OfficialLangCatalog::instance();
    QVERIFY(catalog.set_language_and_persist(QStringLiteral("zh-cn")));
    catalog.reload_from_settings();

    bool dialog_seen = false;
    bool prompt_localized = false;
    bool target_localized = false;
    bool view_uses_filemanager_stack = false;
    bool up_button_is_icon_tool_button = false;
    bool up_button_left_of_path = false;
    bool browse_button_is_meaningful = false;
    bool ok_disabled_without_selection = false;

    QTimer::singleShot(0, [&]() {
      QDialog* dialog = find_archive_add_sources_dialog();
      if (dialog == nullptr) {
        return;
      }
      dialog_seen = true;

      if (auto* prompt = dialog->findChild<QLabel*>(
              QStringLiteral("archiveAddSourcesPromptLabel"))) {
        prompt_localized = prompt->text().contains(QStringLiteral("选择"));
      }
      if (auto* target = dialog->findChild<QLabel*>(
              QStringLiteral("archiveAddSourcesTargetLabel"))) {
        target_localized =
            target->text().contains(QStringLiteral("压缩包文件夹")) &&
            target->text().contains(QStringLiteral("/sample.txt"));
      }

      auto* view =
          dialog->findChild<z7::ui::filemanager::DragAwareStructuredListView*>(
              QStringLiteral("archiveAddSourcesView"));
      auto* proxy =
          view != nullptr
              ? dynamic_cast<z7::ui::widgets::StructuredListSortFilterProxy*>(
                    view->model())
              : nullptr;
      auto* source =
          proxy != nullptr
              ? dynamic_cast<z7::ui::filemanager::DirectoryListModel*>(
                    proxy->sourceModel())
              : nullptr;
      view_uses_filemanager_stack =
          view != nullptr && proxy != nullptr && source != nullptr &&
          source->data_mode() ==
              z7::ui::filemanager::DirectoryListModel::DataMode::kFilesystem;

      auto* up = dialog->findChild<QToolButton*>(
          QStringLiteral("archiveAddSourcesUpButton"));
      auto* path = dialog->findChild<QComboBox*>(
          QStringLiteral("archiveAddSourcesPathCombo"));
      auto* browse = dialog->findChild<QToolButton*>(
          QStringLiteral("archiveAddSourcesBrowseButton"));
      up_button_is_icon_tool_button =
          up != nullptr && !up->icon().isNull() && !up->toolTip().isEmpty();
      up_button_left_of_path =
          up != nullptr && path != nullptr &&
          up->parentWidget() == path->parentWidget() &&
          up->geometry().right() <= path->geometry().left();
      browse_button_is_meaningful =
          browse != nullptr && browse->text() != QStringLiteral("...") &&
          !browse->icon().isNull() && !browse->toolTip().isEmpty();

      if (auto* ok = dialog->findChild<QPushButton*>(
              QStringLiteral("archiveAddSourcesOkButton"))) {
        ok_disabled_without_selection = !ok->isEnabled();
      }
      dialog->reject();
    });

    const auto result = z7::ui::filemanager::show_archive_add_sources_dialog(
        nullptr,
        QStringLiteral("Add"),
        root.path(),
        QStringLiteral("sample.txt"));

    QVERIFY(dialog_seen);
    QVERIFY(prompt_localized);
    QVERIFY(target_localized);
    QVERIFY(view_uses_filemanager_stack);
    QVERIFY(up_button_is_icon_tool_button);
    QVERIFY(up_button_left_of_path);
    QVERIFY(browse_button_is_meaningful);
    QVERIFY(ok_disabled_without_selection);
    QVERIFY(!result.accepted);
  }

void FileManagerBehaviorTest::archiveAddSourcesDialogNavigatesAndReturnsSelectedFilesystemPaths() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");
    const QDir root_dir(root.path());
    QVERIFY(root_dir.mkpath(QStringLiteral("folder")));
    QFile file(root_dir.filePath(QStringLiteral("file.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("payload");
    file.close();
    QFile nested(root_dir.filePath(QStringLiteral("folder/inner.txt")));
    QVERIFY(nested.open(QIODevice::WriteOnly | QIODevice::Truncate));
    nested.write("nested");
    nested.close();

    const QString root_abs = QFileInfo(root.path()).absoluteFilePath();
    const QString folder_abs =
        QFileInfo(root_dir.filePath(QStringLiteral("folder"))).absoluteFilePath();
    const QString file_abs =
        QFileInfo(root_dir.filePath(QStringLiteral("file.txt"))).absoluteFilePath();

    bool dialog_seen = false;
    bool path_enter_navigated = false;
    bool up_navigated = false;
    bool double_click_navigated = false;
    bool ok_enabled_after_selection = false;

    QTimer::singleShot(0, [&]() {
      QDialog* dialog = find_archive_add_sources_dialog();
      if (dialog == nullptr) {
        return;
      }
      dialog_seen = true;

      auto* view =
          dialog->findChild<z7::ui::filemanager::DragAwareStructuredListView*>(
              QStringLiteral("archiveAddSourcesView"));
      auto* proxy =
          view != nullptr
              ? dynamic_cast<z7::ui::widgets::StructuredListSortFilterProxy*>(
                    view->model())
              : nullptr;
      auto* source =
          proxy != nullptr
              ? dynamic_cast<z7::ui::filemanager::DirectoryListModel*>(
                    proxy->sourceModel())
              : nullptr;
      auto* path = dialog->findChild<QComboBox*>(
          QStringLiteral("archiveAddSourcesPathCombo"));
      auto* up = dialog->findChild<QToolButton*>(
          QStringLiteral("archiveAddSourcesUpButton"));
      auto* ok = dialog->findChild<QPushButton*>(
          QStringLiteral("archiveAddSourcesOkButton"));
      if (view == nullptr || source == nullptr || path == nullptr ||
          path->lineEdit() == nullptr || up == nullptr || ok == nullptr) {
        dialog->reject();
        return;
      }

      path->lineEdit()->setText(QDir::toNativeSeparators(folder_abs));
      QTest::keyClick(path->lineEdit(), Qt::Key_Return);
      QCoreApplication::processEvents();
      path_enter_navigated =
          QFileInfo(source->directory()).absoluteFilePath() == folder_abs;

      up->click();
      QCoreApplication::processEvents();
      up_navigated =
          QFileInfo(source->directory()).absoluteFilePath() == root_abs;

      const QModelIndex folder_index =
          archive_add_sources_index_by_name(view, QStringLiteral("folder"));
      const bool signal_invoked = QMetaObject::invokeMethod(
          view,
          "primary_double_clicked",
          Qt::DirectConnection,
          Q_ARG(QModelIndex, folder_index));
      QCoreApplication::processEvents();
      double_click_navigated =
          signal_invoked &&
          QFileInfo(source->directory()).absoluteFilePath() == folder_abs;

      up->click();
      QCoreApplication::processEvents();
      const QModelIndex file_index =
          archive_add_sources_index_by_name(view, QStringLiteral("file.txt"));
      if (file_index.isValid() && view->selectionModel() != nullptr) {
        view->selectionModel()->setCurrentIndex(
            file_index,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Current);
      }
      QCoreApplication::processEvents();
      ok_enabled_after_selection = ok->isEnabled();
      ok->click();
    });

    const auto result = z7::ui::filemanager::show_archive_add_sources_dialog(
        nullptr,
        QStringLiteral("Add"),
        root.path(),
        QString());

    QVERIFY(dialog_seen);
    QVERIFY(path_enter_navigated);
    QVERIFY(up_navigated);
    QVERIFY(double_click_navigated);
    QVERIFY(ok_enabled_after_selection);
    QVERIFY(result.accepted);
    QCOMPARE(result.selected_paths, QStringList{file_abs});
  }

void FileManagerBehaviorTest::archiveViewSelectCommandsFollowPanelSelectSemantics() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    auto write_file = [&root](const QString& relative_path) {
      QFile file(QDir(root.path()).filePath(relative_path));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("x");
    };
    write_file(QStringLiteral("a.txt"));
    write_file(QStringLiteral("b.txt"));
    write_file(QStringLiteral("c.log"));
    write_file(QStringLiteral("README"));
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("dir1")));
    write_file(QStringLiteral("dir1/leaf.bin"));

    const QString archive_path =
        QDir(root.path()).filePath(QStringLiteral("selection.7z"));
    QString archive_error;
    QVERIFY2(create_archive_via_backend(root.path(),
                                        archive_path,
                                        QStringList{
                                            QStringLiteral("a.txt"),
                                            QStringLiteral("b.txt"),
                                            QStringLiteral("c.log"),
                                            QStringLiteral("README"),
                                            QStringLiteral("dir1"),
                                        },
                                        &archive_error),
             qPrintable(archive_error));

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QTRY_VERIFY_WITH_TIMEOUT(row_by_name(window, QStringLiteral("a.txt")) >= 0, 20000);

    auto sorted_entries = [&window]() {
      QStringList entries = window.selected_archive_entries();
      entries.sort();
      return entries;
    };
    auto focus_entry = [&window](const QString& name) {
      const int row = row_by_name(window, name);
      QVERIFY(row >= 0);
      const QModelIndex index =
          window.active_panel_controller().ui.details_view->model()->index(row, 0);
      QVERIFY(index.isValid());
      window.active_panel_controller().ui.details_view->setCurrentIndex(index);
      window.refresh_action_states();
    };

    const QStringList all_entries = [] {
      QStringList entries{
          QStringLiteral("README"),
          QStringLiteral("a.txt"),
          QStringLiteral("b.txt"),
          QStringLiteral("c.log"),
          QStringLiteral("dir1"),
      };
      entries.sort();
      return entries;
    }();

    QVERIFY(row_by_name(window, QStringLiteral("..")) >= 0);
    schedule_input_dialog_submit(QStringLiteral("*.txt"), true);
    window.select_action_->trigger();
    QCOMPARE(sorted_entries(),
             QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));

    schedule_input_dialog_submit(QStringLiteral("a.*"), true);
    window.deselect_action_->trigger();
    QCOMPARE(sorted_entries(), QStringList{QStringLiteral("b.txt")});

    window.deselect_all_action_->trigger();
    QVERIFY(sorted_entries().isEmpty());

    schedule_input_dialog_submit(QStringLiteral("*.log"), true);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Plus,
                    Qt::KeypadModifier);
    QCOMPARE(sorted_entries(), QStringList{QStringLiteral("c.log")});

    schedule_input_dialog_submit(QStringLiteral("c.*"), true);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Minus,
                    Qt::KeypadModifier);
    QVERIFY(sorted_entries().isEmpty());

    focus_entry(QStringLiteral("a.txt"));
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Plus,
                    Qt::AltModifier | Qt::KeypadModifier);
    QCOMPARE(sorted_entries(),
             QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Minus,
                    Qt::AltModifier | Qt::KeypadModifier);
    QVERIFY(sorted_entries().isEmpty());

    focus_entry(QStringLiteral("dir1"));
    window.select_by_type_action_->trigger();
    QCOMPARE(sorted_entries(), QStringList{QStringLiteral("dir1")});
    window.deselect_all_action_->trigger();

    const int parent_row = row_by_name(window, QStringLiteral(".."));
    QVERIFY(parent_row >= 0);
    const QModelIndex parent_index =
        window.active_panel_controller().ui.details_view->model()->index(parent_row, 0);
    QVERIFY(parent_index.isValid());
    window.active_panel_controller().ui.details_view->selectionModel()->select(
        parent_index,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QVERIFY(window.active_selected_rows_include_parent_link());
    window.select_all_action_->trigger();
    QVERIFY(!window.active_selected_rows_include_parent_link());
    QCOMPARE(sorted_entries(), all_entries);

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Asterisk,
                    Qt::NoModifier);
    QVERIFY(!window.active_selected_rows_include_parent_link());
    QVERIFY(sorted_entries().isEmpty());
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Asterisk,
                    Qt::NoModifier);
    QVERIFY(!window.active_selected_rows_include_parent_link());
    QCOMPARE(sorted_entries(), all_entries);
  }

void FileManagerBehaviorTest::copyToSupportsSingleTargetRenameAndPersistsHistory() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_path = QDir(root.path()).filePath(QStringLiteral("source.txt"));
    const QString target_dir = QDir(root.path()).filePath(QStringLiteral("renamed dir"));
    const QString target_path = QDir(target_dir).filePath(QStringLiteral("renamed.txt"));
    const QString raw_target = QStringLiteral("renamed dir. /renamed.txt. ");
    {
      QFile file(source_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("copy rename test");
    }

    QStringList seeded_history;
    seeded_history.reserve(22);
    for (int i = 0; i < 22; ++i) {
      seeded_history << QDir(root.path()).filePath(
          QStringLiteral("history-%1").arg(i, 2, 10, QLatin1Char('0')));
    }
    seeded_history[7] = target_path;
    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/History/CopyTo"),
                        QStringList{QDir(root.path()).filePath(QStringLiteral("legacy-only"))});
      settings.setValue(QStringLiteral("FM/CopyHistory"), seeded_history);
      settings.sync();
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int source_row = row_by_name(window, QStringLiteral("source.txt"));
    QVERIFY(source_row >= 0);
    select_rows_in_active_panel(&window, {source_row});

    schedule_copy_move_dialog_submit(raw_target, true);
    window.on_copy_to_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY(QFileInfo::exists(source_path));
    QVERIFY(QFileInfo::exists(target_path));
    QVERIFY(!QFileInfo::exists(QDir(root.path()).filePath(raw_target)));

    z7::platform::qt::PortableSettings settings;
    const QStringList history = settings.value(QStringLiteral("FM/CopyHistory")).toStringList();
    QCOMPARE(settings.value(QStringLiteral("FM/History/CopyTo")).toStringList().size(), 1);
    QVERIFY(!history.contains(QDir(root.path()).filePath(QStringLiteral("legacy-only"))));
    QStringList expected_history;
    expected_history.reserve(20);
    const QString normalized_target =
        QDir::cleanPath(QDir::fromNativeSeparators(target_path));
    expected_history << normalized_target;
    for (const QString& entry : history) {
      QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(entry)), entry);
    }
    for (const QString& entry : seeded_history) {
      const QString normalized =
          QDir::cleanPath(QDir::fromNativeSeparators(entry.trimmed()));
      if (normalized.isEmpty() || normalized == normalized_target ||
          expected_history.contains(normalized)) {
        continue;
      }
      expected_history << normalized;
      if (expected_history.size() == 20) {
        break;
      }
    }
    QCOMPARE(history, expected_history);
  }

void FileManagerBehaviorTest::copyToStoresFinalCorrectedDirectoryHistory() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString first_path = QDir(root.path()).filePath(QStringLiteral("first.txt"));
    const QString second_path = QDir(root.path()).filePath(QStringLiteral("second.txt"));
    {
      QFile file(first_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("first");
    }
    {
      QFile file(second_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("second");
    }

    const QString raw_destination = QStringLiteral("out dir. /nested. /");
    const QString corrected_dir =
        QDir(root.path()).filePath(QStringLiteral("out dir/nested"));
    const QString corrected_history = corrected_dir + QLatin1Char('/');
    {
      QStringList seeded_history;
      seeded_history.reserve(22);
      for (int i = 0; i < 22; ++i) {
        seeded_history << QDir(root.path()).filePath(
            QStringLiteral("dir-history-%1").arg(i, 2, 10, QLatin1Char('0')));
      }
      seeded_history[3] = corrected_dir.toUpper();
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/CopyHistory"), seeded_history);
      settings.sync();
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int first_row = row_by_name(window, QStringLiteral("first.txt"));
    const int second_row = row_by_name(window, QStringLiteral("second.txt"));
    QVERIFY(first_row >= 0);
    QVERIFY(second_row >= 0);
    select_rows_in_active_panel(&window, {first_row, second_row});

    schedule_copy_move_dialog_submit(raw_destination, true);
    window.on_copy_to_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr,
                             20000);

    QVERIFY(QFileInfo::exists(QDir(corrected_dir).filePath(QStringLiteral("first.txt"))));
    QVERIFY(QFileInfo::exists(QDir(corrected_dir).filePath(QStringLiteral("second.txt"))));
    QVERIFY(!QFileInfo::exists(QDir(root.path()).filePath(raw_destination)));

    z7::platform::qt::PortableSettings settings;
    const QStringList history =
        settings.value(QStringLiteral("FM/CopyHistory")).toStringList();
    QCOMPARE(history.size(), 20);
    QCOMPARE(history.front(), corrected_history);
    QVERIFY(!history.mid(1).contains(corrected_dir, Qt::CaseInsensitive));
    QVERIFY(!history.contains(QDir(root.path()).filePath(raw_destination)));
  }

void FileManagerBehaviorTest::copyToDoesNotSaveHistoryWhenDestinationDirectoryCreationFails() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_path = QDir(root.path()).filePath(QStringLiteral("source.txt"));
    const QString blocked_path = QDir(root.path()).filePath(QStringLiteral("blocked"));
    {
      QFile file(source_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("source");
    }
    {
      QFile file(blocked_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("not a directory");
    }

    const QStringList seeded_history{
        QDir(root.path()).filePath(QStringLiteral("history-sentinel"))};
    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/CopyHistory"), seeded_history);
      settings.sync();
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int source_row = row_by_name(window, QStringLiteral("source.txt"));
    QVERIFY(source_row >= 0);
    select_rows_in_active_panel(&window, {source_row});

    QString warning_text;
    schedule_copy_move_dialog_submit(QStringLiteral("blocked/child/"), true);
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           nullptr,
                                           &warning_text);
    window.on_copy_to_requested();

    QTRY_VERIFY_WITH_TIMEOUT(!warning_text.isEmpty(), 5000);
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QVERIFY(QFileInfo::exists(source_path));
    QVERIFY(!QFileInfo::exists(QDir(root.path()).filePath(QStringLiteral("blocked/child"))));

    z7::platform::qt::PortableSettings settings;
    QCOMPARE(settings.value(QStringLiteral("FM/CopyHistory")).toStringList(),
             seeded_history);
  }

void FileManagerBehaviorTest::copyMoveShortcutsFollowOriginalF5F6Routes() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_path = QDir(root.path()).filePath(QStringLiteral("source.txt"));
    const QString move_path = QDir(root.path()).filePath(QStringLiteral("move-me.txt"));
    {
      QFile file(source_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("copy by f5");
    }
    {
      QFile file(move_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("move by f6");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int source_row = row_by_name(window, QStringLiteral("source.txt"));
    QVERIFY(source_row >= 0);
    select_rows_in_active_panel(&window, {source_row});

    const QString copy_target = QDir(root.path()).filePath(QStringLiteral("copied-by-f5.txt"));
    schedule_copy_move_dialog_submit(copy_target, true);
    QTest::keyClick(window.active_panel_controller().ui.details_view, Qt::Key_F5);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(QFileInfo::exists(source_path));
    QVERIFY(QFileInfo::exists(copy_target));

    const int move_row = row_by_name(window, QStringLiteral("move-me.txt"));
    QVERIFY(move_row >= 0);
    select_rows_in_active_panel(&window, {move_row});

    const QString move_target = QDir(root.path()).filePath(QStringLiteral("moved-by-f6.txt"));
    schedule_copy_move_dialog_submit(move_target, true);
    QTest::keyClick(window.active_panel_controller().ui.details_view, Qt::Key_F6);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(!QFileInfo::exists(move_path));
    QVERIFY(QFileInfo::exists(move_target));
  }

void FileManagerBehaviorTest::shiftCopyMoveShortcutsUseFocusedItemNameLikeOriginal() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString first_path = QDir(root.path()).filePath(QStringLiteral("first.txt"));
    const QString focused_path = QDir(root.path()).filePath(QStringLiteral("focused.txt"));
    const QString move_path = QDir(root.path()).filePath(QStringLiteral("move-me.txt"));
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("folder")));
    for (const QString& path : {first_path, focused_path, move_path}) {
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write(QFileInfo(path).fileName().toUtf8());
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int first_row = row_by_name(window, QStringLiteral("first.txt"));
    const int focused_row = row_by_name(window, QStringLiteral("focused.txt"));
    const int move_row = row_by_name(window, QStringLiteral("move-me.txt"));
    const int folder_row = row_by_name(window, QStringLiteral("folder"));
    QVERIFY(first_row >= 0);
    QVERIFY(focused_row >= 0);
    QVERIFY(move_row >= 0);
    QVERIFY(folder_row >= 0);

    select_rows_in_active_panel(&window, {first_row, focused_row, folder_row});
    const QModelIndex focused_index =
        window.active_panel_controller().ui.details_view->model()->index(focused_row, 0);
    QVERIFY(focused_index.isValid());
    window.active_panel_controller().ui.details_view->selectionModel()->setCurrentIndex(
        focused_index, QItemSelectionModel::NoUpdate);

    bool copy_dialog_seen = false;
    QString copy_initial_destination;
    QTimer::singleShot(0, [&copy_dialog_seen, &copy_initial_destination]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr ||
            dialog->objectName() != QStringLiteral("copyMoveDialog")) {
          continue;
        }
        auto* combo =
            dialog->findChild<QComboBox*>(QStringLiteral("copyMoveDestinationCombo"));
        QVERIFY(combo != nullptr);
        copy_dialog_seen = true;
        copy_initial_destination = QDir::fromNativeSeparators(combo->currentText().trimmed());
        combo->setEditText(QStringLiteral("focused-copy.txt"));
        dialog->accept();
        return;
      }
    });
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F5,
                    Qt::ShiftModifier);
    QVERIFY(copy_dialog_seen);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(copy_initial_destination, QStringLiteral("focused.txt"));
    QVERIFY(QFileInfo::exists(first_path));
    QVERIFY(QFileInfo::exists(focused_path));
    QVERIFY(QFileInfo::exists(QDir(root.path()).filePath(QStringLiteral("focused-copy.txt"))));
    QVERIFY(!QFileInfo::exists(QDir(root.path()).filePath(QStringLiteral("first-copy.txt"))));

    const int refreshed_first_row = row_by_name(window, QStringLiteral("first.txt"));
    const int refreshed_move_row = row_by_name(window, QStringLiteral("move-me.txt"));
    QVERIFY(refreshed_first_row >= 0);
    QVERIFY(refreshed_move_row >= 0);
    select_rows_in_active_panel(&window, {refreshed_first_row, refreshed_move_row});
    const QModelIndex move_index =
        window.active_panel_controller().ui.details_view->model()->index(refreshed_move_row, 0);
    QVERIFY(move_index.isValid());
    window.active_panel_controller().ui.details_view->selectionModel()->setCurrentIndex(
        move_index, QItemSelectionModel::NoUpdate);

    bool move_dialog_seen = false;
    QString move_initial_destination;
    QTimer::singleShot(0, [&move_dialog_seen, &move_initial_destination]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr ||
            dialog->objectName() != QStringLiteral("copyMoveDialog")) {
          continue;
        }
        auto* combo =
            dialog->findChild<QComboBox*>(QStringLiteral("copyMoveDestinationCombo"));
        QVERIFY(combo != nullptr);
        move_dialog_seen = true;
        move_initial_destination = QDir::fromNativeSeparators(combo->currentText().trimmed());
        combo->setEditText(QStringLiteral("moved-focused.txt"));
        dialog->accept();
        return;
      }
    });
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F6,
                    Qt::ShiftModifier);
    QVERIFY(move_dialog_seen);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(move_initial_destination, QStringLiteral("move-me.txt"));
    QVERIFY(QFileInfo::exists(first_path));
    QVERIFY(!QFileInfo::exists(move_path));
    QVERIFY(QFileInfo::exists(QDir(root.path()).filePath(QStringLiteral("moved-focused.txt"))));
  }

void FileManagerBehaviorTest::archiveViewShiftCopyCopiesFocusedEntryWithinArchive() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    QString archive_error;
    const QString archive_path =
        create_same_folder_archive_fixture(root, &archive_error);
    QVERIFY2(!archive_path.isEmpty(), qPrintable(archive_error));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);
    QVERIFY(window.in_archive_view());

    const int selected_row = row_by_name(window, QStringLiteral("selected.txt"));
    const int unrelated_row = row_by_name(window, QStringLiteral("existing.txt"));
    const int focused_row = row_by_name(window, QStringLiteral("focused.txt"));
    QVERIFY(selected_row >= 0);
    QVERIFY(unrelated_row >= 0);
    QVERIFY(focused_row >= 0);
    select_rows_in_active_panel(&window, {selected_row, unrelated_row});
    QVERIFY(set_archive_current_row_by_name(&window, QStringLiteral("focused.txt")));

    bool dialog_seen = false;
    QString initial_destination;
    schedule_copy_move_dialog_submit_and_capture_initial(
        QStringLiteral("focused-copy.txt"),
        &initial_destination,
        &dialog_seen);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F5,
                    Qt::ShiftModifier);
    QVERIFY(dialog_seen);
    QCOMPARE(initial_destination, QStringLiteral("focused.txt"));

    QTRY_VERIFY_WITH_TIMEOUT(
        row_by_name(window, QStringLiteral("focused-copy.txt")) >= 0,
        30000);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        30000);
    QVERIFY(row_by_name(window, QStringLiteral("focused.txt")) >= 0);
    QVERIFY(row_by_name(window, QStringLiteral("selected-copy.txt")) < 0);

    QTemporaryDir extract_root;
    QVERIFY2(extract_root.isValid(), "failed to create extract temp dir");
    bool text_ok = false;
    QCOMPARE(extract_archive_session_entry_text(
                 window.active_panel_controller().archive.current_token,
                 QStringLiteral("focused-copy.txt"),
                 extract_root.path(),
                 &text_ok),
             QStringLiteral("focused payload"));
    QVERIFY(text_ok);
  }

void FileManagerBehaviorTest::archiveViewShiftMoveMovesFocusedEntryWithinArchive() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    QString archive_error;
    const QString archive_path =
        create_same_folder_archive_fixture(root, &archive_error);
    QVERIFY2(!archive_path.isEmpty(), qPrintable(archive_error));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);
    QVERIFY(window.in_archive_view());

    const int selected_row = row_by_name(window, QStringLiteral("selected.txt"));
    const int move_row = row_by_name(window, QStringLiteral("move-me.txt"));
    QVERIFY(selected_row >= 0);
    QVERIFY(move_row >= 0);
    select_rows_in_active_panel(&window, {selected_row});
    QVERIFY(set_archive_current_row_by_name(&window, QStringLiteral("move-me.txt")));

    bool dialog_seen = false;
    QString initial_destination;
    schedule_copy_move_dialog_submit_and_capture_initial(
        QStringLiteral("moved-focused.txt"),
        &initial_destination,
        &dialog_seen);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F6,
                    Qt::ShiftModifier);
    QVERIFY(dialog_seen);
    QCOMPARE(initial_destination, QStringLiteral("move-me.txt"));

    QTRY_VERIFY_WITH_TIMEOUT(
        row_by_name(window, QStringLiteral("moved-focused.txt")) >= 0,
        30000);
    QTRY_VERIFY_WITH_TIMEOUT(
        row_by_name(window, QStringLiteral("move-me.txt")) < 0,
        30000);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        30000);
    QVERIFY(row_by_name(window, QStringLiteral("selected.txt")) >= 0);

    QTemporaryDir extract_root;
    QVERIFY2(extract_root.isValid(), "failed to create extract temp dir");
    bool text_ok = false;
    QCOMPARE(extract_archive_session_entry_text(
                 window.active_panel_controller().archive.current_token,
                 QStringLiteral("moved-focused.txt"),
                 extract_root.path(),
                 &text_ok),
             QStringLiteral("move payload"));
    QVERIFY(text_ok);
  }

void FileManagerBehaviorTest::archiveViewShiftCopyRejectsExistingTargetName() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    QString archive_error;
    const QString archive_path =
        create_same_folder_archive_fixture(root, &archive_error);
    QVERIFY2(!archive_path.isEmpty(), qPrintable(archive_error));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);
    QVERIFY(window.in_archive_view());

    const int source_row = row_by_name(window, QStringLiteral("focused.txt"));
    QVERIFY(source_row >= 0);
    select_rows_in_active_panel(&window, {source_row});
    QVERIFY(set_archive_current_row_by_name(&window, QStringLiteral("focused.txt")));

    bool dialog_seen = false;
    QString initial_destination;
    QString warning_title;
    QString warning_text;
    schedule_copy_move_dialog_submit_and_capture_initial(
        QStringLiteral("existing.txt"),
        &initial_destination,
        &dialog_seen);
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           &warning_title,
                                           &warning_text);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F5,
                    Qt::ShiftModifier);
    QVERIFY(dialog_seen);
    QCOMPARE(initial_destination, QStringLiteral("focused.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(!warning_text.isEmpty(), 5000);
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QVERIFY(row_by_name(window, QStringLiteral("focused.txt")) >= 0);
    QVERIFY(row_by_name(window, QStringLiteral("existing.txt")) >= 0);

    QTemporaryDir extract_root;
    QVERIFY2(extract_root.isValid(), "failed to create extract temp dir");
    bool existing_ok = false;
    QCOMPARE(extract_archive_session_entry_text(
                 window.active_panel_controller().archive.current_token,
                 QStringLiteral("existing.txt"),
                 extract_root.path(),
                 &existing_ok),
             QStringLiteral("existing payload"));
    QVERIFY(existing_ok);
  }

void FileManagerBehaviorTest::archiveViewShiftCopyCopiesFocusedDirectoryWithinArchive() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    QString archive_error;
    const QString archive_path =
        create_same_folder_archive_fixture(root, &archive_error);
    QVERIFY2(!archive_path.isEmpty(), qPrintable(archive_error));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);
    QVERIFY(window.in_archive_view());

    const int selected_row = row_by_name(window, QStringLiteral("selected.txt"));
    const int folder_row = row_by_name(window, QStringLiteral("folder"));
    QVERIFY(selected_row >= 0);
    QVERIFY(folder_row >= 0);
    select_rows_in_active_panel(&window, {selected_row});
    QVERIFY(set_archive_current_row_by_name(&window, QStringLiteral("folder")));

    bool dialog_seen = false;
    QString initial_destination;
    schedule_copy_move_dialog_submit_and_capture_initial(
        QStringLiteral("folder-copy"),
        &initial_destination,
        &dialog_seen);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F5,
                    Qt::ShiftModifier);
    QVERIFY(dialog_seen);
    QCOMPARE(initial_destination, QStringLiteral("folder"));

    QTRY_VERIFY_WITH_TIMEOUT(
        row_by_name(window, QStringLiteral("folder-copy")) >= 0,
        30000);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        30000);
    QVERIFY(row_by_name(window, QStringLiteral("folder")) >= 0);

    QTemporaryDir extract_root;
    QVERIFY2(extract_root.isValid(), "failed to create extract temp dir");
    bool text_ok = false;
    QCOMPARE(extract_archive_session_entry_text(
                 window.active_panel_controller().archive.current_token,
                 QStringLiteral("folder-copy/nested.txt"),
                 extract_root.path(),
                 &text_ok),
             QStringLiteral("nested payload"));
    QVERIFY(text_ok);
  }

void FileManagerBehaviorTest::copyToUsesCurrentPanelAsDefaultTargetInSinglePanelMode() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_path = QDir(root.path()).filePath(QStringLiteral("single.txt"));
    {
      QFile file(source_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("single panel default");
    }

    z7::ui::filemanager::MainWindow window;
    QVERIFY(!window.two_panels_visible_);
    window.set_current_directory(root.path());

    const int source_row = row_by_name(window, QStringLiteral("single.txt"));
    QVERIFY(source_row >= 0);
    select_rows_in_active_panel(&window, {source_row});

    bool dialog_seen = false;
    QString initial_destination;
    QTimer::singleShot(0, [&dialog_seen, &initial_destination]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr ||
            dialog->objectName() != QStringLiteral("copyMoveDialog")) {
          continue;
        }
        auto* combo =
            dialog->findChild<QComboBox*>(QStringLiteral("copyMoveDestinationCombo"));
        QVERIFY(combo != nullptr);
        dialog_seen = true;
        initial_destination = QDir::fromNativeSeparators(combo->currentText().trimmed());
        dialog->reject();
        return;
      }
    });
    window.on_copy_to_requested();

    QVERIFY(dialog_seen);
    QCOMPARE(initial_destination, QDir::fromNativeSeparators(root.path()));
}

void FileManagerBehaviorTest::copyToUsesOtherPanelAsDefaultTargetInDualPanelMode() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString left_dir = QDir(root.path()).filePath(QStringLiteral("left"));
    const QString right_dir = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(left_dir));
    QVERIFY(QDir().mkpath(right_dir));

    {
      QFile file(QDir(left_dir).filePath(QStringLiteral("left.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("left");
    }

    z7::ui::filemanager::MainWindow window;
    window.two_panels_action_->setChecked(true);
    window.on_two_panels_action_triggered();
    window.set_current_directory_for_panel(0, left_dir);
    window.set_current_directory_for_panel(1, right_dir);
    window.set_active_panel(0);

    const int left_row = row_by_name(window, QStringLiteral("left.txt"));
    QVERIFY(left_row >= 0);
    select_rows_in_active_panel(&window, {left_row});

    bool dialog_seen = false;
    QString initial_destination;
    QTimer::singleShot(0, [&dialog_seen, &initial_destination]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr || dialog->objectName() != QStringLiteral("copyMoveDialog")) {
          continue;
        }
        auto* combo =
            dialog->findChild<QComboBox*>(QStringLiteral("copyMoveDestinationCombo"));
        QVERIFY(combo != nullptr);
        dialog_seen = true;
        initial_destination = QDir::fromNativeSeparators(combo->currentText().trimmed());
        dialog->reject();
        return;
      }
    });
    window.on_copy_to_requested();

    QVERIFY(dialog_seen);
    QCOMPARE(initial_destination, QDir::fromNativeSeparators(right_dir));
  }

void FileManagerBehaviorTest::copyMoveDialogInfoTextShowsOriginalItemSummary() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path =
        QDir(root.path()).filePath(QStringLiteral("info-file.txt"));
    QVERIFY(write_archive_fixture_file(file_path, QByteArray("info payload")));
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("info-folder")));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int file_row = row_by_name(window, QStringLiteral("info-file.txt"));
    const int folder_row = row_by_name(window, QStringLiteral("info-folder"));
    QVERIFY(file_row >= 0);
    QVERIFY(folder_row >= 0);
    select_rows_in_active_panel(&window, {file_row, folder_row});

    bool dialog_seen = false;
    QString info_text;
    schedule_copy_move_dialog_capture_info_and_close(&info_text, &dialog_seen);
    window.on_copy_to_requested();

    QTRY_VERIFY_WITH_TIMEOUT(dialog_seen, 5000);
    QVERIFY2(info_text.contains(localized_label(1031) + QStringLiteral(":")),
             qPrintable(info_text));
    QVERIFY2(info_text.contains(localized_label(1032) + QStringLiteral(":")),
             qPrintable(info_text));
    QVERIFY2(info_text.contains(QDir::toNativeSeparators(root.path())),
             qPrintable(info_text));
    QVERIFY2(info_text.contains(QStringLiteral("info-file.txt")),
             qPrintable(info_text));
    QVERIFY2(info_text.contains(QStringLiteral("info-folder")),
             qPrintable(info_text));
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
  }

void FileManagerBehaviorTest::copyMoveToOppositeArchivePanelUsesArchiveWriteback() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_dir = QDir(root.path()).filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString copy_source =
        QDir(source_dir).filePath(QStringLiteral("copy-to-archive.txt"));
    const QString move_source =
        QDir(source_dir).filePath(QStringLiteral("move-to-archive.txt"));
    QVERIFY(write_archive_fixture_file(copy_source, QByteArray("copy to archive")));
    QVERIFY(write_archive_fixture_file(move_source, QByteArray("move to archive")));

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.two_panels_action_->setChecked(true);
    window.on_two_panels_action_triggered();
    window.set_current_directory_for_panel(0, source_dir);
    window.set_active_panel(1);
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);
    QVERIFY(window.in_archive_view_for_panel(1));
    window.set_active_panel(0);

    const QString archive_panel_target =
        window.panel_controller(1).archive_virtual_display_path();
    QVERIFY(!archive_panel_target.isEmpty());

    const int copy_row = row_by_name(window, QStringLiteral("copy-to-archive.txt"));
    QVERIFY(copy_row >= 0);
    select_rows_in_active_panel(&window, {copy_row});

    bool copy_dialog_seen = false;
    QString copy_initial_destination;
    schedule_copy_move_dialog_submit_and_capture_initial(
        archive_panel_target,
        &copy_initial_destination,
        &copy_dialog_seen);
    window.on_copy_to_requested();
    QTRY_VERIFY_WITH_TIMEOUT(copy_dialog_seen, 5000);
    QCOMPARE(copy_initial_destination,
             QDir::fromNativeSeparators(archive_panel_target));
    QTRY_VERIFY_WITH_TIMEOUT(
        row_by_name_in_panel(window, 1, QStringLiteral("copy-to-archive.txt")) >= 0,
        30000);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        30000);
    QVERIFY(QFileInfo::exists(copy_source));

    {
      QTemporaryDir extract_root;
      QVERIFY2(extract_root.isValid(), "failed to create extract temp dir");
      bool text_ok = false;
      QCOMPARE(extract_archive_session_entry_text(
                   window.panel_controller(1).archive.current_token,
                   QStringLiteral("copy-to-archive.txt"),
                   extract_root.path(),
                   &text_ok),
               QStringLiteral("copy to archive"));
      QVERIFY(text_ok);
    }

    const int move_row = row_by_name(window, QStringLiteral("move-to-archive.txt"));
    QVERIFY(move_row >= 0);
    select_rows_in_active_panel(&window, {move_row});

    bool move_dialog_seen = false;
    QString move_initial_destination;
    schedule_copy_move_dialog_submit_and_capture_initial(
        archive_panel_target,
        &move_initial_destination,
        &move_dialog_seen);
    window.on_move_to_requested();
    QTRY_VERIFY_WITH_TIMEOUT(move_dialog_seen, 5000);
    QCOMPARE(move_initial_destination,
             QDir::fromNativeSeparators(archive_panel_target));
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(move_source), 30000);
    QTRY_VERIFY_WITH_TIMEOUT(
        row_by_name_in_panel(window, 1, QStringLiteral("move-to-archive.txt")) >= 0,
        30000);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        30000);

    QTemporaryDir extract_root;
    QVERIFY2(extract_root.isValid(), "failed to create extract temp dir");
    bool text_ok = false;
    QCOMPARE(extract_archive_session_entry_text(
                 window.panel_controller(1).archive.current_token,
                 QStringLiteral("move-to-archive.txt"),
                 extract_root.path(),
                 &text_ok),
             QStringLiteral("move to archive"));
    QVERIFY(text_ok);

    z7::platform::qt::PortableSettings settings;
    const QStringList history =
        settings.value(QStringLiteral("FM/CopyHistory")).toStringList();
    QVERIFY(!history.isEmpty());
    QCOMPARE(history.front(), archive_panel_target);
  }

void FileManagerBehaviorTest::copyToConflictCancelLeavesSourceUntouched() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_path = QDir(root.path()).filePath(QStringLiteral("conflict.txt"));
    {
      QFile file(source_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("conflict");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int row = row_by_name(window, QStringLiteral("conflict.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    schedule_copy_move_dialog_submit(root.path(), false);
    window.on_copy_to_requested();

    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QVERIFY(QFileInfo::exists(source_path));
  }

void FileManagerBehaviorTest::copyMoveRejectSelfTargetsBeforeConflictPrompt() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString copy_path = QDir(root.path()).filePath(QStringLiteral("copy-self.txt"));
    const QString move_path = QDir(root.path()).filePath(QStringLiteral("move-self.txt"));
    {
      QFile file(copy_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("copy self payload");
    }
    {
      QFile file(move_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("move self payload");
    }
    const QStringList seeded_history{
        QDir(root.path()).filePath(QStringLiteral("self-history-sentinel"))};
    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/CopyHistory"), seeded_history);
      settings.sync();
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int copy_row = row_by_name(window, QStringLiteral("copy-self.txt"));
    QVERIFY(copy_row >= 0);
    select_rows_in_active_panel(&window, {copy_row});

    QString copy_warning;
    schedule_copy_move_dialog_submit(root.path(), true);
    schedule_message_box_capture_and_click(QMessageBox::Ok, nullptr, &copy_warning);
    window.on_copy_to_requested();

    QTRY_VERIFY_WITH_TIMEOUT(!copy_warning.isEmpty(), 5000);
    QCOMPARE(copy_warning,
             QStringLiteral("Cannot copy or move files onto themselves."));
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QFile copy_file(copy_path);
    QVERIFY(copy_file.open(QIODevice::ReadOnly));
    QCOMPARE(copy_file.readAll(), QByteArray("copy self payload"));

    const int move_row = row_by_name(window, QStringLiteral("move-self.txt"));
    QVERIFY(move_row >= 0);
    select_rows_in_active_panel(&window, {move_row});

    QString move_warning;
    schedule_copy_move_dialog_submit(move_path, true);
    schedule_message_box_capture_and_click(QMessageBox::Ok, nullptr, &move_warning);
    window.on_move_to_requested();

    QTRY_VERIFY_WITH_TIMEOUT(!move_warning.isEmpty(), 5000);
    QCOMPARE(move_warning,
             QStringLiteral("Cannot copy or move files onto themselves."));
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QFile move_file(move_path);
    QVERIFY(move_file.open(QIODevice::ReadOnly));
    QCOMPARE(move_file.readAll(), QByteArray("move self payload"));

    z7::platform::qt::PortableSettings settings;
    QCOMPARE(settings.value(QStringLiteral("FM/CopyHistory")).toStringList(),
             seeded_history);
  }

void FileManagerBehaviorTest::copyMoveConflictsUsePerFileOverwritePrompt() {
    auto run_copy_case =
        [](OverwritePromptChoice choice,
           const QByteArray& expected_conflict,
           bool expect_auto_rename) {
          QTemporaryDir root;
          QVERIFY2(root.isValid(), "failed to create temp dir");

          const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
          QVERIFY(QDir().mkpath(output_dir));
          const QString source_path =
              QDir(root.path()).filePath(QStringLiteral("copy.txt"));
          const QString conflict_path = QDir(output_dir).filePath(QStringLiteral("copy.txt"));
          QVERIFY(write_archive_fixture_file(source_path, QByteArray("copy-new")));
          QVERIFY(write_archive_fixture_file(conflict_path, QByteArray("copy-old")));

          z7::ui::filemanager::MainWindow window;
          window.set_current_directory(root.path());
          const int row = row_by_name(window, QStringLiteral("copy.txt"));
          QVERIFY(row >= 0);
          select_rows_in_active_panel(&window, {row});

          bool saw_overwrite = false;
          schedule_copy_move_dialog_submit(output_dir, true);
          schedule_overwrite_prompt_submit(choice, &saw_overwrite);
          window.on_copy_to_requested();

          QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr,
                                   20000);
          QVERIFY(saw_overwrite);
          QVERIFY(QFileInfo::exists(source_path));
          QCOMPARE(read_file_bytes(conflict_path), expected_conflict);
          const QStringList files =
              QDir(output_dir).entryList(QStringList() << QStringLiteral("copy*"),
                                         QDir::Files | QDir::NoDotAndDotDot);
          QCOMPARE(files.size(), expect_auto_rename ? 2 : 1);
          if (expect_auto_rename) {
            QVERIFY(files.contains(QStringLiteral("copy_1.txt")));
            QCOMPARE(read_file_bytes(QDir(output_dir).filePath(QStringLiteral("copy_1.txt"))),
                     QByteArray("copy-new"));
          }
        };

    run_copy_case(OverwritePromptChoice::kYes, QByteArray("copy-new"), false);
    run_copy_case(OverwritePromptChoice::kNo, QByteArray("copy-old"), false);
    run_copy_case(OverwritePromptChoice::kAutoRename, QByteArray("copy-old"), true);

    auto run_move_case =
        [](OverwritePromptChoice choice,
           const QByteArray& expected_conflict,
           bool expect_source_exists) {
          QTemporaryDir root;
          QVERIFY2(root.isValid(), "failed to create temp dir");

          const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
          QVERIFY(QDir().mkpath(output_dir));
          const QString source_path =
              QDir(root.path()).filePath(QStringLiteral("move.txt"));
          const QString conflict_path = QDir(output_dir).filePath(QStringLiteral("move.txt"));
          QVERIFY(write_archive_fixture_file(source_path, QByteArray("move-new")));
          QVERIFY(write_archive_fixture_file(conflict_path, QByteArray("move-old")));

          z7::ui::filemanager::MainWindow window;
          window.set_current_directory(root.path());
          const int row = row_by_name(window, QStringLiteral("move.txt"));
          QVERIFY(row >= 0);
          select_rows_in_active_panel(&window, {row});

          bool saw_overwrite = false;
          schedule_copy_move_dialog_submit(output_dir, true);
          schedule_overwrite_prompt_submit(choice, &saw_overwrite);
          window.on_move_to_requested();

          QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr,
                                   20000);
          QVERIFY(saw_overwrite);
          QCOMPARE(QFileInfo::exists(source_path), expect_source_exists);
          QCOMPARE(read_file_bytes(conflict_path), expected_conflict);
        };

    run_move_case(OverwritePromptChoice::kNo, QByteArray("move-old"), true);
    run_move_case(OverwritePromptChoice::kYes, QByteArray("move-new"), false);
  }

void FileManagerBehaviorTest::copyMoveConflictCancelStopsBeforeLaterSources() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(output_dir));
    QStringList names{QStringLiteral("a.txt"), QStringLiteral("b.txt")};
    QList<int> rows;
    z7::ui::filemanager::MainWindow window;
    for (const QString& name : names) {
      QVERIFY(write_archive_fixture_file(QDir(root.path()).filePath(name),
                                         QByteArray("new-") + name.toUtf8()));
      QVERIFY(write_archive_fixture_file(QDir(output_dir).filePath(name),
                                         QByteArray("old-") + name.toUtf8()));
    }

    window.set_current_directory(root.path());
    for (const QString& name : names) {
      const int row = row_by_name(window, name);
      QVERIFY(row >= 0);
      rows << row;
    }
    select_rows_in_active_panel(&window, rows);

    bool saw_overwrite = false;
    schedule_copy_move_dialog_submit(output_dir, true);
    schedule_overwrite_prompt_submit(OverwritePromptChoice::kCancel, &saw_overwrite);
    window.on_move_to_requested();

    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr,
                             20000);
    QVERIFY(saw_overwrite);
    for (const QString& name : names) {
      QVERIFY(QFileInfo::exists(QDir(root.path()).filePath(name)));
      QCOMPARE(read_file_bytes(QDir(output_dir).filePath(name)),
               QByteArray("old-") + name.toUtf8());
    }
  }

void FileManagerBehaviorTest::overwritePromptDefaultsToNo() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(output_dir));
    const QString source_path = QDir(root.path()).filePath(QStringLiteral("default.txt"));
    const QString conflict_path = QDir(output_dir).filePath(QStringLiteral("default.txt"));
    QVERIFY(write_archive_fixture_file(source_path, QByteArray("new-default")));
    QVERIFY(write_archive_fixture_file(conflict_path, QByteArray("old-default")));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int row = row_by_name(window, QStringLiteral("default.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    bool saw_prompt = false;
    bool default_is_no = false;
    auto* timer = new QTimer(QApplication::instance());
    timer->setInterval(10);
    QObject::connect(timer,
                     &QTimer::timeout,
                     [timer, &saw_prompt, &default_is_no]() {
      QWidgetList candidates;
      if (QWidget* modal = QApplication::activeModalWidget()) {
        candidates << modal;
      }
      candidates << QApplication::topLevelWidgets();
      for (QWidget* widget : candidates) {
        auto* box = qobject_cast<QMessageBox*>(widget);
        if (box == nullptr ||
            box->objectName() != QStringLiteral("overwritePromptDialog")) {
          continue;
        }
        saw_prompt = true;
        default_is_no = box->defaultButton() == box->button(QMessageBox::No);
        box->reject();
        timer->stop();
        timer->deleteLater();
        return;
      }
    });
    timer->start();
    QTimer::singleShot(5000, timer, [timer]() {
      timer->stop();
      timer->deleteLater();
    });

    schedule_copy_move_dialog_submit(output_dir, true);
    window.on_copy_to_requested();

    QTRY_VERIFY_WITH_TIMEOUT(saw_prompt, 5000);
    QVERIFY(default_is_no);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr,
                             20000);
    QCOMPARE(read_file_bytes(conflict_path), QByteArray("old-default"));
  }

void FileManagerBehaviorTest::createAndRenameUseRunnerTaskChain() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString selected_path = QDir(root.path()).filePath(QStringLiteral("selected.txt"));
    const QString source_path = QDir(root.path()).filePath(QStringLiteral("before.txt"));
    {
      QFile file(selected_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("selected");
    }
    {
      QFile file(source_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("rename me");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int source_row = row_by_name(window, QStringLiteral("before.txt"));
    const int selected_row = row_by_name(window, QStringLiteral("selected.txt"));
    QVERIFY(source_row >= 0);
    QVERIFY(selected_row >= 0);
    select_rows_in_active_panel(&window, {selected_row});
    QItemSelectionModel* selection =
        window.active_panel_controller().ui.details_view->selectionModel();
    QVERIFY(selection != nullptr);
    const QModelIndex source_index =
        window.active_panel_controller().ui.details_view->model()->index(
            source_row, 0);
    QVERIFY(source_index.isValid());
    selection->setCurrentIndex(source_index, QItemSelectionModel::NoUpdate);
    window.refresh_action_states();
    QVERIFY(window.rename_action_->isEnabled());
    QCOMPARE(window.focused_path_for_panel(window.active_panel_index_),
             source_path);
    QCOMPARE(window.selected_filesystem_paths_including_parent_link(), QStringList{selected_path});

    const QString renamed = QStringLiteral("after.txt");
    window.resize(900, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.active_panel_controller().ui.details_view->setFocus();
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F2);
    QTRY_VERIFY_WITH_TIMEOUT(visible_inline_rename_editor(window) != nullptr,
                             5000);
    QVERIFY(QApplication::activeModalWidget() == nullptr);
    QLineEdit* rename_editor = visible_inline_rename_editor(window);
    QVERIFY(rename_editor != nullptr);
    QCOMPARE(rename_editor->text(), QStringLiteral("before.txt"));
    QTest::keyClick(rename_editor, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(visible_inline_rename_editor(window) == nullptr,
                             5000);
    QVERIFY(submit_model_rename(window, QStringLiteral("before.txt"), renamed));

    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    const QString renamed_path = QDir(root.path()).filePath(renamed);
    QVERIFY(!QFileInfo::exists(source_path));
    QVERIFY(QFileInfo::exists(renamed_path));
    QVERIFY(QFileInfo::exists(selected_path));
    QCOMPARE(window.focused_path_for_panel(window.active_panel_index_),
             renamed_path);
    QCOMPARE(window.selected_filesystem_paths_including_parent_link(),
             QStringList{renamed_path});

    const QString created_folder = QStringLiteral("created-by-runner");
    schedule_input_dialog_submit(created_folder, true);
    window.on_create_folder_requested();
    QVERIFY(filemanager_behavior_internal::current_runner(window) != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    const QFileInfo created_info(QDir(root.path()).filePath(created_folder));
    QVERIFY(created_info.exists());
    QVERIFY(created_info.isDir());

    const QString shortcut_created_folder =
        QStringLiteral("created-by-f7");
    schedule_input_dialog_submit(shortcut_created_folder, true);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F7);
    QVERIFY(filemanager_behavior_internal::current_runner(window) != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);

    const QFileInfo shortcut_created_folder_info(
        QDir(root.path()).filePath(shortcut_created_folder));
    QVERIFY(shortcut_created_folder_info.exists());
    QVERIFY(shortcut_created_folder_info.isDir());

    const QString created_file = QStringLiteral("created-by-runner.txt");
    schedule_input_dialog_submit(created_file, true);
    window.on_create_file_requested();
    QVERIFY(filemanager_behavior_internal::current_runner(window) != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    const QFileInfo created_file_info(QDir(root.path()).filePath(created_file));
    QVERIFY(created_file_info.exists());
    QVERIFY(created_file_info.isFile());

    const QList<QKeySequence> create_file_shortcuts =
        window.create_file_action_->shortcuts();
    QVERIFY(create_file_shortcuts.contains(QKeySequence(QStringLiteral("Ctrl+N"))));
    QVERIFY(create_file_shortcuts.contains(QKeySequence(Qt::SHIFT | Qt::Key_F4)));

    const QString shortcut_created_file =
        QStringLiteral("created-by-shift-f4.txt");
    schedule_input_dialog_submit(shortcut_created_file, true);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F4,
                    Qt::ShiftModifier);
    QVERIFY(filemanager_behavior_internal::current_runner(window) != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);

    const QFileInfo shortcut_created_file_info(
        QDir(root.path()).filePath(shortcut_created_file));
    QVERIFY(shortcut_created_file_info.exists());
    QVERIFY(shortcut_created_file_info.isFile());

    const QString ctrl_n_created_file =
        QStringLiteral("created-by-ctrl-n.txt");
    schedule_input_dialog_submit(ctrl_n_created_file, true);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_N,
                    Qt::ControlModifier);
    QVERIFY(filemanager_behavior_internal::current_runner(window) != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);

    const QFileInfo ctrl_n_created_file_info(
        QDir(root.path()).filePath(ctrl_n_created_file));
    QVERIFY(ctrl_n_created_file_info.exists());
    QVERIFY(ctrl_n_created_file_info.isFile());
  }

void FileManagerBehaviorTest::renameActionUsesFocusedItemInArchiveViewLikeOriginal() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString selected_file =
        QDir(root.path()).filePath(QStringLiteral("selected.txt"));
    const QString focused_file =
        QDir(root.path()).filePath(QStringLiteral("focused.txt"));
    for (const QString& path : {selected_file, focused_file}) {
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write(QFileInfo(path).fileName().toUtf8());
      file.close();
    }
    const QString archive_path =
        QDir(root.path()).filePath(QStringLiteral("rename-focused.7z"));
    QString create_error;
    QVERIFY2(create_archive_via_backend(root.path(),
                                        archive_path,
                                        QStringList{QStringLiteral("selected.txt"),
                                                    QStringLiteral("focused.txt")},
                                        &create_error),
             qPrintable(create_error));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);
    QVERIFY(window.in_archive_view());

    const int selected_row = row_by_name(window, QStringLiteral("selected.txt"));
    const int focused_row = row_by_name(window, QStringLiteral("focused.txt"));
    QVERIFY(selected_row >= 0);
    QVERIFY(focused_row >= 0);
    select_rows_in_active_panel(&window, {selected_row});
    QItemSelectionModel* selection =
        window.active_panel_controller().ui.details_view->selectionModel();
    QVERIFY(selection != nullptr);
    const QModelIndex focused_index =
        window.active_panel_controller().ui.details_view->model()->index(
            focused_row, 0);
    QVERIFY(focused_index.isValid());
    selection->setCurrentIndex(focused_index, QItemSelectionModel::NoUpdate);
    window.refresh_action_states();
    QVERIFY(window.rename_action_->isEnabled());
    QCOMPARE(window.focused_path_for_panel(window.active_panel_index_),
             QStringLiteral("focused.txt"));
    QCOMPARE(window.selected_archive_entries(), QStringList{QStringLiteral("selected.txt")});

    window.resize(900, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.on_rename_requested();

    QTRY_VERIFY_WITH_TIMEOUT(visible_inline_rename_editor(window) != nullptr,
                             5000);
    QVERIFY(QApplication::activeModalWidget() == nullptr);
    QLineEdit* rename_editor = visible_inline_rename_editor(window);
    QVERIFY(rename_editor != nullptr);
    QCOMPARE(rename_editor->text(), QStringLiteral("focused.txt"));
    QTest::keyClick(rename_editor, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(visible_inline_rename_editor(window) == nullptr,
                             5000);
    QVERIFY(submit_model_rename(window,
                                QStringLiteral("focused.txt"),
                                QStringLiteral("renamed-focused.txt")));
    QTRY_VERIFY_WITH_TIMEOUT(
        row_by_name(window, QStringLiteral("renamed-focused.txt")) >= 0,
        20000);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);
    QVERIFY(row_by_name(window, QStringLiteral("focused.txt")) < 0);
    QVERIFY(row_by_name(window, QStringLiteral("selected.txt")) >= 0);

    const QModelIndex current =
        window.active_panel_controller().ui.details_view->selectionModel()
            ->currentIndex();
    QVERIFY(current.isValid());
    QCOMPARE(current.row(),
             row_by_name(window, QStringLiteral("renamed-focused.txt")));
    QCOMPARE(window.focused_path_for_panel(window.active_panel_index_),
             QStringLiteral("renamed-focused.txt"));
    QCOMPARE(window.selected_archive_entries(),
             QStringList{QStringLiteral("renamed-focused.txt")});

    QTemporaryDir extract_root;
    QVERIFY2(extract_root.isValid(), "failed to create extract temp dir");
    bool renamed_ok = false;
    QCOMPARE(extract_archive_session_entry_text(
                 window.active_panel_controller().archive.current_token,
                 QStringLiteral("renamed-focused.txt"),
                 extract_root.path(),
                 &renamed_ok),
             QStringLiteral("focused.txt"));
    QVERIFY(renamed_ok);
}

void FileManagerBehaviorTest::filesystemRenameRejectsParentInvalidAndExistingTargets() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QDir root_dir(root.path());
    const QString source_path = root_dir.filePath(QStringLiteral("source.txt"));
    const QString existing_path =
        root_dir.filePath(QStringLiteral("existing.txt"));
    {
      QFile file(source_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("source payload");
    }
    {
      QFile file(existing_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("existing payload");
    }

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();
    window.set_current_directory(root.path());
    window.refresh_action_states();

    QVERIFY(row_by_name(window, QStringLiteral("..")) >= 0);
    QVERIFY(row_by_name(window, QStringLiteral("source.txt")) >= 0);
    QVERIFY(source_name_index_is_editable(window, QStringLiteral("source.txt")));
    QVERIFY(!source_name_index_is_editable(window, QStringLiteral("..")));
    QVERIFY(set_active_current_row_by_name(&window, QStringLiteral("..")));
    QVERIFY(!window.rename_action_->isEnabled());
    QVERIFY(!window.edit_focused_item_label_for_panel(window.active_panel_index_));

    QVERIFY(set_active_current_row_by_name(&window, QStringLiteral("source.txt")));
    QVERIFY(window.rename_action_->isEnabled());
    const QStringList invalid_names{
        QString(),
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("bad/name.txt"),
        QStringLiteral("bad\\name.txt"),
    };
    for (const QString& invalid_name : invalid_names) {
      QString warning_text;
      schedule_message_box_capture_and_click(QMessageBox::Ok,
                                             nullptr,
                                             &warning_text);
      QVERIFY(!submit_model_rename(window,
                                   QStringLiteral("source.txt"),
                                   invalid_name));
      QTRY_VERIFY_WITH_TIMEOUT(!warning_text.isEmpty(), 5000);
      QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
      QVERIFY(QFileInfo::exists(source_path));
      QVERIFY(QFileInfo::exists(existing_path));
    }

    QString conflict_warning;
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           nullptr,
                                           &conflict_warning);
    QVERIFY(!submit_model_rename(window,
                                 QStringLiteral("source.txt"),
                                 QStringLiteral("existing.txt")));
    QTRY_VERIFY_WITH_TIMEOUT(!conflict_warning.isEmpty(), 5000);
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QVERIFY(QFileInfo::exists(source_path));
    QVERIFY(QFileInfo::exists(existing_path));
  }

void FileManagerBehaviorTest::archiveRenameRejectsInvalidExistingAndMissingSession() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    QString archive_error;
    const QString archive_path =
        create_same_folder_archive_fixture(root, &archive_error);
    QVERIFY2(!archive_path.isEmpty(), qPrintable(archive_error));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);
    QVERIFY(window.in_archive_view());

    QVERIFY(set_archive_current_row_by_name(&window, QStringLiteral("focused.txt")));
    QVERIFY(window.rename_action_->isEnabled());
    QVERIFY(source_name_index_is_editable(window, QStringLiteral("focused.txt")));

    const QStringList invalid_names{
        QString(),
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("bad/name.txt"),
        QStringLiteral("bad\\name.txt"),
    };
    for (const QString& invalid_name : invalid_names) {
      QString warning_text;
      schedule_message_box_capture_and_click(QMessageBox::Ok,
                                             nullptr,
                                             &warning_text);
      QVERIFY(!submit_model_rename(window,
                                   QStringLiteral("focused.txt"),
                                   invalid_name));
      QTRY_VERIFY_WITH_TIMEOUT(!warning_text.isEmpty(), 5000);
      QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
      QVERIFY(row_by_name(window, QStringLiteral("focused.txt")) >= 0);
      QVERIFY(row_by_name(window, QStringLiteral("existing.txt")) >= 0);
    }

    QString conflict_warning;
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           nullptr,
                                           &conflict_warning);
    QVERIFY(!submit_model_rename(window,
                                 QStringLiteral("focused.txt"),
                                 QStringLiteral("existing.txt")));
    QTRY_VERIFY_WITH_TIMEOUT(!conflict_warning.isEmpty(), 5000);
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QVERIFY(row_by_name(window, QStringLiteral("focused.txt")) >= 0);
    QVERIFY(row_by_name(window, QStringLiteral("existing.txt")) >= 0);

    const z7::app::ArchiveSessionToken saved_token =
        window.active_panel_controller().archive.current_token;
    window.active_panel_controller().archive.current_token =
        z7::app::ArchiveSessionToken();
    window.refresh_action_states();
    QVERIFY(!window.rename_action_->isEnabled());
    QVERIFY(!source_name_index_is_editable(window, QStringLiteral("focused.txt")));
    QVERIFY(!window.edit_focused_item_label_for_panel(window.active_panel_index_));
    window.active_panel_controller().archive.current_token = saved_token;
  }

void FileManagerBehaviorTest::archiveRenameRejectsParentLink() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);
    QVERIFY(window.in_archive_view());

    QVERIFY(row_by_name(window, QStringLiteral("..")) >= 0);
    QVERIFY(!source_name_index_is_editable(window, QStringLiteral("..")));
    QVERIFY(set_archive_current_row_by_name(&window, QStringLiteral("..")));
    QVERIFY(!window.rename_action_->isEnabled());
    QVERIFY(!window.edit_focused_item_label_for_panel(window.active_panel_index_));
  }

void FileManagerBehaviorTest::deleteInArchiveViewRequiresConfirmation() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    int entry_row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(entry_row >= 0);
    select_rows_in_active_panel(&window, {entry_row});

    const QString expected_title =
        without_mnemonic(z7::ui::runtime_support::L(6100));
    const QString expected_text =
        z7::ui::runtime_support::LF(6103, {QStringLiteral("sample.txt")});

    struct DeletePromptSnapshot {
      QString title;
      QString text;
      QMessageBox::StandardButtons buttons = QMessageBox::NoButton;
      QMessageBox::StandardButton default_button = QMessageBox::NoButton;
    };
    QList<DeletePromptSnapshot> prompts;
    QList<QMessageBox::StandardButton> scripted_replies = {
        QMessageBox::No,
        QMessageBox::Cancel,
        QMessageBox::Yes};
    window.question_box_ =
        [&prompts, &scripted_replies](const QString& title,
                                      const QString& text,
                                      QMessageBox::StandardButtons buttons,
                                      QMessageBox::StandardButton default_button) {
          DeletePromptSnapshot snapshot;
          snapshot.title = title;
          snapshot.text = text;
          snapshot.buttons = buttons;
          snapshot.default_button = default_button;
          prompts.push_back(snapshot);
          if (scripted_replies.isEmpty()) {
            return QMessageBox::No;
          }
          const QMessageBox::StandardButton reply = scripted_replies.front();
          scripted_replies.pop_front();
          return reply;
        };

    // Archive delete should keep original behavior and always ask for
    // Yes/No/Cancel, even if filesystem confirm-delete runtime switch is off.
    window.confirm_delete_ = false;

    window.on_delete_requested();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(prompts.size(), 1);
    QCOMPARE(prompts.front().title, expected_title);
    QCOMPARE(prompts.front().text, expected_text);
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QCOMPARE(row_by_name(window, QStringLiteral("sample.txt")), entry_row);

    select_rows_in_active_panel(&window, {entry_row});
    window.on_delete_requested();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(prompts.size(), 2);
    QCOMPARE(prompts.at(1).title, expected_title);
    QCOMPARE(prompts.at(1).text, expected_text);
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QCOMPARE(row_by_name(window, QStringLiteral("sample.txt")), entry_row);

    select_rows_in_active_panel(&window, {entry_row});
    window.on_delete_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(prompts.size(), 3);
    QCOMPARE(prompts.at(2).title, expected_title);
    QCOMPARE(prompts.at(2).text, expected_text);
    const auto expected_buttons =
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel;
    QCOMPARE(static_cast<int>(prompts.front().buttons),
             static_cast<int>(expected_buttons));
    QCOMPARE(static_cast<int>(prompts.at(1).buttons),
             static_cast<int>(expected_buttons));
    QCOMPARE(static_cast<int>(prompts.at(2).buttons),
             static_cast<int>(expected_buttons));
    QCOMPARE(prompts.front().default_button, QMessageBox::Yes);
    QCOMPARE(prompts.at(1).default_button, QMessageBox::Yes);
    QCOMPARE(prompts.at(2).default_button, QMessageBox::Yes);
    QTRY_VERIFY_WITH_TIMEOUT(
        row_by_name(window, QStringLiteral("sample.txt")) < 0,
        20000);
  }

void FileManagerBehaviorTest::propertiesDialogUsesTwoColumnTableAndShowsExpectedRows() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_a = QDir(root.path()).filePath(QStringLiteral("a.txt"));
    const QString file_b = QDir(root.path()).filePath(QStringLiteral("b.txt"));
    {
      QFile file(file_a);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("a");
    }
    {
      QFile file(file_b);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("b");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    QStringList single_keys;
    int single_columns = 0;
    bool single_header_visible = false;
    bool single_has_cancel_button = false;
    const int row_a = row_by_name(window, QStringLiteral("a.txt"));
    QVERIFY(row_a >= 0);
    select_rows_in_active_panel(&window, {row_a});
    QTimer::singleShot(0, [&single_keys,
                           &single_columns,
                           &single_header_visible,
                           &single_has_cancel_button]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr || dialog->objectName() != QStringLiteral("propertiesDialog")) {
          continue;
        }
        auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("propertiesTable"));
        QVERIFY(table != nullptr);
        single_columns = table->columnCount();
        single_header_visible = table->horizontalHeader()->isVisible();
        if (auto* buttons = dialog->findChild<QDialogButtonBox*>()) {
          single_has_cancel_button =
              buttons->button(QDialogButtonBox::Cancel) != nullptr;
        }
        for (int i = 0; i < table->rowCount(); ++i) {
          if (table->item(i, 0) != nullptr) {
            single_keys << table->item(i, 0)->text();
          }
        }
        dialog->accept();
        return;
      }
    });
    window.show_properties_dialog();
    QCOMPARE(single_columns, 2);
    QVERIFY(single_header_visible);
    QVERIFY(single_has_cancel_button);
    QVERIFY(single_keys.contains(without_mnemonic(z7::ui::runtime_support::L(1004))));
    QVERIFY(single_keys.contains(without_mnemonic(z7::ui::runtime_support::L(1020))));
    QVERIFY(single_keys.contains(without_mnemonic(z7::ui::runtime_support::L(1007))));

    QStringList multi_keys;
    int multi_row_count = 0;
    const int row_b = row_by_name(window, QStringLiteral("b.txt"));
    QVERIFY(row_b >= 0);
    window.active_panel_controller().ui.details_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    select_rows_in_active_panel(&window, {row_a, row_b});
    QTimer::singleShot(0, [&multi_keys, &multi_row_count]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr || dialog->objectName() != QStringLiteral("propertiesDialog")) {
          continue;
        }
        auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("propertiesTable"));
        QVERIFY(table != nullptr);
        multi_row_count = table->rowCount();
        for (int i = 0; i < table->rowCount(); ++i) {
          if (table->item(i, 0) != nullptr) {
            multi_keys << table->item(i, 0)->text();
          }
        }
        dialog->accept();
        return;
      }
    });
    window.show_properties_dialog();
    QVERIFY(multi_row_count >= 3);
    QVERIFY(multi_keys.contains(without_mnemonic(z7::ui::runtime_support::L(1007))));
  }

void FileManagerBehaviorTest::propertiesDialogShowsArchiveMetadataFields() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    QStringList keys;
    QStringList values;
    bool dialog_seen = false;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, [&keys, &values, &dialog_seen, &timer]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr || dialog->objectName() != QStringLiteral("propertiesDialog")) {
          continue;
        }
        auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("propertiesTable"));
        QVERIFY(table != nullptr);
        for (int i = 0; i < table->rowCount(); ++i) {
          if (table->item(i, 0) != nullptr) {
            keys << table->item(i, 0)->text();
          }
          if (table->item(i, 1) != nullptr) {
            values << table->item(i, 1)->text();
          }
        }
        dialog_seen = true;
        dialog->accept();
        timer.stop();
        return;
      }
    });
    timer.start();
    window.show_properties_dialog();
    QTRY_VERIFY_WITH_TIMEOUT(dialog_seen, 20000);

    QVERIFY(keys.contains(without_mnemonic(z7::ui::runtime_support::L(1003))));
    QVERIFY(keys.contains(without_mnemonic(z7::ui::runtime_support::L(1022))));
    QVERIFY(keys.contains(without_mnemonic(z7::ui::runtime_support::L(1019))));
    QVERIFY(keys.contains(QStringLiteral("------------------------")));
    bool has_archive_path = false;
    for (const QString& value : values) {
      if (value.contains(QStringLiteral("sample.7z"), Qt::CaseInsensitive)) {
        has_archive_path = true;
        break;
      }
    }
    QVERIFY(has_archive_path);
  }

void FileManagerBehaviorTest::propertiesDialogInArchiveViewUsesSessionWhenArchivePathMissing() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    QVERIFY(QFile::remove(archive_path));
    QVERIFY(!QFileInfo::exists(archive_path));

    QStringList keys;
    QStringList values;
    bool dialog_seen = false;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, [&keys, &values, &dialog_seen, &timer]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr || dialog->objectName() != QStringLiteral("propertiesDialog")) {
          continue;
        }
        auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("propertiesTable"));
        QVERIFY(table != nullptr);
        for (int i = 0; i < table->rowCount(); ++i) {
          if (table->item(i, 0) != nullptr) {
            keys << table->item(i, 0)->text();
          }
          if (table->item(i, 1) != nullptr) {
            values << table->item(i, 1)->text();
          }
        }
        dialog_seen = true;
        dialog->accept();
        timer.stop();
        return;
      }
    });
    timer.start();

    window.show_properties_dialog();
    QTRY_VERIFY_WITH_TIMEOUT(dialog_seen, 20000);

    QVERIFY(keys.contains(without_mnemonic(z7::ui::runtime_support::L(1003))));
    QVERIFY(keys.contains(without_mnemonic(z7::ui::runtime_support::L(1022))));
    bool has_archive_path = false;
    for (const QString& value : values) {
      if (value.contains(QStringLiteral("sample.7z"), Qt::CaseInsensitive)) {
        has_archive_path = true;
        break;
      }
    }
    QVERIFY(has_archive_path);
  }


// End of archive.cpp
