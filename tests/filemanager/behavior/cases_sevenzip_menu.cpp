// tests/filemanager/behavior/cases_sevenzip_menu.cpp
// Role: 7-Zip menu state and command argument behavior cases.

#include "internal.h"

#include <QStatusBar>

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::sevenZipMenuStateTracksSelectionKinds() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_file = QDir(root.path()).filePath(QStringLiteral("pack.7z"));
    const QString text_file = QDir(root.path()).filePath(QStringLiteral("note.txt"));
    const QString folder_path = QDir(root.path()).filePath(QStringLiteral("folder"));

    {
      QFile f(archive_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("dummy archive");
      f.close();
    }
    {
      QFile f(text_file);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write("plain text");
      f.close();
    }
    QVERIFY(QDir().mkpath(folder_path));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int archive_row = row_by_name(window, QStringLiteral("pack.7z"));
    const int text_row = row_by_name(window, QStringLiteral("note.txt"));
    const int folder_row = row_by_name(window, QStringLiteral("folder"));
    QVERIFY(archive_row >= 0);
    QVERIFY(text_row >= 0);
    QVERIFY(folder_row >= 0);

    select_rows_in_active_panel(&window, {archive_row});
    auto archive_state = window.compute_seven_zip_menu_state(false);
    QVERIFY(archive_state.visible);
    QVERIFY(archive_state.show_open);
    QVERIFY(archive_state.show_extract_group);
    QVERIFY(archive_state.show_test);

    select_rows_in_active_panel(&window, {text_row});
    auto text_state = window.compute_seven_zip_menu_state(false);
    QVERIFY(text_state.visible);
    QVERIFY(!text_state.show_open);
    QVERIFY(!text_state.show_extract_group);

    select_rows_in_active_panel(&window, {folder_row});
    auto folder_state = window.compute_seven_zip_menu_state(false);
    QVERIFY(folder_state.visible);
    QVERIFY(!folder_state.show_extract_group);
    QVERIFY(folder_state.show_compress_group);

    select_rows_in_active_panel(&window, {archive_row, folder_row});
    auto mixed_state = window.compute_seven_zip_menu_state(false);
    QVERIFY(mixed_state.visible);
    QVERIFY(!mixed_state.show_extract_group);
    QVERIFY(mixed_state.show_compress_group);
}

void FileManagerBehaviorTest::sevenZipMenuStateHiddenInArchiveView() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.open_archive_inside(archive_path);

    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const auto state = window.compute_seven_zip_menu_state(false);
    QVERIFY(!state.visible);
}

void FileManagerBehaviorTest::sevenZipCommandsBuildExpectedArgs() {
    using namespace z7::ui::gui;
    using namespace z7::ui::gui::bridge_internal;

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString input_file = QDir(root.path()).filePath(QStringLiteral("toolchains.yml"));
    QFile file(input_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("name: toolchains");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int row = row_by_name(window, QStringLiteral("toolchains.yml"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    reset_bridge_segments_for_test();
    window.run_sevenzip_add_to_archive();
    BridgeTaskPayload payload;
    QString payload_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window, BridgeCommandKind::kAdd, &payload, &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kAdd);
    QVERIFY(payload.show_dialog);
    QVERIFY(payload.refresh_after_finish);
    QVERIFY(payload.archive_path.endsWith(QStringLiteral("toolchains")));
    QVERIFY(payload.input_paths.contains(input_file));

    reset_bridge_segments_for_test();
    window.run_sevenzip_add_to_type(QStringLiteral("7z"));
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window, BridgeCommandKind::kAdd, &payload, &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kAdd);
    QCOMPARE(payload.archive_type, QStringLiteral("7z"));
    QVERIFY(payload.refresh_after_finish);
    QVERIFY(payload.archive_path.endsWith(QStringLiteral(".7z")));
    QVERIFY(payload.input_paths.contains(input_file));

    reset_bridge_segments_for_test();
    window.run_sevenzip_hash(QStringLiteral("SHA256"));
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window, BridgeCommandKind::kHash, &payload, &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kHash);
    QCOMPARE(payload.hash_method, QStringLiteral("SHA256"));
    QVERIFY(payload.input_paths.contains(input_file));

    reset_bridge_segments_for_test();
    window.run_sevenzip_generate_sha256();
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window, BridgeCommandKind::kAdd, &payload, &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kAdd);
    QCOMPARE(payload.archive_type, QStringLiteral("hash"));
    QVERIFY(payload.archive_path.endsWith(QStringLiteral("toolchains.yml.sha256")));
    QVERIFY(payload.input_paths.contains(input_file));
    QVERIFY(payload.refresh_after_finish);

    reset_bridge_segments_for_test();
    window.run_sevenzip_checksum_test();
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window, BridgeCommandKind::kCli, &payload, &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kCli);
    QCOMPARE(payload.cli_argv,
             QStringList({QStringLiteral("t"),
                          QStringLiteral("-thash"),
                          input_file}));
    QCOMPARE(payload.cli_working_dir, root.path());
    QVERIFY(!payload.refresh_after_finish);
}

