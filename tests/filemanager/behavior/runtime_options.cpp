// tests/filemanager/behavior/runtime_options.cpp
// Role: Options and language behavior cases.

#include "internal.h"

#include "extract_memory_settings.h"
#include "options/internal.h"
#include "shell_integration_menu.h"
#include "shared/external_command_parser.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::externalCommandParserKeepsUnquotedExistingPathWithSpaces() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString tool_dir =
        QDir(root.path()).filePath(QStringLiteral("tools with spaces"));
    QVERIFY(QDir().mkpath(tool_dir));
    const QString tool_path =
        QDir(tool_dir).filePath(QStringLiteral("viewer tool"));
    {
      QFile file(tool_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("#!/bin/sh\n");
      file.close();
    }

    const z7::ui::filemanager::ExternalCommandParts parsed =
        z7::ui::filemanager::parse_external_command_line(
            tool_path + QStringLiteral(" --line \"two words\""));
    QCOMPARE(parsed.program, tool_path);
    QCOMPARE(parsed.arguments, QStringLiteral("--line \"two words\""));

    const QString rebuilt =
        z7::ui::filemanager::options_internal::rebuild_command_line_with_program(
            tool_path + QStringLiteral(" --line \"two words\""),
            QStringLiteral("/tmp/replacement viewer"));
    QCOMPARE(rebuilt, QStringLiteral("\"/tmp/replacement viewer\" --line \"two words\""));
}

