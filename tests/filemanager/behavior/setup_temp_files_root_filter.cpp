// tests/filemanager/behavior/setup_temp_files_root_filter.cpp
// Role: Temp-files dialog root filtering behavior case.

#include "internal.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::tempFilesActionOpensCleanupDialogWithRootFilter() {
  QTemporaryDir fake_temp_root;
  QVERIFY(fake_temp_root.isValid());

  const QString allowed_file_name = QStringLiteral("7zE1234ABCD");
  const QString allowed_dir_name = QStringLiteral("7zO87654321");
  const QString blocked_file_name = QStringLiteral("normal.txt");
  const QString blocked_dir_name = QStringLiteral("randomFolder");

  {
    QFile allowed_file(QDir(fake_temp_root.path()).filePath(allowed_file_name));
    QVERIFY(allowed_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    allowed_file.write("allowed");
    allowed_file.close();
  }
  QVERIFY(QDir().mkpath(QDir(fake_temp_root.path()).filePath(allowed_dir_name)));

  {
    QFile blocked_file(QDir(fake_temp_root.path()).filePath(blocked_file_name));
    QVERIFY(blocked_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    blocked_file.write("blocked");
    blocked_file.close();
  }
  QVERIFY(QDir().mkpath(QDir(fake_temp_root.path()).filePath(blocked_dir_name)));

  bool dialog_seen = false;
  bool parent_disabled = false;
  QString captured_title;
  QStringList visible_names;

  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [&]() {
    const QWidgetList top_levels = QApplication::topLevelWidgets();
    for (QWidget* widget : top_levels) {
      auto* dialog = qobject_cast<QDialog*>(widget);
      if (dialog == nullptr || dialog->objectName() != QStringLiteral("tempFilesDialog")) {
        continue;
      }

      dialog_seen = true;
      captured_title = dialog->windowTitle();
      if (auto* parent_button = dialog->findChild<QPushButton*>(
              QStringLiteral("tempFilesParentButton"))) {
        parent_disabled = !parent_button->isEnabled();
      }
      if (auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("tempFilesTable"))) {
        visible_names.clear();
        for (int row = 0; row < table->rowCount(); ++row) {
          if (QTableWidgetItem* item = table->item(row, 0)) {
            visible_names.push_back(item->text());
          }
        }
      }

      dialog->accept();
      timer->stop();
      timer->deleteLater();
      return;
    }
  });
  timer->start();

  z7::ui::filemanager::TempFilesDialog dialog(fake_temp_root.path());
  dialog.exec();

  QVERIFY(dialog_seen);
  QVERIFY(parent_disabled);

  QSet<QString> visible_set;
  for (const QString& name : visible_names) {
    visible_set.insert(name);
  }
  QVERIFY(visible_set.contains(allowed_file_name));
  QVERIFY(visible_set.contains(allowed_dir_name));
  QVERIFY(!visible_set.contains(blocked_file_name));
  QVERIFY(!visible_set.contains(blocked_dir_name));

  QString expected_title = localized_label(910);
  expected_title.remove(QStringLiteral("..."));
  expected_title = expected_title.trimmed();
  if (expected_title.isEmpty() || expected_title.startsWith(QLatin1Char('#'))) {
    expected_title = QStringLiteral("Delete Temporary Files");
  }
  QCOMPARE(captured_title, expected_title);
}
