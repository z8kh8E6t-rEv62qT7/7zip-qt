// tests/filemanager/behavior/benchmark.cpp
// Role: Benchmark action route behavior cases.

#include "internal.h"

#include <QStatusBar>

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::benchmarkActionsUseSevenZipBridgeInFilesystemMode() {
  using namespace z7::ui::gui;
  using namespace z7::ui::gui::bridge_internal;

  reset_bridge_segments_for_test();
  z7::ui::filemanager::MainWindow window;

  window.on_benchmark_requested();
  QVERIFY(!window.task_ipc_owner_instance_id_.isEmpty());
  QVERIFY(window.panels_[0].ui.status_transient_message != nullptr);
  QVERIFY(window.panels_[0].ui.status_transient_message->text()
              .contains(QStringLiteral("7zG benchmark")));
  QCOMPARE(window.findChildren<QStatusBar*>().size(), 2);
  QVERIFY(window.findChildren<QStatusBar*>(
              QString(), Qt::FindDirectChildrenOnly).isEmpty());
  BridgeSlotRaw first_slot;
  int first_slot_index = -1;
  QVERIFY(find_bridge_slot_for_owner_and_command(
      window.task_ipc_owner_instance_id_, BridgeCommandKind::kBenchmark, &first_slot, &first_slot_index));
  QVERIFY(first_slot.request_payload_size > 0);
  QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
  QVERIFY(filemanager_behavior_internal::current_progress_dialog(window) == nullptr);

  window.on_benchmark2_requested();
  QVERIFY(window.panels_[0].ui.status_transient_message->text()
              .contains(QStringLiteral("7zG benchmark")));
  QCOMPARE(window.findChildren<QStatusBar*>().size(), 2);
  BridgeSlotRaw second_slot;
  int second_slot_index = -1;
  QVERIFY(find_bridge_slot_for_owner_and_command(
      window.task_ipc_owner_instance_id_, BridgeCommandKind::kBenchmark, &second_slot, &second_slot_index));
  QVERIFY(second_slot.request_payload_size > 0);
  QVERIFY(second_slot.session_id >= first_slot.session_id);
  QVERIFY(filemanager_behavior_internal::current_runner(window) == nullptr);
  QVERIFY(filemanager_behavior_internal::current_progress_dialog(window) == nullptr);
}
