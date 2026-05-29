// tests/filemanager/behavior/startup_open.cpp
// Role: Startup target and app-level open request routing behavior.

#include "internal.h"

#include <QFileOpenEvent>

using namespace filemanager_behavior_internal;

namespace {

void close_main_windows(QVector<z7::ui::filemanager::MainWindow*>* windows) {
  if (windows == nullptr) {
    return;
  }
  for (z7::ui::filemanager::MainWindow* window : *windows) {
    if (window == nullptr) {
      continue;
    }
    window->close();
    window->deleteLater();
  }
  windows->clear();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void wait_for_archive_view(z7::ui::filemanager::MainWindow* window,
                           const QString& archive_path) {
  QVERIFY(window != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(*window) == nullptr, 20000);
  QVERIFY(window->in_archive_view());
  QCOMPARE(window->active_panel_controller().archive.source_archive, archive_path);
}

QString duplicate_archive(const QString& archive_path,
                          const QString& duplicated_file_name) {
  const QFileInfo info(archive_path);
  const QString duplicated_path =
      QDir(info.absolutePath()).filePath(duplicated_file_name);
  QFile::remove(duplicated_path);
  if (!QFile::copy(archive_path, duplicated_path)) {
    return QString();
  }
  return duplicated_path;
}

}  // namespace

void FileManagerBehaviorTest::startupOpenArgumentsTreatTaskIpcTokensAsUnknown() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to create sample archive");

  const QStringList ipc_only_args = {
      QStringLiteral("7zFM"),
      QStringLiteral("--task-ipc-session=11"),
      QStringLiteral("--task-ipc-generation=7"),
      QStringLiteral("--ignored-non-ipc-token")
  };
  const auto ipc_only =
      z7::apps::filemanager::parse_startup_open_arguments(ipc_only_args);
  QVERIFY(!ipc_only.has_recognized_open_args);
  QVERIFY(ipc_only.requests.isEmpty());

  const QStringList ordinary_only_args = {
      QStringLiteral("7zFM"),
      QStringLiteral("-t7z"),
      archive_path
  };
  const auto ordinary_only =
      z7::apps::filemanager::parse_startup_open_arguments(ordinary_only_args);
  QVERIFY(ordinary_only.has_recognized_open_args);
  QCOMPARE(ordinary_only.requests.size(), 1);
  QCOMPARE(ordinary_only.requests.front().path, archive_path);
  QCOMPARE(ordinary_only.requests.front().type_hint, QStringLiteral("7z"));

  const QStringList task_ipc_tokens_with_type_only_args = {
      QStringLiteral("7zFM"),
      QStringLiteral("--task-ipc-session=11"),
      QStringLiteral("--task-ipc-generation=7"),
      QStringLiteral("-t7z")
  };
  const auto task_ipc_tokens_with_type_only =
      z7::apps::filemanager::parse_startup_open_arguments(
          task_ipc_tokens_with_type_only_args);
  QVERIFY(task_ipc_tokens_with_type_only.has_recognized_open_args);
  QVERIFY(task_ipc_tokens_with_type_only.requests.isEmpty());

  const QStringList task_ipc_tokens_with_path_args = {
      QStringLiteral("7zFM"),
      QStringLiteral("--task-ipc-session=11"),
      QStringLiteral("--task-ipc-generation=7"),
      archive_path
  };
  const auto task_ipc_tokens_with_path =
      z7::apps::filemanager::parse_startup_open_arguments(
          task_ipc_tokens_with_path_args);
  QVERIFY(task_ipc_tokens_with_path.has_recognized_open_args);
  QCOMPARE(task_ipc_tokens_with_path.requests.size(), 1);
  QCOMPARE(task_ipc_tokens_with_path.requests.front().path, archive_path);
  QVERIFY(task_ipc_tokens_with_path.requests.front().type_hint.isEmpty());

  const QStringList task_ipc_tokens_with_type_and_path_args = {
      QStringLiteral("7zFM"),
      QStringLiteral("--task-ipc-session=11"),
      QStringLiteral("--task-ipc-generation=7"),
      QStringLiteral("-t7z"),
      archive_path
  };
  const auto task_ipc_tokens_with_type_and_path =
      z7::apps::filemanager::parse_startup_open_arguments(
          task_ipc_tokens_with_type_and_path_args);
  QVERIFY(task_ipc_tokens_with_type_and_path.has_recognized_open_args);
  QCOMPARE(task_ipc_tokens_with_type_and_path.requests.size(), 1);
  QCOMPARE(task_ipc_tokens_with_type_and_path.requests.front().path, archive_path);
  QCOMPARE(task_ipc_tokens_with_type_and_path.requests.front().type_hint,
           QStringLiteral("7z"));

