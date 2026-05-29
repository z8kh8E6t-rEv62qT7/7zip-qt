// tests/filemanager/behavior/context_menu.cpp
// Role: Context menu composition and 7-Zip submenu parity checks.

#include "internal.h"

using namespace filemanager_behavior_internal;

namespace {

int index_of_action(const QList<QAction*>& actions, QAction* target) {
  for (int i = 0; i < actions.size(); ++i) {
    if (actions.at(i) == target) {
      return i;
    }
  }
  return -1;
}

QAction* find_action_by_visible_text(QMenu* menu, const QString& visible_text) {
  if (menu == nullptr) {
    return nullptr;
  }
  for (QAction* action : menu->actions()) {
    if (action == nullptr || action->isSeparator()) {
      continue;
    }
    if (without_mnemonic(action->text()) == visible_text) {
      return action;
    }
  }
  return nullptr;
}

}  // namespace

void FileManagerBehaviorTest::contextMenuIncludesSevenZipAndCoreActions() {
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

    const auto state = window.compute_seven_zip_menu_state(false);
    QVERIFY(state.visible);

    QMenu menu;
    window.populate_context_menu(&menu, state);

    const QList<QAction*> actions = menu.actions();
    QVERIFY(!actions.isEmpty());
    QVERIFY(actions.first() != nullptr);
    QVERIFY(actions.first()->menu() != nullptr);
    QCOMPARE(actions.first()->menu()->title(), QStringLiteral("7-Zip"));

    const int open_index = index_of_action(actions, window.open_action_);
    const int rename_index = index_of_action(actions, window.rename_action_);
    const int split_index = index_of_action(actions, window.split_action_);
    const int properties_index = index_of_action(actions, window.properties_action_);
    const int create_folder_index = index_of_action(actions, window.create_folder_action_);
    const int link_index = index_of_action(actions, window.link_action_);
    const int alternate_streams_index =
        index_of_action(actions, window.alternate_streams_action_);

    QVERIFY(open_index >= 0);
    QVERIFY(rename_index >= 0);
    QVERIFY(split_index >= 0);
    QVERIFY(properties_index >= 0);
    QVERIFY(create_folder_index >= 0);
    QVERIFY(link_index >= 0);
    QVERIFY(alternate_streams_index >= 0);

    QVERIFY(open_index < rename_index);
    QVERIFY(rename_index < split_index);
    QVERIFY(split_index < properties_index);
    QVERIFY(properties_index < create_folder_index);
    QVERIFY(create_folder_index < link_index);
    QVERIFY(link_index < alternate_streams_index);

    QCOMPARE(actions.at(alternate_streams_index)->text(),
             window.alternate_streams_action_->text());
    QCOMPARE(actions.at(alternate_streams_index)->toolTip(),
             window.alternate_streams_action_->toolTip());

    QAction* crc_action = nullptr;
    for (QAction* action : actions) {
      if (action != nullptr && action->menu() != nullptr &&
          action->text() == QStringLiteral("CRC")) {
        crc_action = action;
        break;
      }
    }
    QVERIFY(crc_action != nullptr);
    QVERIFY(crc_action->menu() != nullptr);
}

void FileManagerBehaviorTest::contextMenuSevenZipSubmenuTracksSelectionState() {
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

    QMenu menu;
    const auto archive_state = window.compute_seven_zip_menu_state(false);
    QVERIFY(archive_state.visible);
    window.populate_context_menu(&menu, archive_state);
    QVERIFY(!menu.actions().isEmpty());
    QMenu* seven_menu = menu.actions().first()->menu();
    QVERIFY(seven_menu != nullptr);
    QCOMPARE(seven_menu->title(), QStringLiteral("7-Zip"));

    const QString extract_files_label =
        without_mnemonic(localized_label(2323));
    const QString test_archive_label =
        without_mnemonic(localized_label(2325));

    QAction* extract_files_action = find_action_by_visible_text(seven_menu, extract_files_label);
    QAction* test_archive_action = find_action_by_visible_text(seven_menu, test_archive_label);
    QVERIFY(extract_files_action != nullptr);
    QVERIFY(test_archive_action != nullptr);
    QVERIFY(extract_files_action->isEnabled());
    QVERIFY(test_archive_action->isEnabled());

    window.active_panel_controller().ui.details_view->selectionModel()->clearSelection();
    window.refresh_action_states();
    const auto no_selection_state = window.compute_seven_zip_menu_state(false);
    QVERIFY(!no_selection_state.visible);

    QMenu menu_no_selection;
    window.populate_context_menu(&menu_no_selection, no_selection_state);
    QVERIFY(!menu_no_selection.actions().isEmpty());
    QVERIFY(menu_no_selection.actions().first() == window.open_action_);
    for (QAction* action : menu_no_selection.actions()) {
      if (action == nullptr || action->menu() == nullptr) {
        continue;
      }
      QVERIFY(action->menu()->title() != QStringLiteral("7-Zip"));
    }
}
