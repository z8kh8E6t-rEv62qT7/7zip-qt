// tests/filemanager/behavior/setup.cpp
// Role: Setup-focused behavior cases.

#include "internal.h"
#include "archive_large_pages.h"
#include "extract_memory_settings.h"
#include "large_pages_settings.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::init() {
    cancel_scheduled_message_box_handlers();
    reset_filemanager_open_launcher_for_test();
    reset_task_ipc_segments_for_test();
    clear_runtime_settings();
    if (auto* app =
            qobject_cast<z7::apps::filemanager::FileOpenApplication*>(
                QCoreApplication::instance())) {
      app->set_open_request_handler({});
      app->take_pending_open_requests();
    }
    auto& catalog = z7::ui::runtime_support::OfficialLangCatalog::instance();
    catalog.set_language_and_persist(QStringLiteral("-"));
    catalog.reload_from_settings();
    z7::app::set_large_pages_probe_override_for_test(-1);
  }

void FileManagerBehaviorTest::portableInitializationFailsWhenRootIsFile() {
    const QString valid_root = z7::platform::qt::portable_settings_root_dir();
    QTemporaryDir temp_root;
    QVERIFY(temp_root.isValid());
    const QString blocked = QDir(temp_root.path()).filePath(QStringLiteral("blocked-root"));
    QFile marker(blocked);
    QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Truncate));
    marker.close();

    z7::platform::qt::set_portable_settings_root(blocked);
    QString init_error;
    QVERIFY(!z7::platform::qt::initialize_portable_settings(&init_error));
    QVERIFY(!init_error.isEmpty());

    z7::platform::qt::set_portable_settings_root(valid_root);
    init_error.clear();
    QVERIFY(z7::platform::qt::initialize_portable_settings(&init_error));
  }

void FileManagerBehaviorTest::portableSettingsRoundTripsBinaryValuesAsTypedJson() {
    clear_runtime_settings();
    const QByteArray payload =
        QByteArray::fromHex("000102037f80ff") + QByteArray("text\0tail", 9);

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("BinaryProbe"), payload);
      settings.sync();
    }

    {
      z7::platform::qt::PortableSettings settings;
      QCOMPARE(settings.value(QStringLiteral("BinaryProbe"), QByteArray())
                   .toByteArray(),
               payload);
    }

    const QJsonObject root = read_settings_json_root();
    const QString app_name =
        QCoreApplication::applicationName().trimmed().isEmpty()
            ? QStringLiteral("7zFM")
            : QCoreApplication::applicationName();
    const QJsonObject app_json =
        root.value(QStringLiteral("apps"))
            .toObject()
            .value(app_name)
            .toObject();
    const QJsonObject encoded =
        app_json.value(QStringLiteral("BinaryProbe")).toObject();
    QCOMPARE(encoded.value(QStringLiteral("__z7_type")).toString(),
             QStringLiteral("QByteArray"));
    QCOMPARE(QByteArray::fromBase64(
                 encoded.value(QStringLiteral("base64")).toString().toLatin1()),
             payload);
  }

void FileManagerBehaviorTest::topLevelMenuOrderMatchesOriginalResource() {
    z7::ui::filemanager::MainWindow window;

    QMenuBar* menu_bar = window.menuBar();
    QVERIFY(menu_bar != nullptr);
    const QList<QAction*> top_level_actions = menu_bar->actions();
    QCOMPARE(top_level_actions.size(), 6);
    QCOMPARE(top_level_actions.at(0)->menu(), window.file_menu_);
    QCOMPARE(top_level_actions.at(1)->menu(), window.edit_menu_);
    QCOMPARE(top_level_actions.at(2)->menu(), window.view_menu_);
    QCOMPARE(top_level_actions.at(3)->menu(), window.favorites_menu_);
    QCOMPARE(top_level_actions.at(4)->menu(), window.tools_menu_);
    QCOMPARE(top_level_actions.at(5)->menu(), window.help_menu_);
}

