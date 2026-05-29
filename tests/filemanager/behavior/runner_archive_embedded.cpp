// tests/filemanager/behavior/runner_archive_embedded.cpp
// Role: Embedded archive navigation and parent-return behavior cases.

#include "internal.h"

#include "native_archive_session_registry.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::openEmbeddedZipArchiveUsesNonTempStrategyAndParentExitClearsSessions() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString outer_archive = create_archive_with_embedded_zip_archive(root, false);
    QVERIFY2(!outer_archive.isEmpty(), "failed to create embedded zip fixture");

    QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(0));

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(outer_archive);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(1));

    const int child_row = row_by_name(window, QStringLiteral("child.zip"));
    QVERIFY(child_row >= 0);
    select_rows_in_active_panel(&window, {child_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 1);
    QVERIFY(window.active_panel_controller().archive.temp_session == nullptr);
    QVERIFY(window.archive_temp_sessions_.isEmpty());
    QVERIFY(window.active_panel_controller().archive.current_token.is_valid());
    QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(2));

    const std::shared_ptr<z7::app::ArchiveOpenSession> child_session =
        z7::app::ArchiveSessionRegistry::instance().find(
            window.active_panel_controller().archive.current_token);
    QVERIFY(child_session != nullptr);
    QVERIFY(child_session->strategy() == z7::app::OpenArchiveSessionResult::Strategy::kStream ||
            child_session->strategy() == z7::app::OpenArchiveSessionResult::Strategy::kMemory);
    QVERIFY(child_session->strategy() != z7::app::OpenArchiveSessionResult::Strategy::kTempFile);
    QVERIFY(row_by_name(window, QStringLiteral("zip-child.txt")) >= 0);

    window.on_open_parent_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(1));

    window.on_open_parent_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(!window.in_archive_view());
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QFileInfo(outer_archive).absolutePath());
    QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(0));
}

void FileManagerBehaviorTest::openEmbeddedArchiveInsideAndParentRestoresOuterArchive() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString outer_archive = create_archive_with_embedded_archive(root);
    QVERIFY2(!outer_archive.isEmpty(), "failed to create embedded archive fixture");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(outer_archive);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.source_archive,
             QFileInfo(outer_archive).absoluteFilePath());
    QCOMPARE(window.active_panel_controller().archive.virtual_display_source,
             QFileInfo(outer_archive).absoluteFilePath());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 0);
    QVERIFY(window.active_panel_controller().archive.temp_session == nullptr);
    QVERIFY(window.active_panel_controller().archive.current_token.is_valid());
    QVERIFY(window.archive_temp_sessions_.isEmpty());

    const int child_row = row_by_name(window, QStringLiteral("child.7z"));
    QVERIFY(child_row >= 0);
    select_rows_in_active_panel(&window, {child_row});

    int external_open_count = 0;
    window.external_opener_ = [&external_open_count](const QString&) {
      ++external_open_count;
      return true;
    };

    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(external_open_count, 0);
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 1);
    QVERIFY(window.active_panel_controller().archive.temp_session == nullptr);
    QVERIFY(window.archive_temp_sessions_.isEmpty());
    QCOMPARE(window.active_panel_controller().archive.source_archive,
             QFileInfo(outer_archive).absoluteFilePath());
    QVERIFY(window.active_panel_controller().archive.current_token.is_valid());
    QVERIFY(window.active_panel_controller().archive.virtual_display_source.contains(
        QStringLiteral("outer.7z")));
    QVERIFY(window.active_panel_controller().archive.virtual_display_source.contains(
        QStringLiteral("child.7z")));
    QVERIFY(window.path_combo_ != nullptr);
    const QString path_bar_text = window.path_combo_->currentText();
    const QString window_title = window.windowTitle();
    QVERIFY(path_bar_text.contains(QStringLiteral("outer.7z")));
    QVERIFY(path_bar_text.contains(QStringLiteral("child.7z")));
    QVERIFY(!path_bar_text.contains(QStringLiteral("::")));
    QVERIFY(path_bar_text.endsWith(QDir::separator()));
    QVERIFY(window_title.contains(QStringLiteral("outer.7z")));
    QVERIFY(window_title.contains(QStringLiteral("child.7z")));
    QVERIFY(!window_title.contains(QStringLiteral("::")));
    QVERIFY(window_title.endsWith(QDir::separator()));
    QVERIFY(row_by_name(window, QStringLiteral("child.txt")) >= 0);

    window.on_open_parent_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.source_archive,
             QFileInfo(outer_archive).absoluteFilePath());
    QCOMPARE(window.active_panel_controller().archive.virtual_display_source,
             QFileInfo(outer_archive).absoluteFilePath());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir, QString());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 0);
    QVERIFY(window.active_panel_controller().archive.temp_session == nullptr);
    QVERIFY(window.active_panel_controller().archive.current_token.is_valid());
    QVERIFY(window.archive_temp_sessions_.isEmpty());

    window.on_open_parent_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(!window.in_archive_view());
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QFileInfo(outer_archive).absolutePath());
    QVERIFY(window.archive_temp_sessions_.isEmpty());
}

