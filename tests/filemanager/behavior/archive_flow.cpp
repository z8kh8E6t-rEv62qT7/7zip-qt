// tests/filemanager/behavior/archive_flow.cpp
// Role: In-archive flow and navigation behavior cases.

#include "internal.h"

#include "gui_app_controller.h"
#include "gui_task_runner_helpers.h"
#include "gui_task_spec_ipc.h"

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

bool write_text_file(const QString& path, const QByteArray& contents) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(contents) == contents.size();
}

QString create_tree_archive(const QTemporaryDir& root) {
  QDir root_dir(root.path());
  if (!root_dir.mkpath(QStringLiteral("dir/sub"))) {
    return {};
  }
  if (!write_text_file(root_dir.filePath(QStringLiteral("dir/a.txt")),
                       QByteArrayLiteral("alpha")) ||
      !write_text_file(root_dir.filePath(QStringLiteral("dir/report.docx")),
                       QByteArrayLiteral("office")) ||
      !write_text_file(root_dir.filePath(QStringLiteral("dir/sub/b.txt")),
                       QByteArrayLiteral("beta"))) {
    return {};
  }

  const QString archive_path = root_dir.filePath(QStringLiteral("tree.7z"));
  QString error;
  if (!create_archive_via_backend(root.path(),
                                  archive_path,
                                  QStringList{QStringLiteral("dir")},
                                  &error)) {
    qWarning() << "create_tree_archive failed:" << error;
    return {};
  }
  return archive_path;
}

QVector<z7::task_ipc_runtime::TaskIpcExtractPathRemap> archive_export_remaps(
    const z7::task_ipc_runtime::TaskIpcPayload& payload) {
  if (!payload.archive_export.has_value()) {
    return {};
  }
  return payload.archive_export->path_remaps;
}

#ifdef Q_OS_WIN
bool write_zone_identifier_ads(const QString& base_path,
                               const QByteArray& contents) {
  QFile file(base_path + QStringLiteral(":Zone.Identifier"));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(contents) == contents.size();
}

QByteArray read_zone_identifier_ads(const QString& base_path) {
  QFile file(base_path + QStringLiteral(":Zone.Identifier"));
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}
#endif

z7::ui::gui::GuiTaskCompletion run_gui_task_spec_for_test(
    const z7::ui::gui::GuiTaskSpec& spec) {
  z7::ui::gui::GuiTaskCompletion completion;
  bool finished = false;
  z7::ui::gui::GuiAppController controller;
  controller.run_task_spec_async(
      spec,
      QStringLiteral("Copy"),
      {},
      [&completion, &finished](
          const z7::ui::gui::GuiTaskCompletion& result) {
        completion = result;
        finished = true;
      });
  if (!QTest::qWaitFor([&finished]() { return finished; }, 20000)) {
    completion.exit_code = 255;
    completion.summary = QStringLiteral("Timed out waiting for GUI task.");
  }
  return completion;
}

}  // namespace

void FileManagerBehaviorTest::openInsideEntersArchiveView() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QVERIFY(filemanager_behavior_internal::current_runner(window) != nullptr);
    QVERIFY(filemanager_behavior_internal::current_progress_dialog(window) == nullptr ||
            !filemanager_behavior_internal::current_progress_dialog(window)->isVisible());

    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir, QString());
    QCOMPARE(window.active_panel_controller().archive.source_archive,
             QFileInfo(archive_path).absoluteFilePath());
    QVERIFY(row_by_name(window, QStringLiteral("sample.txt")) >= 0);
  }

void FileManagerBehaviorTest::testActionUsesSevenZipBridgeInFilesystemMode() {
    using namespace z7::ui::gui;
    using namespace z7::ui::gui::bridge_internal;

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    reset_bridge_segments_for_test();

    const int row = row_by_name(window, QStringLiteral("sample.7z"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    window.on_test_requested();

    QVERIFY(!window.task_ipc_owner_instance_id_.isEmpty());
    BridgeTaskPayload payload;
    QString read_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kTest,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QCOMPARE(payload.command, BridgeCommandKind::kTest);
    QVERIFY(payload.archive_paths.contains(QFileInfo(archive_path).absoluteFilePath()));
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QVERIFY(filemanager_behavior_internal::current_progress_dialog(window) == nullptr);
  }

void FileManagerBehaviorTest::testActionUsesOperSmartItems() {
    using namespace z7::ui::gui;
    using namespace z7::ui::gui::bridge_internal;

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");
    const QString plain_path =
        QDir(root.path()).filePath(QStringLiteral("plain.txt"));
    {
      QFile file(plain_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("plain");
    }

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();
    window.set_current_directory(root.path());
    const int archive_row = row_by_name(window, QFileInfo(archive_path).fileName());
    const int plain_row = row_by_name(window, QStringLiteral("plain.txt"));
    const int parent_row = row_by_name(window, QStringLiteral(".."));
    QVERIFY(archive_row >= 0);
    QVERIFY(plain_row >= 0);
    QVERIFY(parent_row >= 0);

    QItemSelectionModel* selection =
        window.active_panel_controller().ui.details_view->selectionModel();
    QVERIFY(selection != nullptr);
    select_rows_in_active_panel(&window, {plain_row});

    reset_bridge_segments_for_test();
    window.on_test_requested();

    BridgeTaskPayload payload;
    QString payload_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kTest,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kTest);
    QCOMPARE(payload.archive_paths, QStringList{QFileInfo(plain_path).absoluteFilePath()});

    QStringList expected_all{
        QFileInfo(archive_path).absoluteFilePath(),
        QFileInfo(QDir(root.path()).filePath(QStringLiteral("sample.txt"))).absoluteFilePath(),
        QFileInfo(plain_path).absoluteFilePath(),
    };
    expected_all.sort();

    selection->clearSelection();
    selection->clearCurrentIndex();
    reset_bridge_segments_for_test();
    window.on_test_requested();

    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kTest,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kTest);
    payload.archive_paths.sort();
    QCOMPARE(payload.archive_paths, expected_all);

    select_rows_in_active_panel(&window, {archive_row, plain_row});
    reset_bridge_segments_for_test();
    window.on_test_requested();

    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kTest,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kTest);
    payload.archive_paths.sort();
    QStringList expected_selected{
        QFileInfo(archive_path).absoluteFilePath(),
        QFileInfo(plain_path).absoluteFilePath(),
    };
    expected_selected.sort();
    QCOMPARE(payload.archive_paths, expected_selected);

    select_rows_in_active_panel(&window, {parent_row, plain_row});
    reset_bridge_segments_for_test();
    window.on_test_requested();

    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kTest,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kTest);
    QCOMPARE(payload.archive_paths,
             QStringList{QFileInfo(plain_path).absoluteFilePath()});

    select_rows_in_active_panel(&window, {parent_row});
    reset_bridge_segments_for_test();
    window.on_test_requested();

    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kTest,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kTest);
    payload.archive_paths.sort();
    QCOMPARE(payload.archive_paths, expected_all);
  }