void FileManagerBehaviorTest::toolsMenuMatchesOriginalVisibleEntriesAndKeepsBenchmark2HiddenExtension() {
    z7::ui::filemanager::MainWindow window;

    QVERIFY(window.tools_menu_ != nullptr);
    const QList<QAction*> actions = window.tools_menu_->actions();
    QVERIFY(actions.size() >= 6);
    QCOMPARE(actions.at(0), window.options_action_);
    QVERIFY(actions.at(1)->isSeparator());
    QCOMPARE(actions.at(2), window.benchmark_action_);
    QVERIFY(!actions.at(3)->isVisible());
    QCOMPARE(actions.at(3), window.benchmark2_action_);
    QVERIFY(actions.at(4)->isSeparator());
    QCOMPARE(actions.at(5), window.temp_files_action_);

    QVERIFY(window.options_action_->isEnabled());
    QVERIFY(window.benchmark_action_->isEnabled());
    QVERIFY(window.benchmark2_action_ != nullptr);
    QVERIFY(!window.benchmark2_action_->isVisible());
    QVERIFY(window.temp_files_action_->isEnabled());
  }

void FileManagerBehaviorTest::benchmark2VisibilityFollowsDiffSuperModeSetting() {
    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/Diff"), QString());

    z7::ui::filemanager::MainWindow window;
    window.refresh_action_states();
    QVERIFY(window.benchmark2_action_ != nullptr);
    QVERIFY(!window.benchmark2_action_->isVisible());

    settings.setValue(QStringLiteral("FM/Diff"),
                      QStringLiteral("\"/usr/local/bin/mydiff\" --cmp"));
    window.refresh_action_states();
    QVERIFY(window.benchmark2_action_->isVisible());
    QVERIFY(window.benchmark2_action_->isEnabled());

    settings.setValue(QStringLiteral("FM/Diff"), QString());
    window.refresh_action_states();
    QVERIFY(!window.benchmark2_action_->isVisible());
  }