  const QStringList ignored_unknown_args = {
      QStringLiteral("7zFM"),
      QStringLiteral("--not-a-real-arg")
  };
  const auto ignored_unknown =
      z7::apps::filemanager::parse_startup_open_arguments(ignored_unknown_args);
  QVERIFY(!ignored_unknown.has_recognized_open_args);
  QVERIFY(ignored_unknown.requests.isEmpty());
}

void FileManagerBehaviorTest::openStartupTargetAppliesArchiveDirectoryAndParentFallbackRules() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to create sample archive");

  const QString startup_dir = QDir(root.path()).filePath(QStringLiteral("startup-dir"));
  QVERIFY(QDir().mkpath(startup_dir));

  const QString plain_parent_dir =
      QDir(root.path()).filePath(QStringLiteral("plain-parent"));
  QVERIFY(QDir().mkpath(plain_parent_dir));
  const QString plain_file_path =
      QDir(plain_parent_dir).filePath(QStringLiteral("notes.txt"));
  QFile plain_file(plain_file_path);
  QVERIFY(plain_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  plain_file.write("plain text");
  plain_file.close();

  const QString fallback_dir = QDir(root.path()).filePath(QStringLiteral("fallback"));
  QVERIFY(QDir().mkpath(fallback_dir));

  {
    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(fallback_dir);
    window.open_startup_target(archive_path);
    wait_for_archive_view(&window, archive_path);
  }

  {
    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(fallback_dir);
    window.open_startup_target(startup_dir);
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(startup_dir).absolutePath());
    QVERIFY(!window.in_archive_view());
  }

  {
    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(fallback_dir);
    window.open_startup_target(
        QDir(root.path()).filePath(QStringLiteral("missing.7z")));
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(fallback_dir).absolutePath());
    QVERIFY(!window.in_archive_view());
  }

  {
    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(fallback_dir);
    window.open_startup_target(plain_file_path);
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(plain_parent_dir).absolutePath());
    QVERIFY(!window.in_archive_view());
  }
}

void FileManagerBehaviorTest::startupRestoresSavedPanelPathsOnEmptyLaunch() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString left = QDir(root.path()).filePath(QStringLiteral("left"));
  const QString right = QDir(root.path()).filePath(QStringLiteral("right"));
  QVERIFY(QDir().mkpath(left));
  QVERIFY(QDir().mkpath(right));

  {
    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/View/PanelPath0"), root.path());
    settings.setValue(QStringLiteral("FM/View/PanelPath1"), root.path());
    settings.setValue(QStringLiteral("FM/PanelPath0"), left);
    settings.setValue(QStringLiteral("FM/PanelPath1"), right);
    settings.sync();
  }

  z7::ui::filemanager::MainWindow window;
  QCOMPARE(QDir(window.current_directory_for_panel(0)).absolutePath(),
           QDir(left).absolutePath());
  QCOMPARE(QDir(window.current_directory_for_panel(1)).absolutePath(),
           QDir(right).absolutePath());
  QVERIFY(window.panels_[1].ui.container != nullptr);
  QVERIFY(!window.panels_[1].ui.container->isVisible());
  QVERIFY(window.folder_history_.contains(QDir(left).absolutePath()));
  QVERIFY(window.folder_history_.contains(QDir(right).absolutePath()));
}