void FileManagerBehaviorTest::compressActionUsesSevenZipBridgeInFilesystemMode() {
    using namespace z7::ui::gui;
    using namespace z7::ui::gui::bridge_internal;

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString plain_file = QDir(root.path()).filePath(QStringLiteral("plain.txt"));
    {
      QFile file(plain_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("plain");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    reset_bridge_segments_for_test();

    const int row = row_by_name(window, QStringLiteral("plain.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    window.on_compress_requested();

    QVERIFY(!window.task_ipc_owner_instance_id_.isEmpty());
    BridgeTaskPayload payload;
    QString read_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kAdd,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QCOMPARE(payload.command, BridgeCommandKind::kAdd);
    QVERIFY(payload.show_dialog);
    QVERIFY(payload.input_paths.contains(QFileInfo(plain_file).absoluteFilePath()));
    QVERIFY(!payload.archive_path.trimmed().isEmpty());
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QVERIFY(filemanager_behavior_internal::current_progress_dialog(window) == nullptr);
  }

void FileManagerBehaviorTest::extractActionUsesSevenZipBridgeInFilesystemMode() {
    using namespace z7::ui::gui;
    using namespace z7::ui::gui::bridge_internal;

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    reset_bridge_segments_for_test();

    const int row = row_by_name(window, QStringLiteral("sample.7z"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    window.on_extract_requested();

    QVERIFY(!window.task_ipc_owner_instance_id_.isEmpty());
    BridgeTaskPayload payload;
    QString read_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kExtract,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QCOMPARE(payload.command, BridgeCommandKind::kExtract);
    QVERIFY(payload.archive_paths.contains(QFileInfo(archive_path).absoluteFilePath()));
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QVERIFY(filemanager_behavior_internal::current_progress_dialog(window) == nullptr);
  }

void FileManagerBehaviorTest::extractActionFollowsExtractOptionSettings() {
    using namespace z7::ui::gui;
    using namespace z7::ui::gui::bridge_internal;

    struct SettingsResetGuard {
      ~SettingsResetGuard() { ::clear_runtime_settings(); }
    } reset_settings;
    clear_runtime_settings();
    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("Options/ElimDupExtract"), false);
    settings.setValue(QStringLiteral("Options/WriteZoneIdExtract"), 2);
    settings.sync();

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    reset_bridge_segments_for_test();

    const int row = row_by_name(window, QStringLiteral("sample.7z"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    window.on_extract_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    BridgeTaskPayload payload;
    QString read_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kExtract,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QCOMPARE(payload.command, BridgeCommandKind::kExtract);
    QVERIFY(payload.show_dialog);
    QCOMPARE(payload.extract_zone_id_mode, QStringLiteral("office"));
    QVERIFY(payload.archive_paths.contains(QFileInfo(archive_path).absoluteFilePath()));
  }

void FileManagerBehaviorTest::extractActionUsesOperatedItemsAndRejectsDirectories() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");
    const QString plain_path =
        QDir(root.path()).filePath(QStringLiteral("plain.txt"));
    {
      QFile file(plain_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("plain");
    }
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("folder")));

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();
    window.set_current_directory(root.path());
    reset_bridge_segments_for_test();

    const int archive_row = row_by_name(window, QFileInfo(archive_path).fileName());
    const int plain_row = row_by_name(window, QStringLiteral("plain.txt"));
    const int folder_row = row_by_name(window, QStringLiteral("folder"));
    const int parent_row = row_by_name(window, QStringLiteral(".."));
    QVERIFY(archive_row >= 0);
    QVERIFY(plain_row >= 0);
    QVERIFY(folder_row >= 0);
    QVERIFY(parent_row >= 0);

    select_rows_in_active_panel(&window, {archive_row, plain_row});
    window.on_extract_requested();

    z7::ui::gui::BridgeTaskPayload payload;
    QString payload_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 z7::ui::gui::BridgeCommandKind::kExtract,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, z7::ui::gui::BridgeCommandKind::kExtract);
    QVERIFY(payload.show_dialog);
    QStringList expected_paths{
        QFileInfo(archive_path).absoluteFilePath(),
        QFileInfo(plain_path).absoluteFilePath(),
    };
    expected_paths.sort();
    payload.archive_paths.sort();
    QCOMPARE(payload.archive_paths, expected_paths);

    reset_bridge_segments_for_test();
    select_rows_in_active_panel(&window, {parent_row, archive_row});
    window.on_extract_requested();

    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 z7::ui::gui::BridgeCommandKind::kExtract,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, z7::ui::gui::BridgeCommandKind::kExtract);
    QCOMPARE(payload.archive_paths,
             QStringList{QFileInfo(archive_path).absoluteFilePath()});

    reset_bridge_segments_for_test();
    QString parent_only_warning;
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           nullptr,
                                           &parent_only_warning,
                                           3000,
                                           10);
    select_rows_in_active_panel(&window, {parent_row});
    window.on_extract_requested();
    QTRY_COMPARE_WITH_TIMEOUT(parent_only_warning, localized_label(3015), 3000);

    z7::ui::gui::BridgeTaskPayload parent_only_payload;
    QString parent_only_error;
    QVERIFY(!read_latest_bridge_payload_for_command(
        window,
        z7::ui::gui::BridgeCommandKind::kExtract,
        &parent_only_payload,
        &parent_only_error));

    reset_bridge_segments_for_test();
    QString warning_text;
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           nullptr,
                                           &warning_text,
                                           3000,
                                           10);
    select_rows_in_active_panel(&window, {archive_row, folder_row});
    window.on_extract_requested();

    QTRY_COMPARE_WITH_TIMEOUT(warning_text, localized_label(3015), 3000);

    z7::ui::gui::BridgeTaskPayload rejected_payload;
    QString rejected_error;
    QVERIFY(!read_latest_bridge_payload_for_command(
        window,
        z7::ui::gui::BridgeCommandKind::kExtract,
        &rejected_payload,
        &rejected_error));
  }

void FileManagerBehaviorTest::extractActionInArchiveViewUsesOperSmartItems() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_tree_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare tree archive");
    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("oper-smart-export"));
    QVERIFY(QDir().mkpath(output_dir));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int row = row_by_name(window, QStringLiteral("dir"));
    QVERIFY(row >= 0);
    QItemSelectionModel* selection =
        window.active_panel_controller().ui.details_view->selectionModel();
    QVERIFY(selection != nullptr);
    selection->clearSelection();
    const QModelIndex index =
        window.active_panel_controller().ui.details_view->model()->index(row, 0);
    QVERIFY(index.isValid());
    selection->setCurrentIndex(index, QItemSelectionModel::NoUpdate);

    schedule_copy_move_dialog_submit(output_dir + QLatin1Char('/'), true, 6000, 10);
    window.on_extract_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    z7::task_ipc_runtime::TaskIpcPayload payload;
    QString read_error;
    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(payload.archive_export.has_value());
    QCOMPARE(payload.archive_export->archive_entry_paths,
             QStringList{QStringLiteral("dir")});
    QCOMPARE(payload.archive_export->output_dir, output_dir);

    window.task_ipc_owner_instance_id_.clear();
    selection->clearSelection();
    selection->clearCurrentIndex();
    reset_bridge_segments_for_test();
    schedule_copy_move_dialog_submit(output_dir + QLatin1Char('/'), true, 6000, 10);
    window.on_extract_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(payload.archive_export.has_value());
    QStringList entries = payload.archive_export->archive_entry_paths;
    entries.sort();
    QStringList expected_entries{QStringLiteral("dir")};
    expected_entries.sort();
    QCOMPARE(entries, expected_entries);
	  }

void FileManagerBehaviorTest::archiveExtractTestAndCrcUseOperSmartEntriesWithParentLinkSelection() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_tree_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare tree archive");
    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("parent-link-export"));
    QVERIFY(QDir().mkpath(output_dir));

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QVERIFY(row_by_name(window, QStringLiteral("..")) >= 0);

    const int dir_row = row_by_name(window, QStringLiteral("dir"));
    QVERIFY(dir_row >= 0);
    select_rows_in_active_panel(&window, {dir_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("dir"));

    const int parent_row = row_by_name(window, QStringLiteral(".."));
    const int file_row = row_by_name(window, QStringLiteral("a.txt"));
    const int subdir_row = row_by_name(window, QStringLiteral("sub"));
    QVERIFY(parent_row >= 0);
    QVERIFY(file_row >= 0);
    QVERIFY(subdir_row >= 0);

    select_rows_in_active_panel(&window, {parent_row, file_row});
    schedule_copy_move_dialog_submit(output_dir + QLatin1Char('/'), true, 6000, 10);
    window.on_extract_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    z7::task_ipc_runtime::TaskIpcPayload payload;
    QString read_error;
    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(payload.archive_export.has_value());
    QCOMPARE(payload.archive_export->archive_entry_paths,
             QStringList{QStringLiteral("dir/a.txt")});
    QCOMPARE(payload.archive_export->output_dir, output_dir);

    window.task_ipc_owner_instance_id_.clear();
    select_rows_in_active_panel(&window, {parent_row});
    schedule_copy_move_dialog_submit(output_dir + QLatin1Char('/'), true, 6000, 10);
    window.on_extract_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(payload.archive_export.has_value());
    QStringList expected_dir_entries{QStringLiteral("dir/a.txt"),
                                     QStringLiteral("dir/report.docx"),
                                     QStringLiteral("dir/sub")};
    expected_dir_entries.sort();
    QStringList actual_dir_entries = payload.archive_export->archive_entry_paths;
    actual_dir_entries.sort();
    QCOMPARE(actual_dir_entries, expected_dir_entries);

    window.task_ipc_owner_instance_id_.clear();
    select_rows_in_active_panel(&window, {parent_row, file_row});
    window.on_test_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kTest,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(payload.test.has_value());
    QCOMPARE(payload.test->archive_inputs,
             QStringList{QStringLiteral("dir/a.txt")});

    window.task_ipc_owner_instance_id_.clear();
    select_rows_in_active_panel(&window, {parent_row});
    window.on_test_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kTest,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(payload.test.has_value());
    actual_dir_entries = payload.test->archive_inputs;
    actual_dir_entries.sort();
    QCOMPARE(actual_dir_entries, expected_dir_entries);

    window.task_ipc_owner_instance_id_.clear();
    select_rows_in_active_panel(&window, {parent_row, file_row});
    window.on_hash_with_method_requested(QStringLiteral("SHA256"));
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kHash,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(payload.hash.has_value());
    QCOMPARE(payload.hash->hash_method, QStringLiteral("SHA256"));
    QCOMPARE(payload.hash->input_paths,
             QStringList{QStringLiteral("dir/a.txt")});

    window.task_ipc_owner_instance_id_.clear();
    select_rows_in_active_panel(&window, {parent_row});
    window.on_hash_with_method_requested(QStringLiteral("SHA256"));
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kHash,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(payload.hash.has_value());
    QCOMPARE(payload.hash->hash_method, QStringLiteral("SHA256"));
    actual_dir_entries = payload.hash->input_paths;
    actual_dir_entries.sort();
    QCOMPARE(actual_dir_entries, expected_dir_entries);
}

