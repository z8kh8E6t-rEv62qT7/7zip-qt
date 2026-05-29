// tests/filemanager/behavior/setup_favorites.cpp
// Role: Favorites and help/link behavior cases.

#include "internal.h"

#include "main_window/model/model.h"

#include <QKeyEvent>

using namespace filemanager_behavior_internal;

namespace {

#if defined(Q_OS_MACOS)
constexpr quint32 kMacRightControlNativeModifier = 0x00002000;
#endif

bool dispatch_panel_key(z7::ui::filemanager::MainWindow* window,
                        int key,
                        Qt::KeyboardModifiers modifiers,
                        quint32 native_modifiers = 0) {
    auto& panel = window->active_panel_controller();
    auto* target = panel.ui.details_view;
    if (target == nullptr) {
      return false;
    }
    QKeyEvent event(QEvent::KeyPress,
                    key,
                    modifiers,
                    0,
                    0,
                    native_modifiers,
                    QString());
    return window->eventFilter(target, &event);
}

}  // namespace

void FileManagerBehaviorTest::addToFavoritesPersistsSlotAndOpensSavedFolder() {
    clear_runtime_settings();

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir_a = QDir(root.path()).filePath(QStringLiteral("A"));
    const QString dir_b = QDir(root.path()).filePath(QStringLiteral("B"));
    QVERIFY(QDir().mkpath(dir_a));
    QVERIFY(QDir().mkpath(dir_b));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(dir_a);
    window.refresh_action_states();

    window.rebuild_favorites_menu();
    QVERIFY(window.add_to_favorites_menu_ != nullptr);
    QCOMPARE(window.add_to_favorites_menu_->actions().size(), 10);
    const QList<QAction*> initial_favorites_actions =
        window.favorites_menu_->actions();
    QCOMPARE(initial_favorites_actions.size(), 12);
    QCOMPARE(initial_favorites_actions.at(0), window.add_to_favorites_menu_->menuAction());
    QVERIFY(initial_favorites_actions.at(1)->isSeparator());
    for (int slot = 0; slot < 10; ++slot) {
      QAction* action = window.add_to_favorites_menu_->actions().at(slot);
      QCOMPARE(action->data().toInt(), slot);
      QVERIFY(action->text().contains(QString::number(slot)));

      QAction* open_slot = initial_favorites_actions.at(slot + 2);
      QCOMPARE(open_slot->data().toString(), QString());
      QVERIFY(!open_slot->isEnabled());
      QVERIFY(open_slot->text().contains(QStringLiteral("-")));
    }

    QAction* slot_action = nullptr;
    for (QAction* action : window.add_to_favorites_menu_->actions()) {
      if (action != nullptr && action->data().toInt() == 3) {
        slot_action = action;
        break;
      }
    }
    QVERIFY(slot_action != nullptr);
    slot_action->trigger();

    z7::platform::qt::PortableSettings settings;
    const QStringList shortcuts =
        settings.value(QStringLiteral("FM/FolderShortcuts")).toStringList();
    QCOMPARE(shortcuts.size(), 10);
    QCOMPARE(shortcuts.at(3), QDir(dir_a).absolutePath());
    QVERIFY(!settings.contains(QStringLiteral("FM/Favorites/Slot3")));

    window.rebuild_favorites_menu();
    const QList<QAction*> populated_favorites_actions =
        window.favorites_menu_->actions();
    QCOMPARE(populated_favorites_actions.size(), 12);
    QCOMPARE(populated_favorites_actions.at(0),
             window.add_to_favorites_menu_->menuAction());
    QVERIFY(populated_favorites_actions.at(1)->isSeparator());

    QAction* open_action = nullptr;
    for (int slot = 0; slot < 10; ++slot) {
      QAction* action = populated_favorites_actions.at(slot + 2);
      if (slot == 3) {
        QCOMPARE(action->data().toString(), QDir(dir_a).absolutePath());
        QVERIFY(action->isEnabled());
        QVERIFY(action->text().contains(QStringLiteral("3")));
        open_action = action;
      } else {
        QCOMPARE(action->data().toString(), QString());
        QVERIFY(!action->isEnabled());
        QVERIFY(action->text().contains(QStringLiteral("-")));
      }
    }
    QVERIFY(open_action != nullptr);

    window.set_current_directory(dir_b);
    QCOMPARE(QDir(window.current_directory()).absolutePath(), QDir(dir_b).absolutePath());
    open_action->trigger();
    QCOMPARE(QDir(window.current_directory()).absolutePath(), QDir(dir_a).absolutePath());

    settings.clear();
    settings.setValue(QStringLiteral("FM/Favorites/Slot3"),
                      QDir(dir_a).absolutePath());
    window.rebuild_favorites_menu();
    const QList<QAction*> legacy_favorites_actions =
        window.favorites_menu_->actions();
    QCOMPARE(legacy_favorites_actions.size(), 12);
    for (int slot = 0; slot < 10; ++slot) {
      QAction* action = legacy_favorites_actions.at(slot + 2);
      QCOMPARE(action->data().toString(), QString());
      QVERIFY(!action->isEnabled());
    }
}

