// tests/filemanager/behavior/runner_open_behavior.cpp
// Role: Open request routing behavior cases.

#include "internal.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::openOnArchivePrefersInternalOpen() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int archive_row = row_by_name(window, QStringLiteral("sample.7z"));
    QVERIFY(archive_row >= 0);
    select_rows_in_active_panel(&window, {archive_row});

    int external_open_count = 0;
    window.external_opener_ = [&external_open_count](const QString&) {
      ++external_open_count;
      return true;
    };

    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(external_open_count, 0);
}

void FileManagerBehaviorTest::openOnUnknownSuffixArchivePrefersInternalOpen() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString disguised_archive =
        QDir(root.path()).filePath(QStringLiteral("sample.unknown"));
    QVERIFY(QFile::copy(archive_path, disguised_archive));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int archive_row =
        row_by_name(window, QStringLiteral("sample.unknown"));
    QVERIFY(archive_row >= 0);
    select_rows_in_active_panel(&window, {archive_row});

    int external_open_count = 0;
    window.external_opener_ = [&external_open_count](const QString&) {
      ++external_open_count;
      return true;
    };

    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
    QCOMPARE(external_open_count, 0);
}

void FileManagerBehaviorTest::openOnUnknownSuffixNonArchiveFallsBackToExternalOpen() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString plain_file =
        QDir(root.path()).filePath(QStringLiteral("plain.unknown"));
    {
      QFile file(plain_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("plain");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int row = row_by_name(window, QStringLiteral("plain.unknown"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    QStringList opened_paths;
    window.external_opener_ = [&opened_paths](const QString& path) {
      opened_paths << QFileInfo(path).absoluteFilePath();
      return true;
    };

    window.on_open_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QTRY_COMPARE(opened_paths.size(), 1);
    QCOMPARE(opened_paths.front(), QFileInfo(plain_file).absoluteFilePath());
    QVERIFY(!window.in_archive_view());
}

void FileManagerBehaviorTest::openOnAlwaysStartExtensionUsesExternalOpen() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString disguised_archive =
        QDir(root.path()).filePath(QStringLiteral("packed.txt"));
    QVERIFY(QFile::copy(archive_path, disguised_archive));

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int archive_row = row_by_name(window, QStringLiteral("packed.txt"));
    QVERIFY(archive_row >= 0);
    select_rows_in_active_panel(&window, {archive_row});

    QStringList opened_paths;
    window.external_opener_ = [&opened_paths](const QString& path) {
      opened_paths << QFileInfo(path).absoluteFilePath();
      return true;
    };

    window.on_open_requested();
    QCOMPARE(opened_paths.size(), 1);
    QCOMPARE(opened_paths.front(), QFileInfo(disguised_archive).absoluteFilePath());
    QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
    QVERIFY(!window.in_archive_view());
}

void FileManagerBehaviorTest::openOutsideUsesExternalOpenForArchive() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int archive_row = row_by_name(window, QStringLiteral("sample.7z"));
    QVERIFY(archive_row >= 0);
    select_rows_in_active_panel(&window, {archive_row});

    QStringList opened_paths;
    window.external_opener_ = [&opened_paths](const QString& path) {
      opened_paths << QFileInfo(path).absoluteFilePath();
      return true;
    };

    window.on_open_outside_requested();
    QCOMPARE(opened_paths.size(), 1);
    QCOMPARE(opened_paths.front(), QFileInfo(archive_path).absoluteFilePath());
    QVERIFY(!window.in_archive_view());
}

void FileManagerBehaviorTest::externalOpenBlocksSuspiciousFilenamesLikeOriginal() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QVector<QPair<QString, QString>> cases{
        {QStringLiteral("invoice") + QChar(0x202E) + QStringLiteral("cod.exe"),
         QStringLiteral("[RLO]")},
        {QStringLiteral("many     spaces.txt"),
         QStringLiteral("many spaces.txt")},
    };

    for (const auto& test_case : cases) {
      const QString file_name = test_case.first;
      const QString file_path = QDir(root.path()).filePath(file_name);
      {
        QFile file(file_path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("blocked");
      }

      z7::ui::filemanager::MainWindow window;
      window.set_current_directory(root.path());
      const int row = row_by_name(window, file_name);
      QVERIFY(row >= 0);
      select_rows_in_active_panel(&window, {row});

      int external_open_count = 0;
      window.external_opener_ = [&external_open_count](const QString&) {
        ++external_open_count;
        return true;
      };

      QString warning_text;
      schedule_message_box_capture_and_click(QMessageBox::Ok,
                                             nullptr,
                                             &warning_text);
      window.on_open_outside_requested();

      QCOMPARE(external_open_count, 0);
      QVERIFY2(warning_text.contains(QStringLiteral("looks like a virus")),
               qPrintable(warning_text));
      QVERIFY2(warning_text.contains(test_case.second),
               qPrintable(warning_text));
      QVERIFY2(warning_text.contains(file_name),
               qPrintable(warning_text));
    }
}

void FileManagerBehaviorTest::openOnMultipleFilesUsesExternalOpen() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_a = QDir(root.path()).filePath(QStringLiteral("a.txt"));
    const QString file_b = QDir(root.path()).filePath(QStringLiteral("b.txt"));
    {
      QFile file(file_a);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("a");
    }
    {
      QFile file(file_b);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("b");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int row_a = row_by_name(window, QStringLiteral("a.txt"));
    const int row_b = row_by_name(window, QStringLiteral("b.txt"));
    QVERIFY(row_a >= 0);
    QVERIFY(row_b >= 0);
    QVERIFY(window.active_panel_controller().ui.details_view != nullptr);
    QVERIFY(window.active_panel_controller().ui.details_view->selectionModel() != nullptr);
    QItemSelectionModel* selection = window.active_panel_controller().ui.details_view->selectionModel();
    selection->clearSelection();
    const QModelIndex idx_a = window.active_panel_controller().ui.details_view->model()->index(row_a, 0);
    const QModelIndex idx_b = window.active_panel_controller().ui.details_view->model()->index(row_b, 0);
    QVERIFY(idx_a.isValid());
    QVERIFY(idx_b.isValid());
    selection->select(idx_a, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->select(idx_b, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->setCurrentIndex(idx_b, QItemSelectionModel::NoUpdate);

    QStringList opened_paths;
    window.external_opener_ = [&opened_paths](const QString& path) {
      opened_paths << QFileInfo(path).absoluteFilePath();
      return true;
    };

    window.on_open_requested();
    QCOMPARE(opened_paths.size(), 2);
    QVERIFY(opened_paths.contains(QFileInfo(file_a).absoluteFilePath()));
    QVERIFY(opened_paths.contains(QFileInfo(file_b).absoluteFilePath()));
    QVERIFY(!window.in_archive_view());
}

void FileManagerBehaviorTest::openInsideUsesFocusedItemWhenMultipleSelected() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    const QString side_file = QDir(root.path()).filePath(QStringLiteral("side.txt"));
    {
      QFile file(side_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("side");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int archive_row = row_by_name(window, QStringLiteral("sample.7z"));
    const int side_row = row_by_name(window, QStringLiteral("side.txt"));
    QVERIFY(archive_row >= 0);
    QVERIFY(side_row >= 0);

    QVERIFY(window.active_panel_controller().ui.details_view != nullptr);
    QVERIFY(window.active_panel_controller().ui.details_view->selectionModel() != nullptr);
    QItemSelectionModel* selection = window.active_panel_controller().ui.details_view->selectionModel();
    selection->clearSelection();
    const QModelIndex side_idx = window.active_panel_controller().ui.details_view->model()->index(side_row, 0);
    const QModelIndex archive_idx = window.active_panel_controller().ui.details_view->model()->index(archive_row, 0);
    QVERIFY(side_idx.isValid());
    QVERIFY(archive_idx.isValid());
    selection->select(side_idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->select(archive_idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    selection->setCurrentIndex(archive_idx, QItemSelectionModel::NoUpdate);
    window.on_open_inside_requested();

    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    QVERIFY(window.in_archive_view());
}

void FileManagerBehaviorTest::openInsideOnNonArchiveDoesNotFallbackToExternalOpen() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString plain_file = QDir(root.path()).filePath(QStringLiteral("plain.txt"));
    {
      QFile file(plain_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("plain");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int plain_row = row_by_name(window, QStringLiteral("plain.txt"));
    QVERIFY(plain_row >= 0);
    select_rows_in_active_panel(&window, {plain_row});

    int external_open_count = 0;
    window.external_opener_ = [&external_open_count](const QString&) {
      ++external_open_count;
      return true;
    };

    schedule_message_box_autoclose();
    window.on_open_inside_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
    close_message_boxes();
    QCOMPARE(external_open_count, 0);
    QVERIFY(!window.in_archive_view());
}

void FileManagerBehaviorTest::openInsideVariantStarDoesNotFallbackToExternalOpen() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow star_window;
    star_window.set_current_directory(root.path());
    const int archive_row_star = row_by_name(star_window, QStringLiteral("sample.7z"));
    QVERIFY(archive_row_star >= 0);
    select_rows_in_active_panel(&star_window, {archive_row_star});
    int external_open_count = 0;
    star_window.external_opener_ = [&external_open_count](const QString&) {
      ++external_open_count;
      return true;
    };
    star_window.on_open_inside_one_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(star_window) == nullptr, 20000);
    QCOMPARE(external_open_count, 0);
    QVERIFY(star_window.in_archive_view());
    QCOMPARE(star_window.active_panel_controller().archive.type_hint,
             QStringLiteral("*"));
}

void FileManagerBehaviorTest::openInsideVariantParserDoesNotFallbackToExternalOpen() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    z7::ui::filemanager::MainWindow parser_window;
    parser_window.set_current_directory(root.path());
    const int archive_row = row_by_name(parser_window, QStringLiteral("sample.7z"));
    QVERIFY(archive_row >= 0);
    select_rows_in_active_panel(&parser_window, {archive_row});
    int external_open_count = 0;
    parser_window.external_opener_ = [&external_open_count](const QString&) {
      ++external_open_count;
      return true;
    };
    schedule_message_box_autoclose();
    parser_window.on_open_inside_parser_requested();
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(parser_window) == nullptr, 20000);
    close_message_boxes();
    QCOMPARE(external_open_count, 0);
    if (parser_window.in_archive_view()) {
      QCOMPARE(parser_window.active_panel_controller().archive.type_hint,
               QStringLiteral("#"));
    }
}
