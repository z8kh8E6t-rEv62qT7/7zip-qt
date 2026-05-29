// tests/filemanager/behavior/archive_commands.cpp
// Role: Command bridge and view/edit behavior cases.

#include "internal.h"

#include "main_window/model/model.h"

#include <vector>

using namespace filemanager_behavior_internal;

namespace {

QStringList temp_dirs_with_prefix(const QString& prefix) {
  QDir temp_root(QDir::tempPath());
  return temp_root.entryList(QStringList{prefix + QStringLiteral("*")},
                             QDir::Dirs | QDir::NoDotAndDotDot,
                             QDir::Name);
}

QString extract_entry_text(const QString& archive_path,
                           const QString& entry_name,
                           const QString& output_root) {
  z7::app::ExtractRequest request;
  request.archive_path = to_native_path_string(archive_path);
  request.output_dir = to_native_path_string(output_root);
  request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
  request.entries = std::vector<std::string>{to_native_path_string(entry_name)};
  z7::app::ArchiveRequest archive_request;
  archive_request.payload = request;
  const z7::app::OperationOutcome outcome =
      run_archive_request_and_await(archive_request);
  z7::app::ExtractResult result;
  if (const std::optional<z7::app::ExtractResult> typed =
          z7::app::outcome_payload_as<z7::app::ExtractResult>(outcome);
      typed.has_value()) {
    result = *typed;
  } else {
    result.ok = outcome.ok;
    result.error = outcome.error;
    result.native_exit_code = outcome.native_code;
    result.native_execution = outcome.native_execution;
    result.summary = outcome.summary;
  }
  if (!result.ok) {
    return QString();
  }
  QFile file(QDir(output_root).filePath(entry_name));
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  return QString::fromUtf8(file.readAll());
}

QString extract_entry_text_from_session(z7::app::ArchiveSessionToken token,
                                        const QString& entry_name,
                                        const QString& output_root) {
  z7::app::ExtractRequest request;
  request.session_token = token;
  request.output_dir = to_native_path_string(output_root);
  request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
  request.entries = std::vector<std::string>{to_native_path_string(entry_name)};
  z7::app::ArchiveRequest archive_request;
  archive_request.payload = request;
  const z7::app::OperationOutcome outcome =
      run_archive_request_and_await(archive_request);
  z7::app::ExtractResult result;
  if (const std::optional<z7::app::ExtractResult> typed =
          z7::app::outcome_payload_as<z7::app::ExtractResult>(outcome);
      typed.has_value()) {
    result = *typed;
  } else {
    result.ok = outcome.ok;
    result.error = outcome.error;
    result.native_exit_code = outcome.native_code;
    result.native_execution = outcome.native_execution;
    result.summary = outcome.summary;
  }
  if (!result.ok) {
    return QString();
  }
  QFile file(QDir(output_root).filePath(entry_name));
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  return QString::fromUtf8(file.readAll());
}

}  // namespace