void FileManagerBehaviorTest::embeddedArchiveParentLinkReturnsToOuterArchive() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString outer_archive = create_archive_with_embedded_archive(root);
    QVERIFY2(!outer_archive.isEmpty(), "failed to create embedded archive fixture");

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/ShowDots"), true);
      settings.sync();
    }

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(outer_archive);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int child_row = row_by_name(window, QStringLiteral("child.7z"));
    QVERIFY(child_row >= 0);
    select_rows_in_active_panel(&window, {child_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 1);
    QVERIFY(window.active_panel_controller().archive.temp_session == nullptr);
    QVERIFY(window.archive_temp_sessions_.isEmpty());
    QVERIFY(row_by_name(window, QStringLiteral("..")) >= 0);

    const int up_row = row_by_name(window, QStringLiteral(".."));
    QVERIFY(up_row >= 0);
    select_rows_in_active_panel(&window, {up_row});
    window.activate_panel_selection(Qt::NoModifier);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.source_archive,
             QFileInfo(outer_archive).absoluteFilePath());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir, QString());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 0);
    QVERIFY(window.active_panel_controller().archive.temp_session == nullptr);
    QVERIFY(window.archive_temp_sessions_.isEmpty());
    QVERIFY(row_by_name(window, QStringLiteral("child.7z")) >= 0);
}

void FileManagerBehaviorTest::openEmbeddedArchiveInSubdirAndParentRestoresVirtualDir() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString outer_archive =
        create_archive_with_embedded_archive_in_folder(root);
    QVERIFY2(!outer_archive.isEmpty(),
             "failed to create subdir embedded archive fixture");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(outer_archive);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir, QString());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 0);

    const int pack_row = row_by_name(window, QStringLiteral("pack"));
    QVERIFY(pack_row >= 0);
    select_rows_in_active_panel(&window, {pack_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("pack"));

    const int child_row = row_by_name(window, QStringLiteral("child2.7z"));
    QVERIFY(child_row >= 0);
    select_rows_in_active_panel(&window, {child_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 20000);

    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 1);
    QVERIFY(window.active_panel_controller().archive.temp_session == nullptr);
    QVERIFY(window.archive_temp_sessions_.isEmpty());
    QCOMPARE(window.active_panel_controller().archive.source_archive,
             QFileInfo(outer_archive).absoluteFilePath());
    QVERIFY(window.active_panel_controller().archive.virtual_display_source.contains(
        QStringLiteral("pack")));
    QVERIFY(window.active_panel_controller().archive.virtual_display_source.contains(
        QStringLiteral("child2.7z")));

    window.on_open_parent_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("pack"));
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 0);
    QVERIFY(window.active_panel_controller().archive.temp_session == nullptr);
    QVERIFY(window.archive_temp_sessions_.isEmpty());
    QVERIFY(row_by_name(window, QStringLiteral("child2.7z")) >= 0);
}

void FileManagerBehaviorTest::openSameNamedEmbeddedArchiveDoesNotLeaveStaleProgressDialog() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString shared_base = QStringLiteral("apps.apple.com-main");
    const QString outer_archive =
        create_archive_with_same_name_embedded_archive(root);
    QVERIFY2(!outer_archive.isEmpty(),
             "failed to create same-name embedded archive fixture");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(outer_archive);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int container_row = row_by_name(window, shared_base);
    QVERIFY(container_row >= 0);
    select_rows_in_active_panel(&window, {container_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 20000);
    QCOMPARE(window.active_panel_controller().archive.virtual_dir, shared_base);

    const QString child_archive_name = shared_base + QStringLiteral(".7z");
    const int child_row = row_by_name(window, child_archive_name);
    QVERIFY(child_row >= 0);
    select_rows_in_active_panel(&window, {child_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 20000);

    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 1);
    QVERIFY(window.active_panel_controller().archive.current_token.is_valid());
    QVERIFY(row_by_name(window, QStringLiteral("same-name-leaf.txt")) >= 0);

    QVERIFY(window.path_combo_ != nullptr);
    const QString path_bar_text = window.path_combo_->currentText();
    const QString window_title = window.windowTitle();
    QVERIFY(path_bar_text.count(child_archive_name) >= 2);
    QVERIFY(window_title.count(child_archive_name) >= 2);
    QVERIFY(path_bar_text.contains(shared_base + QDir::separator() + child_archive_name));
    QVERIFY(window_title.contains(shared_base + QDir::separator() + child_archive_name));
}

