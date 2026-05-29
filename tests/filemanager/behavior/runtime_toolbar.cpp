// tests/filemanager/behavior/runtime_toolbar.cpp
// Role: Toolbar and selection-state behavior cases.

#include "internal.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::coreToolbarButtonsFollowSelectionAndContextRules() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString plain_file = QDir(root.path()).filePath(QStringLiteral("plain.txt"));
  {
    QFile file(plain_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("plain");
  }
  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  z7::ui::filemanager::MainWindow window;
  window.set_current_directory(root.path());
  QVERIFY(window.active_panel_controller().ui.details_view != nullptr);
  QVERIFY(window.active_panel_controller().ui.details_view->selectionModel() != nullptr);
  window.active_panel_controller().ui.details_view->selectionModel()->clearSelection();
  window.refresh_action_states();

  QVERIFY(!window.compress_action_->isEnabled());
  QVERIFY(!window.extract_action_->isEnabled());
  // Test follows original OperSmart fallback: no explicit selection still
  // tests the visible real archive candidates.
  QVERIFY(window.test_action_->isEnabled());
  QVERIFY(!window.copy_to_action_->isEnabled());
  QVERIFY(!window.move_to_action_->isEnabled());
  QVERIFY(!window.delete_action_->isEnabled());
  QVERIFY(!window.properties_action_->isEnabled());

  const int plain_row = row_by_name(window, QStringLiteral("plain.txt"));
  QVERIFY(plain_row >= 0);
  select_rows_in_active_panel(&window, {plain_row});
  window.refresh_action_states();
  QVERIFY(window.compress_action_->isEnabled());
  // Filesystem Extract/Test stay available for real files; archive validity is
  // resolved by the runner just like the original File Manager command path.
  QVERIFY(window.extract_action_->isEnabled());
  QVERIFY(window.test_action_->isEnabled());
  QVERIFY(window.copy_to_action_->isEnabled());
  QVERIFY(window.move_to_action_->isEnabled());
  QVERIFY(window.delete_action_->isEnabled());
  QVERIFY(window.properties_action_->isEnabled());

  const int archive_row = row_by_name(window, QStringLiteral("sample.7z"));
  QVERIFY(archive_row >= 0);
  select_rows_in_active_panel(&window, {archive_row});
  window.refresh_action_states();
  QVERIFY(window.compress_action_->isEnabled());
  QVERIFY(window.extract_action_->isEnabled());
  QVERIFY(window.test_action_->isEnabled());

  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QVERIFY(window.in_archive_view());
  window.refresh_action_states();
  QVERIFY(window.compress_action_->isEnabled());
  QVERIFY(window.extract_action_->isEnabled());
  QVERIFY(window.test_action_->isEnabled());
  QVERIFY(!window.copy_to_action_->isEnabled());
  QVERIFY(!window.move_to_action_->isEnabled());
  QVERIFY(!window.delete_action_->isEnabled());
  QVERIFY(window.properties_action_->isEnabled());

  const int entry_row = row_by_name(window, QStringLiteral("sample.txt"));
  QVERIFY(entry_row >= 0);
  select_rows_in_active_panel(&window, {entry_row});
  window.refresh_action_states();
  QVERIFY(window.compress_action_->isEnabled());
  QVERIFY(window.extract_action_->isEnabled());
  QVERIFY(window.test_action_->isEnabled());
  QVERIFY(window.copy_to_action_->isEnabled());
  QVERIFY(window.move_to_action_->isEnabled());
  QVERIFY(window.delete_action_->isEnabled());
  QVERIFY(window.properties_action_->isEnabled());
}