void FileManagerBehaviorTest::favoriteDigitShortcutsSetAndOpenSlotsLikeOriginal() {
    clear_runtime_settings();

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir_a = QDir(root.path()).filePath(QStringLiteral("A"));
    const QString dir_b = QDir(root.path()).filePath(QStringLiteral("B"));
    const QString dir_c = QDir(root.path()).filePath(QStringLiteral("C"));
    QVERIFY(QDir().mkpath(dir_a));
    QVERIFY(QDir().mkpath(dir_b));
    QVERIFY(QDir().mkpath(dir_c));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(dir_a);
    window.refresh_action_states();

    QVERIFY(dispatch_panel_key(&window,
                               Qt::Key_7,
                               Qt::AltModifier | Qt::ShiftModifier));
    z7::platform::qt::PortableSettings settings;
    QStringList shortcuts =
        settings.value(QStringLiteral("FM/FolderShortcuts")).toStringList();
    QCOMPARE(shortcuts.size(), 10);
    QCOMPARE(shortcuts.at(7), QDir(dir_a).absolutePath());

    window.set_current_directory(dir_b);
    QVERIFY(dispatch_panel_key(&window, Qt::Key_7, Qt::AltModifier));
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(dir_a).absolutePath());

    window.set_current_directory(dir_c);
    QVERIFY(dispatch_panel_key(&window,
                               Qt::Key_5,
                               Qt::ControlModifier | Qt::ShiftModifier));
    shortcuts = settings.value(QStringLiteral("FM/FolderShortcuts")).toStringList();
    QCOMPARE(shortcuts.at(5), QDir(dir_c).absolutePath());

    window.set_current_directory(dir_b);
    QVERIFY(dispatch_panel_key(&window, Qt::Key_5, Qt::ControlModifier));
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(dir_c).absolutePath());

#if defined(Q_OS_MACOS)
    window.set_current_directory(dir_a);
    QVERIFY(dispatch_panel_key(&window,
                               Qt::Key_1,
                               Qt::ControlModifier | Qt::ShiftModifier,
                               kMacRightControlNativeModifier));
    shortcuts = settings.value(QStringLiteral("FM/FolderShortcuts")).toStringList();
    QCOMPARE(shortcuts.at(1), QDir(dir_a).absolutePath());

    window.set_current_directory(dir_b);
    QVERIFY(dispatch_panel_key(&window,
                               Qt::Key_1,
                               Qt::ControlModifier,
                               kMacRightControlNativeModifier));
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(dir_a).absolutePath());
#endif

    window.set_current_directory(dir_b);
    auto& panel = window.active_panel_controller();
    window.on_view_mode_action_triggered(
        z7::ui::filemanager::MainWindow::PanelController::kViewModeDetails);
    QVERIFY(dispatch_panel_key(&window, Qt::Key_1, Qt::ControlModifier));
    QCOMPARE(panel.view_mode,
             z7::ui::filemanager::MainWindow::PanelController::kViewModeLargeIcons);
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(dir_b).absolutePath());
}