void FileManagerBehaviorTest::sevenZipExtractAndTestCommandsBuildExpectedArgs() {
    using namespace z7::ui::gui;
    using namespace z7::ui::gui::bridge_internal;

    struct SettingsResetGuard {
      ~SettingsResetGuard() { ::clear_runtime_settings(); }
    } reset_settings;
    clear_runtime_settings();

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_file = QDir(root.path()).filePath(QStringLiteral("pack.7z"));
    QFile file(archive_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("dummy archive");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int row = row_by_name(window, QStringLiteral("pack.7z"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    reset_bridge_segments_for_test();
    window.run_sevenzip_extract_files_dialog();
    BridgeTaskPayload payload;
    QString payload_error;
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window, BridgeCommandKind::kExtract, &payload, &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kExtract);
    QVERIFY(payload.show_dialog);
    QVERIFY(payload.refresh_after_finish);
    QVERIFY(payload.archive_paths.contains(archive_file));
    QCOMPARE(payload.output_dir, QDir::fromNativeSeparators(root.path()));
    QVERIFY(payload.extract_split_dest_enabled);
    QVERIFY(payload.extract_split_dest_name.endsWith(QStringLiteral("pack") + QDir::separator()));
    QVERIFY(!payload.extract_eliminate_root_duplication);
    QCOMPARE(payload.extract_zone_id_mode, QStringLiteral("none"));

    reset_bridge_segments_for_test();
    window.run_sevenzip_extract_here();
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window, BridgeCommandKind::kExtract, &payload, &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kExtract);
    QVERIFY(!payload.show_dialog);
    QVERIFY(payload.refresh_after_finish);
    QCOMPARE(payload.output_dir, QDir::fromNativeSeparators(root.path()));
    QVERIFY(payload.archive_paths.contains(archive_file));
    QVERIFY(!payload.extract_eliminate_root_duplication);
    QCOMPARE(payload.extract_zone_id_mode, QStringLiteral("none"));

    reset_bridge_segments_for_test();
    window.run_sevenzip_extract_to();
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window, BridgeCommandKind::kExtract, &payload, &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kExtract);
    QVERIFY(payload.extract_eliminate_root_duplication);
    QCOMPARE(payload.extract_zone_id_mode, QStringLiteral("none"));
    QVERIFY(payload.extract_split_dest_enabled);
    QVERIFY(payload.extract_split_dest_name.endsWith(QStringLiteral("pack") + QDir::separator()));
    QVERIFY(payload.refresh_after_finish);
    QVERIFY(payload.archive_paths.contains(archive_file));

    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("Options/ElimDupExtract"), false);
    settings.setValue(QStringLiteral("Options/WriteZoneIdExtract"), 1);
    settings.sync();

    reset_bridge_segments_for_test();
    window.run_sevenzip_extract_to();
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window, BridgeCommandKind::kExtract, &payload, &payload_error),
             qPrintable(payload_error));
    QVERIFY(!payload.extract_eliminate_root_duplication);
    QCOMPARE(payload.extract_zone_id_mode, QStringLiteral("all"));

    reset_bridge_segments_for_test();
    window.run_sevenzip_test_archive();
    QVERIFY2(read_latest_bridge_payload_for_command(
                 window, BridgeCommandKind::kTest, &payload, &payload_error),
             qPrintable(payload_error));
    QCOMPARE(payload.command, BridgeCommandKind::kTest);
    QVERIFY(!payload.refresh_after_finish);
    QVERIFY(payload.archive_paths.contains(archive_file));
}

