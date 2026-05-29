// tests/filemanager/behavior/runner_path.cpp
// Role: Path bar and CRC menu behavior cases.

#include "internal.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::pathBarMatchesParentButtonAndEditableCombo() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    QVERIFY(window.path_combo_ != nullptr);
    QVERIFY(window.path_combo_->isEditable());
    QVERIFY(window.up_dir_button_ != nullptr);
    QVERIFY(window.up_dir_button_->isEnabled());

    window.set_current_directory(QDir::rootPath());
    QVERIFY(!window.up_dir_button_->isEnabled());

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QVERIFY(window.up_dir_button_->isEnabled());
  }

void FileManagerBehaviorTest::pathBarCanNavigateByEnterAndPopupItems() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString level1 = QDir(root.path()).filePath(QStringLiteral("level1"));
    const QString level2 = QDir(level1).filePath(QStringLiteral("level2"));
    QVERIFY(QDir().mkpath(level2));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(level2);
    QVERIFY(window.path_combo_ != nullptr);

    window.path_combo_->setEditText(QDir::toNativeSeparators(level1));
    QVERIFY(QMetaObject::invokeMethod(window.path_combo_->lineEdit(),
                                      "returnPressed",
                                      Qt::DirectConnection));
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(level1).absolutePath());

    window.rebuild_path_bar_popup_items();
    const int root_item =
        window.path_combo_->findData(QDir(root.path()).absolutePath(), Qt::UserRole);
    QVERIFY(root_item >= 0);
    const QString popup_path =
        window.path_combo_->itemData(root_item, Qt::UserRole).toString();
    QVERIFY(!popup_path.isEmpty());
    QVERIFY(window.navigate_to_path_from_bar(popup_path));
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(root.path()).absolutePath());
  }