void FileManagerBehaviorTest::optionsDialogEditorApplyPersistsWithoutClosing() {
    clear_runtime_settings();

    z7::ui::filemanager::OptionsDialog dialog;
    auto* viewer_edit = dialog.findChild<QLineEdit*>(QStringLiteral("viewerCommandEdit"));
    auto* editor_edit = dialog.findChild<QLineEdit*>(QStringLiteral("editorCommandEdit"));
    auto* diff_edit = dialog.findChild<QLineEdit*>(QStringLiteral("diffCommandEdit"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
    QVERIFY(viewer_edit != nullptr);
    QVERIFY(editor_edit != nullptr);
    QVERIFY(diff_edit != nullptr);
    QVERIFY(buttons != nullptr);
    QVERIFY(buttons->button(QDialogButtonBox::Apply) != nullptr);

    viewer_edit->setText(QStringLiteral("/tmp/viewer-app"));
    editor_edit->setText(QStringLiteral("/tmp/editor-app"));
    diff_edit->setText(QStringLiteral("/tmp/diff-app"));
    QVERIFY(buttons->button(QDialogButtonBox::Apply)->isEnabled());

    buttons->button(QDialogButtonBox::Apply)->click();
    QVERIFY(!buttons->button(QDialogButtonBox::Apply)->isEnabled());
    QCOMPARE(dialog.result(), 0);

    z7::platform::qt::PortableSettings settings;
    QCOMPARE(settings.value(QStringLiteral("FM/Viewer")).toString(),
             QStringLiteral("/tmp/viewer-app"));
    QCOMPARE(settings.value(QStringLiteral("FM/Editor")).toString(),
             QStringLiteral("/tmp/editor-app"));
    QCOMPARE(settings.value(QStringLiteral("FM/Diff")).toString(),
             QStringLiteral("/tmp/diff-app"));
  }

void FileManagerBehaviorTest::optionsDialogEditorOkAndCancelSemantics() {
    clear_runtime_settings();

    {
      z7::ui::filemanager::OptionsDialog dialog;
      auto* viewer_edit = dialog.findChild<QLineEdit*>(QStringLiteral("viewerCommandEdit"));
      auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
      QVERIFY(viewer_edit != nullptr);
      QVERIFY(buttons != nullptr);
      viewer_edit->setText(QStringLiteral("/tmp/cancel-should-not-save"));
      buttons->button(QDialogButtonBox::Cancel)->click();
      QCOMPARE(dialog.result(), QDialog::Rejected);
    }

    {
      z7::platform::qt::PortableSettings settings;
      QCOMPARE(settings.value(QStringLiteral("FM/Viewer"), QString()).toString(), QString());
    }

    {
      z7::ui::filemanager::OptionsDialog dialog;
      auto* viewer_edit = dialog.findChild<QLineEdit*>(QStringLiteral("viewerCommandEdit"));
      auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
      QVERIFY(viewer_edit != nullptr);
      QVERIFY(buttons != nullptr);
      viewer_edit->setText(QStringLiteral("/tmp/applied-value"));
      buttons->button(QDialogButtonBox::Apply)->click();
      viewer_edit->setText(QStringLiteral("/tmp/unapplied-value"));
      buttons->button(QDialogButtonBox::Cancel)->click();
      QCOMPARE(dialog.result(), QDialog::Rejected);
    }

    {
      z7::platform::qt::PortableSettings settings;
      QCOMPARE(settings.value(QStringLiteral("FM/Viewer")).toString(),
               QStringLiteral("/tmp/applied-value"));
    }

    {
      z7::ui::filemanager::OptionsDialog dialog;
      auto* viewer_edit = dialog.findChild<QLineEdit*>(QStringLiteral("viewerCommandEdit"));
      auto* editor_edit = dialog.findChild<QLineEdit*>(QStringLiteral("editorCommandEdit"));
      auto* diff_edit = dialog.findChild<QLineEdit*>(QStringLiteral("diffCommandEdit"));
      auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
      QVERIFY(viewer_edit != nullptr);
      QVERIFY(editor_edit != nullptr);
      QVERIFY(diff_edit != nullptr);
      QVERIFY(buttons != nullptr);
      viewer_edit->setText(QStringLiteral("/tmp/ok-viewer"));
      editor_edit->setText(QStringLiteral("/tmp/ok-editor"));
      diff_edit->setText(QStringLiteral("/tmp/ok-diff"));
      buttons->button(QDialogButtonBox::Ok)->click();
      QCOMPARE(dialog.result(), QDialog::Accepted);
    }

    {
      z7::platform::qt::PortableSettings settings;
      QCOMPARE(settings.value(QStringLiteral("FM/Viewer")).toString(),
               QStringLiteral("/tmp/ok-viewer"));
      QCOMPARE(settings.value(QStringLiteral("FM/Editor")).toString(),
               QStringLiteral("/tmp/ok-editor"));
      QCOMPARE(settings.value(QStringLiteral("FM/Diff")).toString(),
               QStringLiteral("/tmp/ok-diff"));
    }
  }

void FileManagerBehaviorTest::optionsDialogFoldersPageUsesWorkingDirModeControls() {
    z7::ui::filemanager::OptionsDialog dialog;

    auto* system_radio = dialog.findChild<QRadioButton*>(QStringLiteral("foldersWorkSystemRadio"));
    auto* current_radio = dialog.findChild<QRadioButton*>(QStringLiteral("foldersWorkCurrentRadio"));
    auto* specified_radio =
        dialog.findChild<QRadioButton*>(QStringLiteral("foldersWorkSpecifiedRadio"));
    auto* path_edit = dialog.findChild<QLineEdit*>(QStringLiteral("foldersWorkPathEdit"));
    auto* browse_button =
        dialog.findChild<QPushButton*>(QStringLiteral("foldersWorkPathBrowseButton"));
    auto* removable_only =
        dialog.findChild<QCheckBox*>(QStringLiteral("foldersWorkRemovableOnlyCheckBox"));
    auto* group = dialog.findChild<QGroupBox*>(QStringLiteral("foldersWorkGroup"));

    QVERIFY(system_radio != nullptr);
    QVERIFY(current_radio != nullptr);
    QVERIFY(specified_radio != nullptr);
    QVERIFY(path_edit != nullptr);
    QVERIFY(browse_button != nullptr);
    QVERIFY(removable_only != nullptr);
    QVERIFY(group != nullptr);

    QVERIFY(!group->title().toLower().contains(QStringLiteral("placeholder")));
    QVERIFY(system_radio->isChecked());
    QVERIFY(!path_edit->isEnabled());
    QVERIFY(!browse_button->isEnabled());
    QVERIFY(removable_only->isChecked());

    specified_radio->setChecked(true);
    QVERIFY(path_edit->isEnabled());
    QVERIFY(browse_button->isEnabled());

    current_radio->setChecked(true);
    QVERIFY(!path_edit->isEnabled());
    QVERIFY(!browse_button->isEnabled());
  }

void FileManagerBehaviorTest::optionsDialogFoldersPagePersistsWorkingDirSettings() {
    clear_runtime_settings();

    const QString expected_path =
        QDir::cleanPath(QDir::tempPath() + QStringLiteral("/z7-work-dir"));
    {
      z7::ui::filemanager::OptionsDialog dialog;
      auto* specified_radio =
          dialog.findChild<QRadioButton*>(QStringLiteral("foldersWorkSpecifiedRadio"));
      auto* path_edit = dialog.findChild<QLineEdit*>(QStringLiteral("foldersWorkPathEdit"));
      auto* removable_only =
          dialog.findChild<QCheckBox*>(QStringLiteral("foldersWorkRemovableOnlyCheckBox"));
      QVERIFY(specified_radio != nullptr);
      QVERIFY(path_edit != nullptr);
      QVERIFY(removable_only != nullptr);

      specified_radio->setChecked(true);
      path_edit->setText(expected_path);
      removable_only->setChecked(false);
      QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));
    }

    z7::platform::qt::PortableSettings settings;
    QCOMPARE(settings.value(QStringLiteral("Options/WorkDirType")).toInt(), 2);
    QCOMPARE(settings.value(QStringLiteral("Options/WorkDirPath")).toString(), expected_path);
    QCOMPARE(settings.value(QStringLiteral("Options/TempRemovableOnly")).toBool(), false);
    QVERIFY(!settings.contains(QStringLiteral("FM/Folders/WorkDirType")));
    QVERIFY(!settings.contains(QStringLiteral("FM/Folders/WorkDirPath")));
    QVERIFY(!settings.contains(QStringLiteral("FM/Folders/ForRemovableOnly")));

    {
      z7::ui::filemanager::OptionsDialog dialog;
      auto* specified_radio =
          dialog.findChild<QRadioButton*>(QStringLiteral("foldersWorkSpecifiedRadio"));
      auto* path_edit = dialog.findChild<QLineEdit*>(QStringLiteral("foldersWorkPathEdit"));
      auto* removable_only =
          dialog.findChild<QCheckBox*>(QStringLiteral("foldersWorkRemovableOnlyCheckBox"));
      QVERIFY(specified_radio != nullptr);
      QVERIFY(path_edit != nullptr);
      QVERIFY(removable_only != nullptr);
      QVERIFY(specified_radio->isChecked());
      QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(path_edit->text())), expected_path);
      QCOMPARE(removable_only->isChecked(), false);
    }
  }