void FileManagerBehaviorTest::selectCommandsFollowPanelSelectSemantics() {
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString root = temp_dir.path();

    auto create_file = [&root](const QString& name) {
      QFile file(QDir(root).filePath(name));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("x");
      file.close();
    };
    create_file(QStringLiteral("a.txt"));
    create_file(QStringLiteral("b.txt"));
    create_file(QStringLiteral("c.log"));
    create_file(QStringLiteral("d.log"));
    create_file(QStringLiteral("README"));
    QVERIFY(QDir(root).mkpath(QStringLiteral("dir1")));
    QVERIFY(QDir(root).mkpath(QStringLiteral("dir2")));

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();
    window.set_current_directory(root);
    window.refresh_directory();
    window.refresh_action_states();

    QVERIFY(window.select_action_ != nullptr);
    QVERIFY(window.deselect_action_ != nullptr);
    QVERIFY(window.select_all_action_ != nullptr);
    QVERIFY(window.deselect_all_action_ != nullptr);
    QVERIFY(window.invert_selection_action_ != nullptr);
    QVERIFY(window.select_by_type_action_ != nullptr);
    QVERIFY(window.deselect_by_type_action_ != nullptr);
    QVERIFY(row_by_name(window, QStringLiteral("..")) >= 0);

    schedule_input_dialog_submit(QStringLiteral("*.txt"), true);
    window.select_action_->trigger();
    const QStringList txt_selected = window.selected_filesystem_paths_including_parent_link();
    QCOMPARE(txt_selected.size(), 2);
    QVERIFY(txt_selected.contains(QDir(root).filePath(QStringLiteral("a.txt"))));
    QVERIFY(txt_selected.contains(QDir(root).filePath(QStringLiteral("b.txt"))));

    schedule_input_dialog_submit(QStringLiteral("a.*"), true);
    window.deselect_action_->trigger();
    const QStringList txt_after_deselect = window.selected_filesystem_paths_including_parent_link();
    QCOMPARE(txt_after_deselect.size(), 1);
    QCOMPARE(txt_after_deselect.front(), QDir(root).filePath(QStringLiteral("b.txt")));

    window.deselect_all_action_->trigger();
    QVERIFY(window.selected_filesystem_paths_including_parent_link().isEmpty());
    schedule_input_dialog_submit(QStringLiteral("*.log"), true);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Plus,
                    Qt::KeypadModifier);
    const QStringList log_selected_by_mask_shortcut = window.selected_filesystem_paths_including_parent_link();
    QCOMPARE(log_selected_by_mask_shortcut.size(), 2);
    QVERIFY(log_selected_by_mask_shortcut.contains(
        QDir(root).filePath(QStringLiteral("c.log"))));
    QVERIFY(log_selected_by_mask_shortcut.contains(
        QDir(root).filePath(QStringLiteral("d.log"))));

    schedule_input_dialog_submit(QStringLiteral("c.*"), true);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Minus,
                    Qt::KeypadModifier);
    const QStringList log_after_mask_shortcut_deselect = window.selected_filesystem_paths_including_parent_link();
    QCOMPARE(log_after_mask_shortcut_deselect.size(), 1);
    QCOMPARE(log_after_mask_shortcut_deselect.front(),
             QDir(root).filePath(QStringLiteral("d.log")));

    const int log_row = row_by_name(window, QStringLiteral("c.log"));
    QVERIFY(log_row >= 0);
    const QModelIndex log_index = window.active_panel_controller().ui.details_view->model()->index(log_row, 0);
    QVERIFY(log_index.isValid());
    window.active_panel_controller().ui.details_view->selectionModel()->clearSelection();
    window.active_panel_controller().ui.details_view->setCurrentIndex(log_index);
    window.active_panel_controller().ui.details_view->selectionModel()->select(
        log_index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    window.refresh_action_states();
    QVERIFY(window.select_by_type_action_->isEnabled());

    window.select_by_type_action_->trigger();
    const QStringList log_selected = window.selected_filesystem_paths_including_parent_link();
    QCOMPARE(log_selected.size(), 2);
    QVERIFY(log_selected.contains(QDir(root).filePath(QStringLiteral("c.log"))));
    QVERIFY(log_selected.contains(QDir(root).filePath(QStringLiteral("d.log"))));

    window.deselect_all_action_->trigger();
    QVERIFY(window.selected_filesystem_paths_including_parent_link().isEmpty());
    window.active_panel_controller().ui.details_view->setCurrentIndex(log_index);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Plus,
                    Qt::AltModifier | Qt::KeypadModifier);
    const QStringList log_selected_by_shortcut = window.selected_filesystem_paths_including_parent_link();
    QCOMPARE(log_selected_by_shortcut.size(), 2);
    QVERIFY(log_selected_by_shortcut.contains(
        QDir(root).filePath(QStringLiteral("c.log"))));
    QVERIFY(log_selected_by_shortcut.contains(
        QDir(root).filePath(QStringLiteral("d.log"))));

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Minus,
                    Qt::AltModifier | Qt::KeypadModifier);
    QVERIFY(window.selected_filesystem_paths_including_parent_link().isEmpty());

    const int dir_row = row_by_name(window, QStringLiteral("dir1"));
    QVERIFY(dir_row >= 0);
    const QModelIndex dir_index = window.active_panel_controller().ui.details_view->model()->index(dir_row, 0);
    QVERIFY(dir_index.isValid());
    window.select_all_action_->trigger();
    QVERIFY(!window.active_selected_rows_include_parent_link());
    QCOMPARE(window.selected_filesystem_paths_including_parent_link().size(), 7);
    window.active_panel_controller().ui.details_view->selectionModel()->setCurrentIndex(
        dir_index, QItemSelectionModel::Current);
    window.active_panel_controller().ui.details_view->scrollTo(dir_index);
    QCOMPARE(window.focused_path_for_panel(window.active_panel_index_),
             QDir(root).filePath(QStringLiteral("dir1")));
    window.refresh_action_states();
    QVERIFY(window.deselect_by_type_action_->isEnabled());

    window.deselect_by_type_action_->trigger();
    const QStringList after_dir_deselect = window.selected_filesystem_paths_including_parent_link();
    QVERIFY(!after_dir_deselect.contains(QDir(root).filePath(QStringLiteral("dir1"))));
    QVERIFY(!after_dir_deselect.contains(QDir(root).filePath(QStringLiteral("dir2"))));
    QVERIFY(after_dir_deselect.contains(QDir(root).filePath(QStringLiteral("a.txt"))));
    QVERIFY(after_dir_deselect.contains(QDir(root).filePath(QStringLiteral("b.txt"))));
    QVERIFY(after_dir_deselect.contains(QDir(root).filePath(QStringLiteral("c.log"))));
    QVERIFY(after_dir_deselect.contains(QDir(root).filePath(QStringLiteral("d.log"))));
    QVERIFY(after_dir_deselect.contains(QDir(root).filePath(QStringLiteral("README"))));

    window.deselect_all_action_->trigger();
    QVERIFY(!window.active_selected_rows_include_parent_link());
    QVERIFY(window.selected_filesystem_paths_including_parent_link().isEmpty());

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_A,
                    Qt::ControlModifier);
    QVERIFY(!window.active_selected_rows_include_parent_link());
    QCOMPARE(window.selected_filesystem_paths_including_parent_link().size(), 7);
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Minus,
                    Qt::ShiftModifier);
    QVERIFY(!window.active_selected_rows_include_parent_link());
    QVERIFY(window.selected_filesystem_paths_including_parent_link().isEmpty());
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Plus,
                    Qt::ShiftModifier);
    QVERIFY(!window.active_selected_rows_include_parent_link());
    QCOMPARE(window.selected_filesystem_paths_including_parent_link().size(), 7);
    window.deselect_all_action_->trigger();
    QVERIFY(window.selected_filesystem_paths_including_parent_link().isEmpty());

    window.invert_selection_action_->trigger();
    QVERIFY(!window.active_selected_rows_include_parent_link());
    const QStringList inverted_all = window.selected_filesystem_paths_including_parent_link();
    QCOMPARE(inverted_all.size(), 7);
    QVERIFY(inverted_all.contains(QDir(root).filePath(QStringLiteral("a.txt"))));
    QVERIFY(inverted_all.contains(QDir(root).filePath(QStringLiteral("b.txt"))));
    QVERIFY(inverted_all.contains(QDir(root).filePath(QStringLiteral("c.log"))));
    QVERIFY(inverted_all.contains(QDir(root).filePath(QStringLiteral("d.log"))));
    QVERIFY(inverted_all.contains(QDir(root).filePath(QStringLiteral("README"))));
    QVERIFY(inverted_all.contains(QDir(root).filePath(QStringLiteral("dir1"))));
    QVERIFY(inverted_all.contains(QDir(root).filePath(QStringLiteral("dir2"))));

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Asterisk,
                    Qt::NoModifier);
    QVERIFY(!window.active_selected_rows_include_parent_link());
    QVERIFY(window.selected_filesystem_paths_including_parent_link().isEmpty());
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Asterisk,
                    Qt::NoModifier);
    QVERIFY(!window.active_selected_rows_include_parent_link());
    QCOMPARE(window.selected_filesystem_paths_including_parent_link().size(), 7);

    const int parent_row = row_by_name(window, QStringLiteral(".."));
    const QModelIndex parent_index =
        window.active_panel_controller().ui.details_view->model()->index(parent_row, 0);
    QVERIFY(parent_index.isValid());
    window.active_panel_controller().ui.details_view->selectionModel()->select(
        parent_index,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QVERIFY(window.active_selected_rows_include_parent_link());
    window.invert_selection_action_->trigger();
    QVERIFY(!window.active_selected_rows_include_parent_link());
    QCOMPARE(window.selected_filesystem_paths_including_parent_link().size(), 7);
  }