void FileManagerBehaviorTest::coreActionsAreSharedAcrossMenusAndToolbars() {
    z7::ui::filemanager::MainWindow window;

    QVERIFY(window.file_menu_ != nullptr);
    QVERIFY(window.archive_toolbar_ != nullptr);
    QVERIFY(window.standard_toolbar_ != nullptr);

    const QList<QAction*> file_actions = window.file_menu_->actions();
    QVERIFY(file_actions.size() >= 32);
    QCOMPARE(file_actions.at(0), window.open_action_);
    QCOMPARE(file_actions.at(1), window.open_inside_action_);
    QCOMPARE(file_actions.at(2), window.open_inside_one_action_);
    QCOMPARE(file_actions.at(3), window.open_inside_parser_action_);
    QCOMPARE(file_actions.at(4), window.open_outside_action_);
    QCOMPARE(file_actions.at(5), window.view_action_);
    QCOMPARE(file_actions.at(6), window.edit_action_);
    QVERIFY(file_actions.at(7)->isSeparator());
    QCOMPARE(file_actions.at(8), window.rename_action_);
    QCOMPARE(file_actions.at(9), window.copy_to_action_);
    QCOMPARE(file_actions.at(10), window.move_to_action_);
    QCOMPARE(file_actions.at(11), window.delete_action_);
    QVERIFY(file_actions.at(12)->isSeparator());
    QCOMPARE(file_actions.at(13), window.split_action_);
    QCOMPARE(file_actions.at(14), window.combine_action_);
    QVERIFY(file_actions.at(15)->isSeparator());
    QCOMPARE(file_actions.at(16), window.properties_action_);
    QCOMPARE(file_actions.at(17), window.comment_action_);
    QCOMPARE(file_actions.at(18), window.crc_menu_->menuAction());
    QCOMPARE(file_actions.at(19), window.diff_action_);
    QVERIFY(file_actions.at(20)->isSeparator());
    QCOMPARE(file_actions.at(21), window.create_folder_action_);
    QCOMPARE(file_actions.at(22), window.create_file_action_);
    QVERIFY(file_actions.at(23)->isSeparator());
    QCOMPARE(file_actions.at(24), window.link_action_);
    QCOMPARE(file_actions.at(25), window.alternate_streams_action_);
    QVERIFY(file_actions.at(26)->isSeparator());
    QCOMPARE(file_actions.at(27), window.exit_action_);
    QCOMPARE(file_actions.at(28), window.version_edit_action_);
    QCOMPARE(file_actions.at(29), window.version_commit_action_);
    QCOMPARE(file_actions.at(30), window.version_revert_action_);
    QCOMPARE(file_actions.at(31), window.version_diff_action_);

    QCOMPARE(window.open_action_->shortcut(), QKeySequence(Qt::Key_Return));
    QCOMPARE(window.open_inside_action_->shortcut(),
             QKeySequence(QStringLiteral("Ctrl+PgDown")));
    QCOMPARE(window.open_outside_action_->shortcut(),
             QKeySequence(QStringLiteral("Shift+Return")));
    QCOMPARE(window.view_action_->shortcut(), QKeySequence(Qt::Key_F3));
    QCOMPARE(window.edit_action_->shortcut(), QKeySequence(Qt::Key_F4));
    QCOMPARE(window.rename_action_->shortcut(), QKeySequence(Qt::Key_F2));
    QCOMPARE(window.copy_to_action_->shortcut(), QKeySequence(Qt::Key_F5));
    QCOMPARE(window.move_to_action_->shortcut(), QKeySequence(Qt::Key_F6));
    QCOMPARE(window.properties_action_->shortcut(),
             QKeySequence(QStringLiteral("Alt+Return")));
    QCOMPARE(window.comment_action_->shortcut(),
             QKeySequence(QStringLiteral("Ctrl+Z")));
    QCOMPARE(window.create_folder_action_->shortcut(), QKeySequence(Qt::Key_F7));
    QVERIFY(window.create_file_action_->shortcuts().contains(
        QKeySequence(QStringLiteral("Ctrl+N"))));

    const QList<QAction*> archive_toolbar_actions = window.archive_toolbar_->actions();
    QCOMPARE(archive_toolbar_actions.size(), 3);
    QCOMPARE(archive_toolbar_actions.at(0), window.compress_action_);
    QCOMPARE(archive_toolbar_actions.at(1), window.extract_action_);
    QCOMPARE(archive_toolbar_actions.at(2), window.test_action_);

    const QList<QAction*> standard_toolbar_actions = window.standard_toolbar_->actions();
    QCOMPARE(standard_toolbar_actions.size(), 4);
    QCOMPARE(standard_toolbar_actions.at(0), window.copy_to_action_);
    QCOMPARE(standard_toolbar_actions.at(1), window.move_to_action_);
    QCOMPARE(standard_toolbar_actions.at(2), window.delete_action_);
    QCOMPARE(standard_toolbar_actions.at(3), window.properties_action_);

    const QList<QKeySequence> delete_shortcuts = window.delete_action_->shortcuts();
    QVERIFY(delete_shortcuts.contains(QKeySequence(Qt::Key_Delete)));
    QVERIFY(delete_shortcuts.contains(QKeySequence(Qt::SHIFT | Qt::Key_Delete)));
  }

