// tests/filemanager/behavior/runtime.cpp
// Role: Runtime settings and visual behavior cases.

#include "internal.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::toolbarMenuActionsApplyImmediatelyAndPersist() {
    clear_runtime_settings();
    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/View/ShowArchiveToolbar"), false);
      settings.setValue(QStringLiteral("FM/View/ShowStandardToolbar"), false);
      settings.setValue(QStringLiteral("FM/View/ToolbarLargeButtons"), true);
      settings.setValue(QStringLiteral("FM/View/ToolbarShowButtonsText"), false);
      settings.sync();
    }

    {
      z7::ui::filemanager::MainWindow window;
      QVERIFY(window.archive_toolbar_action_->isChecked());
      QVERIFY(window.standard_toolbar_action_->isChecked());
      QVERIFY(!window.large_buttons_action_->isChecked());
      QVERIFY(window.show_buttons_text_action_->isChecked());

      window.archive_toolbar_action_->setChecked(false);
      window.standard_toolbar_action_->setChecked(true);
      window.large_buttons_action_->setChecked(true);
      window.show_buttons_text_action_->setChecked(false);
      window.apply_runtime_settings();

      QVERIFY(!window.archive_toolbar_action_->isChecked());
      QVERIFY(window.standard_toolbar_action_->isChecked());
      QVERIFY(window.large_buttons_action_->isChecked());
      QVERIFY(!window.show_buttons_text_action_->isChecked());
      QVERIFY(window.archive_toolbar_->isHidden());
      QVERIFY(!window.standard_toolbar_->isHidden());
      QCOMPARE(window.archive_toolbar_->iconSize(), QSize(24, 24));
      QCOMPARE(window.standard_toolbar_->iconSize(), QSize(24, 24));
      QCOMPARE(window.archive_toolbar_->toolButtonStyle(), Qt::ToolButtonIconOnly);
      QCOMPARE(window.standard_toolbar_->toolButtonStyle(), Qt::ToolButtonIconOnly);
    }

    {
      z7::platform::qt::PortableSettings settings;
      QCOMPARE(settings.value(QStringLiteral("FM/Toolbars")).toULongLong(), 6ULL);
      QCOMPARE(settings.value(QStringLiteral("FM/View/ShowArchiveToolbar")).toBool(),
               false);
      QCOMPARE(settings.value(QStringLiteral("FM/View/ShowStandardToolbar")).toBool(),
               false);
      QCOMPARE(settings.value(QStringLiteral("FM/View/ToolbarLargeButtons")).toBool(),
               true);
      QCOMPARE(settings.value(QStringLiteral("FM/View/ToolbarShowButtonsText")).toBool(),
               false);
    }

    z7::ui::filemanager::MainWindow restored;
    restored.load_runtime_settings();
    QCOMPARE(restored.archive_toolbar_action_->isChecked(), false);
    QCOMPARE(restored.standard_toolbar_action_->isChecked(), true);
    QCOMPARE(restored.large_buttons_action_->isChecked(), true);
    QCOMPARE(restored.show_buttons_text_action_->isChecked(), false);
  }