void FileManagerBehaviorTest::optionsDialogContainsSevenTabsAndFooterButtons() {
    z7::ui::filemanager::OptionsDialog dialog;
    const auto windows_only = z7::ui::runtime_support::PlatformSupport::kWindowsOnly;
    const QString finder_tab_suffix =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        QString();
#else
        QStringLiteral(" (Windows/macOS)");
#endif

    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("optionsTabs"));
    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 7);
    QCOMPARE(tabs->tabText(0),
             z7::ui::runtime_support::with_platform_suffix_if_unsupported(
                 z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2200)),
                 windows_only));
    QCOMPARE(tabs->tabText(1),
             z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(0)) +
                 finder_tab_suffix);
    QCOMPARE(tabs->tabText(2), z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2400)));
    QCOMPARE(tabs->tabText(3), z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2103)));
    QCOMPARE(tabs->tabText(4), z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2500)));
    QCOMPARE(tabs->tabText(5), QStringLiteral("Qt"));
    QCOMPARE(tabs->tabText(6), z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2101)));

    auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
    QVERIFY(buttons != nullptr);
    QVERIFY(buttons->button(QDialogButtonBox::Ok) != nullptr);
    QVERIFY(buttons->button(QDialogButtonBox::Cancel) != nullptr);
    QVERIFY(buttons->button(QDialogButtonBox::Apply) != nullptr);
    QVERIFY(buttons->button(QDialogButtonBox::Help) != nullptr);
    QVERIFY(!buttons->button(QDialogButtonBox::Apply)->isEnabled());
  }