void FileManagerBehaviorTest::startupTargetsOverrideRestoredPanelPaths() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString restored_dir = QDir(root.path()).filePath(QStringLiteral("restored"));
  const QString other_panel_dir = QDir(root.path()).filePath(QStringLiteral("other-panel"));
  const QString startup_dir = QDir(root.path()).filePath(QStringLiteral("startup-dir"));
  const QString plain_parent_dir = QDir(root.path()).filePath(QStringLiteral("plain-parent"));
  QVERIFY(QDir().mkpath(restored_dir));
  QVERIFY(QDir().mkpath(other_panel_dir));
  QVERIFY(QDir().mkpath(startup_dir));
  QVERIFY(QDir().mkpath(plain_parent_dir));

  const QString plain_file_path = QDir(plain_parent_dir).filePath(QStringLiteral("notes.txt"));
  QFile plain_file(plain_file_path);
  QVERIFY(plain_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  plain_file.write("plain text");
  plain_file.close();

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to create sample archive");

  auto seed_panel_paths = [&]() {
    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/PanelPath0"), restored_dir);
    settings.setValue(QStringLiteral("FM/PanelPath1"), other_panel_dir);
    settings.sync();
  };

  {
    seed_panel_paths();
    z7::ui::filemanager::MainWindow window;
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(restored_dir).absolutePath());
    window.open_startup_target(startup_dir);
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(startup_dir).absolutePath());
    QCOMPARE(QDir(window.current_directory_for_panel(1)).absolutePath(),
             QDir(other_panel_dir).absolutePath());
    QVERIFY(!window.in_archive_view());
  }

  {
    seed_panel_paths();
    z7::ui::filemanager::MainWindow window;
    window.open_startup_target(plain_file_path);
    QCOMPARE(QDir(window.current_directory()).absolutePath(),
             QDir(plain_parent_dir).absolutePath());
    QVERIFY(!window.in_archive_view());
  }

  {
    seed_panel_paths();
    z7::ui::filemanager::MainWindow window;
    window.open_startup_target(archive_path);
    wait_for_archive_view(&window, archive_path);
    QCOMPARE(QDir(window.current_directory_for_panel(1)).absolutePath(),
             QDir(other_panel_dir).absolutePath());
  }
}

void FileManagerBehaviorTest::startupPanelPathFallsBackToNearestExistingAncestor() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString existing_parent = QDir(root.path()).filePath(QStringLiteral("existing"));
  QVERIFY(QDir().mkpath(existing_parent));
  const QString missing_leaf =
      QDir(existing_parent).filePath(QStringLiteral("missing/leaf"));

  {
    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("FM/PanelPath0"), missing_leaf);
    settings.sync();
  }

  z7::ui::filemanager::MainWindow window;
  QCOMPARE(QDir(window.current_directory()).absolutePath(),
           QDir(existing_parent).absolutePath());
}

void FileManagerBehaviorTest::shutdownFromArchiveViewPersistsOuterFilesystemPanelPath() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_nested_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare nested archive");
  const QString root_dir = QDir(root.path()).absolutePath();

  {
    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root_dir);
    window.open_archive_inside(archive_path);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());

    const int top_row = row_by_name(window, QStringLiteral("top"));
    QVERIFY(top_row >= 0);
    select_rows_in_active_panel(&window, {top_row});
    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QCOMPARE(window.active_panel_controller().archive.virtual_dir,
             QStringLiteral("top"));

    QVERIFY(window.close());
  }

  {
    z7::platform::qt::PortableSettings settings;
    QCOMPARE(QDir(settings.value(QStringLiteral("FM/PanelPath0")).toString()).absolutePath(),
             root_dir);
    QVERIFY(!settings.contains(QStringLiteral("FM/View/PanelPath0")));
  }

  z7::ui::filemanager::MainWindow restored;
  QVERIFY(!restored.in_archive_view());
  QCOMPARE(QDir(restored.current_directory()).absolutePath(), root_dir);
}

void FileManagerBehaviorTest::folderHistoryPersistsGloballyDedupesAndCapsAtOneHundred() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  QStringList dirs;
  for (int i = 0; i < 102; ++i) {
    const QString dir =
        QDir(root.path()).filePath(QStringLiteral("dir-%1").arg(i, 3, 10, QLatin1Char('0')));
    QVERIFY(QDir().mkpath(dir));
    dirs << QDir(dir).absolutePath();
  }

  {
    z7::ui::filemanager::MainWindow window;
    for (int i = 0; i < dirs.size(); ++i) {
      window.set_current_directory_for_panel(i % 2, dirs.at(i));
    }
    window.set_current_directory_for_panel(0, dirs.at(50));
    QCOMPARE(window.folder_history_.front(), dirs.at(50));
    QVERIFY(window.folder_history_.contains(dirs.at(101)));
    QVERIFY(window.close());
  }

  z7::ui::filemanager::MainWindow restored;
  QCOMPARE(restored.folder_history_.size(), 100);
  QVERIFY(restored.folder_history_.contains(dirs.at(50)));
  QVERIFY(restored.folder_history_.contains(dirs.at(101)));
  QVERIFY(!restored.folder_history_.contains(dirs.at(0)));

  QSet<QString> seen;
  for (const QString& path : restored.folder_history_) {
    const QString key = path.toCaseFolded();
    QVERIFY2(!seen.contains(key), qPrintable(QStringLiteral("duplicate history entry: %1").arg(path)));
    seen.insert(key);
  }

  z7::platform::qt::PortableSettings settings;
  QCOMPARE(settings.value(QStringLiteral("FM/FolderHistory")).toStringList().size(), 100);
  QVERIFY(!settings.contains(QStringLiteral("FM/View/FolderHistory")));
}