void FileManagerBehaviorTest::extractActionUsesSimpleDialogInArchiveView() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    QStringList inputs;
    for (int i = 0; i < 6; ++i) {
      const QString name = QStringLiteral("entry_%1.txt").arg(i + 1);
      const QString path = QDir(root.path()).filePath(name);
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write(QByteArray("line-") + QByteArray::number(i + 1));
      file.close();
      inputs << name;
    }

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("simple_dialog.7z"));
    QString create_error;
    QVERIFY2(create_archive_via_backend(root.path(), archive_path, inputs, &create_error),
             qPrintable(create_error));
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    window.details_mode_action_->trigger();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QList<int> selected_rows;
    for (int i = 0; i < 6; ++i) {
      const int row = row_by_name(window, QStringLiteral("entry_%1.txt").arg(i + 1));
      QVERIFY(row >= 0);
      selected_rows << row;
    }
    QItemSelectionModel* selection = window.active_panel_controller().ui.details_view->selectionModel();
    QVERIFY(selection != nullptr);
    selection->clearSelection();
    std::sort(selected_rows.begin(), selected_rows.end());
    const QModelIndex first_index =
        window.active_panel_controller().ui.details_view->model()->index(selected_rows.front(), 0);
    const QModelIndex last_index =
        window.active_panel_controller().ui.details_view->model()->index(selected_rows.back(), 0);
    QVERIFY(first_index.isValid());
    QVERIFY(last_index.isValid());
    selection->select(QItemSelection(first_index, last_index),
                      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QCOMPARE(selection->selectedRows(0).size(), 6);

    bool seen_copy_dialog = false;
    bool seen_extract_dialog = false;
    QString copy_dialog_title;
    QString copy_dialog_prompt;
    QString initial_destination;
    QString info_text;
    bool has_destination_combo = false;
    bool has_browse_button = false;
    int ok_x = -1;
    int cancel_x = -1;
    auto* timer = new QTimer(QApplication::instance());
    timer->setInterval(10);
    QObject::connect(timer,
                     &QTimer::timeout,
                     [timer,
                      &seen_copy_dialog,
                      &seen_extract_dialog,
                      &copy_dialog_title,
                      &copy_dialog_prompt,
                      &initial_destination,
                      &info_text,
                      &has_destination_combo,
                      &has_browse_button,
                      &ok_x,
                      &cancel_x]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr) {
          continue;
        }
        if (dialog->objectName() == QStringLiteral("copyMoveDialog")) {
          seen_copy_dialog = true;
          copy_dialog_title = dialog->windowTitle();
          if (auto* prompt =
                  dialog->findChild<QLabel*>(QStringLiteral("copyMovePromptLabel"))) {
            copy_dialog_prompt = prompt->text();
          }
          if (auto* combo =
                  dialog->findChild<QComboBox*>(QStringLiteral("copyMoveDestinationCombo"))) {
            has_destination_combo = true;
            initial_destination =
                QDir::fromNativeSeparators(combo->currentText().trimmed());
          }
          has_browse_button =
              dialog->findChild<QPushButton*>(QStringLiteral("copyMoveBrowseButton")) != nullptr;
          if (auto* label = dialog->findChild<QLabel*>(QStringLiteral("copyMoveInfoLabel"))) {
            info_text = label->text();
          }
          if (auto* ok = dialog->findChild<QPushButton*>(QStringLiteral("copyMoveOkButton"))) {
            ok_x = ok->x();
          }
          if (auto* cancel =
                  dialog->findChild<QPushButton*>(QStringLiteral("copyMoveCancelButton"))) {
            cancel_x = cancel->x();
          }
          dialog->reject();
          timer->stop();
          timer->deleteLater();
          return;
        }
        if (dialog->findChild<QComboBox*>(QStringLiteral("extractOutputCombo")) != nullptr) {
          seen_extract_dialog = true;
          dialog->reject();
          timer->stop();
          timer->deleteLater();
          return;
        }
      }
    });
    timer->start();
    QTimer::singleShot(4000, timer, [timer]() {
      if (timer != nullptr) {
        timer->stop();
        timer->deleteLater();
      }
    });

    window.on_extract_requested();

    QVERIFY(seen_copy_dialog);
    QVERIFY(!seen_extract_dialog);
    QCOMPARE(copy_dialog_title, localized_label(6000));
    QCOMPARE(copy_dialog_prompt, localized_label(6002));
    QVERIFY(!initial_destination.isEmpty());
    QVERIFY(initial_destination != QStringLiteral("/"));
    QCOMPARE(initial_destination, QDir(root.path()).absolutePath());
    QVERIFY(has_destination_combo);
    QVERIFY(has_browse_button);
    QVERIFY(ok_x >= 0);
    QVERIFY(cancel_x >= 0);
    QVERIFY(ok_x < cancel_x);
    QVERIFY(!info_text.isEmpty());
    QVERIFY(info_text.contains(localized_label(1032) +
                               QStringLiteral(":")));
    QVERIFY(!info_text.contains(localized_label(1031) +
                                QStringLiteral(":")));
    QVERIFY(!info_text.contains(localized_label(1007) +
                                QStringLiteral(":")));
    QVERIFY(info_text.contains(QFileInfo(archive_path).absoluteFilePath()));
    QVERIFY2(info_text.contains(QStringLiteral("...")), qPrintable(info_text));
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QVERIFY(filemanager_behavior_internal::current_progress_dialog(window) == nullptr);
  }

void FileManagerBehaviorTest::extractActionInArchiveViewUsesSessionWhenArchivePathMissing() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out-session"));
    QVERIFY(QDir().mkpath(output_dir));
    const QString tracker_log_path =
        QDir(root.path()).filePath(QStringLiteral("extract-session-task-ipc-log.json"));
    ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                          prepend_to_path(fake_launcher_dir()));
    ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                         tracker_log_path.toUtf8());
    ScopedEnvVar claim_env(QByteArrayLiteral("Z7_FAKE_TRACKER_CLAIM_TASK_IPC"),
                           QByteArrayLiteral("1"));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    QString warning_text;
    schedule_copy_move_dialog_submit(output_dir + QLatin1Char('/'), true, 6000, 10);
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           nullptr,
                                           &warning_text,
                                           10000,
                                           10);
    QVERIFY(QFile::remove(archive_path));
    QVERIFY(!QFileInfo::exists(archive_path));

    window.on_extract_requested();
    QVERIFY2(warning_text.isEmpty(), qPrintable(warning_text));
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 5000);

    QVERIFY(!window.task_ipc_owner_instance_id_.isEmpty());
    z7::task_ipc_runtime::TaskIpcPayload payload;
    QString read_error;
    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QCOMPARE(payload.command,
             z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport);
    QVERIFY(!payload.open.has_value());
    QVERIFY(!payload.extract.has_value());
    QVERIFY(payload.archive_export.has_value());
    QCOMPARE(payload.archive_export->root_archive_path,
             QFileInfo(archive_path).absoluteFilePath());
    QCOMPARE(payload.archive_export->nested_archive_entries, QStringList{});
    QCOMPARE(payload.archive_export->output_dir, output_dir);
    QCOMPARE(payload.archive_export->archive_entry_paths,
             QStringList{QStringLiteral("sample.txt")});
    QCOMPARE(payload.archive_export->overwrite_mode, QStringLiteral("ask"));
    QCOMPARE(payload.archive_export->path_mode, QStringLiteral("full"));
    QVERIFY(!payload.archive_export->restore_file_security);
    QCOMPARE(payload.archive_export->zone_id_mode, QStringLiteral("none"));
    QVERIFY(!payload.refresh_after_finish);

    QJsonObject tracker_log;
    QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                              !tracker_log.isEmpty()),
                             5000);
    const QJsonObject task_ipc_log =
        tracker_log.value(QStringLiteral("task_ipc")).toObject();
    QVERIFY(task_ipc_log.value(QStringLiteral("claimed")).toBool());
    QVERIFY(task_ipc_log.value(QStringLiteral("completed")).toBool());
    QCOMPARE(task_ipc_log.value(QStringLiteral("payload"))
                 .toObject()
                 .value(QStringLiteral("command"))
                 .toString(),
             QStringLiteral("archive_export"));
}

