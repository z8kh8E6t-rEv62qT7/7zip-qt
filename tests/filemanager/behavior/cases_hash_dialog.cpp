// tests/filemanager/behavior/cases_hash_dialog.cpp
// Role: Hash result row formatting and checksum dialog behavior cases.

#include "internal.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::hashResultRowsFollowOriginalRules() {
    z7::ui::filemanager::MainWindow window;

    z7::app::HashSummary single;
    single.num_dirs = 0;
    single.num_files = 1;
    single.files_size = 6148;
    single.first_file_name = ".DS_Store";
    z7::app::HashMethodDigest single_digest;
    single_digest.method_name = "CRC32";
    single_digest.data_sum = "3D1C2BB7";
    single_digest.has_data_sum = true;
    single.methods.push_back(single_digest);

    const auto single_rows = window.build_hash_result_rows(single);
    QCOMPARE(single_rows.size(), 3);
    QCOMPARE(single_rows.at(0).first, localized_label(1004));
    QCOMPARE(single_rows.at(0).second, QStringLiteral(".DS_Store"));
    QCOMPARE(single_rows.at(1).first, localized_label(1007));
    QCOMPARE(single_rows.at(1).second, QStringLiteral("6148 bytes : 6 KiB"));
    QCOMPARE(single_rows.at(2).first, QStringLiteral("CRC32"));
    QCOMPARE(single_rows.at(2).second, QStringLiteral("3D1C2BB7"));

    z7::app::HashSummary multi;
    multi.num_dirs = 1;
    multi.num_files = 2;
    multi.files_size = 1200;
    multi.main_name = "folder";
    z7::app::HashMethodDigest multi_digest;
    multi_digest.method_name = "SHA256";
    multi_digest.data_sum = "DATA_SUM";
    multi_digest.names_sum = "NAMES_SUM";
    multi_digest.has_data_sum = true;
    multi_digest.has_names_sum = true;
    multi.methods.push_back(multi_digest);

    const auto multi_rows = window.build_hash_result_rows(multi);
    QCOMPARE(multi_rows.size(), 6);
    QCOMPARE(multi_rows.at(0).first, localized_label(1004));
    QCOMPARE(multi_rows.at(0).second, QStringLiteral("folder"));
    QCOMPARE(multi_rows.at(1).first, localized_label(1031));
    QCOMPARE(multi_rows.at(1).second, QStringLiteral("1"));
    QCOMPARE(multi_rows.at(2).first, localized_label(1032));
    QCOMPARE(multi_rows.at(2).second, QStringLiteral("2"));
    QCOMPARE(multi_rows.at(3).first, localized_label(1007));
    QCOMPARE(multi_rows.at(3).second, QStringLiteral("1200 bytes : 1 KiB"));
    QCOMPARE(multi_rows.at(4).first, QStringLiteral("SHA256 checksum for data"));
    QCOMPARE(multi_rows.at(4).second, QStringLiteral("DATA_SUM"));
    QCOMPARE(multi_rows.at(5).first, QStringLiteral("SHA256 checksum for data and names"));
    QCOMPARE(multi_rows.at(5).second, QStringLiteral("NAMES_SUM"));
}

void FileManagerBehaviorTest::hashCompletionShowsChecksumInformationDialog() {
    close_checksum_dialogs();
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString input_file = QDir(root.path()).filePath(QStringLiteral("hash_result.bin"));
    QFile file(input_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("hash-result");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int row = row_by_name(window, QStringLiteral("hash_result.bin"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});

    window.start_hash_task(QStringList{input_file}, QStringLiteral("CRC32"), false);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);

    const QString checksum_title = z7::ui::runtime_support::L(7501);
    QDialog* checksum_dialog = nullptr;
    const bool dialog_seen = QTest::qWaitFor([&checksum_dialog, &checksum_title]() {
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog != nullptr &&
            dialog->windowTitle() == checksum_title) {
          checksum_dialog = dialog;
          return true;
        }
      }
      return false;
    }, 5000);

    if (dialog_seen) {
      QVERIFY(checksum_dialog != nullptr);
      auto* table = checksum_dialog->findChild<QTableWidget*>();
      QVERIFY(table != nullptr);
      QVERIFY(table->rowCount() >= 3);
      QCOMPARE(table->item(0, 0)->text(), localized_label(1004));
      QCOMPARE(table->item(1, 0)->text(), localized_label(1007));
      QCOMPARE(table->item(2, 0)->text(), QStringLiteral("CRC32"));
      checksum_dialog->close();
    }
    close_checksum_dialogs();
}

void FileManagerBehaviorTest::hashCancelDoesNotShowChecksumDialog() {
    close_checksum_dialogs();
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    QStringList input_files;
    for (int i = 0; i < 128; ++i) {
      const QString input_file =
          QDir(root.path()).filePath(QStringLiteral("cancel-%1.bin").arg(i, 3, 10, QLatin1Char('0')));
      QFile file(input_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write(QByteArray(512 * 1024, static_cast<char>('A' + (i % 26))));
      file.close();
      input_files << input_file;
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.start_hash_task(input_files, QStringLiteral("SHA256"), false);
    z7::ui::filemanager::ArchiveProcessRunner* runner =
        filemanager_behavior_internal::current_runner(window);
    if (runner == nullptr) {
      QTest::qWait(30);
      runner = filemanager_behavior_internal::current_runner(window);
    }
    if (runner != nullptr) {
      QTest::qWait(30);
      runner->cancel();
    }
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 5000);

    const QString checksum_title = z7::ui::runtime_support::L(7501);
    QTest::qWait(300);
    const QWidgetList top_levels = QApplication::topLevelWidgets();
    for (QWidget* widget : top_levels) {
      auto* dialog = qobject_cast<QDialog*>(widget);
      if (dialog == nullptr) {
        continue;
      }
      QVERIFY(dialog->windowTitle() != checksum_title);
    }
    close_checksum_dialogs();
}