void FileManagerBehaviorTest::versionControlMenuFrontendFollowsOriginalGatingAndStaysDisabled() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString writable_file = QDir(root.path()).filePath(QStringLiteral("writable.txt"));
    const QString readonly_file = QDir(root.path()).filePath(QStringLiteral("readonly.txt"));
    const QString folder = QDir(root.path()).filePath(QStringLiteral("folder"));
    QVERIFY(QDir().mkpath(folder));
    for (const QString& path : {writable_file, readonly_file}) {
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("version control fixture");
      file.close();
    }
    QVERIFY(QFile::setPermissions(
        readonly_file,
        QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther));
    QVERIFY2(!QFileInfo(readonly_file).isWritable(),
             "readonly fixture is unexpectedly writable");

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const auto verify_all_hidden = [&window]() {
      QVERIFY(!window.version_edit_action_->isVisible());
      QVERIFY(!window.version_commit_action_->isVisible());
      QVERIFY(!window.version_revert_action_->isVisible());
      QVERIFY(!window.version_diff_action_->isVisible());
    };
    const auto verify_all_disabled = [&window]() {
      QVERIFY(!window.version_edit_action_->isEnabled());
      QVERIFY(!window.version_commit_action_->isEnabled());
      QVERIFY(!window.version_revert_action_->isEnabled());
      QVERIFY(!window.version_diff_action_->isEnabled());
    };

    window.refresh_action_states();
    verify_all_hidden();
    verify_all_disabled();

    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/7vc"), QStringLiteral("/tmp/7vc-root"));
    window.refresh_action_states();
    verify_all_hidden();

    settings.setValue(QStringLiteral("FM/Diff"), QStringLiteral("/usr/bin/diff"));

    const int writable_row = row_by_name(window, QStringLiteral("writable.txt"));
    const int readonly_row = row_by_name(window, QStringLiteral("readonly.txt"));
    const int folder_row = row_by_name(window, QStringLiteral("folder"));
    QVERIFY(writable_row >= 0);
    QVERIFY(readonly_row >= 0);
    QVERIFY(folder_row >= 0);

    select_rows_in_active_panel(&window, {writable_row});
    window.refresh_action_states();
    QVERIFY(!window.version_edit_action_->isVisible());
    QVERIFY(window.version_commit_action_->isVisible());
    QVERIFY(window.version_revert_action_->isVisible());
    QVERIFY(window.version_diff_action_->isVisible());
    verify_all_disabled();
    QVERIFY(window.version_commit_action_->text().contains(QStringLiteral("Ver Commit")));
    QVERIFY(window.version_commit_action_->text().contains(QStringLiteral("Unsupported")));

    QMenu context_menu;
    window.populate_context_menu(&context_menu,
                                 window.compute_seven_zip_menu_state(false));
    const QList<QAction*> context_actions = context_menu.actions();
    QVERIFY(context_actions.contains(window.version_commit_action_));
    QVERIFY(context_actions.contains(window.version_revert_action_));
    QVERIFY(context_actions.contains(window.version_diff_action_));
    const int context_alt_streams_index =
        context_actions.indexOf(window.alternate_streams_action_);
    const int context_version_commit_index =
        context_actions.indexOf(window.version_commit_action_);
    QVERIFY(context_alt_streams_index >= 0);
    QVERIFY(context_version_commit_index > context_alt_streams_index);
    int visible_before_commit_index = -1;
    for (int i = context_version_commit_index - 1; i >= 0; --i) {
      if (context_actions.at(i)->isVisible()) {
        visible_before_commit_index = i;
        break;
      }
    }
    QVERIFY(visible_before_commit_index > context_alt_streams_index);
    QVERIFY(context_actions.at(visible_before_commit_index)->isSeparator());

    select_rows_in_active_panel(&window, {readonly_row});
    window.refresh_action_states();
    QVERIFY(window.version_edit_action_->isVisible());
    QVERIFY(!window.version_commit_action_->isVisible());
    QVERIFY(!window.version_revert_action_->isVisible());
    QVERIFY(!window.version_diff_action_->isVisible());
    verify_all_disabled();
    QVERIFY(window.version_edit_action_->text().contains(QStringLiteral("Ver Edit")));
    QVERIFY(window.version_edit_action_->text().contains(QStringLiteral("Unsupported")));

    select_rows_in_active_panel(&window, {folder_row});
    window.refresh_action_states();
    verify_all_hidden();

    select_rows_in_active_panel(&window, {writable_row, readonly_row});
    window.refresh_action_states();
    verify_all_hidden();
}