void FileManagerBehaviorTest::extractActionInArchiveViewUsesRuntimeOverwritePrompt() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(output_dir));
    const QString existing_path = QDir(output_dir).filePath(QStringLiteral("sample.txt"));
    {
      QFile existing(existing_path);
      QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
      existing.write("existing-content");
    }
    const QString tracker_log_path =
        QDir(root.path()).filePath(QStringLiteral("extract-overwrite-task-ipc-log.json"));
    ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                          prepend_to_path(fake_launcher_dir()));
    ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                         tracker_log_path.toUtf8());
    ScopedEnvVar claim_env(QByteArrayLiteral("Z7_FAKE_TRACKER_CLAIM_TASK_IPC"),
                           QByteArrayLiteral("1"));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    QString warning_text;
    schedule_copy_move_dialog_submit(output_dir + QLatin1Char('/'), true, 6000, 10);
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           nullptr,
                                           &warning_text,
                                           10000,
                                           10);

    window.on_extract_requested();
    QVERIFY2(warning_text.isEmpty(), qPrintable(warning_text));
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 5000);

    z7::task_ipc_runtime::TaskIpcPayload payload;
    QString read_error;
    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(!payload.open.has_value());
    QVERIFY(!payload.extract.has_value());
    QVERIFY(payload.archive_export.has_value());
    QCOMPARE(payload.archive_export->root_archive_path,
             QFileInfo(archive_path).absoluteFilePath());
    QCOMPARE(payload.archive_export->output_dir, output_dir);
    QCOMPARE(payload.archive_export->archive_entry_paths,
             QStringList{QStringLiteral("sample.txt")});
    QCOMPARE(payload.archive_export->overwrite_mode, QStringLiteral("ask"));

    QJsonObject tracker_log;
    QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                              !tracker_log.isEmpty()),
                             5000);
    const QJsonObject task_ipc_log =
        tracker_log.value(QStringLiteral("task_ipc")).toObject();
    QVERIFY(task_ipc_log.value(QStringLiteral("claimed")).toBool());
    QVERIFY(task_ipc_log.value(QStringLiteral("completed")).toBool());

    QFile existing(existing_path);
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("existing-content"));
  }

void FileManagerBehaviorTest::copyToInArchiveViewUsesArchiveExportRoute() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString output_dir =
        QDir(root.path()).filePath(QStringLiteral("copy-to-out"));
    const QString raw_output_dir =
        QDir(root.path()).filePath(QStringLiteral("copy-to-out. ")) +
        QLatin1Char('/');
    const QString tracker_log_path =
        QDir(root.path()).filePath(QStringLiteral("copy-to-archive-export-log.json"));
    ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                          prepend_to_path(fake_launcher_dir()));
    ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                         tracker_log_path.toUtf8());
    ScopedEnvVar claim_env(QByteArrayLiteral("Z7_FAKE_TRACKER_CLAIM_TASK_IPC"),
                           QByteArrayLiteral("1"));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    schedule_copy_move_dialog_submit(raw_output_dir, true, 6000, 10);
    window.on_copy_to_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 5000);

    z7::task_ipc_runtime::TaskIpcPayload payload;
    QString read_error;
    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QCOMPARE(payload.command,
             z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport);
    QVERIFY(!payload.open.has_value());
    QVERIFY(!payload.extract.has_value());
    QVERIFY(payload.archive_export.has_value());
    QVERIFY(QFileInfo(output_dir).isDir());
    QVERIFY(!QFileInfo::exists(raw_output_dir));
    QCOMPARE(payload.archive_export->root_archive_path,
             QFileInfo(archive_path).absoluteFilePath());
    QCOMPARE(payload.archive_export->nested_archive_entries, QStringList{});
    QCOMPARE(payload.archive_export->output_dir, output_dir);
    QCOMPARE(payload.archive_export->archive_entry_paths,
             QStringList{QStringLiteral("sample.txt")});
    QCOMPARE(payload.archive_export->overwrite_mode, QStringLiteral("ask"));
    QCOMPARE(payload.archive_export->path_mode, QStringLiteral("full"));
    QVERIFY(!payload.archive_export->restore_file_security);
    QCOMPARE(payload.archive_export->zone_id_mode, QStringLiteral("none"));
    {
      z7::platform::qt::PortableSettings settings;
      const QStringList history =
          settings.value(QStringLiteral("FM/CopyHistory")).toStringList();
      QVERIFY(!history.isEmpty());
      QCOMPARE(history.front(), output_dir + QLatin1Char('/'));
      QVERIFY(!history.contains(raw_output_dir));
    }

    QJsonObject tracker_log;
    QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                              !tracker_log.isEmpty()),
                             5000);
    const QJsonObject task_ipc_log =
        tracker_log.value(QStringLiteral("task_ipc")).toObject();
    QVERIFY(task_ipc_log.value(QStringLiteral("claimed")).toBool());
    QCOMPARE(task_ipc_log.value(QStringLiteral("payload"))
                 .toObject()
                 .value(QStringLiteral("command"))
                 .toString(),
             QStringLiteral("archive_export"));
  }

void FileManagerBehaviorTest::archiveExportStoresFinalCorrectedCopyHistory() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString output_dir =
        QDir(root.path()).filePath(QStringLiteral("archive-history-out"));
    const QString raw_output_dir =
        QDir(root.path()).filePath(QStringLiteral("archive-history-out. ")) +
        QLatin1Char('/');
    const QString tracker_log_path =
        QDir(root.path()).filePath(QStringLiteral("archive-history-log.json"));
    ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                          prepend_to_path(fake_launcher_dir()));
    ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                         tracker_log_path.toUtf8());
    ScopedEnvVar claim_env(QByteArrayLiteral("Z7_FAKE_TRACKER_CLAIM_TASK_IPC"),
                           QByteArrayLiteral("1"));

    const QStringList seeded_history{
        output_dir,
        QDir(root.path()).filePath(QStringLiteral("older-history"))};
    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/CopyHistory"), seeded_history);
      settings.sync();
    }

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);
    QVERIFY(window.in_archive_view());

    const int row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    schedule_copy_move_dialog_submit(raw_output_dir, true, 6000, 10);
    window.on_copy_to_requested();
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_runner(window) == nullptr,
        20000);
    QTRY_VERIFY_WITH_TIMEOUT(
        filemanager_behavior_internal::current_progress_dialog(window) == nullptr,
        5000);

    z7::task_ipc_runtime::TaskIpcPayload payload;
    QString read_error;
    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(payload.archive_export.has_value());
    QCOMPARE(payload.archive_export->output_dir, output_dir);

    z7::platform::qt::PortableSettings settings;
    const QStringList history =
        settings.value(QStringLiteral("FM/CopyHistory")).toStringList();
    QCOMPARE(history.value(0), output_dir + QLatin1Char('/'));
    QCOMPARE(history.value(1), seeded_history.value(1));
    QVERIFY(!history.contains(output_dir));
    QVERIFY(!history.contains(raw_output_dir));
  }