void FileManagerBehaviorTest::favoritesStoreAndOpenArchiveFolderPrefixesLikeOriginal() {
    clear_runtime_settings();

    QTemporaryDir root;
    QVERIFY(root.isValid());
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

    const QString expected_prefix =
        window.active_panel_controller().archive_virtual_display_path();
    QVERIFY(expected_prefix.contains(QStringLiteral("nested.7z")));
    QVERIFY(expected_prefix.endsWith(QStringLiteral("top")));

    window.rebuild_favorites_menu();
    QVERIFY(window.add_to_favorites_menu_ != nullptr);
    QAction* set_slot = window.add_to_favorites_menu_->actions().at(4);
    QVERIFY(set_slot != nullptr);
    QCOMPARE(set_slot->data().toInt(), 4);
    set_slot->trigger();

    z7::platform::qt::PortableSettings settings;
    const QStringList shortcuts =
        settings.value(QStringLiteral("FM/FolderShortcuts")).toStringList();
    QCOMPARE(shortcuts.size(), 10);
    QCOMPARE(shortcuts.at(4), expected_prefix);

    window.on_open_parent_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir, QString());

    window.rebuild_favorites_menu();
    const QList<QAction*> favorite_actions = window.favorites_menu_->actions();
    QVERIFY(favorite_actions.size() >= 6);
    QAction* open_slot = favorite_actions.at(4 + 2);
    QVERIFY(open_slot != nullptr);
    QVERIFY(open_slot->isEnabled());
    QCOMPARE(open_slot->data().toString(), expected_prefix);
    open_slot->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(window.active_panel_controller().archive.source_archive,
             QFileInfo(archive_path).absoluteFilePath());
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("top"));
    QVERIFY(row_by_name(window, QStringLiteral("inner")) >= 0);
}

void FileManagerBehaviorTest::helpMenuMatchesOriginalOrderAndContentsIsQtPlaceholder() {
    z7::ui::filemanager::MainWindow window;

    QVERIFY(window.help_menu_ != nullptr);
    QVERIFY(window.contents_action_ != nullptr);
    QVERIFY(window.about_action_ != nullptr);

    const QList<QAction*> actions = window.help_menu_->actions();
    QVERIFY(actions.size() >= 3);
    QCOMPARE(actions.at(0), window.contents_action_);
    QVERIFY(actions.at(1)->isSeparator());
    QCOMPARE(actions.at(2), window.about_action_);

    QCOMPARE(window.contents_action_->shortcut(), QKeySequence(Qt::Key_F1));
    QVERIFY(!window.contents_action_->isEnabled());
    QVERIFY(window.about_action_->isEnabled());
}

void FileManagerBehaviorTest::linkDialogShowsOriginalModesWithOnlySymlinksEnabled() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString selected_file = QDir(root.path()).filePath(QStringLiteral("selected.txt"));
    QFile f(selected_file);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("selected");
    f.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.refresh_directory();

    const int selected_row = row_by_name(window, QStringLiteral("selected.txt"));
    QVERIFY(selected_row >= 0);
    select_rows_in_active_panel(&window, {selected_row});
    window.refresh_action_states();
    QVERIFY(window.link_action_ != nullptr);
    QVERIFY(window.link_action_->isEnabled());

    bool saw_dialog = false;
    schedule_link_dialog_interaction([&](QDialog* dialog) {
      saw_dialog = true;
      QCOMPARE(dialog->windowTitle(),
               z7::ui::runtime_support::strip_mnemonic(
                   z7::ui::runtime_support::L(558)));

      auto* from_combo = dialog->findChild<QComboBox*>(QStringLiteral("linkFromCombo"));
      auto* to_combo = dialog->findChild<QComboBox*>(QStringLiteral("linkToCombo"));
      auto* hard = dialog->findChild<QRadioButton*>(QStringLiteral("linkTypeHardRadio"));
      auto* file_symlink =
          dialog->findChild<QRadioButton*>(QStringLiteral("linkTypeFileSymlinkRadio"));
      auto* dir_symlink =
          dialog->findChild<QRadioButton*>(QStringLiteral("linkTypeDirectorySymlinkRadio"));
      auto* junction =
          dialog->findChild<QRadioButton*>(QStringLiteral("linkTypeJunctionRadio"));
      auto* wsl = dialog->findChild<QRadioButton*>(QStringLiteral("linkTypeWslRadio"));
      auto* buttons =
          dialog->findChild<QDialogButtonBox*>(QStringLiteral("linkDialogButtons"));
      QVERIFY(from_combo != nullptr);
      QVERIFY(to_combo != nullptr);
      QVERIFY(hard != nullptr);
      QVERIFY(file_symlink != nullptr);
      QVERIFY(dir_symlink != nullptr);
      QVERIFY(junction != nullptr);
      QVERIFY(wsl != nullptr);
      QVERIFY(buttons != nullptr);

      QVERIFY(from_combo->currentText().contains(QStringLiteral("selected.txt.link")));
      QCOMPARE(QDir::fromNativeSeparators(to_combo->currentText()),
               QFileInfo(selected_file).absoluteFilePath());

      QVERIFY(!hard->isEnabled());
      QVERIFY(file_symlink->isEnabled());
      QVERIFY(dir_symlink->isEnabled());
      QVERIFY(!junction->isEnabled());
      QVERIFY(!wsl->isEnabled());
      QVERIFY(file_symlink->isChecked());
      QVERIFY(buttons->button(QDialogButtonBox::Cancel) != nullptr);
      dialog->reject();
    });

    window.link_action_->trigger();
    QVERIFY(saw_dialog);
}