void FileManagerBehaviorTest::optionsDialogQtPagePersistsSharedStartupSettingsAndAppliesStyle() {
    clear_runtime_settings();

    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    QVERIFY(app != nullptr);
    const QString original_style = app->style() != nullptr ? app->style()->objectName() : QString();
    const Qt::HighDpiScaleFactorRoundingPolicy original_rounding =
        QGuiApplication::highDpiScaleFactorRoundingPolicy();

    z7::ui::filemanager::OptionsDialog dialog;
    auto* preferred = dialog.findChild<QComboBox*>(QStringLiteral("qtPreferredStyleCombo"));
    auto* hidpi = dialog.findChild<QComboBox*>(QStringLiteral("qtHiDpiPolicyCombo"));
    auto* hint = dialog.findChild<QLabel*>(QStringLiteral("qtRestartHintLabel"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
    QVERIFY(preferred != nullptr);
    QVERIFY(hidpi != nullptr);
    QVERIFY(hint != nullptr);
    QVERIFY(buttons != nullptr);
    QVERIFY(hint->text().toLower().contains(QStringLiteral("restart")));

    auto find_combo_index = [](QComboBox* combo, const QString& value) {
      if (combo == nullptr) {
        return -1;
      }
      for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toString().compare(value, Qt::CaseInsensitive) == 0) {
          return i;
        }
      }
      return -1;
    };

    QString target_style;
    const QStringList styles = z7::platform::qt::available_qt_styles();
    for (const QString& style : styles) {
      if (style.compare(original_style, Qt::CaseInsensitive) != 0) {
        target_style = style;
        break;
      }
    }
    if (target_style.isEmpty()) {
      target_style = QStringLiteral("Fusion");
    }

    const int preferred_index = find_combo_index(preferred, target_style);
    QVERIFY(preferred_index >= 0);
    preferred->setCurrentIndex(preferred_index);

    int hidpi_index =
        hidpi->findData(static_cast<int>(Qt::HighDpiScaleFactorRoundingPolicy::Round), Qt::UserRole);
    if (hidpi_index < 0) {
      hidpi_index = 0;
    }
    hidpi->setCurrentIndex(hidpi_index);

    QPushButton* apply_button = buttons->button(QDialogButtonBox::Apply);
    QVERIFY(apply_button != nullptr);
    QVERIFY(apply_button->isEnabled());

    QSignalSpy applied_spy(&dialog, SIGNAL(settings_applied()));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));
    QCOMPARE(applied_spy.count(), 1);
    QVERIFY(!apply_button->isEnabled());

    z7::platform::qt::PortableSettings shared(
        QCoreApplication::organizationName(),
        QStringLiteral("7z-shared"));
    QCOMPARE(shared.value(QStringLiteral("Qt/Startup/PreferredStyle")).toString(), target_style);
    QCOMPARE(shared.value(QStringLiteral("Qt/Startup/HiDpiRoundingPolicy")).toString(),
             QStringLiteral("round"));
    QCOMPARE(QGuiApplication::highDpiScaleFactorRoundingPolicy(), original_rounding);

    if (app->style() != nullptr &&
        target_style.compare(original_style, Qt::CaseInsensitive) != 0) {
      QVERIFY(app->style()->objectName().compare(original_style, Qt::CaseInsensitive) != 0);
    }

    z7::platform::qt::AppStartupConfig restore =
        z7::platform::qt::default_startup_config(
            z7::platform::qt::StartupAppKind::kFileManager);
    restore.preferred_style = original_style;
    z7::platform::qt::apply_post_app_startup(*app, restore);
  }