void FileManagerBehaviorTest::archiveExportFlatViewUsesNoPathsRoute() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_tree_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare tree archive");

    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("flat-out"));
    QVERIFY(QDir().mkpath(output_dir));
    const QString tracker_log_path =
        QDir(root.path()).filePath(QStringLiteral("flat-archive-export-log.json"));
    ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                          prepend_to_path(fake_launcher_dir()));
    ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                         tracker_log_path.toUtf8());
    ScopedEnvVar claim_env(QByteArrayLiteral("Z7_FAKE_TRACKER_CLAIM_TASK_IPC"),
                           QByteArrayLiteral("1"));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    window.on_flat_view_action_triggered();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    const int row = row_by_name(window, QStringLiteral("dir/a.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    schedule_copy_move_dialog_submit(output_dir + QLatin1Char('/'), true, 6000, 10);
    window.on_copy_to_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    z7::task_ipc_runtime::TaskIpcPayload payload;
    QString read_error;
    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(payload.archive_export.has_value());
    QCOMPARE(payload.archive_export->output_dir, output_dir);
    QCOMPARE(payload.archive_export->archive_entry_paths,
             QStringList{QStringLiteral("dir/a.txt")});
    QCOMPARE(payload.archive_export->path_mode, QStringLiteral("no"));
    QVERIFY(payload.archive_export->path_remaps.isEmpty());
  }

void FileManagerBehaviorTest::archiveExportSubdirectoryUsesCurrentPathRemap() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_tree_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare tree archive");

    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("subdir-out"));
    QVERIFY(QDir().mkpath(output_dir));
    const QString tracker_log_path =
        QDir(root.path()).filePath(QStringLiteral("subdir-archive-export-log.json"));
    ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                          prepend_to_path(fake_launcher_dir()));
    ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                         tracker_log_path.toUtf8());
    ScopedEnvVar claim_env(QByteArrayLiteral("Z7_FAKE_TRACKER_CLAIM_TASK_IPC"),
                           QByteArrayLiteral("1"));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    const int dir_row = row_by_name(window, QStringLiteral("dir"));
    QVERIFY(dir_row >= 0);
    select_rows_in_active_panel(&window, {dir_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("dir"));

    const int file_row = row_by_name(window, QStringLiteral("a.txt"));
    QVERIFY(file_row >= 0);
    select_rows_in_active_panel(&window, {file_row});

    schedule_copy_move_dialog_submit(output_dir + QLatin1Char('/'), true, 6000, 10);
    window.on_extract_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    z7::task_ipc_runtime::TaskIpcPayload payload;
    QString read_error;
    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QVERIFY(payload.archive_export.has_value());
    QCOMPARE(payload.archive_export->output_dir, output_dir);
    QCOMPARE(payload.archive_export->archive_entry_paths,
             QStringList{QStringLiteral("dir/a.txt")});
    QCOMPARE(payload.archive_export->path_mode, QStringLiteral("full"));
    const auto remaps = archive_export_remaps(payload);
    QCOMPARE(static_cast<int>(remaps.size()), 1);
    QCOMPARE(remaps.front().match_kind,
             z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind::kArchivePrefix);
    QCOMPARE(remaps.front().source_path, QStringLiteral("dir"));
    QCOMPARE(remaps.front().destination_path, output_dir);
  }

void FileManagerBehaviorTest::archiveExportSingleTargetsUseDirectoryDestinations() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_tree_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare tree archive");

    const QString tracker_log_path =
        QDir(root.path()).filePath(QStringLiteral("single-target-archive-export-log.json"));
    ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                          prepend_to_path(fake_launcher_dir()));
    ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                         tracker_log_path.toUtf8());
    ScopedEnvVar claim_env(QByteArrayLiteral("Z7_FAKE_TRACKER_CLAIM_TASK_IPC"),
                           QByteArrayLiteral("1"));

    const QString file_target_dir =
        QDir(root.path()).filePath(QStringLiteral("single-file-target"));
    {
      z7::ui::filemanager::MainWindow window;
      window.open_archive_inside(archive_path);
      QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
      const int dir_row = row_by_name(window, QStringLiteral("dir"));
      QVERIFY(dir_row >= 0);
      select_rows_in_active_panel(&window, {dir_row});
      window.on_open_requested();
      QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
      const int file_row = row_by_name(window, QStringLiteral("a.txt"));
      QVERIFY(file_row >= 0);
      select_rows_in_active_panel(&window, {file_row});

      schedule_copy_move_dialog_submit(file_target_dir, true, 6000, 10);
      window.on_copy_to_requested();
      QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

      z7::task_ipc_runtime::TaskIpcPayload payload;
      QString read_error;
      QVERIFY2(read_latest_task_ipc_payload_for_command(
                   window,
                   z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                   &payload,
                   &read_error),
               qPrintable(read_error));
      QVERIFY(payload.archive_export.has_value());
      QCOMPARE(payload.archive_export->output_dir, file_target_dir);
      QCOMPARE(payload.archive_export->archive_entry_paths,
               QStringList{QStringLiteral("dir/a.txt")});
      QCOMPARE(payload.archive_export->path_mode, QStringLiteral("full"));
      const auto remaps = archive_export_remaps(payload);
      QCOMPARE(static_cast<int>(remaps.size()), 1);
      QCOMPARE(remaps.front().match_kind,
               z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind::kArchivePrefix);
      QCOMPARE(remaps.front().source_path, QStringLiteral("dir"));
      QCOMPARE(remaps.front().destination_path, file_target_dir);
    }

    const QString dir_target_dir =
        QDir(root.path()).filePath(QStringLiteral("single-dir-target"));
    {
      z7::ui::filemanager::MainWindow window;
      window.open_archive_inside(archive_path);
      QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
      const int dir_row = row_by_name(window, QStringLiteral("dir"));
      QVERIFY(dir_row >= 0);
      select_rows_in_active_panel(&window, {dir_row});

      schedule_copy_move_dialog_submit(dir_target_dir, true, 6000, 10);
      window.on_extract_requested();
      QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

      z7::task_ipc_runtime::TaskIpcPayload payload;
      QString read_error;
      QVERIFY2(read_latest_task_ipc_payload_for_command(
                   window,
                   z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                   &payload,
                   &read_error),
               qPrintable(read_error));
      QVERIFY(payload.archive_export.has_value());
      QCOMPARE(payload.archive_export->output_dir, dir_target_dir);
      QCOMPARE(payload.archive_export->archive_entry_paths,
               QStringList{QStringLiteral("dir")});
      QCOMPARE(payload.archive_export->path_mode, QStringLiteral("full"));
      const auto remaps = archive_export_remaps(payload);
      QCOMPARE(static_cast<int>(remaps.size()), 0);
    }
  }

void FileManagerBehaviorTest::archiveExportZoneIdModeFollowsWriteZoneSetting() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString tracker_log_path =
        QDir(root.path()).filePath(QStringLiteral("zone-archive-export-log.json"));
    ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                          prepend_to_path(fake_launcher_dir()));
    ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                         tracker_log_path.toUtf8());
    ScopedEnvVar claim_env(QByteArrayLiteral("Z7_FAKE_TRACKER_CLAIM_TASK_IPC"),
                           QByteArrayLiteral("1"));

    struct ZoneCase {
      std::optional<int> setting;
      QString expected;
    };
    const QVector<ZoneCase> cases{
        {std::nullopt, QStringLiteral("none")},
        {-1, QStringLiteral("none")},
        {0, QStringLiteral("none")},
        {1, QStringLiteral("all")},
        {2, QStringLiteral("office")},
        {99, QStringLiteral("none")},
    };

    for (int i = 0; i < cases.size(); ++i) {
      reset_task_ipc_segments_for_test();
      z7::platform::qt::PortableSettings settings;
      settings.clear();
      if (cases.at(i).setting.has_value()) {
        settings.setValue(QStringLiteral("Options/WriteZoneIdExtract"),
                          *cases.at(i).setting);
      }
      settings.sync();

      z7::ui::filemanager::MainWindow window;
      window.open_archive_inside(archive_path);
      QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
      const int row = row_by_name(window, QStringLiteral("sample.txt"));
      QVERIFY(row >= 0);
      select_rows_in_active_panel(&window, {row});

      const QString output_dir =
          QDir(root.path()).filePath(QStringLiteral("zone-out-%1").arg(i));
      QVERIFY(QDir().mkpath(output_dir));
      schedule_copy_move_dialog_submit(output_dir + QLatin1Char('/'), true, 6000, 10);
      window.on_extract_requested();
      QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

      z7::task_ipc_runtime::TaskIpcPayload payload;
      QString read_error;
      QVERIFY2(read_latest_task_ipc_payload_for_command(
                   window,
                   z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport,
                   &payload,
                   &read_error),
               qPrintable(read_error));
      QVERIFY(payload.archive_export.has_value());
      QCOMPARE(payload.archive_export->zone_id_mode, cases.at(i).expected);
    }
  }