void FileManagerBehaviorTest::foldersHistoryDialogDeletesEntriesPersistsAndOpensFocusedRow() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString remove_one =
      QDir(root.path()).absoluteFilePath(QStringLiteral("remove-one"));
  const QString keep =
      QDir(root.path()).absoluteFilePath(QStringLiteral("keep-open"));
  const QString remove_two =
      QDir(root.path()).absoluteFilePath(QStringLiteral("remove-two"));
  QVERIFY(QDir().mkpath(remove_one));
  QVERIFY(QDir().mkpath(keep));
  QVERIFY(QDir().mkpath(remove_two));

  z7::ui::filemanager::MainWindow window;
  window.set_folder_history({remove_one, keep, remove_two});

  bool saw_dialog = false;
  bool saw_remove_two = false;
  bool clicked_delete = false;
  bool clicked_ok = false;
  schedule_folders_history_dialog_interaction([&](QDialog* dialog) {
    saw_dialog = true;
    auto* list =
        dialog->findChild<QListWidget*>(QStringLiteral("foldersHistoryList"));
    auto* delete_button =
        dialog->findChild<QPushButton*>(QStringLiteral("foldersHistoryDeleteButton"));
    auto* buttons =
        dialog->findChild<QDialogButtonBox*>(QStringLiteral("foldersHistoryButtons"));
    if (list == nullptr || delete_button == nullptr || buttons == nullptr) {
      dialog->reject();
      return;
    }

    for (int row = 0; row < list->count(); ++row) {
      if (list->item(row)->text() == remove_two) {
        list->item(row)->setSelected(true);
        saw_remove_two = true;
        break;
      }
    }
    if (!saw_remove_two) {
      dialog->reject();
      return;
    }

    delete_button->click();
    clicked_delete = true;
    if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok)) {
      ok->click();
      clicked_ok = true;
    } else {
      dialog->reject();
    }
  });

  window.on_folders_history_requested();

  QVERIFY(saw_dialog);
  QVERIFY(saw_remove_two);
  QVERIFY(clicked_delete);
  QVERIFY(clicked_ok);
  const QStringList expected{keep};
  QCOMPARE(window.folder_history_, expected);
  QCOMPARE(QDir(window.current_directory()).absolutePath(), keep);

  z7::platform::qt::PortableSettings settings;
  QCOMPARE(settings.value(QStringLiteral("FM/FolderHistory")).toStringList(),
           expected);
}

void FileManagerBehaviorTest::foldersHistoryDialogCancelKeepsPersistedListUnchanged() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString first =
      QDir(root.path()).absoluteFilePath(QStringLiteral("first"));
  const QString second =
      QDir(root.path()).absoluteFilePath(QStringLiteral("second"));
  QVERIFY(QDir().mkpath(first));
  QVERIFY(QDir().mkpath(second));

  z7::ui::filemanager::MainWindow window;
  const QStringList original{first, second};
  window.set_folder_history(original);

  bool saw_dialog = false;
  bool clicked_delete = false;
  bool clicked_cancel = false;
  schedule_folders_history_dialog_interaction([&](QDialog* dialog) {
    saw_dialog = true;
    auto* delete_button =
        dialog->findChild<QPushButton*>(QStringLiteral("foldersHistoryDeleteButton"));
    auto* buttons =
        dialog->findChild<QDialogButtonBox*>(QStringLiteral("foldersHistoryButtons"));
    if (delete_button == nullptr || buttons == nullptr) {
      dialog->reject();
      return;
    }

    delete_button->click();
    clicked_delete = true;
    if (QPushButton* cancel = buttons->button(QDialogButtonBox::Cancel)) {
      cancel->click();
      clicked_cancel = true;
    } else {
      dialog->reject();
    }
  });

  window.on_folders_history_requested();

  QVERIFY(saw_dialog);
  QVERIFY(clicked_delete);
  QVERIFY(clicked_cancel);
  QCOMPARE(window.folder_history_, original);

  z7::platform::qt::PortableSettings settings;
  QCOMPARE(settings.value(QStringLiteral("FM/FolderHistory")).toStringList(),
           original);
}

