// tests/filemanager/behavior/setup_helpers_ui.cpp
// Role: UI helper and utility functions for filemanager behavior tests.

#include "internal.h"

namespace filemanager_behavior_internal {

namespace {

QList<QPointer<QTimer>>& scheduled_message_box_handlers() {
  static QList<QPointer<QTimer>> handlers;
  return handlers;
}

void register_message_box_handler(QTimer* timer) {
  if (timer != nullptr) {
    scheduled_message_box_handlers().append(QPointer<QTimer>(timer));
  }
}

void unregister_message_box_handler(QTimer* timer) {
  scheduled_message_box_handlers().removeIf(
      [timer](const QPointer<QTimer>& handler) {
        return handler.isNull() || handler.data() == timer;
      });
}

void stop_message_box_handler(QTimer* timer) {
  if (timer == nullptr) {
    return;
  }
  unregister_message_box_handler(timer);
  timer->stop();
  timer->deleteLater();
}

}  // namespace

void clear_runtime_settings() {
  z7::platform::qt::PortableSettings settings;
  settings.clear();
  settings.sync();

  z7::platform::qt::PortableSettings large_pages(
      QString(), QStringLiteral("7zFM"));
  large_pages.clear();
  large_pages.sync();

  z7::platform::qt::PortableSettings shared(
      QCoreApplication::organizationName(),
      QStringLiteral("7z-shared"));
  shared.clear();
  shared.sync();
}

QJsonObject read_settings_json_root() {
  QFile file(z7::platform::qt::portable_settings_file_path());
  if (!file.open(QIODevice::ReadOnly)) {
    return QJsonObject{};
  }
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    return QJsonObject{};
  }
  return doc.object();
}

