// tests/filemanager/behavior/setup_capability.cpp
// Role: Capability key/reason matrix and placeholder-guard behavior cases.

#include "internal.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::actionCapabilityMatrixUsesUnifiedReasonKey() {
    z7::ui::filemanager::MainWindow window;
    window.refresh_action_states();
    const bool windows_supported =
        z7::ui::runtime_support::is_platform_supported(
            z7::ui::runtime_support::PlatformSupport::kWindowsOnly);
    const bool has_rows =
        window.active_panel_controller().ui.details_view != nullptr &&
        window.active_panel_controller().ui.details_view->model() != nullptr &&
        window.active_panel_controller().ui.details_view->model()->rowCount() > 0;
    struct ExpectedActionState {
      QAction* action = nullptr;
      const char* expected_key = nullptr;
      const char* expected_reason = nullptr;
      bool enabled = false;
    };

    const std::array<ExpectedActionState, 12> expected = {{
        {window.split_action_, "", "", false},
        {window.combine_action_, "", "", false},
        {window.comment_action_, "", "", false},
        {window.link_action_, "", "", false},
        {window.alternate_streams_action_,
         windows_supported ? "" : "AlternateStreams",
         windows_supported ? "" : kWindowsOnlyReason,
         false},
        {window.select_action_, "", "", has_rows},
        {window.deselect_action_, "", "", has_rows},
        {window.select_by_type_action_, "", "", false},
        {window.deselect_by_type_action_, "", "", false},
        {add_to_favorites_action(window), "", "", true},
        {window.contents_action_, "", "", false},
        {window.temp_files_action_, "", "", true},
    }};

    for (const ExpectedActionState& item : expected) {
      QVERIFY(item.action != nullptr);
      QCOMPARE(item.action->isEnabled(), item.enabled);
      QCOMPARE(capability_key(item.action), QString::fromLatin1(item.expected_key));
      QCOMPARE(capability_reason(item.action), QString::fromLatin1(item.expected_reason));
    }

    QVERIFY(window.alternate_streams_action_ != nullptr);
    if (windows_supported) {
      QVERIFY(!without_mnemonic(window.alternate_streams_action_->text())
                   .contains(QStringLiteral("Windows")));
      QCOMPARE(window.alternate_streams_action_->toolTip(), QString());
    } else {
      QVERIFY(without_mnemonic(window.alternate_streams_action_->text())
                  .contains(QStringLiteral("Windows")));
      QVERIFY(window.alternate_streams_action_->toolTip().contains(QStringLiteral("Windows")));
    }
  }

void FileManagerBehaviorTest::placeholderActionsShowNotImplementedNotice() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_file = QDir(root.path()).filePath(QStringLiteral("pack.7z"));
    QFile file(archive_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("dummy archive");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    window.refresh_action_states();

    const QSet<QString> allowed_capability_keys = {
        QStringLiteral("Split"),
        QStringLiteral("Combine")};

    const QList<QAction*> static_actions = window.findChildren<QAction*>();
    for (QAction* action : static_actions) {
      if (action == nullptr) {
        continue;
      }
      const QString reason = capability_reason(action);
      if (reason != QString::fromLatin1(kBackendUnsupportedReason)) {
        continue;
      }

      const QString key = capability_key(action);
      QVERIFY2(allowed_capability_keys.contains(key),
               qPrintable(QStringLiteral("Unexpected backend-unsupported reason on action key: %1")
                              .arg(key)));
    }

    const int row = row_by_name(window, QStringLiteral("pack.7z"));
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});
    const auto state = window.compute_seven_zip_menu_state(false);
    QMenu menu;
    QMenu* seven = window.append_seven_zip_submenu(&menu, state);
    QVERIFY(seven != nullptr);

    QList<QMenu*> pending{seven};
    while (!pending.isEmpty()) {
      QMenu* current = pending.takeFirst();
      for (QAction* action : current->actions()) {
        if (action == nullptr) {
          continue;
        }
        if (action->menu() != nullptr) {
          pending << action->menu();
        }
        QVERIFY2(capability_reason(action) != QString::fromLatin1(kBackendUnsupportedReason),
                 qPrintable(QStringLiteral("7-Zip submenu action still carries backend-unsupported reason: %1")
                                .arg(action->text())));
      }
    }
  }