void FileManagerBehaviorTest::runtimeSettingsAreAppliedToMainWindowAndSingleClickOpens() {
    clear_runtime_settings();
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString child_dir = QDir(root.path()).filePath(QStringLiteral("child"));
    QVERIFY(QDir().mkpath(child_dir));
    {
      QFile file(QDir(root.path()).filePath(QStringLiteral("sample.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("runtime settings\n");
      file.close();
    }

    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/ShowDots"), true);
    settings.setValue(QStringLiteral("FM/ShowRealFileIcons"), true);
    settings.setValue(QStringLiteral("FM/FullRow"), false);
    settings.setValue(QStringLiteral("FM/ShowGrid"), true);
    settings.setValue(QStringLiteral("FM/SingleClick"), true);
    settings.setValue(QStringLiteral("FM/AlternativeSelection"), true);
    settings.setValue(QStringLiteral("FM/Settings/ShowDots"), false);
    settings.setValue(QStringLiteral("FM/Settings/ShowRealFileIcons"), false);
    settings.setValue(QStringLiteral("FM/Settings/FullRowSelect"), true);
    settings.setValue(QStringLiteral("FM/Settings/ShowGridLines"), false);
    settings.setValue(QStringLiteral("FM/Settings/SingleClickOpen"), false);
    settings.setValue(QStringLiteral("FM/Settings/AlternativeSelectionMode"), false);
    settings.sync();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    QCOMPARE(window.display_settings_.show_dots, true);
    QCOMPARE(window.display_settings_.show_real_file_icons, true);
    QCOMPARE(window.display_settings_.full_row_select, false);
    QCOMPARE(window.display_settings_.show_grid_lines, true);
    QCOMPARE(window.display_settings_.single_click_open, true);
    QCOMPARE(window.display_settings_.alternative_selection_mode, true);

    QVERIFY(row_by_name(window, QStringLiteral("..")) >= 0);
    QCOMPARE(window.panels_[0].ui.details_view->selectionMode(),
             QAbstractItemView::SingleSelection);
    QCOMPARE(window.panels_[0].ui.details_view->selectionBehavior(),
             QAbstractItemView::SelectItems);
    QVERIFY(!window.panels_[0].ui.details_view->config().style.row_hover_bg.isValid());
    QVERIFY(window.panels_[0].ui.details_view->config().style.grid_line.isValid());

    settings.setValue(QStringLiteral("FM/FullRow"), true);
    settings.sync();
    z7::ui::filemanager::MainWindow full_row_window;
    full_row_window.set_current_directory(root.path());
    QCOMPARE(full_row_window.display_settings_.full_row_select, true);
    QCOMPARE(full_row_window.panels_[0].ui.details_view->selectionMode(),
             QAbstractItemView::SingleSelection);
    QCOMPARE(full_row_window.panels_[0].ui.details_view->selectionBehavior(),
             QAbstractItemView::SelectItems);
    QVERIFY(full_row_window.panels_[0].ui.details_view->config().style.row_hover_bg.isValid());
    QVERIFY(full_row_window.panels_[0].ui.details_view->config().style.grid_line.isValid());

    const int child_row = row_by_name(window, QStringLiteral("child"));
    QVERIFY(child_row >= 0);
    const QModelIndex child_index = window.active_panel_controller().ui.details_view->model()->index(child_row, 0);
    QVERIFY(child_index.isValid());
    QVERIFY(QMetaObject::invokeMethod(window.active_panel_controller().ui.details_view,
                                      "primary_clicked",
                                      Qt::DirectConnection,
                                      Q_ARG(QModelIndex, child_index)));

    QTRY_COMPARE(QDir(window.current_directory()).absolutePath(),
                 QDir(child_dir).absolutePath());
  }

void FileManagerBehaviorTest::autoRefreshMenuDefaultToggleAndPersistenceFollowsOriginal() {
    clear_runtime_settings();
    z7::ui::filemanager::MainWindow window;

    QVERIFY(window.auto_refresh_action_ != nullptr);
    QVERIFY(window.auto_refresh_action_->isCheckable());
    QVERIFY(window.auto_refresh_action_->isChecked());
    QVERIFY(window.auto_refresh_timer_ != nullptr);
    QVERIFY(window.auto_refresh_timer_->isActive());

    window.panels_[0].runtime.auto_refresh_watched_dir =
        QStringLiteral("left-panel-binding");
    window.panels_[1].runtime.auto_refresh_watched_dir =
        QStringLiteral("right-panel-binding");

    window.auto_refresh_action_->trigger();
    QVERIFY(!window.auto_refresh_action_->isChecked());
    QVERIFY(!window.auto_refresh_timer_->isActive());
    QVERIFY(window.panels_[0].runtime.auto_refresh_watched_dir.isEmpty());
    QVERIFY(window.panels_[1].runtime.auto_refresh_watched_dir.isEmpty());

    {
      z7::platform::qt::PortableSettings settings;
      QCOMPARE(settings.value(QStringLiteral("FM/View/AutoRefresh"), true).toBool(),
               false);
    }

    z7::ui::filemanager::MainWindow restored;
    QVERIFY(restored.auto_refresh_action_ != nullptr);
    QVERIFY(!restored.auto_refresh_action_->isChecked());
    QVERIFY(restored.auto_refresh_timer_ != nullptr);
    QVERIFY(!restored.auto_refresh_timer_->isActive());

    restored.auto_refresh_action_->trigger();
    QVERIFY(restored.auto_refresh_action_->isChecked());
    QVERIFY(restored.auto_refresh_timer_->isActive());

    {
      z7::platform::qt::PortableSettings settings;
      QCOMPARE(settings.value(QStringLiteral("FM/View/AutoRefresh"), false).toBool(),
               true);
    }
}

void FileManagerBehaviorTest::autoRefreshKeepsSelectionAfterReload() {
    clear_runtime_settings();
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString a_path = QDir(root.path()).filePath(QStringLiteral("a.txt"));
    const QString b_path = QDir(root.path()).filePath(QStringLiteral("b.txt"));
    {
      QFile file(a_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("a");
    }
    {
      QFile file(b_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("b");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int a_row = row_by_name(window, QStringLiteral("a.txt"));
    QVERIFY(a_row >= 0);
    select_rows_in_active_panel(&window, {a_row});
    QCOMPARE(window.selected_filesystem_paths_including_parent_link().size(), 1);
    QCOMPARE(window.selected_filesystem_paths_including_parent_link().front(), QFileInfo(a_path).absoluteFilePath());

    QVERIFY(window.auto_refresh_action_ != nullptr);
    window.auto_refresh_action_->setChecked(true);
    window.apply_runtime_settings();
    QVERIFY(window.auto_refresh_timer_ != nullptr);
    QVERIFY(window.auto_refresh_timer_->isActive());

    {
      QFile file(b_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
      file.write(" updated");
    }
    window.mark_panel_auto_refresh_dirty(window.active_panel_index_);

    QTRY_VERIFY_WITH_TIMEOUT(window.selected_filesystem_paths_including_parent_link().contains(
                                 QFileInfo(a_path).absoluteFilePath()),
                             3000);
    QCOMPARE(window.selected_filesystem_paths_including_parent_link().size(), 1);

    window.auto_refresh_action_->setChecked(false);
    window.apply_runtime_settings();
}

void FileManagerBehaviorTest::manualRefreshShortcutReloadsFocusedPanel() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    {
      QFile file(QDir(root.path()).filePath(QStringLiteral("before.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("before");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    QVERIFY(row_by_name(window, QStringLiteral("before.txt")) >= 0);
    QCOMPARE(row_by_name(window, QStringLiteral("after.txt")), -1);

    {
      QFile file(QDir(root.path()).filePath(QStringLiteral("after.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("after");
    }
    QCOMPARE(row_by_name(window, QStringLiteral("after.txt")), -1);

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_R,
                    Qt::ControlModifier);

    QTRY_VERIFY_WITH_TIMEOUT(
        row_by_name(window, QStringLiteral("after.txt")) >= 0,
        3000);
}

void FileManagerBehaviorTest::fileIconsUseArchiveFallbackAndRemainVisibleAcrossViewModes() {
    clear_runtime_settings();
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    {
      QFile seven_z(QDir(root.path()).filePath(QStringLiteral("icon-test.7z")));
      QVERIFY(seven_z.open(QIODevice::WriteOnly | QIODevice::Truncate));
      seven_z.write("stub archive");
      seven_z.close();
    }
    {
      QFile split(QDir(root.path()).filePath(QStringLiteral("part.001")));
      QVERIFY(split.open(QIODevice::WriteOnly | QIODevice::Truncate));
      split.write("stub split");
      split.close();
    }
    {
      QFile text(QDir(root.path()).filePath(QStringLiteral("plain.txt")));
      QVERIFY(text.open(QIODevice::WriteOnly | QIODevice::Truncate));
      text.write("plain");
      text.close();
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    const QIcon seven_icon = decoration_icon_for_name(window, QStringLiteral("icon-test.7z"));
    const QIcon split_icon = decoration_icon_for_name(window, QStringLiteral("part.001"));
    const QIcon text_icon = decoration_icon_for_name(window, QStringLiteral("plain.txt"));

    QVERIFY(icon_has_pixels(seven_icon));
    QVERIFY(icon_has_pixels(split_icon));
    QVERIFY(icon_has_pixels(text_icon));
    QVERIFY(icon_matches_resource(seven_icon, QStringLiteral(":/z7/archive-icons/7z.ico")));
    QVERIFY(icon_matches_resource(split_icon, QStringLiteral(":/z7/archive-icons/split.ico")));

    window.display_settings_.show_real_file_icons = true;
    window.apply_runtime_settings();

    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("icon-test.7z"))));
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("part.001"))));
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("plain.txt"))));

    window.large_icons_action_->trigger();
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("icon-test.7z"))));
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("part.001"))));
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("plain.txt"))));

    window.small_icons_action_->trigger();
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("icon-test.7z"))));
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("part.001"))));
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("plain.txt"))));

    window.list_mode_action_->trigger();
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("icon-test.7z"))));
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("part.001"))));
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("plain.txt"))));

    window.details_mode_action_->trigger();
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("icon-test.7z"))));
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("part.001"))));
    QVERIFY(icon_has_pixels(decoration_icon_for_name(window, QStringLiteral("plain.txt"))));
  }