void FileManagerBehaviorTest::viewAndEditUseConfiguredCommandWithFocusedFileLikeOriginal() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_a = QDir(root.path()).filePath(QStringLiteral("a.txt"));
    const QString file_b = QDir(root.path()).filePath(QStringLiteral("b.txt"));
    const QString folder = QDir(root.path()).filePath(QStringLiteral("folder"));
    {
      QFile file(file_a);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("a");
      file.close();
    }
    {
      QFile file(file_b);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("b");
      file.close();
    }
    QVERIFY(QDir().mkpath(folder));
    const QString tool_dir =
        QDir(root.path()).filePath(QStringLiteral("tools with spaces"));
    QVERIFY(QDir().mkpath(tool_dir));
    const QString viewer_tool =
        QDir(tool_dir).filePath(QStringLiteral("custom viewer"));
    {
      QFile file(viewer_tool);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("#!/bin/sh\n");
      file.close();
    }

    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/Viewer"),
                      viewer_tool + QStringLiteral(" --flag \"two words\""));
    settings.setValue(QStringLiteral("FM/Editor"),
                      QStringLiteral("\"/usr/local/bin/custom editor\" -x"));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int row_a = row_by_name(window, QStringLiteral("a.txt"));
    const int row_b = row_by_name(window, QStringLiteral("b.txt"));
    const int folder_row = row_by_name(window, QStringLiteral("folder"));
    QVERIFY(row_a >= 0);
    QVERIFY(row_b >= 0);
    QVERIFY(folder_row >= 0);
    QVERIFY(window.active_panel_controller().ui.details_view != nullptr);
    QVERIFY(window.active_panel_controller().ui.details_view->selectionModel() != nullptr);

    int launch_count = 0;
    QString last_program;
    QStringList last_args;
    QString last_workdir;
    window.external_command_launcher_ =
        [&launch_count, &last_program, &last_args, &last_workdir](
            const QString& program,
            const QStringList& args,
            const QString& working_dir,
            qint64*) {
          ++launch_count;
          last_program = program;
          last_args = args;
          last_workdir = working_dir;
          return true;
        };

    QItemSelectionModel* selection = window.active_panel_controller().ui.details_view->selectionModel();
    selection->clearSelection();
    const QModelIndex idx_a = window.active_panel_controller().ui.details_view->model()->index(row_a, 0);
    const QModelIndex idx_b = window.active_panel_controller().ui.details_view->model()->index(row_b, 0);
    const QModelIndex folder_idx =
        window.active_panel_controller().ui.details_view->model()->index(folder_row, 0);
    QVERIFY(idx_a.isValid());
    QVERIFY(idx_b.isValid());
    QVERIFY(folder_idx.isValid());
    selection->select(idx_a, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->select(idx_b, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->setCurrentIndex(idx_b, QItemSelectionModel::NoUpdate);
    window.on_view_requested();
    QCOMPARE(launch_count, 1);
    QCOMPARE(last_program, viewer_tool);
    const QStringList expected_view_args = {
        QStringLiteral("--flag"),
        QStringLiteral("two words"),
        QFileInfo(file_b).absoluteFilePath()};
    QCOMPARE(last_args, expected_view_args);
    QCOMPARE(last_workdir, QDir(root.path()).absolutePath());

    selection->clearSelection();
    selection->select(idx_b, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->setCurrentIndex(idx_a, QItemSelectionModel::NoUpdate);
    window.on_edit_requested();
    QCOMPARE(launch_count, 2);
    QCOMPARE(last_program, QStringLiteral("/usr/local/bin/custom editor"));
    const QStringList expected_edit_args = {
        QStringLiteral("-x"),
        QFileInfo(file_a).absoluteFilePath()};
    QCOMPARE(last_args, expected_edit_args);

    selection->clearSelection();
    selection->select(idx_b, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->setCurrentIndex(folder_idx, QItemSelectionModel::NoUpdate);
    window.on_view_requested();
    QCOMPARE(launch_count, 2);

    selection->clearSelection();
    selection->select(idx_b, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->setCurrentIndex(folder_idx, QItemSelectionModel::NoUpdate);
    window.on_edit_requested();
    QCOMPARE(launch_count, 2);
  }

void FileManagerBehaviorTest::viewCommandCalculatesSelectedDirectoryFullSizeLikeOriginal() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString folder = QDir(root.path()).filePath(QStringLiteral("folder"));
    const QString subfolder = QDir(folder).filePath(QStringLiteral("sub"));
    QVERIFY(QDir().mkpath(subfolder));
    const QString nested_a = QDir(folder).filePath(QStringLiteral("a.bin"));
    const QString nested_b = QDir(subfolder).filePath(QStringLiteral("b.bin"));
    const QString focused_file =
        QDir(root.path()).filePath(QStringLiteral("focused.txt"));
    {
      QFile file(nested_a);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("abc");
    }
    {
      QFile file(nested_b);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("hello");
    }
    {
      QFile file(focused_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("focus");
    }

    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/Viewer"),
                      QStringLiteral("\"/usr/local/bin/custom viewer\""));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int folder_row = row_by_name(window, QStringLiteral("folder"));
    const int focused_row = row_by_name(window, QStringLiteral("focused.txt"));
    QVERIFY(folder_row >= 0);
    QVERIFY(focused_row >= 0);

    int launch_count = 0;
    window.external_command_launcher_ =
        [&launch_count](const QString&, const QStringList&, const QString&, qint64*) {
          ++launch_count;
          return true;
        };

    QAbstractItemModel* model =
        window.active_panel_controller().ui.details_view->model();
    QVERIFY(model != nullptr);
    QItemSelectionModel* selection =
        window.active_panel_controller().ui.details_view->selectionModel();
    QVERIFY(selection != nullptr);
    const QModelIndex folder_index = model->index(folder_row, 0);
    const QModelIndex focused_index = model->index(focused_row, 0);
    QVERIFY(folder_index.isValid());
    QVERIFY(focused_index.isValid());
    selection->clearSelection();
    selection->select(folder_index,
                      QItemSelectionModel::Select |
                          QItemSelectionModel::Rows);
    selection->setCurrentIndex(focused_index, QItemSelectionModel::NoUpdate);

    window.on_view_requested();
    QCOMPARE(launch_count, 0);

    const int refreshed_folder_row = row_by_name(window, QStringLiteral("folder"));
    QVERIFY(refreshed_folder_row >= 0);
    QCOMPARE(model->data(model->index(refreshed_folder_row,
                                      z7::ui::filemanager::DirectoryListModel::kSizeColumn),
                         Qt::DisplayRole)
                 .toString(),
             QStringLiteral("8"));
    QCOMPARE(model->data(model->index(refreshed_folder_row,
                                      z7::ui::filemanager::DirectoryListModel::kFoldersColumn),
                         Qt::DisplayRole)
                 .toString(),
             QStringLiteral("1"));
    QCOMPARE(model->data(model->index(refreshed_folder_row,
                                      z7::ui::filemanager::DirectoryListModel::kFilesColumn),
                         Qt::DisplayRole)
                 .toString(),
             QStringLiteral("2"));

    select_rows_in_active_panel(&window, {focused_row});
    window.on_view_requested();
    QCOMPARE(launch_count, 1);
  }

void FileManagerBehaviorTest::viewAndEditShowConfigurationMessageWhenCommandIsEmpty() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("sample.txt"));
    {
      QFile file(file_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("sample");
      file.close();
    }

    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/Viewer"), QString());
    settings.setValue(QStringLiteral("FM/Editor"), QString());

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    int external_open_count = 0;
    int launcher_count = 0;
    window.external_opener_ = [&external_open_count](const QString&) {
      ++external_open_count;
      return true;
    };
    window.external_command_launcher_ =
        [&launcher_count](const QString&, const QStringList&, const QString&, qint64*) {
          ++launcher_count;
          return true;
        };

    QString first_title;
    QString first_message;
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           &first_title,
                                           &first_message,
                                           5000,
                                           10);
    window.on_view_requested();
    QVERIFY(!first_title.trimmed().isEmpty() ||
            !first_message.trimmed().isEmpty());

    QString second_title;
    QString second_message;
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           &second_title,
                                           &second_message,
                                           5000,
                                           10);
    window.on_edit_requested();
    QVERIFY(!second_title.trimmed().isEmpty() ||
            !second_message.trimmed().isEmpty());

    QCOMPARE(launcher_count, 0);
    QCOMPARE(external_open_count, 0);
  }