void FileManagerBehaviorTest::archivePathBarSupportsDisplayAbsoluteAndRelativeInputsWithoutLegacyMarker() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_nested_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare nested archive");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QVERIFY(window.path_combo_ != nullptr);
    QVERIFY(window.path_combo_->lineEdit() != nullptr);

    const QString root_display = window.path_combo_->currentText();
    QVERIFY(!root_display.isEmpty());
    QVERIFY(!root_display.contains(QStringLiteral("::")));
    QVERIFY(root_display.endsWith(QDir::separator()));

    window.path_combo_->setEditText(root_display + QStringLiteral("top"));
    QVERIFY(QMetaObject::invokeMethod(window.path_combo_->lineEdit(),
                                      "returnPressed",
                                      Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_COMPARE_WITH_TIMEOUT(window.active_panel_controller().archive.virtual_dir,
                              QStringLiteral("top"),
                              20000);

    window.path_combo_->setEditText(QStringLiteral("/top/inner"));
    QVERIFY(QMetaObject::invokeMethod(window.path_combo_->lineEdit(),
                                      "returnPressed",
                                      Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_COMPARE_WITH_TIMEOUT(window.active_panel_controller().archive.virtual_dir,
                              QStringLiteral("top/inner"),
                              20000);

    window.path_combo_->setEditText(QStringLiteral("top"));
    QVERIFY(QMetaObject::invokeMethod(window.path_combo_->lineEdit(),
                                      "returnPressed",
                                      Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_COMPARE_WITH_TIMEOUT(window.active_panel_controller().archive.virtual_dir,
                              QStringLiteral("top"),
                              20000);

    window.rebuild_path_bar_popup_items();
    bool saw_top = false;
    for (int i = 0; i < window.path_combo_->count(); ++i) {
      QVERIFY(!window.path_combo_->itemText(i).contains(QStringLiteral("::")));
      const QString data =
          window.path_combo_->itemData(i, Qt::UserRole).toString();
      if (!saw_top && data.contains(QStringLiteral("top"))) {
        saw_top = true;
        QVERIFY(window.navigate_to_path_from_bar(data));
        QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
      }
    }
    QVERIFY(saw_top);
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("top"));
    QVERIFY(!window.windowTitle().contains(QStringLiteral("::")));
  }

void FileManagerBehaviorTest::pathBarSpaceDoesNotTriggerOpenAction() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("selected.txt"));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("x");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    QVERIFY(window.path_combo_ != nullptr);
    QVERIFY(window.path_combo_->lineEdit() != nullptr);

    const int row = row_by_name(window, QStringLiteral("selected.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});
    QVERIFY(window.open_action_ != nullptr);
    QVERIFY(window.open_action_->isEnabled());

    QSignalSpy open_spy(window.open_action_, &QAction::triggered);
    auto* path_edit = window.path_combo_->lineEdit();
    path_edit->setText(QStringLiteral("C:\\test"));
    path_edit->setFocus();

    QTest::keyClick(path_edit, Qt::Key_Space);

    QCOMPARE(open_spy.count(), 0);
    QCOMPARE(path_edit->text(), QStringLiteral("C:\\test "));
  }

void FileManagerBehaviorTest::crcMenuBuildsEnabledAndDisabledStates() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString input_file = QDir(root.path()).filePath(QStringLiteral("hash.txt"));
    QFile file(input_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("hash");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int row = row_by_name(window, QStringLiteral("hash.txt"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    QMenu enabled_menu;
    QStringList captured_methods;
    window.populate_crc_hash_menu(
        &enabled_menu,
        true,
        [&captured_methods](const QString& method) { captured_methods << method; });
    QVERIFY(enabled_menu.menuAction()->isEnabled());
    QCOMPARE(enabled_menu.actions().size(), 11);
    QCOMPARE(window.crc_menu_->title(), QStringLiteral("CRC"));

    const QStringList expected_labels = {
        QStringLiteral("CRC-32"),
        QStringLiteral("CRC-64"),
        QStringLiteral("XXH64"),
        QStringLiteral("MD5"),
        QStringLiteral("SHA-1"),
        QStringLiteral("SHA-256"),
        QStringLiteral("SHA-384"),
        QStringLiteral("SHA-512"),
        QStringLiteral("SHA3-256"),
        QStringLiteral("BLAKE2sp"),
        QStringLiteral("*")};
    const QStringList expected_methods = {
        QStringLiteral("CRC32"),
        QStringLiteral("CRC64"),
        QStringLiteral("XXH64"),
        QStringLiteral("MD5"),
        QStringLiteral("SHA1"),
        QStringLiteral("SHA256"),
        QStringLiteral("SHA384"),
        QStringLiteral("SHA512"),
        QStringLiteral("SHA3-256"),
        QStringLiteral("BLAKE2sp"),
        QStringLiteral("*")};

    for (int i = 0; i < enabled_menu.actions().size(); ++i) {
      QCOMPARE(enabled_menu.actions().at(i)->text(), expected_labels.at(i));
    }
    for (QAction* action : enabled_menu.actions()) {
      QVERIFY(action->isEnabled());
      action->trigger();
    }
    QCOMPARE(captured_methods, expected_methods);

    QMenu disabled_menu;
    bool called = false;
    window.populate_crc_hash_menu(
        &disabled_menu,
        false,
        [&called](const QString&) { called = true; });
    QVERIFY(!disabled_menu.menuAction()->isEnabled());
    QCOMPARE(disabled_menu.actions().size(), 11);
    for (QAction* action : disabled_menu.actions()) {
      QVERIFY(!action->isEnabled());
    }
    for (QAction* action : disabled_menu.actions()) {
      action->trigger();
    }
    QVERIFY(!called);

    window.rebuild_file_crc_menu();
    QVERIFY(window.crc_menu_->menuAction()->isEnabled());
    window.active_panel_controller().ui.details_view->clearSelection();
    window.rebuild_file_crc_menu();
    QVERIFY(window.crc_menu_->menuAction()->isEnabled());
    const auto empty_state = window.compute_seven_zip_menu_state(false);
    QVERIFY(!empty_state.visible);
    QVERIFY(!empty_state.show_crc_group);
  }

void FileManagerBehaviorTest::fileCrcUsesInternalHashTaskNotSevenzipLauncher() {
    close_checksum_dialogs();
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString input_file = QDir(root.path()).filePath(QStringLiteral("hash.bin"));
    QFile file(input_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("hash-data");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int row = row_by_name(window, QStringLiteral("hash.bin"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    bool external_command_launcher_called = false;
    window.external_command_launcher_ =
        [&external_command_launcher_called](const QString&,
                                    const QStringList&,
                                    const QString&,
                                    qint64*) {
          external_command_launcher_called = true;
          return true;
        };

    window.on_hash_with_method_requested(QStringLiteral("SHA256"));
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(!external_command_launcher_called);
    close_checksum_dialogs();
  }

void FileManagerBehaviorTest::fileCrcUsesOperSmartItems() {
    using namespace z7::ui::gui;
    using namespace z7::ui::gui::bridge_internal;

    close_checksum_dialogs();
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString first_path =
        QDir(root.path()).filePath(QStringLiteral("first.bin"));
    const QString second_path =
        QDir(root.path()).filePath(QStringLiteral("second.bin"));
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

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();
    window.set_current_directory(root.path());

    const int first_row = row_by_name(window, QStringLiteral("first.bin"));
    const int second_row = row_by_name(window, QStringLiteral("second.bin"));
    const int parent_row = row_by_name(window, QStringLiteral(".."));
    QVERIFY(first_row >= 0);
    QVERIFY(second_row >= 0);
    QVERIFY(parent_row >= 0);

    QItemSelectionModel* selection =
        window.active_panel_controller().ui.details_view->selectionModel();
    QVERIFY(selection != nullptr);
    auto request_hash = [&window]() {
      QString warning_text;
      schedule_message_box_capture_and_click(QMessageBox::Ok,
                                             nullptr,
                                             &warning_text,
                                             3000,
                                             10);
      window.on_hash_with_method_requested(QStringLiteral("SHA256"));
      QVERIFY2(warning_text.isEmpty(), qPrintable(warning_text));
    };
    select_rows_in_active_panel(&window, {second_row});

    reset_bridge_segments_for_test();
    request_hash();

    BridgeTaskPayload payload;
    QString payload_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kHash,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kHash);
    QCOMPARE(payload.hash_method, QStringLiteral("SHA256"));
    QStringList expected_second{QFileInfo(second_path).absoluteFilePath()};
    expected_second.sort();
    payload.input_paths.sort();
    QCOMPARE(payload.input_paths, expected_second);

    select_rows_in_active_panel(&window, {parent_row, second_row});
    reset_bridge_segments_for_test();
    request_hash();

    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kHash,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kHash);
    QCOMPARE(payload.hash_method, QStringLiteral("SHA256"));
    payload.input_paths.sort();
    QCOMPARE(payload.input_paths, expected_second);

    selection->clearSelection();
    selection->clearCurrentIndex();
    reset_bridge_segments_for_test();
    request_hash();

    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kHash,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kHash);
    QStringList expected_all{
        QFileInfo(first_path).absoluteFilePath(),
        QFileInfo(second_path).absoluteFilePath(),
    };
    expected_all.sort();
    payload.input_paths.sort();
    QCOMPARE(payload.input_paths, expected_all);

    select_rows_in_active_panel(&window, {parent_row});
    reset_bridge_segments_for_test();
    request_hash();

    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kHash,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kHash);
    payload.input_paths.sort();
    QCOMPARE(payload.input_paths, expected_all);

    select_rows_in_active_panel(&window, {first_row, second_row});
    reset_bridge_segments_for_test();
    window.run_sevenzip_hash(QStringLiteral("SHA256"));

    QVERIFY2(read_latest_bridge_payload_for_command(
                 window,
                 BridgeCommandKind::kHash,
                 &payload,
                 &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kHash);
    payload.input_paths.sort();
    QCOMPARE(payload.input_paths, expected_all);

    close_checksum_dialogs();
  }


// End of runner.cpp

// End of runner_path.cpp
