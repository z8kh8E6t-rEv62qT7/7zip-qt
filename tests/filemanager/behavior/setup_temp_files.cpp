// tests/filemanager/behavior/setup_temp_files.cpp
// Role: Temp-files dialog behavior cases.

#include "internal.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::tempFilesDialogDeleteViaKeyButtonAndContextMenu() {
  QTemporaryDir fake_temp_root;
  QVERIFY(fake_temp_root.isValid());

  const QString key_target_name = QStringLiteral("7zE11111111");
  const QString button_target_name = QStringLiteral("7zO22222222");
  const QString context_target_name = QStringLiteral("7zS33333333");
  const QString properties_target_name = QStringLiteral("7zE44444444");

  const QString key_target_path = QDir(fake_temp_root.path()).filePath(key_target_name);
  const QString button_target_path = QDir(fake_temp_root.path()).filePath(button_target_name);
  const QString context_target_path = QDir(fake_temp_root.path()).filePath(context_target_name);
  const QString properties_target_path = QDir(fake_temp_root.path()).filePath(properties_target_name);

  {
    QFile file(key_target_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("key");
    file.close();
  }
  QVERIFY(QDir().mkpath(button_target_path));
  {
    QFile file(QDir(button_target_path).filePath(QStringLiteral("child.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("child");
    file.close();
  }
  {
    QFile file(context_target_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("context");
    file.close();
  }
  {
    QFile file(properties_target_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("properties");
    file.close();
  }

  bool dialog_seen = false;
  bool key_deleted = false;
  bool button_deleted = false;
  bool context_deleted = false;
  bool menu_rules_verified = false;
  bool properties_dialog_seen = false;
  QString properties_text;
  const QString delete_label = localized_label(7205);
  const QString open_outside_label = localized_label(542);
  const QString properties_label = localized_label(551);

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
      auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("tempFilesTable"));
      auto* delete_button =
          dialog->findChild<QPushButton*>(QStringLiteral("tempFilesDeleteButton"));
      auto* close_button =
          dialog->findChild<QPushButton*>(QStringLiteral("tempFilesCloseButton"));
      auto* context_menu = dialog->findChild<QMenu*>(QStringLiteral("tempFilesContextMenu"));
      QAction* context_delete_action =
          dialog->findChild<QAction*>(QStringLiteral("tempFilesContextDeleteAction"));
      QAction* context_open_outside_action =
          dialog->findChild<QAction*>(QStringLiteral("tempFilesContextOpenOutsideAction"));
      QAction* context_open_outside_7zip_action =
          dialog->findChild<QAction*>(QStringLiteral("tempFilesContextOpenOutside7ZipAction"));
      QAction* context_properties_action =
          dialog->findChild<QAction*>(QStringLiteral("tempFilesContextPropertiesAction"));
      if (table == nullptr || delete_button == nullptr || close_button == nullptr ||
          context_menu == nullptr || context_delete_action == nullptr ||
          context_open_outside_action == nullptr ||
          context_open_outside_7zip_action == nullptr ||
          context_properties_action == nullptr) {
        dialog->reject();
        timer->stop();
        timer->deleteLater();
        return;
      }

      auto action_caption = [](const QAction* action) {
        if (action == nullptr) {
          return QString();
        }
        return without_mnemonic(action->text()).section(QLatin1Char('\t'), 0, 0).trimmed();
      };
      const QString delete_caption = action_caption(context_delete_action);
      const QString open_outside_caption = action_caption(context_open_outside_action);
      const QString properties_caption = action_caption(context_properties_action);
      QVERIFY((!delete_label.isEmpty() && delete_caption == delete_label) ||
              delete_caption.contains(QStringLiteral("Delete"), Qt::CaseInsensitive));
      QVERIFY((!open_outside_label.isEmpty() && open_outside_caption == open_outside_label) ||
              open_outside_caption.contains(QStringLiteral("Open Outside"), Qt::CaseInsensitive));
      QVERIFY((!properties_label.isEmpty() && properties_caption == properties_label) ||
              properties_caption.contains(QStringLiteral("Properties"), Qt::CaseInsensitive));
      QVERIFY(action_caption(context_open_outside_7zip_action)
                  .contains(open_outside_caption, Qt::CaseInsensitive));
      QVERIFY(action_caption(context_open_outside_7zip_action).contains(
          QStringLiteral("7-Zip"), Qt::CaseInsensitive));

      const QList<QAction*> context_actions = context_menu->actions();
      QVERIFY(context_actions.size() >= 6);
      QCOMPARE(context_actions.at(0), context_delete_action);
      QVERIFY(context_actions.at(1)->isSeparator());
      QCOMPARE(context_actions.at(2), context_open_outside_action);
      QCOMPARE(context_actions.at(3), context_open_outside_7zip_action);
      QVERIFY(context_actions.at(4)->isSeparator());
      QCOMPARE(context_actions.at(5), context_properties_action);

      auto select_row_by_name = [table](const QString& name) {
        for (int row = 0; row < table->rowCount(); ++row) {
          if (QTableWidgetItem* item = table->item(row, 0)) {
            if (item->text() == name) {
              table->setCurrentCell(row, 0);
              table->clearSelection();
              table->selectRow(row);
              return row;
            }
          }
        }
        return -1;
      };

      const int properties_row = select_row_by_name(properties_target_name);
      const int key_row = select_row_by_name(key_target_name);
      const int button_row = select_row_by_name(button_target_name);

      if (key_row >= 0 && button_row >= 0) {
        table->clearSelection();
        if (table->selectionModel() != nullptr) {
          table->selectionModel()->select(table->model()->index(key_row, 0),
                                          QItemSelectionModel::Select |
                                              QItemSelectionModel::Rows);
          table->selectionModel()->select(table->model()->index(button_row, 0),
                                          QItemSelectionModel::Select |
                                              QItemSelectionModel::Rows);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        QVERIFY(context_delete_action->isVisible());
        QVERIFY(!context_open_outside_action->isVisible());
        QVERIFY(!context_open_outside_7zip_action->isVisible());
        QVERIFY(!context_properties_action->isVisible());
      }

      if (properties_row >= 0) {
        table->setCurrentCell(properties_row, 0);
        table->clearSelection();
        table->selectRow(properties_row);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        QVERIFY(context_delete_action->isVisible());
        QVERIFY(context_open_outside_action->isVisible());
        QVERIFY(context_open_outside_7zip_action->isVisible());
        QVERIFY(context_properties_action->isVisible());

        schedule_message_box_capture_and_click(QMessageBox::Ok,
                                               nullptr,
                                               &properties_text,
                                               5000,
                                               10);
        context_properties_action->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!properties_text.isEmpty(), 3000);
        properties_dialog_seen = properties_text.contains(properties_target_name);
      }

      table->clearSelection();
      QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
      QVERIFY(!context_delete_action->isVisible());
      QVERIFY(context_open_outside_action->isVisible());
      QVERIFY(context_open_outside_7zip_action->isVisible());
      QVERIFY(context_properties_action->isVisible());
      menu_rules_verified = true;

      const int key_delete_row = select_row_by_name(key_target_name);
      if (key_delete_row >= 0) {
        schedule_message_box_button_click(QMessageBox::Yes);
        table->setCurrentCell(key_delete_row, 0);
        table->clearSelection();
        table->selectRow(key_delete_row);
        QTest::keyClick(table, Qt::Key_Delete);
        QTest::qWait(20);
        key_deleted = !QFileInfo::exists(key_target_path);
      }

      const int button_delete_row = select_row_by_name(button_target_name);
      if (button_delete_row >= 0) {
        schedule_message_box_button_click(QMessageBox::Yes);
        table->setCurrentCell(button_delete_row, 0);
        table->clearSelection();
        table->selectRow(button_delete_row);
        delete_button->click();
        QTest::qWait(20);
        button_deleted = !QFileInfo::exists(button_target_path);
      }

      const int context_delete_row = select_row_by_name(context_target_name);
      if (context_delete_row >= 0) {
        schedule_message_box_button_click(QMessageBox::Yes);
        table->setCurrentCell(context_delete_row, 0);
        table->clearSelection();
        table->selectRow(context_delete_row);
        const QString text = action_caption(context_delete_action);
        if ((!delete_label.isEmpty() && text == delete_label) ||
            text.contains(QStringLiteral("Delete"), Qt::CaseInsensitive)) {
          context_delete_action->trigger();
        }
        QTest::qWait(40);
        context_deleted = !QFileInfo::exists(context_target_path);
      }

      close_button->click();
      timer->stop();
      timer->deleteLater();
      return;
    }
  });
  timer->start();

  z7::ui::filemanager::TempFilesDialog dialog(fake_temp_root.path());
  dialog.exec();

  QVERIFY(dialog_seen);
  QVERIFY(menu_rules_verified);
  QVERIFY(properties_dialog_seen);
  QVERIFY(key_deleted);
  QVERIFY(button_deleted);
  QVERIFY(context_deleted);
}

void FileManagerBehaviorTest::tempFilesDialogDeleteFailureShowsSystemErrorAndPath() {
#ifdef Q_OS_WIN
  QSKIP("Permission-based delete failure scenario is not stable on Windows.");
#endif

  QTemporaryDir fake_temp_root;
  QVERIFY(fake_temp_root.isValid());

  const QString blocked_dir_name = QStringLiteral("7zO44444444");
  const QString blocked_dir_path = QDir(fake_temp_root.path()).filePath(blocked_dir_name);
  QVERIFY(QDir().mkpath(blocked_dir_path));
  {
    QFile file(QDir(blocked_dir_path).filePath(QStringLiteral("child.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("blocked");
    file.close();
  }

  bool dialog_seen = false;
  bool stale_entry_removed = false;
  bool delete_failure_reported = false;
  QString warning_title;
  QString warning_text;

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
      auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("tempFilesTable"));
      auto* delete_button =
          dialog->findChild<QPushButton*>(QStringLiteral("tempFilesDeleteButton"));
      auto* close_button =
          dialog->findChild<QPushButton*>(QStringLiteral("tempFilesCloseButton"));
      if (table == nullptr || delete_button == nullptr || close_button == nullptr) {
        dialog->reject();
        timer->stop();
        timer->deleteLater();
        return;
      }

      int target_row = -1;
      for (int row = 0; row < table->rowCount(); ++row) {
        if (QTableWidgetItem* item = table->item(row, 0)) {
          if (item->text() == blocked_dir_name) {
            target_row = row;
            break;
          }
        }
      }
      if (target_row < 0) {
        close_button->click();
        timer->stop();
        timer->deleteLater();
        return;
      }

      table->setCurrentCell(target_row, 0);
      table->clearSelection();
      table->selectRow(target_row);
      stale_entry_removed = QDir(blocked_dir_path).removeRecursively();

      schedule_message_box_button_click(QMessageBox::Yes);
      schedule_message_box_capture_and_click(
          QMessageBox::Ok,
          &warning_title,
          &warning_text);
      delete_button->click();
      QTest::qWait(100);

      delete_failure_reported =
          stale_entry_removed && !QFileInfo::exists(blocked_dir_path) &&
          !warning_text.isEmpty();
      close_button->click();
      timer->stop();
      timer->deleteLater();
      return;
    }
  });
  timer->start();

  z7::ui::filemanager::TempFilesDialog dialog(fake_temp_root.path());
  dialog.exec();

  QVERIFY(dialog_seen);
  QVERIFY(stale_entry_removed);
  QVERIFY(delete_failure_reported);
  QVERIFY(!warning_text.isEmpty());

  const QString expected_title = localized_label(6107);
  if (!warning_title.isEmpty() &&
      !expected_title.isEmpty() &&
      !expected_title.startsWith(QLatin1Char('#'))) {
    QCOMPARE(warning_title, expected_title);
  }

  const QString native_path = QDir::toNativeSeparators(blocked_dir_path);
  QVERIFY(warning_text.contains(native_path));

  const QStringList lines = warning_text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
  QVERIFY(!lines.isEmpty());
  QVERIFY(!lines.front().trimmed().isEmpty());
}
