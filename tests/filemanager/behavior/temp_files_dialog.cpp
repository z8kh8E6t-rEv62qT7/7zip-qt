// tests/filemanager/behavior/temp_files_dialog.cpp
// Role: Column and sorting parity tests for temp-files cleanup dialog.

#include "internal.h"

using namespace filemanager_behavior_internal;

namespace {

bool write_bytes(const QString& path, int size) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  file.write(QByteArray(size, 'x'));
  file.close();
  return true;
}

bool set_mtime_utc(const QString& path, qint64 secs_since_epoch) {
  QFile file(path);
  if (!file.open(QIODevice::ReadWrite)) {
    return false;
  }
  const bool ok = file.setFileTime(
      QDateTime::fromSecsSinceEpoch(secs_since_epoch, QTimeZone::UTC),
      QFileDevice::FileModificationTime);
  file.close();
  return ok;
}

QStringList first_column_names(QTableWidget* table) {
  QStringList out;
  if (table == nullptr) {
    return out;
  }
  for (int row = 0; row < table->rowCount(); ++row) {
    if (QTableWidgetItem* item = table->item(row, 0)) {
      out.push_back(item->text());
    }
  }
  return out;
}

bool click_header_column(QHeaderView* header, int section) {
  if (header == nullptr) {
    return false;
  }
  return QMetaObject::invokeMethod(header,
                                   "sectionClicked",
                                   Qt::DirectConnection,
                                   Q_ARG(int, section));
}

}  // namespace

void FileManagerBehaviorTest::tempFilesDialogColumnsMatchOriginalSixColumnLayout() {
  QTemporaryDir fake_temp_root;
  QVERIFY(fake_temp_root.isValid());

  const QString file_path =
      QDir(fake_temp_root.path()).filePath(QStringLiteral("7zE00000001"));
  QVERIFY(write_bytes(file_path, 8));

  z7::ui::filemanager::TempFilesDialog dialog(fake_temp_root.path());

  bool dialog_seen = false;
  int column_count = -1;
  QStringList labels;

  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [&]() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
      auto* dialog = qobject_cast<QDialog*>(widget);
      if (dialog == nullptr || dialog->objectName() != QStringLiteral("tempFilesDialog")) {
        continue;
      }
      dialog_seen = true;
      auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("tempFilesTable"));
      QVERIFY(table != nullptr);
      column_count = table->columnCount();
      for (int i = 0; i < column_count; ++i) {
        labels.push_back(table->horizontalHeaderItem(i)->text());
      }
      dialog->accept();
      timer->stop();
      timer->deleteLater();
      return;
    }
  });
  timer->start();

  dialog.exec();

  QVERIFY(dialog_seen);
  QCOMPARE(column_count, 6);
  QCOMPARE(labels.size(), 6);
  QCOMPARE(labels.at(0), localized_label(1004));
  QCOMPARE(labels.at(1), localized_label(1012));
  QCOMPARE(labels.at(2), localized_label(1007));
  QCOMPARE(labels.at(3), localized_label(1032));
  QCOMPARE(labels.at(4), localized_label(1031));
  QCOMPARE(labels.at(5), QStringLiteral("%1-2").arg(localized_label(1004)));
}

void FileManagerBehaviorTest::tempFilesDialogDefaultSortUsesModifiedWithDirectoryFirst() {
  QTemporaryDir fake_temp_root;
  QVERIFY(fake_temp_root.isValid());

  const QString dir_name = QStringLiteral("7zE00000001");
  const QString old_name = QStringLiteral("7zE00000002");
  const QString new_name = QStringLiteral("7zE00000003");

  const QString dir_path = QDir(fake_temp_root.path()).filePath(dir_name);
  const QString old_path = QDir(fake_temp_root.path()).filePath(old_name);
  const QString new_path = QDir(fake_temp_root.path()).filePath(new_name);

  QVERIFY(QDir().mkpath(dir_path));
  QVERIFY(write_bytes(old_path, 10));
  QVERIFY(write_bytes(new_path, 12));
  QVERIFY(set_mtime_utc(old_path, 1000));
  QVERIFY(set_mtime_utc(new_path, 2000));

  z7::ui::filemanager::TempFilesDialog dialog(fake_temp_root.path());

  bool dialog_seen = false;
  QStringList order;

  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [&]() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
      auto* dialog = qobject_cast<QDialog*>(widget);
      if (dialog == nullptr || dialog->objectName() != QStringLiteral("tempFilesDialog")) {
        continue;
      }
      dialog_seen = true;
      auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("tempFilesTable"));
      QVERIFY(table != nullptr);
      order = first_column_names(table);
      dialog->accept();
      timer->stop();
      timer->deleteLater();
      return;
    }
  });
  timer->start();

  dialog.exec();

  QVERIFY(dialog_seen);
  QCOMPARE(order.size(), 3);
  QCOMPARE(order.at(0), dir_name);
  QCOMPARE(order.at(1), old_name);
  QCOMPARE(order.at(2), new_name);
}