void close_checksum_dialogs() {
  const QString checksum_title = z7::ui::runtime_support::L(7501);
  const QWidgetList top_levels = QApplication::topLevelWidgets();
  for (QWidget* widget : top_levels) {
    auto* dialog = qobject_cast<QDialog*>(widget);
    if (dialog == nullptr) {
      continue;
    }
    if (dialog->windowTitle() == checksum_title) {
      dialog->close();
    }
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void close_message_boxes() {
  const QWidgetList top_levels = QApplication::topLevelWidgets();
  for (QWidget* widget : top_levels) {
    auto* box = qobject_cast<QMessageBox*>(widget);
    if (box == nullptr) {
      continue;
    }
    box->done(QMessageBox::Ok);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void close_test_result_dialogs(QString* captured_text) {
  const QWidgetList top_levels = QApplication::topLevelWidgets();
  for (QWidget* widget : top_levels) {
    auto* dialog = qobject_cast<QDialog*>(widget);
    if (dialog == nullptr ||
        dialog->objectName() != QStringLiteral("testResultDialog")) {
      continue;
    }
    if (captured_text != nullptr) {
      if (QLabel* label =
              dialog->findChild<QLabel*>(QStringLiteral("testResultTextLabel"))) {
        *captured_text = label->text();
      }
    }
    dialog->accept();
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void cancel_scheduled_message_box_handlers() {
  const QList<QPointer<QTimer>> handlers = scheduled_message_box_handlers();
  scheduled_message_box_handlers().clear();
  for (const QPointer<QTimer>& timer : handlers) {
    if (!timer.isNull()) {
      timer->stop();
      timer->deleteLater();
    }
  }
}

void schedule_message_box_autoclose(int duration_ms, int interval_ms) {
  auto* timer = new QTimer(QApplication::instance());
  register_message_box_handler(timer);
  timer->setInterval(interval_ms);
  QObject::connect(timer, &QTimer::timeout, []() { close_message_boxes(); });
  timer->start();
  QTimer::singleShot(duration_ms, timer, [timer]() {
    close_message_boxes();
    stop_message_box_handler(timer);
  });
}

void schedule_message_box_button_click(QMessageBox::StandardButton button,
                                       int duration_ms,
                                       int interval_ms) {
  auto* timer = new QTimer(QApplication::instance());
  register_message_box_handler(timer);
  timer->setInterval(interval_ms);
  QObject::connect(timer, &QTimer::timeout, [timer, button]() {
    const QWidgetList top_levels = QApplication::topLevelWidgets();
    for (QWidget* widget : top_levels) {
      auto* box = qobject_cast<QMessageBox*>(widget);
      if (box == nullptr) {
        continue;
      }
      if (QAbstractButton* target = box->button(button)) {
        target->click();
      } else {
        box->reject();
      }
      stop_message_box_handler(timer);
      return;
    }
  });
  timer->start();
  QTimer::singleShot(duration_ms, timer, [timer]() {
    stop_message_box_handler(timer);
  });
}

void schedule_message_box_capture_and_click(QMessageBox::StandardButton button,
                                            QString* captured_title,
                                            QString* captured_text,
                                            int duration_ms,
                                            int interval_ms) {
  auto* timer = new QTimer(QApplication::instance());
  register_message_box_handler(timer);
  timer->setInterval(interval_ms);
  QObject::connect(
      timer,
      &QTimer::timeout,
      [timer, button, captured_title, captured_text]() {
        QWidgetList candidates;
        if (QWidget* modal = QApplication::activeModalWidget()) {
          candidates << modal;
        }
        candidates << QApplication::topLevelWidgets();
        candidates << QApplication::allWidgets();
        QSet<QWidget*> seen;
        for (QWidget* widget : candidates) {
          if (widget == nullptr || seen.contains(widget)) {
            continue;
          }
          seen.insert(widget);
          auto* box = qobject_cast<QMessageBox*>(widget);
          if (box == nullptr || !box->isVisible()) {
            continue;
          }
          if (captured_title != nullptr) {
            *captured_title = box->windowTitle();
          }
          if (captured_text != nullptr) {
            *captured_text = box->text();
          }
          if (QAbstractButton* target = box->button(button)) {
            target->click();
          } else {
            box->reject();
          }
          stop_message_box_handler(timer);
          return;
        }
      });
  timer->start();
  QTimer::singleShot(duration_ms, timer, [timer]() {
    stop_message_box_handler(timer);
  });
}

void schedule_test_result_dialog_autoclose(QString* captured_text,
                                           int duration_ms,
                                           int interval_ms) {
  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(interval_ms);
  QObject::connect(timer, &QTimer::timeout, [timer, captured_text]() {
    QString text;
    close_test_result_dialogs(&text);
    if (!text.isEmpty()) {
      if (captured_text != nullptr) {
        *captured_text = text;
      }
      timer->stop();
      timer->deleteLater();
    }
  });
  timer->start();
  QTimer::singleShot(duration_ms, timer, [timer, captured_text]() {
    if (timer == nullptr) {
      return;
    }
    QString text;
    close_test_result_dialogs(&text);
    if (captured_text != nullptr && captured_text->isEmpty() && !text.isEmpty()) {
      *captured_text = text;
    }
    timer->stop();
    timer->deleteLater();
  });
}

void schedule_copy_move_dialog_submit(const QString& destination_path,
                                      bool accept,
                                      int duration_ms,
                                      int interval_ms) {
  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(interval_ms);
  QObject::connect(timer, &QTimer::timeout, [timer, destination_path, accept]() {
    QWidgetList candidates;
    if (QWidget* modal = QApplication::activeModalWidget()) {
      candidates << modal;
    }
    candidates << QApplication::topLevelWidgets();
    candidates << QApplication::allWidgets();
    for (QWidget* widget : candidates) {
      auto* dialog = qobject_cast<QDialog*>(widget);
      if (dialog == nullptr ||
          dialog->objectName() != QStringLiteral("copyMoveDialog")) {
        continue;
      }

      auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("copyMoveDestinationCombo"));
      if (combo != nullptr) {
        combo->setEditText(QDir::toNativeSeparators(destination_path));
      }

      if (accept) {
        dialog->accept();
      } else {
        dialog->reject();
      }

      timer->stop();
      timer->deleteLater();
      return;
    }
  });
  timer->start();
  QTimer::singleShot(duration_ms, timer, [timer]() {
    timer->stop();
    timer->deleteLater();
  });
}

void schedule_input_dialog_submit(const QString& value,
                                  bool accept,
                                  int duration_ms,
                                  int interval_ms) {
  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(interval_ms);
  QObject::connect(timer, &QTimer::timeout, [timer, value, accept]() {
    QWidgetList candidates;
    if (QWidget* modal = QApplication::activeModalWidget()) {
      candidates << modal;
    }
    candidates << QApplication::topLevelWidgets();
    candidates << QApplication::allWidgets();
    QSet<QWidget*> seen;
    for (QWidget* widget : candidates) {
      if (widget == nullptr || seen.contains(widget)) {
        continue;
      }
      seen.insert(widget);
      auto* dialog = qobject_cast<QInputDialog*>(widget);
      if (dialog == nullptr) {
        continue;
      }
      if (dialog->inputMode() == QInputDialog::IntInput) {
        bool ok = false;
        const int int_value = value.toInt(&ok);
        if (ok) {
          dialog->setIntValue(int_value);
        }
      } else {
        dialog->setTextValue(value);
      }
      if (QDialogButtonBox* buttons = dialog->findChild<QDialogButtonBox*>()) {
        if (QPushButton* button = buttons->button(accept ? QDialogButtonBox::Ok
                                                         : QDialogButtonBox::Cancel)) {
          button->click();
        } else if (accept) {
          dialog->accept();
        } else {
          dialog->reject();
        }
      } else if (accept) {
        dialog->accept();
      } else {
        dialog->reject();
      }
      timer->stop();
      timer->deleteLater();
      return;
    }
  });
  timer->start();
  QTimer::singleShot(duration_ms, timer, [timer]() {
    timer->stop();
    timer->deleteLater();
  });
}

void schedule_folders_history_dialog_interaction(
    const std::function<void(QDialog*)>& handler,
    int duration_ms,
    int interval_ms) {
  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(interval_ms);
  QObject::connect(timer, &QTimer::timeout, [timer, handler]() {
    QWidgetList candidates;
    if (QWidget* modal = QApplication::activeModalWidget()) {
      candidates << modal;
    }
    candidates << QApplication::topLevelWidgets();
    QSet<QWidget*> seen;
    for (QWidget* widget : candidates) {
      if (widget == nullptr || seen.contains(widget)) {
        continue;
      }
      seen.insert(widget);
      auto* dialog = qobject_cast<QDialog*>(widget);
      if (dialog == nullptr ||
          dialog->objectName() != QStringLiteral("foldersHistoryDialog")) {
        continue;
      }
      handler(dialog);
      timer->stop();
      timer->deleteLater();
      return;
    }
  });
  timer->start();
  QTimer::singleShot(duration_ms, timer, [timer]() {
    timer->stop();
    timer->deleteLater();
  });
}

void schedule_link_dialog_interaction(const std::function<void(QDialog*)>& handler,
                                      int duration_ms,
                                      int interval_ms) {
  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(interval_ms);
  QObject::connect(timer, &QTimer::timeout, [timer, handler]() {
    QWidgetList candidates;
    if (QWidget* modal = QApplication::activeModalWidget()) {
      candidates << modal;
    }
    candidates << QApplication::topLevelWidgets();
    QSet<QWidget*> seen;
    for (QWidget* widget : candidates) {
      if (widget == nullptr || seen.contains(widget)) {
        continue;
      }
      seen.insert(widget);
      auto* dialog = qobject_cast<QDialog*>(widget);
      if (dialog == nullptr ||
          dialog->objectName() != QStringLiteral("linkDialog")) {
        continue;
      }
      handler(dialog);
      timer->stop();
      timer->deleteLater();
      return;
    }
  });
  timer->start();
  QTimer::singleShot(duration_ms, timer, [timer]() {
    timer->stop();
    timer->deleteLater();
  });
}

void schedule_overwrite_prompt_submit(OverwritePromptChoice choice,
                                      bool* seen_dialog,
                                      int duration_ms,
                                      int interval_ms) {
  if (seen_dialog != nullptr) {
    *seen_dialog = false;
  }
  auto* timer = new QTimer(QApplication::instance());
  timer->setInterval(interval_ms);
  QObject::connect(timer, &QTimer::timeout, [timer, choice, seen_dialog]() {
    const QWidgetList top_levels = QApplication::topLevelWidgets();
    for (QWidget* widget : top_levels) {
      auto* box = qobject_cast<QMessageBox*>(widget);
      if (box == nullptr || box->objectName() != QStringLiteral("overwritePromptDialog")) {
        continue;
      }
      if (seen_dialog != nullptr) {
        *seen_dialog = true;
      }

      QAbstractButton* target = nullptr;
      switch (choice) {
        case OverwritePromptChoice::kYes:
          target = box->button(QMessageBox::Yes);
          break;
        case OverwritePromptChoice::kNo:
          target = box->button(QMessageBox::No);
          break;
        case OverwritePromptChoice::kCancel:
          target = box->button(QMessageBox::Cancel);
          break;
        case OverwritePromptChoice::kYesToAll: {
          const QString expected = without_mnemonic(z7::ui::runtime_support::L(440)).trimmed();
          for (QAbstractButton* button : box->buttons()) {
            if (button != nullptr &&
                without_mnemonic(button->text()).trimmed() == expected) {
              target = button;
              break;
            }
          }
          break;
        }
        case OverwritePromptChoice::kNoToAll: {
          const QString expected = without_mnemonic(z7::ui::runtime_support::L(441)).trimmed();
          for (QAbstractButton* button : box->buttons()) {
            if (button != nullptr &&
                without_mnemonic(button->text()).trimmed() == expected) {
              target = button;
              break;
            }
          }
          break;
        }
        case OverwritePromptChoice::kAutoRename: {
          QString expected = without_mnemonic(z7::ui::runtime_support::L(3424)).trimmed();
          if (expected.isEmpty() || expected.startsWith(QLatin1Char('#'))) {
            expected = QStringLiteral("Auto rename");
          }
          for (QAbstractButton* button : box->buttons()) {
            if (button == nullptr) {
              continue;
            }
            const QString text = without_mnemonic(button->text()).trimmed();
            if (text == expected ||
                text.contains(QStringLiteral("Auto rename"), Qt::CaseInsensitive)) {
              target = button;
              break;
            }
          }
          break;
        }
      }

      if (target != nullptr) {
        target->click();
      } else {
        box->reject();
      }
      timer->stop();
      timer->deleteLater();
      return;
    }
  });
  timer->start();
  QTimer::singleShot(duration_ms, timer, [timer]() {
    timer->stop();
    timer->deleteLater();
  });
}

QString capability_key(const QAction* action) {
  if (action == nullptr) {
    return QString();
  }
  return action->property(kCapabilityKeyProperty).toString();
}

QString capability_reason(const QAction* action) {
  if (action == nullptr) {
    return QString();
  }
  return action->property(kCapabilityReasonProperty).toString();
}

QString without_mnemonic(QString value) {
  value.remove(QLatin1Char('&'));
  return value;
}

QString localized_label(uint32_t id) {
  return without_mnemonic(z7::ui::runtime_support::L(id)).trimmed();
}

quint64 max_completed_bytes_from_progress(const QSignalSpy& progress_spy) {
  quint64 value = 0;
  for (int i = 0; i < progress_spy.count(); ++i) {
    const QList<QVariant> args = progress_spy.at(i);
    if (args.size() < 3) {
      continue;
    }
    value = std::max(value, args.at(2).toULongLong());
  }
  return value;
}

bool spy_contains_stage(const QSignalSpy& stage_spy, const QString& expected) {
  for (int i = 0; i < stage_spy.count(); ++i) {
    const QList<QVariant> args = stage_spy.at(i);
    if (args.isEmpty()) {
      continue;
    }
    if (args.at(0).toString() == expected) {
      return true;
    }
  }
  return false;
}

}  // namespace filemanager_behavior_internal

// End of setup_helpers_ui.cpp