void FileManagerBehaviorTest::parentLinkSelectionDisablesDangerousActions() {
    clear_runtime_settings();
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    {
      QFile file(QDir(root.path()).filePath(QStringLiteral("a.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("parent link test\n");
      file.close();
    }

    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/ShowDots"), true);
    settings.sync();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int up_row = row_by_name(window, QStringLiteral(".."));
    QVERIFY(up_row >= 0);

    select_rows_in_active_panel(&window, {up_row});
    window.refresh_action_states();

    QVERIFY(window.open_action_->isEnabled());
    QVERIFY(!window.open_outside_action_->isEnabled());
    QVERIFY(!window.rename_action_->isEnabled());
    QVERIFY(!window.copy_to_action_->isEnabled());
    QVERIFY(!window.move_to_action_->isEnabled());
    QVERIFY(!window.delete_action_->isEnabled());
    QVERIFY(!window.properties_action_->isEnabled());
    QVERIFY(!window.compress_action_->isEnabled());
  }

void FileManagerBehaviorTest::filesystemParentLinkOpenAndEnterNavigateToParentLikeOriginal() {
    clear_runtime_settings();
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString parent_dir = QDir(root.path()).filePath(QStringLiteral("parent"));
    const QString child_dir = QDir(parent_dir).filePath(QStringLiteral("child"));
    QVERIFY(QDir().mkpath(child_dir));
    {
      QFile file(QDir(child_dir).filePath(QStringLiteral("inside.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("inside child\n");
    }

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();

    auto select_parent_link_in_child = [&]() {
      window.set_current_directory(child_dir);
      QTRY_COMPARE(QDir(window.current_directory()).absolutePath(),
                   QDir(child_dir).absolutePath());
      const int up_row = row_by_name(window, QStringLiteral(".."));
      QVERIFY(up_row >= 0);
      select_rows_in_active_panel(&window, {up_row});
      window.refresh_action_states();
      QVERIFY(window.open_action_->isEnabled());
      QVERIFY(window.active_selected_rows_include_parent_link());
    };
    auto verify_parent_with_child_focused = [&]() {
      QTRY_COMPARE(QDir(window.current_directory()).absolutePath(),
                   QDir(parent_dir).absolutePath());
      QCOMPARE(window.selected_filesystem_paths_including_parent_link(),
               QStringList{QFileInfo(child_dir).absoluteFilePath()});
      const QModelIndex current =
          window.active_panel_controller().ui.details_view->currentIndex();
      QVERIFY(current.isValid());
      QCOMPARE(current.data(Qt::DisplayRole).toString(), QStringLiteral("child"));
    };

    select_parent_link_in_child();
    window.on_open_requested();
    verify_parent_with_child_focused();

    select_parent_link_in_child();
    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Return);
    verify_parent_with_child_focused();
  }

void FileManagerBehaviorTest::filesystemMixedParentLinkOpenUsesFocusedOperatedRulesLikeOriginal() {
    clear_runtime_settings();
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString parent_dir = QDir(root.path()).filePath(QStringLiteral("parent"));
    const QString child_dir = QDir(parent_dir).filePath(QStringLiteral("child"));
    const QString sub_dir = QDir(child_dir).filePath(QStringLiteral("sub"));
    QVERIFY(QDir().mkpath(sub_dir));
    const QString note_path = QDir(child_dir).filePath(QStringLiteral("note.txt"));
    {
      QFile file(note_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("note");
    }

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();

    auto enter_child = [&]() {
      window.set_current_directory(child_dir);
      QTRY_COMPARE(QDir(window.current_directory()).absolutePath(),
                   QDir(child_dir).absolutePath());
    };
    auto select_parent_and_sub = [&]() {
      const int parent_row = row_by_name(window, QStringLiteral(".."));
      const int sub_row = row_by_name(window, QStringLiteral("sub"));
      QVERIFY(parent_row >= 0);
      QVERIFY(sub_row >= 0);
      select_rows_in_active_panel(&window, {parent_row, sub_row});
      const QModelIndex sub_index =
          window.active_panel_controller().ui.details_view->model()->index(sub_row, 0);
      QVERIFY(sub_index.isValid());
      window.active_panel_controller().ui.details_view->selectionModel()
          ->setCurrentIndex(sub_index, QItemSelectionModel::NoUpdate);
    };

    enter_child();
    select_parent_and_sub();
    window.on_open_requested();
    QTRY_COMPARE(QDir(window.current_directory()).absolutePath(),
                 QDir(sub_dir).absolutePath());

    enter_child();
    const int parent_row = row_by_name(window, QStringLiteral(".."));
    const int sub_row = row_by_name(window, QStringLiteral("sub"));
    QVERIFY(parent_row >= 0);
    QVERIFY(sub_row >= 0);
    select_rows_in_active_panel(&window, {parent_row, sub_row});
    const QModelIndex parent_index =
        window.active_panel_controller().ui.details_view->model()->index(parent_row, 0);
    QVERIFY(parent_index.isValid());
    window.active_panel_controller().ui.details_view->selectionModel()
        ->setCurrentIndex(parent_index, QItemSelectionModel::NoUpdate);
    window.on_open_requested();
    QTRY_COMPARE(QDir(window.current_directory()).absolutePath(),
                 QDir(parent_dir).absolutePath());
    QCOMPARE(window.selected_filesystem_paths_including_parent_link(),
             QStringList{QFileInfo(child_dir).absoluteFilePath()});

    enter_child();
    const int note_row = row_by_name(window, QStringLiteral("note.txt"));
    const int parent_row_for_outside = row_by_name(window, QStringLiteral(".."));
    QVERIFY(note_row >= 0);
    QVERIFY(parent_row_for_outside >= 0);
    select_rows_in_active_panel(&window, {parent_row_for_outside, note_row});
    const QModelIndex parent_outside_index =
        window.active_panel_controller().ui.details_view->model()->index(
            parent_row_for_outside, 0);
    QVERIFY(parent_outside_index.isValid());
    window.active_panel_controller().ui.details_view->selectionModel()
        ->setCurrentIndex(parent_outside_index, QItemSelectionModel::NoUpdate);

    QStringList launched;
    window.external_opener_ = [&launched](const QString& path) {
      launched << QFileInfo(path).absoluteFilePath();
      return true;
    };
    window.on_open_outside_requested();
    QCOMPARE(launched, QStringList{QFileInfo(note_path).absoluteFilePath()});
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(child_dir).absolutePath());
  }

// End of runtime.cpp

// End of runtime.cpp