void FileManagerBehaviorTest::optionsDialogFoldersPagePersistsExtractMemoryLimitSettings() {
    clear_runtime_settings();

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("Options/ExtractMemLimitEnabled"), true);
      settings.setValue(QStringLiteral("Options/ExtractMemLimitGB"), 9);
    }

    {
      z7::ui::filemanager::OptionsDialog dialog;
      auto* enabled =
          dialog.findChild<QCheckBox*>(QStringLiteral("settingsMemLimitEnabledCheckBox"));
      auto* spin = dialog.findChild<QSpinBox*>(QStringLiteral("settingsMemLimitSpin"));
      QVERIFY(enabled != nullptr);
      QVERIFY(spin != nullptr);
      QCOMPARE(enabled->isChecked(), false);
      QCOMPARE(spin->isEnabled(), false);
    }

    if (!z7::ui::runtime_support::extract_memory_limit_supported()) {
      QSKIP("Extract memory limit persistence is only active on supported platforms");
    }

    clear_runtime_settings();

    {
      z7::ui::filemanager::OptionsDialog dialog;
      auto* enabled =
          dialog.findChild<QCheckBox*>(QStringLiteral("settingsMemLimitEnabledCheckBox"));
      auto* spin = dialog.findChild<QSpinBox*>(QStringLiteral("settingsMemLimitSpin"));
      auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
      QVERIFY(enabled != nullptr);
      QVERIFY(spin != nullptr);
      QVERIFY(buttons != nullptr);

      const int stored_limit = qBound(spin->minimum(), 6, spin->maximum());
      enabled->setChecked(true);
      QVERIFY(spin->isEnabled());
      spin->setValue(stored_limit);
      QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));

      z7::platform::qt::PortableSettings settings;
      QCOMPARE(settings.value(QStringLiteral("Extraction/MemLimit")).toInt(), stored_limit);
      QVERIFY(!settings.contains(QStringLiteral("Options/ExtractMemLimitEnabled")));
      QVERIFY(!settings.contains(QStringLiteral("Options/ExtractMemLimitGB")));
    }

    {
      z7::ui::filemanager::OptionsDialog dialog;
      auto* enabled =
          dialog.findChild<QCheckBox*>(QStringLiteral("settingsMemLimitEnabledCheckBox"));
      auto* spin = dialog.findChild<QSpinBox*>(QStringLiteral("settingsMemLimitSpin"));
      QVERIFY(enabled != nullptr);
      QVERIFY(spin != nullptr);
      QVERIFY(enabled->isChecked());
      QCOMPARE(spin->value(),
               z7::ui::runtime_support::clamp_extract_memory_limit_gb(
                   6, z7::ui::runtime_support::detect_total_ram_bytes()));

      enabled->setChecked(false);
      QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));
    }

    z7::platform::qt::PortableSettings settings;
    QVERIFY(!settings.contains(QStringLiteral("Extraction/MemLimit")));
    QVERIFY(!settings.contains(QStringLiteral("Options/ExtractMemLimitEnabled")));
    QVERIFY(!settings.contains(QStringLiteral("Options/ExtractMemLimitGB")));
  }