void FileManagerBehaviorTest::sevenZipFileMenuShowsDynamicSubmenuAtTopAndHidesWithoutSelection() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_file = QDir(root.path()).filePath(QStringLiteral("pack.7z"));
    QFile file(archive_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("dummy archive");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int row = row_by_name(window, QStringLiteral("pack.7z"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});
    window.refresh_action_states();
    window.rebuild_file_menu_seven_zip_section();

    const QList<QAction*> with_selection = window.file_menu_->actions();
    QVERIFY(with_selection.size() >= 3);
    QVERIFY(with_selection.at(0)->menu() != nullptr);
    QCOMPARE(with_selection.at(0)->menu()->title(), QStringLiteral("7-Zip"));
    QVERIFY(with_selection.at(1)->isSeparator());
    QCOMPARE(with_selection.at(2), window.open_action_);

    window.rebuild_file_menu_seven_zip_section();
    int seven_zip_count = 0;
    for (QAction* action : window.file_menu_->actions()) {
      if (action != nullptr && action->menu() != nullptr &&
          action->menu()->title() == QStringLiteral("7-Zip")) {
        ++seven_zip_count;
      }
    }
    QCOMPARE(seven_zip_count, 1);

    select_rows_in_active_panel(&window, {});
    window.refresh_action_states();
    window.rebuild_file_menu_seven_zip_section();
    for (QAction* action : window.file_menu_->actions()) {
      if (action != nullptr && action->menu() != nullptr) {
        QVERIFY(action->menu()->title() != QStringLiteral("7-Zip"));
      }
    }
}