void FileManagerBehaviorTest::nestedDeleteRebuildsEmbeddedArchiveViewAndRemovesEntry() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString outer_archive =
        create_archive_with_embedded_archive_in_folder(root);
    QVERIFY2(!outer_archive.isEmpty(),
             "failed to create nested embedded archive fixture");

    z7::ui::filemanager::MainWindow window;
    window.question_box_ = [](const QString&,
                              const QString&,
                              QMessageBox::StandardButtons,
                              QMessageBox::StandardButton) {
      return QMessageBox::Yes;
    };

    window.open_archive_inside(outer_archive);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    int pack_row = row_by_name(window, QStringLiteral("pack"));
    QVERIFY(pack_row >= 0);
    select_rows_in_active_panel(&window, {pack_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("pack"));

    int child_row = row_by_name(window, QStringLiteral("child2.7z"));
    QVERIFY(child_row >= 0);
    select_rows_in_active_panel(&window, {child_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 1);
    QVERIFY(window.active_panel_controller().archive.current_token.is_valid());
    QVERIFY(row_by_name(window, QStringLiteral("child2.txt")) >= 0);

    int file_row = row_by_name(window, QStringLiteral("child2.txt"));
    QVERIFY(file_row >= 0);
    select_rows_in_active_panel(&window, {file_row});
    schedule_message_box_autoclose();
    window.on_delete_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 20000);
    close_message_boxes();

    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 1);
    QVERIFY(window.active_panel_controller().archive.current_token.is_valid());
    QCOMPARE(row_by_name(window, QStringLiteral("child2.txt")), -1);

    window.on_open_parent_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("pack"));

    child_row = row_by_name(window, QStringLiteral("child2.7z"));
    QVERIFY(child_row >= 0);
    select_rows_in_active_panel(&window, {child_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(row_by_name(window, QStringLiteral("child2.txt")), -1);
}

void FileManagerBehaviorTest::nestedDeleteWritebackRestoreDoesNotLeaveStaleProgressDialog() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString outer_archive =
        create_archive_with_embedded_archive_in_folder(root);
    QVERIFY2(!outer_archive.isEmpty(),
             "failed to create nested embedded archive fixture");

    z7::ui::filemanager::MainWindow window;
    window.question_box_ = [](const QString&,
                              const QString&,
                              QMessageBox::StandardButtons,
                              QMessageBox::StandardButton) {
      return QMessageBox::Yes;
    };

    window.open_archive_inside(outer_archive);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    int pack_row = row_by_name(window, QStringLiteral("pack"));
    QVERIFY(pack_row >= 0);
    select_rows_in_active_panel(&window, {pack_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    int child_row = row_by_name(window, QStringLiteral("child2.7z"));
    QVERIFY(child_row >= 0);
    select_rows_in_active_panel(&window, {child_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 20000);
    QVERIFY(row_by_name(window, QStringLiteral("child2.txt")) >= 0);

    const int file_row = row_by_name(window, QStringLiteral("child2.txt"));
    QVERIFY(file_row >= 0);
    select_rows_in_active_panel(&window, {file_row});
    schedule_message_box_autoclose();
    window.on_delete_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_progress_dialog(window) == nullptr, 20000);
    close_message_boxes();

    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 1);
    QVERIFY(window.active_panel_controller().archive.current_token.is_valid());
    QCOMPARE(row_by_name(window, QStringLiteral("child2.txt")), -1);
}

void FileManagerBehaviorTest::nestedArchiveWritebackPlanCapturesReopenChain() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString outer_archive =
        create_archive_with_embedded_archive_in_folder(root);
    QVERIFY2(!outer_archive.isEmpty(),
             "failed to create nested embedded archive fixture");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(outer_archive);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int pack_row = row_by_name(window, QStringLiteral("pack"));
    QVERIFY(pack_row >= 0);
    select_rows_in_active_panel(&window, {pack_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("pack"));

    const int child_row = row_by_name(window, QStringLiteral("child2.7z"));
    QVERIFY(child_row >= 0);
    select_rows_in_active_panel(&window, {child_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.parent_stack.size(), 1);

    const auto writeback_plan = window.build_archive_writeback_plan_for_panel(0);
    QVERIFY(writeback_plan.is_valid());
    QCOMPARE(writeback_plan.source_archive,
             QFileInfo(outer_archive).absoluteFilePath());
    QCOMPARE(writeback_plan.origin_dir,
             QFileInfo(outer_archive).absolutePath());
    QCOMPARE(writeback_plan.reopen_frames.size(), 2);
    QCOMPARE(writeback_plan.root_display_source(),
             QFileInfo(outer_archive).absoluteFilePath());
    QCOMPARE(writeback_plan.current_virtual_dir(), QString());
    QCOMPARE(writeback_plan.nested_archive_entries,
             QStringList{QStringLiteral("pack/child2.7z")});
    QVERIFY(writeback_plan.current_display_source().contains(
        QStringLiteral("pack")));
    QVERIFY(writeback_plan.current_display_source().contains(
        QStringLiteral("child2.7z")));
}