void FileManagerBehaviorTest::archiveExportTaskSpecPreservesPayloadFields() {
    z7::task_ipc_runtime::TaskIpcPayload payload;
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport;
    payload.archive_export =
        z7::task_ipc_runtime::TaskIpcArchiveExportPayload{};
    payload.archive_export->root_archive_path =
        QStringLiteral("/tmp/root.7z");
    payload.archive_export->root_archive_type = QStringLiteral("7z");
    payload.archive_export->nested_archive_entries =
        QStringList{QStringLiteral("nested/inner.zip")};
    payload.archive_export->archive_entry_paths =
        QStringList{QStringLiteral("dir/a.txt"), QStringLiteral("dir/b.txt")};
    payload.archive_export->output_dir = QStringLiteral("/tmp/out");
    payload.archive_export->overwrite_mode = QStringLiteral("rename_existing");
    payload.archive_export->path_mode = QStringLiteral("full");
    payload.archive_export->eliminate_root_duplication = true;
    payload.archive_export->restore_file_security = true;
    payload.archive_export->zone_id_mode = QStringLiteral("office");
    payload.archive_export->password = QStringLiteral("secret");
    z7::task_ipc_runtime::TaskIpcExtractPathRemap remap;
    remap.match_kind =
        z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind::kArchivePrefix;
    remap.source_path = QStringLiteral("dir");
    remap.destination_path = QStringLiteral("/tmp/out");
    payload.archive_export->path_remaps.push_back(remap);

    QString error;
    const std::optional<z7::ui::gui::GuiTaskSpec> spec =
        z7::ui::gui::build_task_spec_from_task_ipc_payload(payload, &error);
    QVERIFY2(spec.has_value(), qPrintable(error));
    QVERIFY(std::holds_alternative<z7::ui::gui::ArchiveExportTaskSpec>(*spec));
    const auto& export_spec =
        std::get<z7::ui::gui::ArchiveExportTaskSpec>(*spec);
    QCOMPARE(QString::fromStdString(export_spec.root_archive_path),
             QStringLiteral("/tmp/root.7z"));
    QCOMPARE(QString::fromStdString(export_spec.root_archive_type),
             QStringLiteral("7z"));
    QCOMPARE(QString::fromStdString(export_spec.output_dir),
             QStringLiteral("/tmp/out"));
    QCOMPARE(QString::fromStdString(export_spec.overwrite_mode),
             QStringLiteral("rename_existing"));
    QCOMPARE(QString::fromStdString(export_spec.path_mode),
             QStringLiteral("full"));
    QVERIFY(export_spec.eliminate_root_duplication);
    QVERIFY(export_spec.restore_file_security);
    QCOMPARE(QString::fromStdString(export_spec.zone_id_mode),
             QStringLiteral("office"));
    QCOMPARE(QString::fromStdString(export_spec.password),
             QStringLiteral("secret"));
    QCOMPARE(static_cast<int>(export_spec.nested_archive_entries.size()), 1);
    QCOMPARE(QString::fromStdString(export_spec.nested_archive_entries.front()),
             QStringLiteral("nested/inner.zip"));
    QCOMPARE(static_cast<int>(export_spec.archive_entry_paths.size()), 2);
    QCOMPARE(QString::fromStdString(export_spec.archive_entry_paths.at(0)),
             QStringLiteral("dir/a.txt"));
    QCOMPARE(QString::fromStdString(export_spec.archive_entry_paths.at(1)),
             QStringLiteral("dir/b.txt"));
    QCOMPARE(static_cast<int>(export_spec.path_remaps.size()), 1);
    QCOMPARE(export_spec.path_remaps.front().match_kind,
             z7::ui::gui::ExtractPathRemapMatchKind::kArchivePrefix);
    QCOMPARE(QString::fromStdString(export_spec.path_remaps.front().source_path),
             QStringLiteral("dir"));
    QCOMPARE(QString::fromStdString(
                 export_spec.path_remaps.front().destination_path),
             QStringLiteral("/tmp/out"));
  }

void FileManagerBehaviorTest::extractTaskSpecPreservesPayloadExtractOptions() {
    z7::task_ipc_runtime::TaskIpcPayload payload;
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
    payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
    payload.extract->archive_inputs = {QStringLiteral("/tmp/root.7z")};
    payload.extract->output_dir = QStringLiteral("/tmp/out");
    payload.extract->overwrite_switch = QStringLiteral("-aot");
    payload.extract->archive_type = QStringLiteral("7z");
    payload.extract->eliminate_root_duplication = true;
    payload.extract->restore_file_security = true;
    payload.extract->zone_id_mode = QStringLiteral("office");
    payload.extract->password = QStringLiteral("secret");
    z7::task_ipc_runtime::TaskIpcExtractPathRemap remap;
    remap.match_kind =
        z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind::kArchivePrefix;
    remap.source_path = QStringLiteral("dir");
    remap.destination_path = QStringLiteral("/tmp/out");
    payload.extract->path_remaps.push_back(remap);

    QString error;
    const std::optional<z7::ui::gui::GuiTaskSpec> spec =
        z7::ui::gui::build_task_spec_from_task_ipc_payload(payload, &error);
    QVERIFY2(spec.has_value(), qPrintable(error));
    QVERIFY(std::holds_alternative<z7::ui::gui::ExtractTaskSpec>(*spec));
    const auto& extract_spec =
        std::get<z7::ui::gui::ExtractTaskSpec>(*spec);
    QCOMPARE(QString::fromStdString(extract_spec.output_dir),
             QStringLiteral("/tmp/out"));
    QCOMPARE(QString::fromStdString(extract_spec.overwrite_switch),
             QStringLiteral("-aot"));
    QCOMPARE(QString::fromStdString(extract_spec.archive_type),
             QStringLiteral("7z"));
    QVERIFY(extract_spec.eliminate_root_duplication);
    QVERIFY(extract_spec.restore_file_security);
    QCOMPARE(QString::fromStdString(extract_spec.zone_id_mode),
             QStringLiteral("office"));
    QCOMPARE(QString::fromStdString(extract_spec.password),
             QStringLiteral("secret"));
    QCOMPARE(static_cast<int>(extract_spec.archive_inputs.size()), 1);
    QCOMPARE(QString::fromStdString(extract_spec.archive_inputs.front()),
             QStringLiteral("/tmp/root.7z"));
    QCOMPARE(static_cast<int>(extract_spec.path_remaps.size()), 1);
    QCOMPARE(extract_spec.path_remaps.front().match_kind,
             z7::ui::gui::ExtractPathRemapMatchKind::kArchivePrefix);

    z7::ui::gui::GuiTaskRunResult run_result;
    z7::app::ArchiveRequest request;
    QVERIFY(z7::ui::gui::build_archive_request(*spec, &run_result, &request));
    QVERIFY(std::holds_alternative<z7::app::ExtractRequest>(request.payload));
    const auto& extract_request =
        std::get<z7::app::ExtractRequest>(request.payload);
    QVERIFY(extract_request.eliminate_root_duplication);
    QCOMPARE(extract_request.zone_id_mode,
             z7::app::ExtractZoneIdMode::kOffice);
    QCOMPARE(extract_request.restore_file_security,
             z7::ui::runtime_support::is_platform_supported(
                 z7::ui::runtime_support::PlatformSupport::kWindowsOnly));
    QCOMPARE(static_cast<int>(extract_request.path_remaps.size()), 1);
  }