void FileManagerBehaviorTest::tempFilesDialogHeaderSortingTogglesAndDefaultDirectionRules() {
  QTemporaryDir fake_temp_root;
  QVERIFY(fake_temp_root.isValid());

  const QString a_name = QStringLiteral("7zE0000000A");
  const QString b_name = QStringLiteral("7zE0000000B");
  const QString c_name = QStringLiteral("7zE0000000C");

  const QString a_path = QDir(fake_temp_root.path()).filePath(a_name);
  const QString b_path = QDir(fake_temp_root.path()).filePath(b_name);
  const QString c_path = QDir(fake_temp_root.path()).filePath(c_name);

  QVERIFY(write_bytes(a_path, 10));
  QVERIFY(write_bytes(b_path, 100));
  QVERIFY(write_bytes(c_path, 50));
  QVERIFY(set_mtime_utc(a_path, 2000));
  QVERIFY(set_mtime_utc(b_path, 3000));
  QVERIFY(set_mtime_utc(c_path, 1000));

  z7::ui::filemanager::TempFilesDialog dialog(fake_temp_root.path());

  bool dialog_seen = false;
  QStringList initial_order;
  QStringList modified_toggle_order;
  QStringList name_default_order;
  QStringList size_default_order;

  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [&]() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
      auto* dialog = qobject_cast<QDialog*>(widget);
      if (dialog == nullptr || dialog->objectName() != QStringLiteral("tempFilesDialog")) {
        continue;
      }
      dialog_seen = true;
      auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("tempFilesTable"));
      QVERIFY(table != nullptr);
      auto* header = table->horizontalHeader();
      QVERIFY(header != nullptr);

      initial_order = first_column_names(table);

      QVERIFY(click_header_column(header, 1));
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      modified_toggle_order = first_column_names(table);

      QVERIFY(click_header_column(header, 0));
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      name_default_order = first_column_names(table);

      QVERIFY(click_header_column(header, 2));
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      size_default_order = first_column_names(table);

      dialog->accept();
      timer->stop();
      timer->deleteLater();
      return;
    }
  });
  timer->start();

  dialog.exec();

  QVERIFY(dialog_seen);
  QCOMPARE(initial_order, QStringList({c_name, a_name, b_name}));
  QCOMPARE(modified_toggle_order, QStringList({b_name, a_name, c_name}));
  QCOMPARE(name_default_order, QStringList({a_name, b_name, c_name}));
  QCOMPARE(size_default_order, QStringList({b_name, c_name, a_name}));
}

void FileManagerBehaviorTest::tempFilesDialogOpenOutside7ZipLaunchesNew7zfmProcess() {
  QTemporaryDir fake_temp_root;
  QVERIFY(fake_temp_root.isValid());

  const QString target_name = QStringLiteral("7zE12345678");
  const QString target_path = QDir(fake_temp_root.path()).filePath(target_name);
  QVERIFY(write_bytes(target_path, 32));

  QVector<z7::platform::qt::filemanager_instance_launcher::LaunchRequest> launches;
  set_filemanager_open_launcher_override_for_test(
      [&launches](
          const z7::platform::qt::filemanager_instance_launcher::LaunchRequest& request,
          QString*) {
        launches.push_back(request);
        return true;
      });

  z7::ui::filemanager::TempFilesDialog dialog(fake_temp_root.path());
  QVERIFY(dialog.table_ != nullptr);
  QVERIFY(dialog.table_->rowCount() >= 1);
  dialog.table_->setCurrentCell(0, 0);
  dialog.table_->clearSelection();
  dialog.table_->selectRow(0);

  dialog.on_open_outside_7zip_requested();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  QCOMPARE(launches.size(), 1);
  QCOMPARE(launches.front().program, QCoreApplication::applicationFilePath());
  QCOMPARE(launches.front().working_dir, fake_temp_root.path());
  QCOMPARE(launches.front().path, target_path);
  QCOMPARE(launches.front().arguments, QStringList{target_path});
  QVERIFY(launches.front().type_hint.isEmpty());
}