void FileManagerBehaviorTest::startupOpenRequestDispatchUsesPrimaryWindowAndAdditionalWindows() {
  auto* app =
      qobject_cast<z7::apps::filemanager::FileOpenApplication*>(
          QCoreApplication::instance());
  QVERIFY(app != nullptr);

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString first_archive = create_sample_archive(root);
  QVERIFY2(!first_archive.isEmpty(), "failed to create first sample archive");
  const QString second_archive =
      duplicate_archive(first_archive, QStringLiteral("second-sample.7z"));
  QVERIFY2(!second_archive.isEmpty(), "failed to duplicate second archive");

  QVector<z7::ui::filemanager::MainWindow*> windows;
  int primary_calls = 0;
  int new_window_calls = 0;
  auto cleanup = [&windows]() { close_main_windows(&windows); };
  auto open_in_primary_window =
      [&](const z7::apps::filemanager::OpenRequest& request) {
        ++primary_calls;
        auto* window = new z7::ui::filemanager::MainWindow();
        windows.push_back(window);
        window->open_startup_target(request.path, request.type_hint);
        window->show();
      };
  auto open_in_new_window =
      [&](const z7::apps::filemanager::OpenRequest& request) {
        ++new_window_calls;
        auto* window = new z7::ui::filemanager::MainWindow();
        windows.push_back(window);
        window->open_startup_target(request.path, request.type_hint);
        window->show();
      };

  app->enqueue_open_requests(
      {z7::apps::filemanager::OpenRequest{first_archive, QString()}});
  z7::apps::filemanager::dispatch_startup_open_requests(
      app->take_pending_open_requests(),
      open_in_primary_window,
      open_in_new_window);
  QCOMPARE(primary_calls, 1);
  QCOMPARE(new_window_calls, 0);
  QCOMPARE(windows.size(), 1);
  wait_for_archive_view(windows.at(0), first_archive);

  cleanup();
  primary_calls = 0;
  new_window_calls = 0;

  app->enqueue_open_requests({
      z7::apps::filemanager::OpenRequest{first_archive, QString()},
      z7::apps::filemanager::OpenRequest{second_archive, QString()},
  });
  z7::apps::filemanager::dispatch_startup_open_requests(
      app->take_pending_open_requests(),
      open_in_primary_window,
      open_in_new_window);
  QCOMPARE(primary_calls, 1);
  QCOMPARE(new_window_calls, 1);
  QCOMPARE(windows.size(), 2);
  wait_for_archive_view(windows.at(0), first_archive);
  wait_for_archive_view(windows.at(1), second_archive);

  cleanup();
}