void FileManagerBehaviorTest::commentActionUsesFocusedItemAndLinkActionUsesSelection() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString selected_file = QDir(root.path()).filePath(QStringLiteral("selected.txt"));
    const QString focused_file = QDir(root.path()).filePath(QStringLiteral("focused.txt"));
    for (const QString& path : {selected_file, focused_file}) {
      QFile f(path);
      QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
      f.write(QFileInfo(path).fileName().toUtf8());
      f.close();
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.refresh_directory();

    const int selected_row = row_by_name(window, QStringLiteral("selected.txt"));
    const int focused_row = row_by_name(window, QStringLiteral("focused.txt"));
    QVERIFY(selected_row >= 0);
    QVERIFY(focused_row >= 0);
    select_rows_in_active_panel(&window, {selected_row});
    QItemSelectionModel* selection =
        window.active_panel_controller().ui.details_view->selectionModel();
    QVERIFY(selection != nullptr);
    const QModelIndex focused_index =
        window.active_panel_controller().ui.details_view->model()->index(focused_row, 0);
    QVERIFY(focused_index.isValid());
    selection->setCurrentIndex(focused_index, QItemSelectionModel::NoUpdate);
    window.refresh_action_states();

    QVERIFY(window.comment_action_ != nullptr);
    QVERIFY(window.link_action_ != nullptr);
    QVERIFY(window.comment_action_->isEnabled());
    QVERIFY(window.link_action_->isEnabled());
    QCOMPARE(capability_reason(window.comment_action_), QString());
    QCOMPARE(window.focused_path_for_panel(window.active_panel_index_), focused_file);
    QCOMPARE(window.selected_filesystem_paths_including_parent_link(), QStringList{selected_file});

    schedule_input_dialog_submit(QStringLiteral("focused comment"), true);
    window.comment_action_->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    window.refresh_directory();
    auto comment_for_name = [&window](const QString& name) {
      const int row = row_by_name(window, name);
      if (row < 0) {
        return QStringLiteral("<missing>");
      }
      return window.active_panel_controller()
          .model->data(
              window.active_panel_controller().model->index(
                  row,
                  z7::ui::filemanager::DirectoryListModel::kCommentColumn),
              Qt::DisplayRole)
          .toString();
    };
    QTRY_COMPARE_WITH_TIMEOUT(comment_for_name(QStringLiteral("focused.txt")),
                              QStringLiteral("focused comment"),
                              20000);
    QCOMPARE(comment_for_name(QStringLiteral("selected.txt")), QString());

    const int refreshed_selected_row = row_by_name(window, QStringLiteral("selected.txt"));
    QVERIFY(refreshed_selected_row >= 0);
    select_rows_in_active_panel(&window, {refreshed_selected_row});
    window.refresh_action_states();
    QVERIFY(window.link_action_->isEnabled());
    QCOMPARE(capability_reason(window.link_action_), QString());

    const QString link_path = QDir(root.path()).filePath(QStringLiteral("selected.link"));
    bool saw_link_dialog = false;
    schedule_link_dialog_interaction([&](QDialog* dialog) {
      saw_link_dialog = true;
      auto* from_combo = dialog->findChild<QComboBox*>(QStringLiteral("linkFromCombo"));
      auto* to_combo = dialog->findChild<QComboBox*>(QStringLiteral("linkToCombo"));
      auto* file_symlink =
          dialog->findChild<QRadioButton*>(QStringLiteral("linkTypeFileSymlinkRadio"));
      if (from_combo == nullptr || to_combo == nullptr || file_symlink == nullptr) {
        dialog->reject();
        return;
      }
      from_combo->setEditText(QDir::toNativeSeparators(link_path));
      to_combo->setEditText(QDir::toNativeSeparators(selected_file));
      file_symlink->setChecked(true);
      if (QPushButton* create =
              dialog->findChild<QPushButton*>(QStringLiteral("linkCreateButton"))) {
        create->click();
      } else {
        dialog->reject();
      }
    });
    window.link_action_->trigger();
    QVERIFY(saw_link_dialog);

    QFileInfo link_info(link_path);
    QVERIFY(link_info.exists());
    QVERIFY(link_info.isSymLink() || link_info.isFile());
}