void FileManagerBehaviorTest::optionsDialogSystemAndSevenZipPagesHaveStaticShellContent() {
    z7::ui::filemanager::OptionsDialog dialog;
    const bool windows_supported =
        z7::ui::runtime_support::is_platform_supported(
            z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
    const bool finder_supported =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        true;
#else
        false;
#endif

    auto* associations =
        dialog.findChild<QTableWidget*>(QStringLiteral("systemAssociationsTable"));
    QVERIFY(associations != nullptr);
    QCOMPARE(associations->columnCount(), 3);
    QVERIFY(associations->rowCount() > 20);

    bool has_7z = false;
    bool has_zip = false;
    bool has_rar = false;
    for (int row = 0; row < associations->rowCount(); ++row) {
      const QTableWidgetItem* item = associations->item(row, 0);
      if (item == nullptr) {
        continue;
      }
      const QString ext = item->text();
      has_7z = has_7z || ext.compare(QStringLiteral("7z"), Qt::CaseInsensitive) == 0;
      has_zip = has_zip || ext.compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0;
      has_rar = has_rar || ext.compare(QStringLiteral("rar"), Qt::CaseInsensitive) == 0;
    }
    QVERIFY(has_7z);
    QVERIFY(has_zip);
    QVERIFY(has_rar);

    auto* zone_combo = dialog.findChild<QComboBox*>(QStringLiteral("zoneIdPolicyCombo"));
    QVERIFY(zone_combo != nullptr);
    QVERIFY(zone_combo->count() >= 1);
    QCOMPARE(zone_combo->isEnabled(), windows_supported);

    auto* context_items =
        dialog.findChild<QListWidget*>(QStringLiteral("contextMenuItemsList"));
    QVERIFY(context_items != nullptr);
    QVERIFY(context_items->count() >= 10);
    QCOMPARE(context_items->isEnabled(), finder_supported);

    auto* current_user_button =
        dialog.findChild<QPushButton*>(QStringLiteral("currentUserAddButton"));
    auto* all_users_button =
        dialog.findChild<QPushButton*>(QStringLiteral("allUsersAddButton"));
    auto* integrate_shell =
        dialog.findChild<QCheckBox*>(QStringLiteral("integrateShellCheckBox"));
    auto* integrate_shell_32 =
        dialog.findChild<QCheckBox*>(QStringLiteral("integrateShell32CheckBox"));
    auto* cascaded_menu =
        dialog.findChild<QCheckBox*>(QStringLiteral("cascadedMenuCheckBox"));
    auto* menu_icons =
        dialog.findChild<QCheckBox*>(QStringLiteral("menuIconsCheckBox"));
    auto* eliminate_dup =
        dialog.findChild<QCheckBox*>(QStringLiteral("eliminateDupRootsCheckBox"));
    QVERIFY(current_user_button != nullptr);
    QVERIFY(all_users_button != nullptr);
    QVERIFY(integrate_shell != nullptr);
    QVERIFY(integrate_shell_32 != nullptr);
    QVERIFY(cascaded_menu != nullptr);
    QVERIFY(menu_icons != nullptr);
    QVERIFY(eliminate_dup != nullptr);

    QCOMPARE(current_user_button->isEnabled(), windows_supported);
    QCOMPARE(all_users_button->isEnabled(), windows_supported);
    QCOMPARE(integrate_shell->isEnabled(), finder_supported);
    QCOMPARE(integrate_shell_32->isEnabled(), windows_supported);
    QCOMPARE(cascaded_menu->isEnabled(), finder_supported);
    QCOMPARE(menu_icons->isEnabled(), finder_supported);
    QCOMPARE(eliminate_dup->isEnabled(), finder_supported);

    const QString open_archive_menu_item = finder_supported
                                               ? (z7::ui::runtime_support::strip_mnemonic(
                                                      z7::ui::runtime_support::L(2322)) +
                                                  QStringLiteral(" >"))
                                               : (z7::ui::runtime_support::strip_mnemonic(
                                                      z7::ui::runtime_support::L(2322)) +
                                                  QStringLiteral(" > (Windows/macOS)"));
    const QString crc_sha_menu_item = finder_supported
                                          ? QStringLiteral("CRC SHA >")
                                          : QStringLiteral("CRC SHA > (Windows/macOS)");
    const QString archive_label = z7::ui::runtime_support::strip_mnemonic(
        z7::ui::runtime_support::L(2321));
    const QString compress_email_item =
        z7::ui::runtime_support::strip_mnemonic(
            z7::ui::runtime_support::L(2329)) +
        QStringLiteral(" (Unsupported)");
    const QString compress_7z_email_item =
        z7::ui::runtime_support::strip_mnemonic(
            z7::ui::runtime_support::LF(
                2330, {archive_label + QStringLiteral(".7z")})) +
        QStringLiteral(" (Unsupported)");
    const QString compress_zip_email_item =
        z7::ui::runtime_support::strip_mnemonic(
            z7::ui::runtime_support::LF(
                2330, {archive_label + QStringLiteral(".zip")})) +
        QStringLiteral(" (Unsupported)");
    const auto verify_disabled_email_placeholder =
        [](const QListWidgetItem* item) {
          QVERIFY(item != nullptr);
          QVERIFY(item->data(Qt::UserRole).toString().isEmpty());
          QVERIFY(!item->flags().testFlag(Qt::ItemIsEnabled));
          QVERIFY(!item->flags().testFlag(Qt::ItemIsSelectable));
          QVERIFY(!item->flags().testFlag(Qt::ItemIsUserCheckable));
          QCOMPARE(item->checkState(), Qt::Unchecked);
          QVERIFY(item->toolTip().contains(QStringLiteral("Not supported")));
        };

    bool has_checkable_item = false;
    bool has_open_archive = false;
    bool has_crc_sha = false;
    bool has_compress_email = false;
    bool has_compress_7z_email = false;
    bool has_compress_zip_email = false;
    for (int i = 0; i < context_items->count(); ++i) {
      const QListWidgetItem* item = context_items->item(i);
      if (item == nullptr) {
        continue;
      }
      has_checkable_item = has_checkable_item || item->flags().testFlag(Qt::ItemIsUserCheckable);
      has_open_archive = has_open_archive || item->text() == open_archive_menu_item;
      has_crc_sha = has_crc_sha || item->text() == crc_sha_menu_item;
      if (item->text() == compress_email_item) {
        has_compress_email = true;
        verify_disabled_email_placeholder(item);
      }
      if (item->text() == compress_7z_email_item) {
        has_compress_7z_email = true;
        verify_disabled_email_placeholder(item);
      }
      if (item->text() == compress_zip_email_item) {
        has_compress_zip_email = true;
        verify_disabled_email_placeholder(item);
      }
    }

    QVERIFY(has_checkable_item);
    QVERIFY(has_open_archive);
    QVERIFY(has_crc_sha);
    QVERIFY(has_compress_email);
    QVERIFY(has_compress_7z_email);
    QVERIFY(has_compress_zip_email);

    if (!windows_supported) {
      QVERIFY(zone_combo->toolTip().contains(QStringLiteral("Windows")));
    }
    if (!finder_supported) {
      QVERIFY(context_items->toolTip().contains(QStringLiteral("macOS")));
      QVERIFY(integrate_shell->text().contains(QStringLiteral("macOS")));
      QVERIFY(cascaded_menu->text().contains(QStringLiteral("macOS")));
    }
  }

void FileManagerBehaviorTest::optionsDialogSevenZipPagePersistsSettingsAndCheckStates() {
    clear_runtime_settings();

    z7::ui::filemanager::OptionsDialog dialog;
    auto* integrate_shell = dialog.findChild<QCheckBox*>(QStringLiteral("integrateShellCheckBox"));
    auto* integrate_shell_32 =
        dialog.findChild<QCheckBox*>(QStringLiteral("integrateShell32CheckBox"));
    auto* cascaded_menu = dialog.findChild<QCheckBox*>(QStringLiteral("cascadedMenuCheckBox"));
    auto* menu_icons = dialog.findChild<QCheckBox*>(QStringLiteral("menuIconsCheckBox"));
    auto* eliminate_dup_roots =
        dialog.findChild<QCheckBox*>(QStringLiteral("eliminateDupRootsCheckBox"));
    auto* zone_combo = dialog.findChild<QComboBox*>(QStringLiteral("zoneIdPolicyCombo"));
    auto* context_items = dialog.findChild<QListWidget*>(QStringLiteral("contextMenuItemsList"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
    QVERIFY(integrate_shell != nullptr);
    QVERIFY(integrate_shell_32 != nullptr);
    QVERIFY(cascaded_menu != nullptr);
    QVERIFY(menu_icons != nullptr);
    QVERIFY(eliminate_dup_roots != nullptr);
    QVERIFY(zone_combo != nullptr);
    QVERIFY(context_items != nullptr);
    QVERIFY(buttons != nullptr);
    QVERIFY(context_items->count() > 0);
    const QStringList default_visible_actions =
        z7::shell_integration::default_shell_integration_visible_actions();
    QVERIFY(context_items->count() >= default_visible_actions.size());

    QPushButton* apply_button = buttons->button(QDialogButtonBox::Apply);
    QVERIFY(apply_button != nullptr);
    QVERIFY(!apply_button->isEnabled());

    integrate_shell->setChecked(false);
    integrate_shell_32->setChecked(false);
    cascaded_menu->setChecked(false);
    menu_icons->setChecked(true);
    eliminate_dup_roots->setChecked(false);
    zone_combo->setCurrentIndex(std::min(2, zone_combo->count() - 1));
    context_items->item(0)->setCheckState(Qt::Unchecked);
    QVERIFY(apply_button->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));
    QVERIFY(!apply_button->isEnabled());

    z7::platform::qt::PortableSettings settings;
    QCOMPARE(settings.value(QStringLiteral("Options/IntegrateToShellMenu")).toBool(), false);
    QCOMPARE(settings.value(QStringLiteral("Options/IntegrateToShellMenu32")).toBool(), false);
    QCOMPARE(settings.value(QStringLiteral("Options/CascadedMenu")).toBool(), false);
    QCOMPARE(settings.value(QStringLiteral("Options/MenuIcons")).toBool(), true);
    QCOMPARE(settings.value(QStringLiteral("Options/ElimDupExtract")).toBool(), false);
    QCOMPARE(settings.value(QStringLiteral("Options/WriteZoneIdExtract")).toInt(),
             zone_combo->currentIndex());

    QVERIFY(!settings.contains(QStringLiteral("Options/ContextMenuChecks")));
    QVERIFY(!settings.contains(QStringLiteral("Options/ContextMenuVisibleActions")));
    QCOMPARE(
        settings.value(QStringLiteral("Options/ContextMenu")).toULongLong(),
        static_cast<qulonglong>(
            z7::shell_integration::
                shell_integration_context_menu_flags_from_visible_actions(
                    default_visible_actions.mid(1))));

    z7::ui::filemanager::OptionsDialog dialog2;
    auto* integrate_shell2 =
        dialog2.findChild<QCheckBox*>(QStringLiteral("integrateShellCheckBox"));
    auto* integrate_shell_322 =
        dialog2.findChild<QCheckBox*>(QStringLiteral("integrateShell32CheckBox"));
    auto* cascaded_menu2 = dialog2.findChild<QCheckBox*>(QStringLiteral("cascadedMenuCheckBox"));
    auto* menu_icons2 = dialog2.findChild<QCheckBox*>(QStringLiteral("menuIconsCheckBox"));
    auto* eliminate_dup_roots2 =
        dialog2.findChild<QCheckBox*>(QStringLiteral("eliminateDupRootsCheckBox"));
    auto* zone_combo2 = dialog2.findChild<QComboBox*>(QStringLiteral("zoneIdPolicyCombo"));
    auto* context_items2 = dialog2.findChild<QListWidget*>(QStringLiteral("contextMenuItemsList"));
    auto* buttons2 = dialog2.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
    QVERIFY(integrate_shell2 != nullptr);
    QVERIFY(integrate_shell_322 != nullptr);
    QVERIFY(cascaded_menu2 != nullptr);
    QVERIFY(menu_icons2 != nullptr);
    QVERIFY(eliminate_dup_roots2 != nullptr);
    QVERIFY(zone_combo2 != nullptr);
    QVERIFY(context_items2 != nullptr);
    QVERIFY(buttons2 != nullptr);
    QVERIFY(!integrate_shell2->isChecked());
    QVERIFY(!integrate_shell_322->isChecked());
    QVERIFY(!cascaded_menu2->isChecked());
    QVERIFY(menu_icons2->isChecked());
    QVERIFY(!eliminate_dup_roots2->isChecked());
    QCOMPARE(zone_combo2->currentIndex(), zone_combo->currentIndex());
    for (int i = 0; i < context_items2->count(); ++i) {
      QListWidgetItem* item = context_items2->item(i);
      QVERIFY(item != nullptr);
      const QString action_id = item->data(Qt::UserRole).toString();
      if (action_id.isEmpty()) {
        QCOMPARE(item->checkState(), Qt::Unchecked);
        continue;
      }
      QCOMPARE(item->checkState(),
               action_id == default_visible_actions.front() ? Qt::Unchecked
                                                            : Qt::Checked);
    }

    for (int i = 0; i < context_items2->count(); ++i) {
      context_items2->item(i)->setCheckState(Qt::Unchecked);
    }
    QPushButton* apply_button2 = buttons2->button(QDialogButtonBox::Apply);
    QVERIFY(apply_button2 != nullptr);
    QVERIFY(apply_button2->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(&dialog2, "on_apply", Qt::DirectConnection));
    QVERIFY(!apply_button2->isEnabled());
    QVERIFY(settings.contains(QStringLiteral("Options/ContextMenu")));
    QCOMPARE(settings.value(QStringLiteral("Options/ContextMenu")).toULongLong(),
             qulonglong{0});

    z7::ui::filemanager::OptionsDialog dialog3;
    auto* context_items3 = dialog3.findChild<QListWidget*>(QStringLiteral("contextMenuItemsList"));
    QVERIFY(context_items3 != nullptr);
    for (int i = 0; i < context_items3->count(); ++i) {
      QListWidgetItem* item = context_items3->item(i);
      QVERIFY(item != nullptr);
      QCOMPARE(item->checkState(), Qt::Unchecked);
    }

    settings.clear();
    settings.setValue(QStringLiteral("Options/ContextMenuVisibleActions"),
                      QStringList({default_visible_actions.front()}));
    z7::ui::filemanager::OptionsDialog legacy_dialog;
    auto* legacy_context_items =
        legacy_dialog.findChild<QListWidget*>(QStringLiteral("contextMenuItemsList"));
    QVERIFY(legacy_context_items != nullptr);
    for (int i = 0; i < legacy_context_items->count(); ++i) {
      QListWidgetItem* item = legacy_context_items->item(i);
      QVERIFY(item != nullptr);
      if (item->data(Qt::UserRole).toString().isEmpty()) {
        QCOMPARE(item->checkState(), Qt::Unchecked);
        continue;
      }
      QCOMPARE(item->checkState(), Qt::Checked);
    }
  }

void FileManagerBehaviorTest::optionsDialogFoldersPagePersistsWorkDirSettings() {
    clear_runtime_settings();

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/Folders/WorkDirType"), 2);
      settings.setValue(QStringLiteral("FM/Folders/WorkDirPath"),
                        QStringLiteral("/tmp/legacy-work-dir"));
      settings.setValue(QStringLiteral("FM/Folders/ForRemovableOnly"), false);
    }
    {
      z7::ui::filemanager::OptionsDialog dialog;
      auto* system_radio =
          dialog.findChild<QRadioButton*>(QStringLiteral("foldersWorkSystemRadio"));
      auto* path_edit = dialog.findChild<QLineEdit*>(QStringLiteral("foldersWorkPathEdit"));
      auto* removable_only =
          dialog.findChild<QCheckBox*>(QStringLiteral("foldersWorkRemovableOnlyCheckBox"));
      QVERIFY(system_radio != nullptr);
      QVERIFY(path_edit != nullptr);
      QVERIFY(removable_only != nullptr);
      QVERIFY(system_radio->isChecked());
      QVERIFY(path_edit->text().isEmpty());
      QVERIFY(removable_only->isChecked());
    }
    clear_runtime_settings();

    z7::ui::filemanager::OptionsDialog dialog;
    auto* system_radio = dialog.findChild<QRadioButton*>(QStringLiteral("foldersWorkSystemRadio"));
    auto* current_radio = dialog.findChild<QRadioButton*>(QStringLiteral("foldersWorkCurrentRadio"));
    auto* specified_radio =
        dialog.findChild<QRadioButton*>(QStringLiteral("foldersWorkSpecifiedRadio"));
    auto* path_edit = dialog.findChild<QLineEdit*>(QStringLiteral("foldersWorkPathEdit"));
    auto* removable_only =
        dialog.findChild<QCheckBox*>(QStringLiteral("foldersWorkRemovableOnlyCheckBox"));
    auto* browse_button =
        dialog.findChild<QPushButton*>(QStringLiteral("foldersWorkPathBrowseButton"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
    QVERIFY(system_radio != nullptr);
    QVERIFY(current_radio != nullptr);
    QVERIFY(specified_radio != nullptr);
    QVERIFY(path_edit != nullptr);
    QVERIFY(removable_only != nullptr);
    QVERIFY(browse_button != nullptr);
    QVERIFY(buttons != nullptr);

    QVERIFY(system_radio->isChecked());
    QVERIFY(!current_radio->isChecked());
    QVERIFY(!specified_radio->isChecked());
    QVERIFY(!path_edit->isEnabled());
    QVERIFY(!browse_button->isEnabled());
    QVERIFY(removable_only->isChecked());

    specified_radio->setChecked(true);
    QVERIFY(path_edit->isEnabled());
    QVERIFY(browse_button->isEnabled());
    path_edit->setText(QStringLiteral("/tmp/z7-work-dir"));
    removable_only->setChecked(false);

    QPushButton* apply_button = buttons->button(QDialogButtonBox::Apply);
    QVERIFY(apply_button != nullptr);
    QVERIFY(apply_button->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));
    QVERIFY(!apply_button->isEnabled());

    z7::platform::qt::PortableSettings settings;
    QCOMPARE(settings.value(QStringLiteral("Options/WorkDirType")).toInt(), 2);
    QCOMPARE(settings.value(QStringLiteral("Options/WorkDirPath")).toString(),
             QStringLiteral("/tmp/z7-work-dir"));
    QCOMPARE(settings.value(QStringLiteral("Options/TempRemovableOnly")).toBool(), false);
    QVERIFY(!settings.contains(QStringLiteral("FM/Folders/WorkDirType")));
    QVERIFY(!settings.contains(QStringLiteral("FM/Folders/WorkDirPath")));
    QVERIFY(!settings.contains(QStringLiteral("FM/Folders/ForRemovableOnly")));

    z7::ui::filemanager::OptionsDialog dialog2;
    auto* specified_radio2 =
        dialog2.findChild<QRadioButton*>(QStringLiteral("foldersWorkSpecifiedRadio"));
    auto* path_edit2 = dialog2.findChild<QLineEdit*>(QStringLiteral("foldersWorkPathEdit"));
    auto* removable_only2 =
        dialog2.findChild<QCheckBox*>(QStringLiteral("foldersWorkRemovableOnlyCheckBox"));
    QVERIFY(specified_radio2 != nullptr);
    QVERIFY(path_edit2 != nullptr);
    QVERIFY(removable_only2 != nullptr);
    QVERIFY(specified_radio2->isChecked());
    QCOMPARE(path_edit2->text(), QStringLiteral("/tmp/z7-work-dir"));
    QVERIFY(!removable_only2->isChecked());
  }

// End of runtime_options.cpp