void FileManagerBehaviorTest::optionsDialogSettingsPageKeepsWindowsOnlyControlsDisabled() {
    z7::ui::filemanager::OptionsDialog dialog;
    const bool windows_supported =
        z7::ui::runtime_support::is_platform_supported(
            z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
    const bool large_pages_supported =
        z7::ui::runtime_support::large_pages_supported();

    auto* show_dots = dialog.findChild<QCheckBox*>(QStringLiteral("settingsShowDotsCheckBox"));
    auto* show_real_icons =
        dialog.findChild<QCheckBox*>(QStringLiteral("settingsShowRealIconsCheckBox"));
    auto* full_row = dialog.findChild<QCheckBox*>(QStringLiteral("settingsFullRowCheckBox"));
    auto* show_grid = dialog.findChild<QCheckBox*>(QStringLiteral("settingsShowGridCheckBox"));
    auto* single_click = dialog.findChild<QCheckBox*>(QStringLiteral("settingsSingleClickCheckBox"));
    auto* alternative =
        dialog.findChild<QCheckBox*>(QStringLiteral("settingsAlternativeSelectionCheckBox"));
    auto* show_system_menu =
        dialog.findChild<QCheckBox*>(QStringLiteral("settingsShowSystemMenuCheckBox"));
    auto* large_pages =
        dialog.findChild<QCheckBox*>(QStringLiteral("settingsUseLargePagesCheckBox"));
    auto* mem_enable =
        dialog.findChild<QCheckBox*>(QStringLiteral("settingsMemLimitEnabledCheckBox"));
    auto* mem_spin = dialog.findChild<QSpinBox*>(QStringLiteral("settingsMemLimitSpin"));
    auto* mem_suffix = dialog.findChild<QLabel*>(QStringLiteral("settingsMemLimitSuffixLabel"));

    QVERIFY(show_dots != nullptr);
    QVERIFY(show_real_icons != nullptr);
    QVERIFY(full_row != nullptr);
    QVERIFY(show_grid != nullptr);
    QVERIFY(single_click != nullptr);
    QVERIFY(alternative != nullptr);
    QVERIFY(show_system_menu != nullptr);
    QVERIFY(large_pages != nullptr);
    QVERIFY(mem_enable != nullptr);
    QVERIFY(mem_spin != nullptr);
    QVERIFY(mem_suffix != nullptr);

    QVERIFY(show_dots->isEnabled());
    QVERIFY(show_real_icons->isEnabled());
    QVERIFY(full_row->isEnabled());
    QVERIFY(show_grid->isEnabled());
    QVERIFY(single_click->isEnabled());
    QVERIFY(alternative->isEnabled());

    QVERIFY(!show_system_menu->isEnabled());
    QCOMPARE(mem_enable->isEnabled(),
             z7::ui::runtime_support::extract_memory_limit_supported());
    QCOMPARE(mem_spin->isEnabled(),
             z7::ui::runtime_support::extract_memory_limit_supported() &&
                 mem_enable->isChecked());
    if (windows_supported) {
      QCOMPARE(show_system_menu->toolTip(), QString());
      QVERIFY(!show_system_menu->text().contains(QStringLiteral("Windows")));
    } else {
      QVERIFY(show_system_menu->toolTip().contains(QStringLiteral("Windows")));
      QVERIFY(show_system_menu->text().contains(QStringLiteral("Windows")));
    }

    QCOMPARE(large_pages->isEnabled(), large_pages_supported);
    if (large_pages_supported) {
      QCOMPARE(large_pages->toolTip(), QString());
      QVERIFY(!large_pages->text().contains(QStringLiteral("macOS")));
    } else {
      QVERIFY(large_pages->toolTip().contains(QStringLiteral("macOS")));
      QVERIFY(large_pages->text().contains(QStringLiteral("macOS")));
    }
    QVERIFY(mem_suffix->text().contains(QStringLiteral("GB")));
  }

void FileManagerBehaviorTest::optionsDialogLargePagesFailureRevertsCheckboxOnSupportedMac() {
    if (!z7::ui::runtime_support::large_pages_supported()) {
      QSKIP("large pages runtime is not supported on this platform");
    }

    clear_runtime_settings();
    z7::app::set_large_pages_probe_override_for_test(0);

    z7::ui::filemanager::OptionsDialog dialog;
    auto* large_pages =
        dialog.findChild<QCheckBox*>(QStringLiteral("settingsUseLargePagesCheckBox"));
    QVERIFY(large_pages != nullptr);
    QVERIFY(large_pages->isEnabled());
    QVERIFY(!large_pages->isChecked());

    QString warning_text;
    QTimer::singleShot(0, [&warning_text]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* box = qobject_cast<QMessageBox*>(widget);
        if (box == nullptr) {
          continue;
        }
        warning_text = box->text();
        box->done(QMessageBox::Ok);
        return;
      }
    });

    large_pages->setChecked(true);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QVERIFY(!large_pages->isChecked());
    QVERIFY(!warning_text.isEmpty());
    QVERIFY(warning_text.contains(QStringLiteral("large memory pages"),
                                  Qt::CaseInsensitive));
  }