void FileManagerBehaviorTest::archiveViewAndEditUnchangedFileCleansTempSession() {
#if defined(Q_OS_WIN)
  QSKIP("Uses POSIX shell fixture command.");
#endif

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  const QString script_path = QDir(root.path()).filePath(QStringLiteral("noop.sh"));
  {
    QFile script(script_path);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("#!/bin/sh\nexit 0\n");
    script.close();
  }
  QVERIFY(QFile::setPermissions(
      script_path,
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
          QFileDevice::ReadGroup | QFileDevice::ExeGroup |
          QFileDevice::ReadOther | QFileDevice::ExeOther));

  z7::platform::qt::PortableSettings settings;
  settings.setValue(QStringLiteral("FM/Viewer"), script_path);

  z7::ui::filemanager::MainWindow window;
  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QVERIFY(window.in_archive_view());

  const int file_row = row_by_name(window, QStringLiteral("sample.txt"));
  QVERIFY(file_row >= 0);
  select_rows_in_active_panel(&window, {file_row});

  const QStringList before_dirs = temp_dirs_with_prefix(QStringLiteral("7zO_"));
  window.on_view_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QTRY_VERIFY_WITH_TIMEOUT(window.archive_temp_sessions_.isEmpty(), 20000);
  const QStringList after_dirs = temp_dirs_with_prefix(QStringLiteral("7zO_"));
  QCOMPARE(after_dirs, before_dirs);
}