void FileManagerBehaviorTest::sevenZipOpenAndOpenAsLaunchNew7zfmProcess() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_file = QDir(root.path()).filePath(QStringLiteral("pack.7z"));
    QFile file(archive_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("dummy archive");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int row = row_by_name(window, QStringLiteral("pack.7z"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    QVector<z7::platform::qt::filemanager_instance_launcher::LaunchRequest> launches;
    set_filemanager_open_launcher_override_for_test(
        [&launches](
            const z7::platform::qt::filemanager_instance_launcher::LaunchRequest& request,
            QString*) {
          launches.push_back(request);
          return true;
        });

    window.run_sevenzip_open_archive();
    window.run_sevenzip_open_archive_as(QStringLiteral("#"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QVERIFY(window.panels_[0].ui.status_transient_message != nullptr);
    QVERIFY(window.panels_[0].ui.status_transient_message->text()
                .contains(QStringLiteral("launched 7zFM open")));
    QCOMPARE(window.findChildren<QStatusBar*>().size(), 2);
    QVERIFY(window.findChildren<QStatusBar*>(
                QString(), Qt::FindDirectChildrenOnly).isEmpty());

    QCOMPARE(launches.size(), 2);
    const QString expected_program = QCoreApplication::applicationFilePath();
    QCOMPARE(launches.at(0).program, expected_program);
    QCOMPARE(launches.at(0).working_dir, root.path());
    QCOMPARE(launches.at(0).path, archive_file);
    QCOMPARE(launches.at(0).arguments, QStringList{archive_file});
    QVERIFY(launches.at(0).type_hint.isEmpty());

    QCOMPARE(launches.at(1).program, expected_program);
    QCOMPARE(launches.at(1).working_dir, root.path());
    QCOMPARE(launches.at(1).path, archive_file);
    QCOMPARE(launches.at(1).arguments, QStringList({QStringLiteral("-t#"), archive_file}));
    QCOMPARE(launches.at(1).type_hint, QStringLiteral("#"));
}

void FileManagerBehaviorTest::startupTypeHintForcesInternalOpenForUnknownSuffixArchive() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to create sample archive");
    const QString renamed_path = QDir(root.path()).filePath(QStringLiteral("payload.bin"));
    QVERIFY(QFile::copy(archive_path, renamed_path));

    z7::ui::filemanager::MainWindow window;
    window.open_startup_target(renamed_path, QStringLiteral("7z"));
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.source_archive, renamed_path);
}

void FileManagerBehaviorTest::sevenZipOpenAsActionsCarryCapabilityKeys() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_file = QDir(root.path()).filePath(QStringLiteral("pack.7z"));
    QFile file(archive_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("dummy archive");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int row = row_by_name(window, QStringLiteral("pack.7z"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    QMenu menu;
    const auto state = window.compute_seven_zip_menu_state(false);
    QVERIFY(state.show_open_as);
    QVERIFY(state.show_compress_group);
    QMenu* seven = window.append_seven_zip_submenu(&menu, state);
    QVERIFY(seven != nullptr);

    int open_as_count = 0;
    QList<QMenu*> pending{seven};
    while (!pending.isEmpty()) {
      QMenu* current = pending.takeFirst();
      for (QAction* action : current->actions()) {
        if (action == nullptr) {
          continue;
        }
        if (action->menu() != nullptr) {
          pending << action->menu();
        }
        const QString key = capability_key(action);
        if (key == QStringLiteral("SevenZipOpenAs")) {
          ++open_as_count;
          QCOMPARE(capability_reason(action), QString());
          QVERIFY(action->isEnabled());
        } else if (!capability_reason(action).isEmpty()) {
          QFAIL(qPrintable(QStringLiteral("Unexpected capability key: %1").arg(key)));
        } else {
          continue;
        }
      }
    }

    QCOMPARE(open_as_count, 7);
}

void FileManagerBehaviorTest::sevenZipSubmenuContainsCrcShaGroup() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString input_file = QDir(root.path()).filePath(QStringLiteral("archive.bin"));
    QFile file(input_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("binary");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const int row = row_by_name(window, QStringLiteral("archive.bin"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    QMenu menu;
    const auto state = window.compute_seven_zip_menu_state(false);
    QMenu* seven = window.append_seven_zip_submenu(&menu, state);
    QVERIFY(seven != nullptr);
    QCOMPARE(seven->title(), QStringLiteral("7-Zip"));

    QMenu* crc_sha_menu = nullptr;
    for (QAction* action : seven->actions()) {
      if (action != nullptr && action->menu() != nullptr &&
          action->text() == QStringLiteral("CRC SHA")) {
        crc_sha_menu = action->menu();
        break;
      }
    }
    QVERIFY(crc_sha_menu != nullptr);

    const QList<QAction*> crc_actions = crc_sha_menu->actions();
    QCOMPARE(crc_actions.size(), 14);
    const QStringList expected_method_labels = {
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

    for (int i = 0; i < expected_method_labels.size(); ++i) {
      QVERIFY(!crc_actions.at(i)->isSeparator());
      QCOMPARE(crc_actions.at(i)->text(), expected_method_labels.at(i));
    }
    QVERIFY(crc_actions.at(11)->isSeparator());
    QCOMPARE(crc_actions.at(12)->text(), QStringLiteral("SHA-256 -> archive.bin.sha256"));
    QCOMPARE(crc_actions.at(13)->text(), QStringLiteral("Checksum : Test"));
}