void FileManagerBehaviorTest::archiveExportRunnerExtractsMultiAndNestedMembers() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString first_path =
        QDir(root.path()).filePath(QStringLiteral("first.txt"));
    const QString second_path =
        QDir(root.path()).filePath(QStringLiteral("second.txt"));
    {
      QFile first(first_path);
      QVERIFY(first.open(QIODevice::WriteOnly | QIODevice::Truncate));
      first.write("first");
      QFile second(second_path);
      QVERIFY(second.open(QIODevice::WriteOnly | QIODevice::Truncate));
      second.write("second");
    }

    const QString multi_archive =
        QDir(root.path()).filePath(QStringLiteral("multi.7z"));
    QString create_error;
    QVERIFY2(create_archive_via_backend(
                 root.path(),
                 multi_archive,
                 QStringList{QStringLiteral("first.txt"),
                             QStringLiteral("second.txt")},
                 &create_error),
             qPrintable(create_error));

    const QString multi_output =
        QDir(root.path()).filePath(QStringLiteral("multi-out"));
    QVERIFY(QDir().mkpath(multi_output));
    z7::ui::gui::ArchiveExportTaskSpec multi_spec;
    multi_spec.root_archive_path = to_native_path_string(multi_archive);
    multi_spec.archive_entry_paths = {std::string("first.txt"),
                                      std::string("second.txt")};
    multi_spec.output_dir = to_native_path_string(multi_output);
    const z7::ui::gui::GuiTaskCompletion multi_completion =
        run_gui_task_spec_for_test(z7::ui::gui::GuiTaskSpec{multi_spec});
    QVERIFY2(multi_completion.exit_code == 0,
             qPrintable(multi_completion.summary));
    QVERIFY(QFileInfo::exists(
        QDir(multi_output).filePath(QStringLiteral("first.txt"))));
    QVERIFY(QFileInfo::exists(
        QDir(multi_output).filePath(QStringLiteral("second.txt"))));

    const QString outer_archive = create_archive_with_embedded_archive(root);
    QVERIFY2(!outer_archive.isEmpty(), "failed to prepare embedded archive");
    const QString nested_output =
        QDir(root.path()).filePath(QStringLiteral("nested-out"));
    QVERIFY(QDir().mkpath(nested_output));
    z7::ui::gui::ArchiveExportTaskSpec nested_spec;
    nested_spec.root_archive_path = to_native_path_string(outer_archive);
    nested_spec.nested_archive_entries = {std::string("child.7z")};
    nested_spec.archive_entry_paths = {std::string("child.txt")};
    nested_spec.output_dir = to_native_path_string(nested_output);
    const z7::ui::gui::GuiTaskCompletion nested_completion =
        run_gui_task_spec_for_test(z7::ui::gui::GuiTaskSpec{nested_spec});
    QVERIFY2(nested_completion.exit_code == 0,
             qPrintable(nested_completion.summary));
    QVERIFY(QFileInfo::exists(
        QDir(nested_output).filePath(QStringLiteral("child.txt"))));
  }

void FileManagerBehaviorTest::archiveExportRunnerHonorsCopyToPathModesAndRemaps() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_tree_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare tree archive");

    const QString flat_output =
        QDir(root.path()).filePath(QStringLiteral("runner-flat"));
    QVERIFY(QDir().mkpath(flat_output));
    z7::ui::gui::ArchiveExportTaskSpec flat_spec;
    flat_spec.root_archive_path = to_native_path_string(archive_path);
    flat_spec.archive_entry_paths = {std::string("dir/a.txt")};
    flat_spec.output_dir = to_native_path_string(flat_output);
    flat_spec.path_mode = "no";
    const z7::ui::gui::GuiTaskCompletion flat_completion =
        run_gui_task_spec_for_test(z7::ui::gui::GuiTaskSpec{flat_spec});
    QVERIFY2(flat_completion.exit_code == 0,
             qPrintable(flat_completion.summary));
    QVERIFY(QFileInfo::exists(
        QDir(flat_output).filePath(QStringLiteral("a.txt"))));
    QVERIFY(!QFileInfo::exists(
        QDir(flat_output).filePath(QStringLiteral("dir/a.txt"))));

    const QString current_dir_output =
        QDir(root.path()).filePath(QStringLiteral("runner-current-dir"));
    z7::ui::gui::ArchiveExportTaskSpec current_dir_spec;
    current_dir_spec.root_archive_path = to_native_path_string(archive_path);
    current_dir_spec.archive_entry_paths = {std::string("dir/a.txt")};
    current_dir_spec.output_dir = to_native_path_string(current_dir_output);
    current_dir_spec.path_remaps.push_back(
        z7::ui::gui::ExtractPathRemap{
            z7::ui::gui::ExtractPathRemapMatchKind::kArchivePrefix,
            std::string("dir"),
            to_native_path_string(current_dir_output)});
    const z7::ui::gui::GuiTaskCompletion current_dir_completion =
        run_gui_task_spec_for_test(
            z7::ui::gui::GuiTaskSpec{current_dir_spec});
    QVERIFY2(current_dir_completion.exit_code == 0,
             qPrintable(current_dir_completion.summary));
    QFile current_dir_file(
        QDir(current_dir_output).filePath(QStringLiteral("a.txt")));
    QVERIFY(current_dir_file.open(QIODevice::ReadOnly));
    QCOMPARE(current_dir_file.readAll(), QByteArrayLiteral("alpha"));
    QVERIFY(!QFileInfo::exists(
        QDir(current_dir_output).filePath(QStringLiteral("dir/a.txt"))));

    const QString single_dir_output =
        QDir(root.path()).filePath(QStringLiteral("runner-single-dir-target"));
    z7::ui::gui::ArchiveExportTaskSpec single_dir_spec;
    single_dir_spec.root_archive_path = to_native_path_string(archive_path);
    single_dir_spec.archive_entry_paths = {std::string("dir")};
    single_dir_spec.output_dir = to_native_path_string(single_dir_output);
    const z7::ui::gui::GuiTaskCompletion single_dir_completion =
        run_gui_task_spec_for_test(
            z7::ui::gui::GuiTaskSpec{single_dir_spec});
    QVERIFY2(single_dir_completion.exit_code == 0,
             qPrintable(single_dir_completion.summary));
    QFile single_dir_file(
        QDir(single_dir_output).filePath(QStringLiteral("dir/a.txt")));
    QVERIFY(single_dir_file.open(QIODevice::ReadOnly));
    QCOMPARE(single_dir_file.readAll(), QByteArrayLiteral("alpha"));
    QVERIFY(QFileInfo::exists(
        QDir(single_dir_output).filePath(QStringLiteral("dir/sub/b.txt"))));
  }

void FileManagerBehaviorTest::archiveExportRunnerPropagatesZoneIdWhenSupported() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_tree_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare tree archive");

#ifdef Q_OS_WIN
    const QByteArray zone_data =
        QByteArrayLiteral("[ZoneTransfer]\r\nZoneId=3\r\n");
    QVERIFY(write_zone_identifier_ads(archive_path, zone_data));
#endif

    const QString office_output =
        QDir(root.path()).filePath(QStringLiteral("zone-office"));
    QVERIFY(QDir().mkpath(office_output));
    z7::ui::gui::ArchiveExportTaskSpec office_spec;
    office_spec.root_archive_path = to_native_path_string(archive_path);
    office_spec.archive_entry_paths = {std::string("dir/report.docx")};
    office_spec.output_dir = to_native_path_string(office_output);
    office_spec.zone_id_mode = "office";
    const z7::ui::gui::GuiTaskCompletion office_completion =
        run_gui_task_spec_for_test(z7::ui::gui::GuiTaskSpec{office_spec});
    QVERIFY2(office_completion.exit_code == 0,
             qPrintable(office_completion.summary));
    const QString office_file =
        QDir(office_output).filePath(QStringLiteral("dir/report.docx"));
    QVERIFY(QFileInfo::exists(office_file));

    const QString all_output =
        QDir(root.path()).filePath(QStringLiteral("zone-all"));
    QVERIFY(QDir().mkpath(all_output));
    z7::ui::gui::ArchiveExportTaskSpec all_spec;
    all_spec.root_archive_path = to_native_path_string(archive_path);
    all_spec.archive_entry_paths = {std::string("dir/a.txt")};
    all_spec.output_dir = to_native_path_string(all_output);
    all_spec.zone_id_mode = "all";
    const z7::ui::gui::GuiTaskCompletion all_completion =
        run_gui_task_spec_for_test(z7::ui::gui::GuiTaskSpec{all_spec});
    QVERIFY2(all_completion.exit_code == 0,
             qPrintable(all_completion.summary));
    const QString all_file =
        QDir(all_output).filePath(QStringLiteral("dir/a.txt"));
    QVERIFY(QFileInfo::exists(all_file));