void FileManagerBehaviorTest::archiveViewAndEditUseFocusedEntryLikeOriginal() {
#if defined(Q_OS_WIN)
  QSKIP("Uses POSIX shell fixture command.");
#endif

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
      QDir(root.path()).filePath(QStringLiteral("focused-view-edit.7z"));
  QString create_error;
  QVERIFY2(create_archive_via_backend(root.path(),
                                      archive_path,
                                      QStringList{QStringLiteral("selected.txt"),
                                                  QStringLiteral("focused.txt")},
                                      &create_error),
           qPrintable(create_error));

  const QString log_path =
      QDir(root.path()).filePath(QStringLiteral("viewed-entry.log"));
  const QString script_path = QDir(root.path()).filePath(QStringLiteral("log.sh"));
  {
    QFile script(script_path);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write(
        "#!/bin/sh\n"
        "log_path=\"$1\"\n"
        "shift\n"
        "opened_path=\"\"\n"
        "for arg do\n"
        "  opened_path=\"$arg\"\n"
        "done\n"
        "printf '%s\\n' \"$(basename \"$opened_path\")\" > \"$log_path\"\n");
    script.close();
  }
  QVERIFY(QFile::setPermissions(
      script_path,
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
          QFileDevice::ReadGroup | QFileDevice::ExeGroup |
          QFileDevice::ReadOther | QFileDevice::ExeOther));

  z7::platform::qt::PortableSettings settings;
  settings.setValue(QStringLiteral("FM/Viewer"),
                    QStringLiteral("\"%1\" \"%2\"").arg(script_path, log_path));

  z7::ui::filemanager::MainWindow window;
  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
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
  QCOMPARE(window.focused_path_for_panel(window.active_panel_index_),
           QStringLiteral("focused.txt"));

  window.on_view_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QTRY_VERIFY_WITH_TIMEOUT(window.archive_temp_sessions_.isEmpty(), 20000);

  QFile log(log_path);
  QVERIFY(log.open(QIODevice::ReadOnly));
  QCOMPARE(QString::fromUtf8(log.readAll()).trimmed(),
           QStringLiteral("focused.txt"));
}