void FileManagerBehaviorTest::startupOpenRequestDedupSuppressesOnlyExactPendingDuplicates() {
  auto* app =
      qobject_cast<z7::apps::filemanager::FileOpenApplication*>(
          QCoreApplication::instance());
  QVERIFY(app != nullptr);

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString first_archive = create_sample_archive(root);
  QVERIFY2(!first_archive.isEmpty(), "failed to create first sample archive");
  const QString second_archive =
      duplicate_archive(first_archive, QStringLiteral("second-dedup.7z"));
  QVERIFY2(!second_archive.isEmpty(), "failed to duplicate second archive");

  app->enqueue_open_requests(
      {z7::apps::filemanager::OpenRequest{first_archive, QString()}});
  QFileOpenEvent duplicate_open_event(first_archive);
  QVERIFY(QCoreApplication::sendEvent(app, &duplicate_open_event));
  QVector<z7::apps::filemanager::OpenRequest> requests =
      app->take_pending_open_requests();
  QCOMPARE(requests.size(), 1);
  QCOMPARE(requests.front().path, first_archive);
  QVERIFY(requests.front().type_hint.isEmpty());
  int primary_calls = 0;
  int new_window_calls = 0;
  z7::apps::filemanager::dispatch_startup_open_requests(
      requests,
      [&](const z7::apps::filemanager::OpenRequest&) { ++primary_calls; },
      [&](const z7::apps::filemanager::OpenRequest&) { ++new_window_calls; });
  QCOMPARE(primary_calls, 1);
  QCOMPARE(new_window_calls, 0);

  app->enqueue_open_requests(
      {z7::apps::filemanager::OpenRequest{first_archive, QString()}});
  QFileOpenEvent non_duplicate_path_event(second_archive);
  QVERIFY(QCoreApplication::sendEvent(app, &non_duplicate_path_event));
  requests = app->take_pending_open_requests();
  QCOMPARE(requests.size(), 2);
  QCOMPARE(requests.at(0).path, first_archive);
  QCOMPARE(requests.at(1).path, second_archive);

  app->enqueue_open_requests(
      {z7::apps::filemanager::OpenRequest{first_archive, QStringLiteral("7z")}});
  QFileOpenEvent non_duplicate_type_event(first_archive);
  QVERIFY(QCoreApplication::sendEvent(app, &non_duplicate_type_event));
  requests = app->take_pending_open_requests();
  QCOMPARE(requests.size(), 2);
  QCOMPARE(requests.at(0).path, first_archive);
  QCOMPARE(requests.at(0).type_hint, QStringLiteral("7z"));
  QCOMPARE(requests.at(1).path, first_archive);
  QVERIFY(requests.at(1).type_hint.isEmpty());
}

void FileManagerBehaviorTest::runtimeFileOpenRequestLaunchesNewProcessWithoutReplacingCurrentWindow() {
  auto* app =
      qobject_cast<z7::apps::filemanager::FileOpenApplication*>(
          QCoreApplication::instance());
  QVERIFY(app != nullptr);

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString first_archive = create_sample_archive(root);
  QVERIFY2(!first_archive.isEmpty(), "failed to create first sample archive");
  QVector<z7::ui::filemanager::MainWindow*> windows;
  auto cleanup = [&windows, app]() {
    app->set_open_request_handler({});
    reset_filemanager_open_launcher_for_test();
    close_main_windows(&windows);
  };
  auto create_window =
      [&](const z7::apps::filemanager::OpenRequest& request) {
        auto* window = new z7::ui::filemanager::MainWindow();
        windows.push_back(window);
        window->open_startup_target(request.path, request.type_hint);
        window->show();
      };

  app->enqueue_open_requests(
      {z7::apps::filemanager::OpenRequest{first_archive, QString()}});
  z7::apps::filemanager::dispatch_startup_open_requests(
      app->take_pending_open_requests(),
      create_window,
      create_window);
  QCOMPARE(windows.size(), 1);
  wait_for_archive_view(windows.at(0), first_archive);

  QVector<z7::platform::qt::filemanager_instance_launcher::LaunchRequest> launches;
  set_filemanager_open_launcher_override_for_test(
      [&launches](
          const z7::platform::qt::filemanager_instance_launcher::LaunchRequest& request,
          QString*) {
        launches.push_back(request);
        return true;
      });

  bool launched = false;
  QString launch_error;
  app->set_open_request_handler(
      [&](const z7::apps::filemanager::OpenRequest& request) {
        launched =
            z7::platform::qt::filemanager_instance_launcher::launch_open_request_for_current_app(
                request.path, request.type_hint, QString(), &launch_error);
      });
  QFileOpenEvent open_event(first_archive);
  QVERIFY(QCoreApplication::sendEvent(app, &open_event));
  QVERIFY2(launched, qPrintable(launch_error));
  QCOMPARE(launches.size(), 1);
  QCOMPARE(launches.front().program, QCoreApplication::applicationFilePath());
  QCOMPARE(launches.front().path, first_archive);
  QCOMPARE(launches.front().arguments, QStringList{first_archive});
  QVERIFY(launches.front().type_hint.isEmpty());
  wait_for_archive_view(windows.at(0), first_archive);
  QCOMPARE(windows.size(), 1);

  cleanup();
}