#ifdef Q_OS_WIN
    QCOMPARE(read_zone_identifier_ads(office_file), zone_data);
    QCOMPARE(read_zone_identifier_ads(all_file), zone_data);
#endif
  }

void FileManagerBehaviorTest::extractActionPassesMultipleArchivesAsArchivesNotEntries() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_a = create_sample_archive(root);
    QVERIFY2(!archive_a.isEmpty(), "failed to prepare first sample archive");

    const QString file_b = QDir(root.path()).filePath(QStringLiteral("other.txt"));
    {
      QFile file(file_b);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("other");
    }
    const QString archive_b = QDir(root.path()).filePath(QStringLiteral("other.7z"));
    QString create_error;
    QVERIFY(create_archive_via_backend(root.path(),
                                       archive_b,
                                       QStringList{QStringLiteral("other.txt")},
                                       &create_error));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.display_settings_.alternative_selection_mode = true;
    window.apply_runtime_settings();
    window.active_panel_controller().ui.details_view->setSelectionMode(QAbstractItemView::ExtendedSelection);

    const int row_a = row_by_name(window, QFileInfo(archive_a).fileName());
    const int row_b = row_by_name(window, QFileInfo(archive_b).fileName());
    QVERIFY(row_a >= 0);
    QVERIFY(row_b >= 0);
    QItemSelectionModel* selection = window.active_panel_controller().ui.details_view->selectionModel();
    QVERIFY(selection != nullptr);
    selection->clearSelection();
    const QModelIndex idx_a = window.active_panel_controller().ui.details_view->model()->index(row_a, 0);
    const QModelIndex idx_b = window.active_panel_controller().ui.details_view->model()->index(row_b, 0);
    QVERIFY(idx_a.isValid());
    QVERIFY(idx_b.isValid());
    selection->select(idx_a, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->select(idx_b, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->setCurrentIndex(idx_b, QItemSelectionModel::NoUpdate);

    const QStringList selected = window.selected_filesystem_paths_including_parent_link();
    QCOMPARE(selected.size(), 2);

    reset_bridge_segments_for_test();
    window.on_extract_requested();

    z7::ui::gui::BridgeTaskPayload payload;
    QString payload_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 z7::ui::gui::BridgeCommandKind::kExtract,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, z7::ui::gui::BridgeCommandKind::kExtract);
    QVERIFY(payload.show_dialog);
    QVERIFY(payload.refresh_after_finish);
    QStringList operand_names;
    operand_names.reserve(payload.archive_paths.size());
    for (const QString& operand : payload.archive_paths) {
      operand_names << QFileInfo(operand).fileName();
    }
    QVERIFY(operand_names.contains(QFileInfo(archive_a).fileName()));
    QVERIFY(operand_names.contains(QFileInfo(archive_b).fileName()));
    QCOMPARE(payload.archive_paths.size(), 2);
  }

void FileManagerBehaviorTest::archiveTestCompletionShowsSummaryMessageBox() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString tracker_log_path =
        QDir(root.path()).filePath(QStringLiteral("test-task-ipc-log.json"));
    ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                          prepend_to_path(fake_launcher_dir()));
    ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                         tracker_log_path.toUtf8());
    ScopedEnvVar claim_env(QByteArrayLiteral("Z7_FAKE_TRACKER_CLAIM_TASK_IPC"),
                           QByteArrayLiteral("1"));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int row = row_by_name(window, QStringLiteral("sample.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    QString warning_text;
    schedule_message_box_capture_and_click(QMessageBox::Ok,
                                           nullptr,
                                           &warning_text,
                                           10000,
                                           10);
    window.on_test_requested();
    QVERIFY2(warning_text.isEmpty(), qPrintable(warning_text));
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QVERIFY(filemanager_behavior_internal::current_progress_dialog(window) == nullptr);

    QVERIFY(!window.task_ipc_owner_instance_id_.isEmpty());
    z7::task_ipc_runtime::TaskIpcPayload payload;
    QString read_error;
    QVERIFY2(read_latest_task_ipc_payload_for_command(
                 window,
                 z7::task_ipc_runtime::TaskIpcCommandKind::kTest,
                 &payload,
                 &read_error),
             qPrintable(read_error));
    QCOMPARE(payload.command, z7::task_ipc_runtime::TaskIpcCommandKind::kTest);
    QVERIFY(payload.open.has_value());
    QVERIFY(payload.test.has_value());
    QCOMPARE(payload.open->archive_path, QFileInfo(archive_path).absoluteFilePath());
    QCOMPARE(payload.open->nested_archive_entries, QStringList{});
    QCOMPARE(payload.test->archive_inputs, QStringList{QStringLiteral("sample.txt")});
    QVERIFY(!payload.refresh_after_finish);

    QJsonObject tracker_log;
    QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                              !tracker_log.isEmpty()),
                             5000);
    const QJsonObject task_ipc_log =
        tracker_log.value(QStringLiteral("task_ipc")).toObject();
    QVERIFY(task_ipc_log.value(QStringLiteral("claimed")).toBool());
    QVERIFY(task_ipc_log.value(QStringLiteral("completed")).toBool());
    QCOMPARE(task_ipc_log.value(QStringLiteral("payload"))
                 .toObject()
                 .value(QStringLiteral("command"))
                 .toString(),
             QStringLiteral("test"));
}

void FileManagerBehaviorTest::archiveTestInArchiveViewUsesSessionWhenArchivePathMissing() {
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

    QString result_text;
    schedule_test_result_dialog_autoclose(&result_text, 25000, 10);
    window.on_test_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 5000);

    if (!result_text.isEmpty()) {
      QVERIFY(result_text.contains(localized_label(1004) + QStringLiteral(":")));
      QVERIFY(result_text.contains(localized_label(1007) + QStringLiteral(":")));
      QVERIFY(result_text.contains(localized_label(3001), Qt::CaseInsensitive));
    }
  }

void FileManagerBehaviorTest::taskProgressDialogTestCancelConfirmationFollowsYesNoCancel() {
    using z7::ui::filemanager::TaskProgressDialog;

    TaskProgressDialog dialog;
    dialog.set_header(QStringLiteral("%1: %2")
                          .arg(z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(3302)),
                               QStringLiteral("sample.7z")));
    dialog.set_test_mode(true);
    dialog.set_pause_available(true);
    dialog.set_running(true);
    dialog.set_stage(QStringLiteral("Running"));
    dialog.set_percent(29);
    dialog.show();

    QTRY_VERIFY_WITH_TIMEOUT(dialog.windowTitle().contains(
                                 z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(3302))),
                             3000);
    QVERIFY(dialog.windowTitle().contains(QStringLiteral("29%")));

    QSignalSpy pause_spy(&dialog, SIGNAL(pause_requested()));
    QSignalSpy resume_spy(&dialog, SIGNAL(resume_requested()));
    QSignalSpy cancel_spy(&dialog, SIGNAL(cancel_requested()));

    const QList<QPushButton*> buttons = dialog.findChildren<QPushButton*>();
    QPushButton* cancel_button = nullptr;
    for (QPushButton* button : buttons) {
      if (button != nullptr &&
          button->text() == z7::ui::runtime_support::L(402)) {
        cancel_button = button;
        break;
      }
    }
    QVERIFY(cancel_button != nullptr);

    std::vector<int> answers = {
        static_cast<int>(QMessageBox::No),
        static_cast<int>(QMessageBox::Cancel),
        static_cast<int>(QMessageBox::Yes)};
    int answer_index = 0;
    dialog.set_cancel_confirmation_handler([&answers, &answer_index]() -> int {
      if (answer_index >= static_cast<int>(answers.size())) {
        return static_cast<int>(QMessageBox::Cancel);
      }
      return answers[answer_index++];
    });

    QTest::mouseClick(cancel_button, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(cancel_spy.count(), 0, 3000);
    QVERIFY(pause_spy.count() >= 1);
    QVERIFY(resume_spy.count() >= 1);

    QTest::mouseClick(cancel_button, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(cancel_spy.count(), 0, 3000);

    QTest::mouseClick(cancel_button, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(cancel_spy.count() > 0, 3000);
  }