void FileManagerBehaviorTest::archiveViewAndEditChangedFilePromptsAndUpdatesArchive() {
#if defined(Q_OS_WIN)
  QSKIP("Uses POSIX shell fixture command.");
#endif

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  const QString script_path = QDir(root.path()).filePath(QStringLiteral("rewrite.sh"));
  {
    QFile script(script_path);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("#!/bin/sh\nprintf \"updated from editor\\n\" > \"$1\"\n");
    script.close();
  }
  QVERIFY(QFile::setPermissions(
      script_path,
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
          QFileDevice::ReadGroup | QFileDevice::ExeGroup |
          QFileDevice::ReadOther | QFileDevice::ExeOther));

  z7::platform::qt::PortableSettings settings;
  settings.setValue(QStringLiteral("FM/Editor"), script_path);

  z7::ui::filemanager::MainWindow window;
  int prompt_count = 0;
  window.question_box_ = [&prompt_count](const QString&,
                                         const QString&,
                                         QMessageBox::StandardButtons,
                                         QMessageBox::StandardButton) {
    ++prompt_count;
    return QMessageBox::Yes;
  };
  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QVERIFY(window.in_archive_view());

  const int file_row = row_by_name(window, QStringLiteral("sample.txt"));
  QVERIFY(file_row >= 0);
  select_rows_in_active_panel(&window, {file_row});

  const QStringList before_dirs = temp_dirs_with_prefix(QStringLiteral("7zO_"));
  window.on_edit_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QTRY_VERIFY_WITH_TIMEOUT(window.archive_temp_sessions_.isEmpty(), 20000);
  QCOMPARE(prompt_count, 1);
  const QStringList after_dirs = temp_dirs_with_prefix(QStringLiteral("7zO_"));
  QCOMPARE(after_dirs, before_dirs);

  const z7::app::ArchiveSessionToken active_token =
      window.active_panel_controller().archive.current_token;
  QVERIFY(active_token.is_valid());
  QTemporaryDir session_extracted_root;
  QVERIFY2(session_extracted_root.isValid(),
           "failed to create session extraction verification dir");
  const QString session_extracted_text = extract_entry_text_from_session(
      active_token, QStringLiteral("sample.txt"), session_extracted_root.path());
  QCOMPARE(session_extracted_text, QStringLiteral("updated from editor\n"));

  bool close_finished = false;
  bool close_ok = false;
  window.close_archive_view_for_panel(
      window.active_panel_index_,
      [&close_finished, &close_ok](bool ok) {
        close_ok = ok;
        close_finished = true;
      });
  QTRY_VERIFY_WITH_TIMEOUT(close_finished, 20000);
  QVERIFY(close_ok);
  QVERIFY(!window.in_archive_view());
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

  QTemporaryDir extracted_root;
  QVERIFY2(extracted_root.isValid(), "failed to create extraction verification dir");
  const QString extracted_text = extract_entry_text(
      archive_path, QStringLiteral("sample.txt"), extracted_root.path());
  QCOMPARE(extracted_text, QStringLiteral("updated from editor\n"));
}

void FileManagerBehaviorTest::archiveViewAndEditDeletedTempFileWarnsAndDoesNotUpdateArchive() {
#if defined(Q_OS_WIN)
  QSKIP("Uses POSIX shell fixture command.");
#endif

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  const QString script_path = QDir(root.path()).filePath(QStringLiteral("delete.sh"));
  {
    QFile script(script_path);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("#!/bin/sh\nrm -f \"$1\"\n");
    script.close();
  }
  QVERIFY(QFile::setPermissions(
      script_path,
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
          QFileDevice::ReadGroup | QFileDevice::ExeGroup |
          QFileDevice::ReadOther | QFileDevice::ExeOther));

  z7::platform::qt::PortableSettings settings;
  settings.setValue(QStringLiteral("FM/Editor"), script_path);

  z7::ui::filemanager::MainWindow window;
  int prompt_count = 0;
  window.question_box_ = [&prompt_count](const QString&,
                                         const QString&,
                                         QMessageBox::StandardButtons,
                                         QMessageBox::StandardButton) {
    ++prompt_count;
    return QMessageBox::Yes;
  };
  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QVERIFY(window.in_archive_view());

  const int file_row = row_by_name(window, QStringLiteral("sample.txt"));
  QVERIFY(file_row >= 0);
  select_rows_in_active_panel(&window, {file_row});

  QString warning_text;
  schedule_message_box_capture_and_click(QMessageBox::Ok,
                                         nullptr,
                                         &warning_text,
                                         10000,
                                         10);
  const QStringList before_dirs = temp_dirs_with_prefix(QStringLiteral("7zO_"));
  window.on_edit_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QTRY_VERIFY_WITH_TIMEOUT(window.archive_temp_sessions_.isEmpty(), 20000);
  QCOMPARE(prompt_count, 1);
  QVERIFY(warning_text.contains(QStringLiteral("Cannot update archive"),
                                Qt::CaseInsensitive));
  QVERIFY(warning_text.contains(QStringLiteral("no longer exists"),
                                Qt::CaseInsensitive));
  const QStringList after_dirs = temp_dirs_with_prefix(QStringLiteral("7zO_"));
  QCOMPARE(after_dirs, before_dirs);

  const z7::app::ArchiveSessionToken active_token =
      window.active_panel_controller().archive.current_token;
  QVERIFY(active_token.is_valid());
  QTemporaryDir session_extracted_root;
  QVERIFY2(session_extracted_root.isValid(),
           "failed to create session extraction verification dir");
  const QString session_extracted_text = extract_entry_text_from_session(
      active_token, QStringLiteral("sample.txt"), session_extracted_root.path());
  QCOMPARE(session_extracted_text, QStringLiteral("7zfm behavior test\n"));

  QTemporaryDir extracted_root;
  QVERIFY2(extracted_root.isValid(), "failed to create extraction verification dir");
  const QString extracted_text = extract_entry_text(
      archive_path, QStringLiteral("sample.txt"), extracted_root.path());
  QCOMPARE(extracted_text, QStringLiteral("7zfm behavior test\n"));
}