void FileManagerBehaviorTest::optionsDialogLargePagesApplyPersistsWhenSupported() {
    if (!z7::ui::runtime_support::large_pages_supported()) {
      QSKIP("large pages runtime is not supported on this platform");
    }

    clear_runtime_settings();
    z7::app::set_large_pages_probe_override_for_test(1);
    {
      z7::platform::qt::PortableSettings legacy_settings(
          QString(), QStringLiteral("7zFM"));
      legacy_settings.setValue(QStringLiteral("Options/LargePages"), true);
      legacy_settings.sync();
    }

    z7::ui::filemanager::OptionsDialog dialog;
    auto* large_pages =
        dialog.findChild<QCheckBox*>(QStringLiteral("settingsUseLargePagesCheckBox"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
    QVERIFY(large_pages != nullptr);
    QVERIFY(buttons != nullptr);
    QVERIFY(large_pages->isEnabled());
    QVERIFY(!large_pages->isChecked());

    QPushButton* apply = buttons->button(QDialogButtonBox::Apply);
    QVERIFY(apply != nullptr);
    QVERIFY(!apply->isEnabled());

    QSignalSpy applied_spy(&dialog, SIGNAL(settings_applied()));
    large_pages->setChecked(true);
    QVERIFY(apply->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));
    QCOMPARE(dialog.result(), 0);
    QCOMPARE(applied_spy.count(), 1);
    QVERIFY(!apply->isEnabled());

    z7::platform::qt::PortableSettings large_page_settings(
        QString(), QStringLiteral("7zFM"));
    QCOMPARE(large_page_settings.value(QStringLiteral("LargePages"), false).toBool(), true);
    QCOMPARE(
        large_page_settings.value(QStringLiteral("Options/LargePages"), false).toBool(),
        true);

    z7::ui::filemanager::OptionsDialog reloaded_dialog;
    auto* reloaded_checkbox =
        reloaded_dialog.findChild<QCheckBox*>(QStringLiteral("settingsUseLargePagesCheckBox"));
    QVERIFY(reloaded_checkbox != nullptr);
    QVERIFY(reloaded_checkbox->isEnabled());
    QVERIFY(reloaded_checkbox->isChecked());
  }

void FileManagerBehaviorTest::optionsDialogApplyPersistsRuntimeSettingsWithoutClosing() {
    clear_runtime_settings();
    z7::ui::filemanager::OptionsDialog dialog;

    auto* show_dots = dialog.findChild<QCheckBox*>(QStringLiteral("settingsShowDotsCheckBox"));
    auto* full_row = dialog.findChild<QCheckBox*>(QStringLiteral("settingsFullRowCheckBox"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
    QVERIFY(show_dots != nullptr);
    QVERIFY(full_row != nullptr);
    QVERIFY(buttons != nullptr);
    QPushButton* apply = buttons->button(QDialogButtonBox::Apply);
    QVERIFY(apply != nullptr);
    QVERIFY(!apply->isEnabled());

    QSignalSpy applied_spy(&dialog, SIGNAL(settings_applied()));
    show_dots->setChecked(true);
    full_row->setChecked(true);
    QVERIFY(apply->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));
    QCOMPARE(dialog.result(), 0);
    QCOMPARE(applied_spy.count(), 1);
    QVERIFY(!apply->isEnabled());

    z7::platform::qt::PortableSettings settings;
    QCOMPARE(settings.value(QStringLiteral("FM/ShowDots"), false).toBool(), true);
    QCOMPARE(settings.value(QStringLiteral("FM/FullRow"), false).toBool(), true);
    QVERIFY(!settings.contains(QStringLiteral("FM/Settings/ShowDots")));
    QVERIFY(!settings.contains(QStringLiteral("FM/Settings/FullRowSelect")));
  }

void FileManagerBehaviorTest::optionsDialogCancelKeepsRuntimeSettingsUnchanged() {
    clear_runtime_settings();
    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/ShowDots"), false);
    settings.sync();

    z7::ui::filemanager::OptionsDialog dialog;
    auto* show_dots = dialog.findChild<QCheckBox*>(QStringLiteral("settingsShowDotsCheckBox"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
    QVERIFY(show_dots != nullptr);
    QVERIFY(buttons != nullptr);
    QPushButton* apply = buttons->button(QDialogButtonBox::Apply);
    QVERIFY(apply != nullptr);

    show_dots->setChecked(true);
    QVERIFY(apply->isEnabled());
    dialog.reject();

    z7::platform::qt::PortableSettings restored;
    QCOMPARE(restored.value(QStringLiteral("FM/ShowDots"), false).toBool(), false);
  }

void FileManagerBehaviorTest::optionsDialogOkPersistsImplementedSettingsOnly() {
    clear_runtime_settings();
    z7::ui::filemanager::OptionsDialog dialog;

    auto* show_dots = dialog.findChild<QCheckBox*>(QStringLiteral("settingsShowDotsCheckBox"));
    auto* show_system_menu =
        dialog.findChild<QCheckBox*>(QStringLiteral("settingsShowSystemMenuCheckBox"));
    QVERIFY(show_dots != nullptr);
    QVERIFY(show_system_menu != nullptr);
    QVERIFY(!show_system_menu->isEnabled());

    QSignalSpy applied_spy(&dialog, SIGNAL(settings_applied()));
    show_dots->setChecked(true);

    QVERIFY(QMetaObject::invokeMethod(&dialog, "on_accept", Qt::DirectConnection));
    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(applied_spy.count(), 1);

    z7::platform::qt::PortableSettings settings;
    QCOMPARE(settings.value(QStringLiteral("FM/ShowDots"), false).toBool(), true);
    QVERIFY(!settings.contains(QStringLiteral("FM/ShowSystemMenu")));
    QVERIFY(!settings.contains(QStringLiteral("FM/Settings/ShowDots")));
    QVERIFY(!settings.contains(QStringLiteral("FM/Settings/ShowSystemMenu")));
    QVERIFY(!settings.contains(QStringLiteral("FM/Settings/UseLargePages")));
  }

void FileManagerBehaviorTest::optionsDialogEditorPageHasOriginalRowsAndButtons() {
    z7::ui::filemanager::OptionsDialog dialog;

    auto* viewer_label = dialog.findChild<QLabel*>(QStringLiteral("viewerCommandLabel"));
    auto* editor_label = dialog.findChild<QLabel*>(QStringLiteral("editorCommandLabel"));
    auto* diff_label = dialog.findChild<QLabel*>(QStringLiteral("diffCommandLabel"));
    auto* viewer_edit = dialog.findChild<QLineEdit*>(QStringLiteral("viewerCommandEdit"));
    auto* editor_edit = dialog.findChild<QLineEdit*>(QStringLiteral("editorCommandEdit"));
    auto* diff_edit = dialog.findChild<QLineEdit*>(QStringLiteral("diffCommandEdit"));
    auto* viewer_browse = dialog.findChild<QPushButton*>(QStringLiteral("viewerBrowseButton"));
    auto* editor_browse = dialog.findChild<QPushButton*>(QStringLiteral("editorBrowseButton"));
    auto* diff_browse = dialog.findChild<QPushButton*>(QStringLiteral("diffBrowseButton"));

    QVERIFY(viewer_label != nullptr);
    QVERIFY(editor_label != nullptr);
    QVERIFY(diff_label != nullptr);
    QVERIFY(viewer_edit != nullptr);
    QVERIFY(editor_edit != nullptr);
    QVERIFY(diff_edit != nullptr);
    QVERIFY(viewer_browse != nullptr);
    QVERIFY(editor_browse != nullptr);
    QVERIFY(diff_browse != nullptr);

    const QString expected_view = z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(543));
    const QString expected_editor =
        z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2104));
    const QString expected_diff =
        z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2105));

    QVERIFY(viewer_label->text().startsWith(expected_view));
    QVERIFY(viewer_label->text().endsWith(QLatin1Char(':')));
    QCOMPARE(editor_label->text(), expected_editor);
    QCOMPARE(diff_label->text(), expected_diff);
    QCOMPARE(viewer_browse->text(), QStringLiteral("..."));
    QCOMPARE(editor_browse->text(), QStringLiteral("..."));
    QCOMPARE(diff_browse->text(), QStringLiteral("..."));
  }


// End of setup.cpp

// End of setup.cpp