void FileManagerBehaviorTest::diffActionVisibilityAndExecutionFollowConfiguredProgram() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString left = QDir(root.path()).filePath(QStringLiteral("left"));
    const QString right = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(left));
    QVERIFY(QDir().mkpath(right));

    const QString left_a = QDir(left).filePath(QStringLiteral("a.txt"));
    const QString left_b = QDir(left).filePath(QStringLiteral("b.txt"));
    const QString right_a = QDir(right).filePath(QStringLiteral("a.txt"));
    {
      QFile file(left_a);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("left a");
      file.close();
    }
    {
      QFile file(left_b);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("left b");
      file.close();
    }
    {
      QFile file(right_a);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("right a");
      file.close();
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(left);
    window.refresh_action_states();
    QVERIFY(!window.diff_action_->isVisible());

    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/Diff"),
                      QStringLiteral("\"/usr/local/bin/mydiff\" --cmp"));
    window.refresh_action_states();
    QVERIFY(window.diff_action_->isVisible());
    QVERIFY(!window.diff_action_->isEnabled());

    int launch_count = 0;
    QString last_program;
    QStringList last_args;
    window.external_command_launcher_ =
        [&launch_count, &last_program, &last_args](
            const QString& program,
            const QStringList& args,
            const QString&,
            qint64*) {
          ++launch_count;
          last_program = program;
          last_args = args;
          return true;
        };

    const int row_a = row_by_name(window, QStringLiteral("a.txt"));
    const int row_b = row_by_name(window, QStringLiteral("b.txt"));
    QVERIFY(row_a >= 0);
    QVERIFY(row_b >= 0);
    QVERIFY(window.active_panel_controller().ui.details_view != nullptr);
    QVERIFY(window.active_panel_controller().ui.details_view->selectionModel() != nullptr);
    QItemSelectionModel* selection = window.active_panel_controller().ui.details_view->selectionModel();
    selection->clearSelection();
    const QModelIndex idx_a = window.active_panel_controller().ui.details_view->model()->index(row_a, 0);
    const QModelIndex idx_b = window.active_panel_controller().ui.details_view->model()->index(row_b, 0);
    QVERIFY(idx_a.isValid());
    QVERIFY(idx_b.isValid());
    selection->select(idx_a, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->select(idx_b, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->setCurrentIndex(idx_b, QItemSelectionModel::NoUpdate);
    window.refresh_action_states();
    QVERIFY(window.diff_action_->isEnabled());
    window.on_diff_requested();
    QCOMPARE(launch_count, 1);
    QCOMPARE(last_program, QStringLiteral("/usr/local/bin/mydiff"));
    QCOMPARE(last_args.size(), 3);
    QCOMPARE(last_args.at(0), QStringLiteral("--cmp"));
    QVERIFY(last_args.contains(QFileInfo(left_a).absoluteFilePath()));
    QVERIFY(last_args.contains(QFileInfo(left_b).absoluteFilePath()));

    window.on_two_panels_action_triggered();
    QVERIFY(window.two_panels_visible_);
    window.set_active_panel(0);
    window.set_current_directory(left);
    window.set_active_panel(1);
    window.set_current_directory(right);
    window.set_active_panel(0);

    const int left_row = row_by_name(window, QStringLiteral("a.txt"));
    QVERIFY(left_row >= 0);
    select_rows_in_active_panel(&window, {left_row});
    window.refresh_action_states();
    QVERIFY(window.diff_action_->isEnabled());
    window.on_diff_requested();
    QCOMPARE(launch_count, 2);
    QCOMPARE(last_program, QStringLiteral("/usr/local/bin/mydiff"));
    QCOMPARE(last_args.size(), 3);
    QCOMPARE(last_args.at(0), QStringLiteral("--cmp"));
    QCOMPARE(last_args.at(1), QFileInfo(left_a).absoluteFilePath());
    QCOMPARE(last_args.at(2), QFileInfo(right_a).absoluteFilePath());

    settings.setValue(QStringLiteral("FM/Diff"), QString());
    window.refresh_action_states();
    QVERIFY(!window.diff_action_->isVisible());
  }

// End of archive_commands.cpp
