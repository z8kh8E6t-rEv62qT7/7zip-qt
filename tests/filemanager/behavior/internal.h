#pragma once

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QNativeIpcKey>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSet>
#include <QSharedMemory>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <limits>
#include <optional>
#include <string>

#ifndef Q_OS_WIN
#include <sys/mman.h>
#endif

#include "app_startup_qt.h"
#include "file_open_support.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#define protected public
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "main_window.h"
#include "main_window/drag_drop/drag_aware_views.h"
#undef protected
#undef private

#include "archive_session.h"
#include "filemanager_instance_launcher.h"
#include "portable_settings.h"
#include "platform_support.h"
#include "archive_process_runner.h"
#include "checksum_result_dialog.h"
#include "task_ipc_runtime.h"
#include "../../../src/task_ipc_runtime/src/task_ipc_runtime_internal.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "official_lang_catalog.h"
#undef private
#include "options_dialog.h"
#include "filemanager_task_progress_dialog.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "dialogs/temp_files/temp_files_dialog.h"
#undef private

namespace z7::ui::gui {

using BridgeCommandKind = z7::task_ipc_runtime::TaskIpcCommandKind;
using BridgeSlotState = z7::task_ipc_runtime::TaskIpcSlotState;

struct BridgeTaskPayload {
  BridgeCommandKind command = BridgeCommandKind::kNone;
  bool show_dialog = false;
  bool refresh_after_finish = true;
  QString caption;
  QString archive_path;
  QString archive_type;
  QStringList archive_paths;
  QString output_dir;
  bool extract_split_dest_enabled = false;
  QString extract_split_dest_name;
  bool extract_eliminate_root_duplication = false;
  QString extract_zone_id_mode;
  QStringList nested_archive_entries;
  QString archive_export_overwrite_mode;
  QString archive_export_path_mode;
  bool archive_export_eliminate_root_duplication = false;
  bool archive_export_restore_file_security = false;
  QString archive_export_zone_id_mode;
  QString hash_method;
  QStringList input_paths;
  QStringList cli_argv;
  QString cli_working_dir;
};

struct BridgeDispatchResult {
  quint64 session_id = 0;
  quint32 generation = 0;
  qint64 worker_pid = 0;
};

struct BridgeClaimedTask {
  int slot_index = -1;
  quint64 session_id = 0;
  quint32 generation = 0;
  QString ipc_shm_name;
  QString ipc_sem_name;
  QString owner_instance_id;
  qint64 launcher_pid = 0;
  qint64 worker_pid = 0;
  BridgeTaskPayload payload;
};

inline BridgeTaskPayload bridge_payload_from_task_ipc_payload(
    const z7::task_ipc_runtime::TaskIpcPayload& payload) {
  BridgeTaskPayload out;
  out.command = static_cast<BridgeCommandKind>(payload.command);
  out.show_dialog = payload.show_dialog;
  out.refresh_after_finish = payload.refresh_after_finish;
  out.caption = payload.caption;
  if (payload.add.has_value()) {
    out.archive_path = payload.add->archive_path;
    out.archive_type = payload.add->archive_type;
    out.input_paths = payload.add->input_paths;
  }
  if (payload.extract.has_value()) {
    out.archive_paths = payload.extract->archive_inputs;
    out.archive_type = payload.extract->archive_type;
    out.output_dir = payload.extract->output_dir;
    out.extract_split_dest_enabled = payload.extract->split_dest_enabled;
    out.extract_split_dest_name = payload.extract->split_dest_name;
    out.extract_eliminate_root_duplication =
        payload.extract->eliminate_root_duplication;
    out.extract_zone_id_mode = payload.extract->zone_id_mode;
  }
  if (payload.archive_export.has_value()) {
    out.archive_path = payload.archive_export->root_archive_path;
    out.archive_type = payload.archive_export->root_archive_type;
    out.nested_archive_entries =
        payload.archive_export->nested_archive_entries;
    out.archive_paths = payload.archive_export->archive_entry_paths;
    out.output_dir = payload.archive_export->output_dir;
    out.archive_export_overwrite_mode =
        payload.archive_export->overwrite_mode;
    out.archive_export_path_mode = payload.archive_export->path_mode;
    out.archive_export_eliminate_root_duplication =
        payload.archive_export->eliminate_root_duplication;
    out.archive_export_restore_file_security =
        payload.archive_export->restore_file_security;
    out.archive_export_zone_id_mode =
        payload.archive_export->zone_id_mode;
  }
  if (payload.test.has_value()) {
    out.archive_paths = payload.test->archive_inputs;
  }
  if (payload.hash.has_value()) {
    out.hash_method = payload.hash->hash_method;
    out.input_paths = payload.hash->input_paths;
  }
  if (payload.cli.has_value()) {
    out.cli_argv = payload.cli->argv;
    out.cli_working_dir = payload.cli->working_dir;
  }
  return out;
}

inline z7::task_ipc_runtime::TaskIpcPayload task_ipc_payload_from_bridge_payload(
    const BridgeTaskPayload& payload) {
  z7::task_ipc_runtime::TaskIpcPayload out;
  out.command =
      static_cast<z7::task_ipc_runtime::TaskIpcCommandKind>(payload.command);
  out.show_dialog = payload.show_dialog;
  out.refresh_after_finish = payload.refresh_after_finish;
  out.caption = payload.caption;

  switch (payload.command) {
    case BridgeCommandKind::kAdd:
      out.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
      out.add->archive_path = payload.archive_path;
      out.add->archive_type = payload.archive_type;
      out.add->input_paths = payload.input_paths;
      break;
    case BridgeCommandKind::kExtract:
      out.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
      out.extract->archive_inputs = payload.archive_paths;
      out.extract->archive_type = payload.archive_type;
      out.extract->output_dir = payload.output_dir;
      out.extract->split_dest_enabled = payload.extract_split_dest_enabled;
      out.extract->split_dest_name = payload.extract_split_dest_name;
      out.extract->eliminate_root_duplication =
          payload.extract_eliminate_root_duplication;
      out.extract->zone_id_mode = payload.extract_zone_id_mode;
      break;
    case BridgeCommandKind::kTest:
      out.test = z7::task_ipc_runtime::TaskIpcTestPayload{};
      out.test->archive_inputs = payload.archive_paths;
      break;
    case BridgeCommandKind::kHash:
      out.hash = z7::task_ipc_runtime::TaskIpcHashPayload{};
      out.hash->hash_method = payload.hash_method;
      out.hash->input_paths = payload.input_paths;
      break;
    case BridgeCommandKind::kBenchmark:
      out.benchmark = z7::task_ipc_runtime::TaskIpcBenchmarkPayload{};
      break;
    case BridgeCommandKind::kOpen:
      out.open = z7::task_ipc_runtime::TaskIpcOpenPayload{};
      out.open->archive_path = payload.archive_path;
      out.open->archive_type = payload.archive_type;
      break;
    case BridgeCommandKind::kArchiveExport:
      out.archive_export =
          z7::task_ipc_runtime::TaskIpcArchiveExportPayload{};
      out.archive_export->root_archive_path = payload.archive_path;
      out.archive_export->root_archive_type = payload.archive_type;
      out.archive_export->nested_archive_entries =
          payload.nested_archive_entries;
      out.archive_export->archive_entry_paths = payload.archive_paths;
      out.archive_export->output_dir = payload.output_dir;
      out.archive_export->overwrite_mode =
          payload.archive_export_overwrite_mode;
      out.archive_export->path_mode = payload.archive_export_path_mode;
      out.archive_export->eliminate_root_duplication =
          payload.archive_export_eliminate_root_duplication;
      out.archive_export->restore_file_security =
          payload.archive_export_restore_file_security;
      out.archive_export->zone_id_mode =
          payload.archive_export_zone_id_mode;
      break;
    case BridgeCommandKind::kCli:
      out.cli = z7::task_ipc_runtime::TaskIpcCliPayload{};
      out.cli->argv = payload.cli_argv;
      out.cli->working_dir = payload.cli_working_dir;
      break;
    case BridgeCommandKind::kNone:
    default:
      break;
  }

  return out;
}

inline QString bridge_bootstrap_key() {
  return z7::task_ipc_runtime::task_ipc_bootstrap_key();
}

inline QString bridge_request_pool_key() {
  return z7::task_ipc_runtime::task_ipc_request_pool_key();
}

inline bool ensure_bridge_bootstrap_ready(QString* error_message) {
  return z7::task_ipc_runtime::ensure_task_ipc_bootstrap_ready(error_message);
}

inline bool dispatch_bridge_task(const QString& worker_program,
                                 const QString& working_dir,
                                 const QString& owner_instance_id,
                                 const BridgeTaskPayload& payload,
                                 BridgeDispatchResult* out_result,
                                 QString* error_message) {
  z7::task_ipc_runtime::TaskIpcDispatchResult dispatch_result;
  const bool ok = z7::task_ipc_runtime::dispatch_task_ipc_task(
      worker_program,
      working_dir,
      owner_instance_id,
      task_ipc_payload_from_bridge_payload(payload),
      &dispatch_result,
      error_message);
  if (ok && out_result != nullptr) {
    out_result->session_id = dispatch_result.session_id;
    out_result->generation = dispatch_result.generation;
    out_result->worker_pid = dispatch_result.worker_pid;
  }
  return ok;
}

inline bool claim_bridge_task_for_worker(quint64 session_id,
                                         quint32 generation,
                                         BridgeClaimedTask* out_task,
                                         QString* error_message) {
  z7::task_ipc_runtime::TaskIpcClaimedTask claimed_task;
  const bool ok = z7::task_ipc_runtime::claim_task_ipc_task_for_worker(
      session_id,
      generation,
      &claimed_task,
      error_message);
  if (ok && out_task != nullptr) {
    out_task->slot_index = claimed_task.slot_index;
    out_task->session_id = claimed_task.session_id;
    out_task->generation = claimed_task.generation;
    out_task->ipc_shm_name = claimed_task.ipc_shm_name;
    out_task->ipc_sem_name = claimed_task.ipc_sem_name;
    out_task->owner_instance_id = claimed_task.owner_instance_id;
    out_task->launcher_pid = claimed_task.launcher_pid;
    out_task->worker_pid = claimed_task.worker_pid;
    out_task->payload = bridge_payload_from_task_ipc_payload(claimed_task.payload);
  }
  return ok;
}

inline bool release_bridge_task_slot(const BridgeClaimedTask& task,
                                     QString* error_message) {
  z7::task_ipc_runtime::TaskIpcClaimedTask claimed_task;
  claimed_task.slot_index = task.slot_index;
  claimed_task.session_id = task.session_id;
  claimed_task.generation = task.generation;
  claimed_task.ipc_shm_name = task.ipc_shm_name;
  claimed_task.ipc_sem_name = task.ipc_sem_name;
  claimed_task.owner_instance_id = task.owner_instance_id;
  claimed_task.launcher_pid = task.launcher_pid;
  claimed_task.worker_pid = task.worker_pid;
  claimed_task.payload = task_ipc_payload_from_bridge_payload(task.payload);
  return z7::task_ipc_runtime::publish_task_ipc_completion_minimal(
      claimed_task,
      0,
      error_message);
}

namespace bridge_internal {

using BridgeBootstrapRaw =
    z7::task_ipc_runtime::task_ipc_internal::TaskIpcBootstrapRaw;
using BridgeSlotRaw =
    z7::task_ipc_runtime::task_ipc_internal::TaskIpcSlotRaw;
using SharedMemoryLock =
    z7::task_ipc_runtime::task_ipc_internal::SharedMemoryLock;

inline const quint32 kBridgeMagic =
    z7::task_ipc_runtime::task_ipc_internal::kTaskIpcMagic;
inline const quint16 kBridgeVersion =
    z7::task_ipc_runtime::task_ipc_internal::kTaskIpcVersion;
inline const quint32 kBridgeRequestPoolMagic =
    z7::task_ipc_runtime::task_ipc_internal::kTaskIpcRequestPoolMagic;
inline const quint16 kBridgeRequestPoolVersion =
    z7::task_ipc_runtime::task_ipc_internal::kTaskIpcRequestPoolVersion;
inline const int kBridgeSlotCount =
    z7::task_ipc_runtime::task_ipc_internal::kTaskIpcSlotCount;
inline const int kBridgeRequestPoolSlotSize =
    z7::task_ipc_runtime::task_ipc_internal::kTaskIpcRequestPoolSlotSize;

inline auto* bootstrap_raw(QSharedMemory* memory) {
  return z7::task_ipc_runtime::task_ipc_internal::bootstrap_raw(memory);
}

inline const auto* bootstrap_raw(const QSharedMemory* memory) {
  return z7::task_ipc_runtime::task_ipc_internal::bootstrap_raw(memory);
}

inline auto* request_pool_raw(QSharedMemory* memory) {
  return z7::task_ipc_runtime::task_ipc_internal::request_pool_raw(memory);
}

inline const auto* request_pool_raw(const QSharedMemory* memory) {
  return z7::task_ipc_runtime::task_ipc_internal::request_pool_raw(memory);
}

inline auto* request_pool_slot_lock(
    z7::task_ipc_runtime::task_ipc_internal::TaskIpcRequestPoolHeaderRaw* raw,
    int slot_index) {
  return z7::task_ipc_runtime::task_ipc_internal::request_pool_slot_lock(
      raw,
      slot_index);
}

inline const auto* request_pool_slot_lock(
    const z7::task_ipc_runtime::task_ipc_internal::TaskIpcRequestPoolHeaderRaw* raw,
    int slot_index) {
  return z7::task_ipc_runtime::task_ipc_internal::request_pool_slot_lock(
      raw,
      slot_index);
}

inline void clear_slot(BridgeSlotRaw* slot, bool bump_generation) {
  z7::task_ipc_runtime::task_ipc_internal::clear_slot(slot, bump_generation);
}

inline QString read_fixed_utf8(const char* bytes, int capacity) {
  return z7::task_ipc_runtime::task_ipc_internal::read_fixed_utf8(bytes,
                                                                  capacity);
}

inline void write_fixed_utf8(const QString& value, char* out, int capacity) {
  z7::task_ipc_runtime::task_ipc_internal::write_fixed_utf8(value,
                                                            out,
                                                            capacity);
}

inline qint64 now_msecs() {
  return z7::task_ipc_runtime::task_ipc_internal::now_msecs();
}

inline bool open_bootstrap_memory(
    bool allow_create,
    std::shared_ptr<QSharedMemory>* out_memory,
    QString* error_message) {
  return z7::task_ipc_runtime::task_ipc_internal::open_bootstrap_memory(
      allow_create,
      out_memory,
      error_message);
}

inline bool open_request_pool_memory(
    bool allow_create,
    std::shared_ptr<QSharedMemory>* out_memory,
    QString* error_message) {
  return z7::task_ipc_runtime::task_ipc_internal::open_request_pool_memory(
      allow_create,
      out_memory,
      error_message);
}

inline QByteArray serialize_task_payload(
    const z7::ui::gui::BridgeTaskPayload& payload,
    QString* error_message) {
  return z7::task_ipc_runtime::task_ipc_internal::serialize_task_payload(
      z7::ui::gui::task_ipc_payload_from_bridge_payload(payload),
      error_message);
}

inline bool write_request_payload_to_slot(QSharedMemory* request_pool_memory,
                                          int slot_index,
                                          const QByteArray& payload,
                                          QString* error_message) {
  return z7::task_ipc_runtime::task_ipc_internal::write_request_payload_to_slot(
      request_pool_memory,
      slot_index,
      payload,
      error_message);
}

inline bool read_request_payload_from_slot(
    QSharedMemory* request_pool_memory,
    int slot_index,
    quint32 payload_size,
    z7::ui::gui::BridgeTaskPayload* out_payload,
    QString* error_message) {
  z7::task_ipc_runtime::TaskIpcPayload decoded_payload;
  if (!z7::task_ipc_runtime::task_ipc_internal::read_request_payload_from_slot(
          request_pool_memory,
          slot_index,
          payload_size,
          &decoded_payload,
          error_message)) {
    return false;
  }
  if (out_payload != nullptr) {
    *out_payload = z7::ui::gui::bridge_payload_from_task_ipc_payload(
        decoded_payload);
  }
  return true;
}

}  // namespace bridge_internal

}  // namespace z7::ui::gui

namespace filemanager_behavior_internal {

std::string to_native_path_string(const QString& path);

inline void detach_bridge_shared_memory_if_present(const QString& native_key) {
#ifndef Q_OS_WIN
  const QByteArray encoded_key = native_key.toUtf8();
  ::shm_unlink(encoded_key.constData());
#endif
  QSharedMemory memory;
#ifdef Q_OS_WIN
  memory.setNativeKey(native_key, QNativeIpcKey::Type::Windows);
#else
  memory.setNativeKey(native_key, QNativeIpcKey::Type::PosixRealtime);
#endif
  if (memory.attach(QSharedMemory::ReadWrite)) {
    memory.detach();
  }
}

inline void reset_bridge_segments_for_test() {
  z7::task_ipc_runtime::task_ipc_internal::update_bootstrap_memory_lease({});
  z7::task_ipc_runtime::task_ipc_internal::update_request_pool_memory_lease({});
  detach_bridge_shared_memory_if_present(
      z7::task_ipc_runtime::task_ipc_bootstrap_key());
  detach_bridge_shared_memory_if_present(
      z7::task_ipc_runtime::task_ipc_request_pool_key());
}

inline void detach_task_ipc_shared_memory_if_present(const QString& native_key) {
#ifndef Q_OS_MACOS
  detach_bridge_shared_memory_if_present(native_key);
#else
  Q_UNUSED(native_key);
#endif
}

inline void reset_task_ipc_segments_for_test() {
#if !defined(Q_OS_MACOS)
  using namespace z7::task_ipc_runtime::task_ipc_internal;
  update_bootstrap_memory_lease({});
  update_request_pool_memory_lease({});
  detach_task_ipc_shared_memory_if_present(z7::task_ipc_runtime::task_ipc_bootstrap_key());
  detach_task_ipc_shared_memory_if_present(z7::task_ipc_runtime::task_ipc_request_pool_key());
#endif
}

inline void reset_filemanager_open_launcher_for_test() {
  z7::platform::qt::filemanager_instance_launcher::reset_launch_override_for_testing();
}

inline void set_filemanager_open_launcher_override_for_test(
    z7::platform::qt::filemanager_instance_launcher::LaunchOverride override) {
  z7::platform::qt::filemanager_instance_launcher::set_launch_override_for_testing(
      std::move(override));
}

inline bool attach_shared_memory_for_test(const QString& native_key,
                                          QSharedMemory* memory,
                                          QString* error) {
  if (memory == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("Target shared memory handle is null.");
    }
    return false;
  }
#ifdef Q_OS_WIN
  memory->setNativeKey(native_key, QNativeIpcKey::Type::Windows);
#else
  memory->setNativeKey(native_key, QNativeIpcKey::Type::PosixRealtime);
#endif
  if (memory->attach(QSharedMemory::ReadWrite)) {
    return true;
  }
  if (error != nullptr) {
    *error = memory->errorString();
  }
  return false;
}

inline int find_slot_index_for_session(
    const z7::ui::gui::bridge_internal::BridgeBootstrapRaw& raw,
    quint64 session_id) {
  for (int i = 0; i < z7::ui::gui::bridge_internal::kBridgeSlotCount; ++i) {
    if (raw.slot_records[i].session_id == session_id) {
      return i;
    }
  }
  return -1;
}

inline bool find_bridge_slot_for_owner_and_command(
    const QString& owner_instance_id,
    z7::ui::gui::BridgeCommandKind command,
    z7::ui::gui::bridge_internal::BridgeSlotRaw* out_slot,
    int* out_slot_index = nullptr) {
  QVector<z7::task_ipc_runtime::TaskIpcEvent> events;
  QString collect_error;
  if (!z7::task_ipc_runtime::collect_task_ipc_events(owner_instance_id,
                                                     &events,
                                                     &collect_error)) {
    return false;
  }

  int best_index = -1;
  quint32 best_sequence = 0;
  z7::ui::gui::bridge_internal::BridgeSlotRaw best_slot{};
  for (int i = 0; i < events.size(); ++i) {
    const z7::task_ipc_runtime::TaskIpcEvent& event = events.at(i);
    if (event.event_kind != z7::task_ipc_runtime::TaskIpcEventKind::kDispatched ||
        event.payload.command !=
            static_cast<z7::task_ipc_runtime::TaskIpcCommandKind>(command)) {
      continue;
    }
    if (best_index >= 0 && event.event_sequence < best_sequence) {
      continue;
    }

    QString encode_error;
    const QByteArray encoded_payload =
        z7::ui::gui::bridge_internal::serialize_task_payload(
            z7::ui::gui::bridge_payload_from_task_ipc_payload(event.payload),
            &encode_error);

    best_index = i;
    best_sequence = event.event_sequence;
    best_slot = {};
    best_slot.generation = event.generation;
    best_slot.state = static_cast<quint32>(z7::ui::gui::BridgeSlotState::kDispatched);
    best_slot.session_id = event.session_id;
    best_slot.command_kind = static_cast<quint32>(command);
    best_slot.launcher_pid = 0;
    best_slot.worker_pid = event.worker_pid;
    best_slot.request_pool_slot = 0;
    best_slot.request_payload_size =
        static_cast<quint32>(encoded_payload.size());
    best_slot.updated_msecs = static_cast<qint64>(event.event_sequence);
    z7::ui::gui::bridge_internal::write_fixed_utf8(owner_instance_id,
                                                   best_slot.owner_instance_id,
                                                   64);
  }

  if (best_index < 0) {
    return false;
  }
  if (out_slot != nullptr) {
    *out_slot = best_slot;
  }
  if (out_slot_index != nullptr) {
    *out_slot_index = 0;
  }
  return true;
}

inline bool read_latest_bridge_payload_for_command(
    const z7::ui::filemanager::MainWindow& window,
    z7::ui::gui::BridgeCommandKind command,
    z7::ui::gui::BridgeTaskPayload* out_payload,
    QString* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (out_payload == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("Bridge payload output is null.");
    }
    return false;
  }
  if (window.task_ipc_owner_instance_id_.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Task IPC owner instance id is empty.");
    }
    return false;
  }

  QVector<z7::task_ipc_runtime::TaskIpcEvent> events;
  QString collect_error;
  if (!z7::task_ipc_runtime::collect_task_ipc_events(
          window.task_ipc_owner_instance_id_, &events, &collect_error)) {
    if (error != nullptr) {
      *error = collect_error;
    }
    return false;
  }

  quint32 best_sequence = 0;
  std::optional<z7::task_ipc_runtime::TaskIpcPayload> best_payload;
  for (const z7::task_ipc_runtime::TaskIpcEvent& event : events) {
    if (event.event_kind != z7::task_ipc_runtime::TaskIpcEventKind::kDispatched ||
        event.payload.command !=
            static_cast<z7::task_ipc_runtime::TaskIpcCommandKind>(command)) {
      continue;
    }
    if (best_payload.has_value() && event.event_sequence < best_sequence) {
      continue;
    }
    best_sequence = event.event_sequence;
    best_payload = event.payload;
  }

  if (!best_payload.has_value()) {
    if (error != nullptr) {
      *error = QStringLiteral("No task IPC payload found for command.");
    }
    return false;
  }

  *out_payload =
      z7::ui::gui::bridge_payload_from_task_ipc_payload(*best_payload);
  return true;
}

inline bool read_latest_task_ipc_payload_for_command(
    const z7::ui::filemanager::MainWindow& window,
    z7::task_ipc_runtime::TaskIpcCommandKind command,
    z7::task_ipc_runtime::TaskIpcPayload* out_payload,
    QString* error) {
  if (error != nullptr) {
    error->clear();
  }
  if (out_payload == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("Task IPC payload output is null.");
    }
    return false;
  }
  if (window.task_ipc_owner_instance_id_.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Task IPC owner instance id is empty.");
    }
    return false;
  }

  QVector<z7::task_ipc_runtime::TaskIpcEvent> events;
  QString collect_error;
  if (!z7::task_ipc_runtime::collect_task_ipc_events(
          window.task_ipc_owner_instance_id_, &events, &collect_error)) {
    if (error != nullptr) {
      *error = collect_error;
    }
    return false;
  }

  quint32 best_sequence = 0;
  std::optional<z7::task_ipc_runtime::TaskIpcPayload> best_payload;
  for (const z7::task_ipc_runtime::TaskIpcEvent& event : events) {
    if (event.event_kind != z7::task_ipc_runtime::TaskIpcEventKind::kDispatched ||
        event.payload.command != command) {
      continue;
    }
    if (best_payload.has_value() && event.event_sequence < best_sequence) {
      continue;
    }
    best_sequence = event.event_sequence;
    best_payload = event.payload;
  }

  if (!best_payload.has_value()) {
    if (error != nullptr) {
      *error = QStringLiteral("No task IPC payload found for command.");
    }
    return false;
  }

  *out_payload = std::move(*best_payload);
  return true;
}

class BlockingOutcomeDelegate final : public z7::app::IArchiveDelegate {
 public:
  explicit BlockingOutcomeDelegate(std::shared_ptr<z7::app::IArchiveDelegate> forward)
      : forward_(std::move(forward)) {}

  void on_lifecycle(z7::app::OperationStage stage, std::string_view message) override {
    if (forward_) {
      forward_->on_lifecycle(stage, message);
    }
  }

  void on_log(const z7::app::ArchiveLog& log) override {
    if (forward_) {
      forward_->on_log(log);
    }
  }

  void on_progress(const z7::app::ProgressSnapshot& progress) override {
    if (forward_) {
      forward_->on_progress(progress);
    }
  }

  void on_finished(const z7::app::OperationOutcome& outcome) override {
    if (forward_) {
      forward_->on_finished(outcome);
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      outcome_ = outcome;
      done_ = true;
    }
    cv_.notify_all();
  }

  std::optional<z7::app::OverwriteDecision> request_overwrite(
      const z7::app::OverwritePrompt& prompt) override {
    if (forward_) {
      return forward_->request_overwrite(prompt);
    }
    return std::nullopt;
  }

  std::optional<z7::app::PasswordReply> request_password(
      const z7::app::PasswordPrompt& prompt) override {
    if (forward_) {
      return forward_->request_password(prompt);
    }
    return std::nullopt;
  }

  std::optional<z7::app::ChoiceReply> request_choice(
      const z7::app::ChoicePrompt& prompt) override {
    if (forward_) {
      return forward_->request_choice(prompt);
    }
    return std::nullopt;
  }

  std::optional<z7::app::MemoryLimitReply> request_memory_limit(
      const z7::app::MemoryLimitPrompt& prompt) override {
    if (forward_) {
      return forward_->request_memory_limit(prompt);
    }
    return std::nullopt;
  }

  z7::app::OperationOutcome await_outcome() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return done_; });
    return outcome_.value_or(z7::app::OperationOutcome{});
  }

 private:
  std::shared_ptr<z7::app::IArchiveDelegate> forward_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::optional<z7::app::OperationOutcome> outcome_;
  bool done_ = false;
};

inline z7::app::OperationOutcome run_archive_request_and_await(
    const z7::app::ArchiveRequest& request,
    std::shared_ptr<z7::app::IArchiveDelegate> delegate = {}) {
  auto completion_delegate = std::make_shared<BlockingOutcomeDelegate>(std::move(delegate));
  z7::app::ArchiveEngine engine;
  z7::app::ArchiveSession session = engine.start(request, completion_delegate);
  if (session.valid()) {
    return completion_delegate->await_outcome();
  }

  z7::app::OperationOutcome outcome;
  outcome.status = z7::app::OperationStatus::kFailed;
  outcome.error_domain = z7::app::ArchiveErrorDomain::kBackendUnavailable;
  outcome.native_code = 2;
  outcome.ok = false;
  outcome.summary = "No archive backend available.";
  return outcome;
}

inline z7::ui::filemanager::ArchiveProcessRunner* current_runner(
    const z7::ui::filemanager::MainWindow& window) {
  if (window.active_runner_tasks_.isEmpty()) {
    return nullptr;
  }
  const auto& task = window.active_runner_tasks_.back();
  return task ? task->runner.data() : nullptr;
}

inline z7::ui::filemanager::TaskProgressDialog* current_progress_dialog(
    const z7::ui::filemanager::MainWindow& window) {
  if (window.active_runner_tasks_.isEmpty()) {
    return nullptr;
  }
  const auto& task = window.active_runner_tasks_.back();
  return task ? task->dialog.data() : nullptr;
}

inline QAction* add_to_favorites_action(
    const z7::ui::filemanager::MainWindow& window) {
  return window.add_to_favorites_menu_ != nullptr
             ? window.add_to_favorites_menu_->menuAction()
             : nullptr;
}

bool create_archive_via_backend(const QString& working_dir,
                                const QString& archive_path,
                                const QStringList& inputs,
                                QString* error);

QString create_sample_archive(const QTemporaryDir& root);
QString create_nested_archive(const QTemporaryDir& root);
QString create_archive_with_embedded_archive(const QTemporaryDir& root);
QString create_archive_with_embedded_archive_in_folder(const QTemporaryDir& root);
QString create_archive_with_same_name_embedded_archive(const QTemporaryDir& root);
QString create_archive_with_embedded_zip_archive(const QTemporaryDir& root,
                                                bool store_outer_archive);

int row_by_name(const z7::ui::filemanager::MainWindow& window, const QString& name);
QStringList first_column_items(const z7::ui::filemanager::MainWindow& window);
QIcon decoration_icon_for_name(const z7::ui::filemanager::MainWindow& window,
                               const QString& name);
bool icon_has_pixels(const QIcon& icon, int extent = 16);
bool icon_matches_resource(const QIcon& actual,
                           const QString& expected_resource_path,
                           int extent = 16);
void select_rows_in_active_panel(z7::ui::filemanager::MainWindow* window,
                                 const QList<int>& rows);

void clear_runtime_settings();
QJsonObject read_settings_json_root();
void close_checksum_dialogs();
void close_message_boxes();
void close_test_result_dialogs(QString* captured_text = nullptr);
void cancel_scheduled_message_box_handlers();
void schedule_message_box_autoclose(int duration_ms = 3000, int interval_ms = 10);
void schedule_message_box_button_click(QMessageBox::StandardButton button,
                                       int duration_ms = 3000,
                                       int interval_ms = 10);
void schedule_message_box_capture_and_click(QMessageBox::StandardButton button,
                                            QString* captured_title = nullptr,
                                            QString* captured_text = nullptr,
                                            int duration_ms = 3000,
                                            int interval_ms = 10);
void schedule_test_result_dialog_autoclose(QString* captured_text,
                                           int duration_ms = 3000,
                                           int interval_ms = 10);
void schedule_copy_move_dialog_submit(const QString& destination_path,
                                      bool accept = true,
                                      int duration_ms = 3000,
                                      int interval_ms = 10);
void schedule_input_dialog_submit(const QString& value,
                                  bool accept = true,
                                  int duration_ms = 3000,
                                  int interval_ms = 10);
void schedule_folders_history_dialog_interaction(
    const std::function<void(QDialog*)>& handler,
    int duration_ms = 3000,
    int interval_ms = 10);
void schedule_link_dialog_interaction(const std::function<void(QDialog*)>& handler,
                                      int duration_ms = 3000,
                                      int interval_ms = 10);
enum class OverwritePromptChoice {
  kYes,
  kNo,
  kYesToAll,
  kNoToAll,
  kAutoRename,
  kCancel
};
void schedule_overwrite_prompt_submit(OverwritePromptChoice choice,
                                      bool* seen_dialog = nullptr,
                                      int duration_ms = 3000,
                                      int interval_ms = 10);

inline constexpr char kCapabilityKeyProperty[] = "z7.fm.capability.key";
inline constexpr char kCapabilityReasonProperty[] = "z7.fm.capability.reason";
inline constexpr char kBackendUnsupportedReason[] = "BackendUnsupported";
inline constexpr char kWindowsOnlyReason[] = "WindowsOnly";
inline constexpr char kMacOnlyReason[] = "MacOnly";
inline constexpr char kLinuxOnlyReason[] = "LinuxOnly";
inline constexpr char kWinMimePerformedDropEffect[] =
    "application/x-qt-windows-mime;value=\"Performed DropEffect\"";
inline constexpr char kWinMimeLogicalPerformedDropEffect[] =
    "application/x-qt-windows-mime;value=\"Logical Performed DropEffect\"";
inline constexpr char kWinMimePerformedDropEffectUnquoted[] =
    "application/x-qt-windows-mime;value=Performed DropEffect";
inline constexpr char kWinMimeLogicalPerformedDropEffectUnquoted[] =
    "application/x-qt-windows-mime;value=Logical Performed DropEffect";

inline quint32 decode_dword_le(const QByteArray& payload) {
  if (payload.size() < 4) {
    return 0;
  }
  return static_cast<quint32>(static_cast<unsigned char>(payload[0])) |
         (static_cast<quint32>(static_cast<unsigned char>(payload[1])) << 8) |
         (static_cast<quint32>(static_cast<unsigned char>(payload[2])) << 16) |
         (static_cast<quint32>(static_cast<unsigned char>(payload[3])) << 24);
}

QString capability_key(const QAction* action);
QString capability_reason(const QAction* action);
QString without_mnemonic(QString value);
QString localized_label(uint32_t id);
quint64 max_completed_bytes_from_progress(const QSignalSpy& progress_spy);
bool spy_contains_stage(const QSignalSpy& stage_spy, const QString& expected);

}  // namespace filemanager_behavior_internal

class FileManagerBehaviorTest final : public QObject {
  Q_OBJECT

 private slots:
  void actionCapabilityMatrixUsesUnifiedReasonKey();
  void addActionInArchiveViewShowsUnsupportedNotice();
  void archiveAddSourcesDialogUsesFileManagerPickerUi();
  void archiveAddSourcesDialogNavigatesAndReturnsSelectedFilesystemPaths();
  void archiveParentLinkEntryNavigatesToParent();
  void archiveFolderHistoryRecordsVirtualDirsLikeOriginal();
  void archiveFolderHistorySelectionLeavesArchiveView();
  void archiveParentLinkVisibilityFollowsShowDotsSetting();
  void archiveParentLinkVisibilityRefreshesOnApplyRuntimeSettings();
  void crossPanelAltUpBindsOppositePanelToSameArchiveFolder();
  void crossPanelAltLeftRightBindOppositePanelToArchiveFocusedFolder();
  void crossPanelAltNavigationUsesArchiveParentTargets();
  void crossPanelAltNavigationSharesArchiveSessionState();
  void archivePathBarSupportsDisplayAbsoluteAndRelativeInputsWithoutLegacyMarker();
  void archivePreviewColumnsToggleBetweenArchiveAndFilesystem();
  void archiveTestCompletionShowsSummaryMessageBox();
  void archiveVirtualNavigationAndParentExit();
  void benchmark2VisibilityFollowsDiffSuperModeSetting();
  void benchmarkRunnerExecutesAndReportsOperationName();
  void benchmarkActionsUseSevenZipBridgeInFilesystemMode();
  void checksumDialogHeaderVisibleAndColumnWidthsPersist();
  void compressActionUsesSevenZipBridgeInFilesystemMode();
  void copyToConflictCancelLeavesSourceUntouched();
  void copyMoveRejectSelfTargetsBeforeConflictPrompt();
  void copyMoveConflictsUsePerFileOverwritePrompt();
  void copyMoveConflictCancelStopsBeforeLaterSources();
  void overwritePromptDefaultsToNo();
  void copyMoveShortcutsFollowOriginalF5F6Routes();
  void shiftCopyMoveShortcutsUseFocusedItemNameLikeOriginal();
  void archiveViewShiftCopyCopiesFocusedEntryWithinArchive();
  void archiveViewShiftMoveMovesFocusedEntryWithinArchive();
  void archiveViewShiftCopyRejectsExistingTargetName();
  void archiveViewShiftCopyCopiesFocusedDirectoryWithinArchive();
  void copyToSupportsSingleTargetRenameAndPersistsHistory();
  void copyToStoresFinalCorrectedDirectoryHistory();
  void copyToDoesNotSaveHistoryWhenDestinationDirectoryCreationFails();
  void copyToUsesCurrentPanelAsDefaultTargetInSinglePanelMode();
  void copyToUsesOtherPanelAsDefaultTargetInDualPanelMode();
  void copyMoveDialogInfoTextShowsOriginalItemSummary();
  void copyMoveToOppositeArchivePanelUsesArchiveWriteback();
  void createAndRenameUseRunnerTaskChain();
  void renameActionUsesFocusedItemInArchiveViewLikeOriginal();
  void filesystemRenameRejectsParentInvalidAndExistingTargets();
  void archiveRenameRejectsInvalidExistingAndMissingSession();
  void archiveRenameRejectsParentLink();
  void coreActionsAreSharedAcrossMenusAndToolbars();
  void versionControlMenuFrontendFollowsOriginalGatingAndStaysDisabled();
  void contextMenuIncludesSevenZipAndCoreActions();
  void contextMenuSevenZipSubmenuTracksSelectionState();
  void coreToolbarButtonsFollowSelectionAndContextRules();
  void crcMenuBuildsEnabledAndDisabledStates();
  void deleteInArchiveViewRequiresConfirmation();
  void deleteInFilesystemViewMovesItemToRecycleBin();
  void deleteRunnerDefaultsToRecycleBinAndCanBypassIt();
  void detailsHeadersAreInteractiveAndPerPanelWidthsPersistWithCorruptionFallback();
  void panelUiStatePersistsListModeFlatViewActivePanelAndSplitter();
  void mainWindowGeometryPersistsOriginalPositionPayload();
  void diffActionVisibilityAndExecutionFollowConfiguredProgram();
  void embeddedArchiveParentLinkReturnsToOuterArchive();
  void extractActionAcceptsTypeHints();
  void extractActionUsesExtractCommand();
  void extractActionUsesSevenZipBridgeInFilesystemMode();
  void extractActionFollowsExtractOptionSettings();
  void extractActionUsesSimpleDialogInArchiveView();
  void extractActionInArchiveViewUsesSessionWhenArchivePathMissing();
  void extractActionInArchiveViewUsesRuntimeOverwritePrompt();
  void extractActionUsesOperatedItemsAndRejectsDirectories();
  void extractActionInArchiveViewUsesOperSmartItems();
  void archiveExtractTestAndCrcUseOperSmartEntriesWithParentLinkSelection();
  void copyToInArchiveViewUsesArchiveExportRoute();
  void archiveExportStoresFinalCorrectedCopyHistory();
  void archiveExportFlatViewUsesNoPathsRoute();
  void archiveExportSubdirectoryUsesCurrentPathRemap();
  void archiveExportSingleTargetsUseDirectoryDestinations();
  void archiveExportZoneIdModeFollowsWriteZoneSetting();
  void archiveExportTaskSpecPreservesPayloadFields();
  void extractTaskSpecPreservesPayloadExtractOptions();
  void archiveExportRunnerExtractsMultiAndNestedMembers();
  void archiveExportRunnerHonorsCopyToPathModesAndRemaps();
  void archiveExportRunnerPropagatesZoneIdWhenSupported();
  void extractActionPassesMultipleArchivesAsArchivesNotEntries();
  void extractAndTestRunnerSupportMultiArchiveAndEntryPaths();
  void bridgeSharedMemoryNativeKeysMatchPlatformConventions();
  void bridgeExistingSharedMemorySegmentsAttachWithoutCreatePermission();
  void bridgeDispatchWritesPayloadIntoFixedRequestPoolSlot();
  void bridgeClaimReadsPayloadFromFixedRequestPoolSlot();
  void bridgeDispatchRejectsPayloadLargerThanFixedRequestPoolSlot();
  void bridgeReusedSlotOverwritesPriorRequestPoolPayload();
  void fileCrcUsesInternalHashTaskNotSevenzipLauncher();
  void fileCrcUsesOperSmartItems();
  void fileIconsUseArchiveFallbackAndRemainVisibleAcrossViewModes();
  void foldersHistoryDialogCancelKeepsPersistedListUnchanged();
  void foldersHistoryDialogDeletesEntriesPersistsAndOpensFocusedRow();
  void hashCancelDoesNotShowChecksumDialog();
  void hashCompletionShowsChecksumInformationDialog();
  void hashProgressDialogPauseBackgroundCancelButtonsAreDeterministic();
  void hashResultRowsFollowOriginalRules();
  void hashRunnerPauseResumeAndFinish();
  void init();
  void languageApplyImmediatelyRetranslatesMainWindow();
  void languageKeyMissingShowsIdPlaceholderWithoutEnglishFallback();
  void missingSavedLanguageFallsBackToEnglishAndPersists();
  void openEmbeddedArchiveInSubdirAndParentRestoresVirtualDir();
  void openEmbeddedArchiveInsideAndParentRestoresOuterArchive();
  void openSameNamedEmbeddedArchiveDoesNotLeaveStaleProgressDialog();
  void nestedDeleteRebuildsEmbeddedArchiveViewAndRemovesEntry();
  void nestedDeleteWritebackRestoreDoesNotLeaveStaleProgressDialog();
  void nestedArchiveWritebackPlanCapturesReopenChain();
  void openEmbeddedZipArchiveUsesNonTempStrategyAndParentExitClearsSessions();
  void openFromArchiveViewFileShowsErrorWhenNotArchive();
  void archiveTestInArchiveViewUsesSessionWhenArchivePathMissing();
  void panelStatusBarsTrackSelectionFocusAndTransientMessages();
  void openInsideFromArchiveViewFileDoesNotFallbackToExternalLaunch();
  void openFromArchiveViewUsesSystemTempForTemporaryExtraction();
  void openFromArchiveViewIgnoresRemovableOnlyWorkDirOnFixedDisk();
  void openFromArchiveViewIgnoresRemovableOnlyWorkDirOnRemovableSource();
  void openFromArchiveViewOutsideTrackedProcessExitCleansSession();
  void openFromArchiveViewOutsideChangedFileDoesNotWriteBack();
  void openFromArchiveViewOutsideQuickObservableExitWaitsForCloseEvent();
  void openFromArchiveViewOutsideQuickObservableExitOnWindowsWaitsForCloseEvent();
  void openFromArchiveViewOutsideOnMacTracksChildProcessUntilExit();
  void openFromArchiveViewOutsideOnMacTrackingInitFailureFailsStrictly();
  void openFromArchiveViewOutsideOnMacTrackingRuntimeFailureFailsStrictly();
  void openInsideEntersArchiveView();
  void openInsideOnNonArchiveDoesNotFallbackToExternalOpen();
  void openInsideUsesFocusedItemWhenMultipleSelected();
  void openInsideVariantParserDoesNotFallbackToExternalOpen();
  void openInsideVariantStarDoesNotFallbackToExternalOpen();
  void openOnAlwaysStartExtensionUsesExternalOpen();
  void openOnArchivePrefersInternalOpen();
  void externalOpenBlocksSuspiciousFilenamesLikeOriginal();
  void openOnMultipleFilesUsesExternalOpen();
  void openOnUnknownSuffixArchivePrefersInternalOpen();
  void openOnUnknownSuffixNonArchiveFallsBackToExternalOpen();
  void openOutsideFromArchiveViewExtractsAndLaunchesEntry();
  void openOutsideUsesExternalOpenForArchive();
  void externalCommandParserKeepsUnquotedExistingPathWithSpaces();
  void optionsDialogApplyImmediatelyPersistsLanguageAndEmitsApplied();
  void optionsDialogApplyPersistsRuntimeSettingsWithoutClosing();
  void optionsDialogAssociationsTableColumnWidthsPersist();
  void optionsDialogCancelKeepsRuntimeSettingsUnchanged();
  void optionsDialogContainsSevenTabsAndFooterButtons();
  void optionsDialogEditorApplyPersistsWithoutClosing();
  void optionsDialogEditorOkAndCancelSemantics();
  void optionsDialogEditorPageHasOriginalRowsAndButtons();
  void optionsDialogFoldersPageUsesWorkingDirModeControls();
  void optionsDialogFoldersPagePersistsExtractMemoryLimitSettings();
  void optionsDialogFoldersPagePersistsWorkingDirSettings();
  void optionsDialogLanguagePageShowsSummaryAndComments();
  void optionsDialogLanguageApplySyncsMacOSIntegrationSnapshot();
  void optionsDialogLanguageSelectionPersistsAndFlagsChange();
  void optionsDialogLargePagesApplyPersistsWhenSupported();
  void optionsDialogLargePagesFailureRevertsCheckboxOnSupportedMac();
  void optionsDialogFoldersPagePersistsWorkDirSettings();
  void optionsDialogOkPersistsImplementedSettingsOnly();
  void optionsDialogQtPagePersistsSharedStartupSettingsAndAppliesStyle();
  void optionsDialogSevenZipPagePersistsSettingsAndCheckStates();
  void optionsDialogSettingsPageKeepsWindowsOnlyControlsDisabled();
  void optionsDialogSystemAndSevenZipPagesHaveStaticShellContent();
  void parentLinkSelectionDisablesDangerousActions();
  void filesystemParentLinkOpenAndEnterNavigateToParentLikeOriginal();
  void filesystemMixedParentLinkOpenUsesFocusedOperatedRulesLikeOriginal();
  void pathBarCanNavigateByEnterAndPopupItems();
  void pathBarMatchesParentButtonAndEditableCombo();
  void pathBarSpaceDoesNotTriggerOpenAction();
  void crossPanelAltUpBindsOppositePanelToSameFilesystemFolder();
  void crossPanelAltLeftRightBindOppositePanelToFocusedFilesystemFolder();
  void crossPanelAltNavigationNoOpsForOnePanelAndFocusedFile();
  void placeholderActionsShowNotImplementedNotice();
  void portableInitializationFailsWhenRootIsFile();
  void portableSettingsRoundTripsBinaryValuesAsTypedJson();
  void progressAndCancelSignalsAreVisible();
  void guiPromptDialogsCanRunConsecutiveScriptedInteractions();
  void propertiesDialogShowsArchiveMetadataFields();
  void propertiesDialogInArchiveViewUsesSessionWhenArchivePathMissing();
  void propertiesDialogUsesTwoColumnTableAndShowsExpectedRows();
  void runtimeSettingsAreAppliedToMainWindowAndSingleClickOpens();
  void autoRefreshMenuDefaultToggleAndPersistenceFollowsOriginal();
  void autoRefreshKeepsSelectionAfterReload();
  void manualRefreshShortcutReloadsFocusedPanel();
  void addToFavoritesPersistsSlotAndOpensSavedFolder();
  void favoriteDigitShortcutsSetAndOpenSlotsLikeOriginal();
  void favoritesStoreAndOpenArchiveFolderPrefixesLikeOriginal();
  void helpMenuMatchesOriginalOrderAndContentsIsQtPlaceholder();
  void linkDialogShowsOriginalModesWithOnlySymlinksEnabled();
  void commentActionUsesFocusedItemAndLinkActionUsesSelection();
  void selectCommandsFollowPanelSelectSemantics();
  void archiveViewSelectCommandsFollowPanelSelectSemantics();
  void tempFilesActionOpensCleanupDialogWithRootFilter();
  void tempFilesDialogDeleteViaKeyButtonAndContextMenu();
  void tempFilesDialogColumnsMatchOriginalSixColumnLayout();
  void tempFilesDialogDefaultSortUsesModifiedWithDirectoryFirst();
  void tempFilesDialogHeaderSortingTogglesAndDefaultDirectionRules();
  void tempFilesDialogDeleteFailureShowsSystemErrorAndPath();
  void tempFilesDialogOpenOutside7ZipLaunchesNew7zfmProcess();
  void sevenZipCommandsBuildExpectedArgs();
  void sevenZipOpenAsActionsCarryCapabilityKeys();
  void sevenZipExtractAndTestCommandsBuildExpectedArgs();
  void sevenZipFileMenuShowsDynamicSubmenuAtTopAndHidesWithoutSelection();
  void sevenZipOpenAndOpenAsLaunchNew7zfmProcess();
  void startupTypeHintForcesInternalOpenForUnknownSuffixArchive();
  void startupOpenArgumentsTreatTaskIpcTokensAsUnknown();
  void openStartupTargetAppliesArchiveDirectoryAndParentFallbackRules();
  void startupRestoresSavedPanelPathsOnEmptyLaunch();
  void startupTargetsOverrideRestoredPanelPaths();
  void startupPanelPathFallsBackToNearestExistingAncestor();
  void shutdownFromArchiveViewPersistsOuterFilesystemPanelPath();
  void folderHistoryPersistsGloballyDedupesAndCapsAtOneHundred();
  void startupOpenRequestDispatchUsesPrimaryWindowAndAdditionalWindows();
  void startupOpenRequestDedupSuppressesOnlyExactPendingDuplicates();
  void runtimeFileOpenRequestLaunchesNewProcessWithoutReplacingCurrentWindow();
  void archiveDragMimeDataDefersMaterializationUntilUrlsRequested();
  void archiveDragMimeDataIncludesMacPromiseMarkerWhenEnabled();
  void macArchiveNativePromiseBackendRejectsInvalidRequest();
  void macNativeDragPolicyAndItemNamesFollowPlatformContract();
  void macNativeDragFilesystemPromiseCopiesSourceWithoutWriter();
  void macNativeDragArchivePromiseUsesDirectExportWriter();
  void macArchiveNativePromiseGraceWaitsForLateRequest();
  void archiveDragStrictFailureShowsWarningWhenDropRejected();
  void archiveDragFailureClassTracksRejectWriteFailureAndCancel();
  void genericQtArchiveDragMaterializationFailureIsReported();
  void dropInternalArchiveSourceToFilesystemExtractsIntoTargetDirectory();
  void dropExternalFilesInFilesystemPanelStartsAddDialogBridge();
  void dropExternalFilesInArchiveViewUpdatesCurrentArchive();
  void dropExternalFilesOnWindowTargetUsesActivePanelFilesystemMode();
  void dropExternalFilesOnWindowTargetUsesActivePanelArchiveMode();
  void dragEnterInternalFsSourceShowsMoveActionOnSameVolumeDefault();
  void dragEnterRightButtonSameVolumePrefersMovePreview();
  void dragEnterRightButtonMixedVolumeBatchPrefersCopyPreview();
  void dropInternalFsSourceOnSamePanelIsIgnored();
  void leftDropInternalFsSourceUsesMoveByDefaultOnSameVolume();
  void leftDropInternalFsSourceUsesCopyByDefaultOnMixedVolumeBatch();
  void leftDropInternalFsSourceMixedVolumeMoveOnlyIsRejectedByDefault();
  void leftDropInternalFsSourceFallsBackToCopyWhenMoveNotAllowed();
  void forcedMoveDropReportsCopyActionForExternalSource();
  void forcedMoveDropReportsMoveActionForInternalSource();
  void canceledDropReportsNonePerformedEffects();
  void addDialogLauncherFailureReportsNonePerformedEffects();
  void taggedInternalSourceMoveReportsCopyActionWhenNotTrusted();
  void taggedTrustedInternalSourceMoveReportsMoveAction();
  void leftDropInternalFsSourceUsesCopyWhenCtrlPressed();
  void leftDropInternalFsSourceUsesCopyByDefaultOnDifferentVolume();
  void leftDropInternalFsSourceCtrlShiftUsesDefaultMoveOnSameVolume();
  void leftDropTaggedInternalFsSourceUsesMoveByDefaultOnSameVolume();
  void filesystemModelMimeDataIncludesInternalSourceTag();
  void dropSamePanelSourceOnWindowTargetStillUsesAddDialogBridge();
  void rightDropExternalFilesOnWindowTargetUsesActivePanelArchiveMode();
  void rightDropExternalFilesCanCopyIntoTargetDirectory();
  void rightDropExternalFilesCanMoveIntoTargetDirectory();
  void rightDropMoveOverrideFallsBackToCopyWhenMoveNotAllowed();
  void rightDropMenuSelectionRespectsPossibleActions();
  void rightDropCopyIntoHoveredFolderRow();
  void rightDropAddToArchiveOnFileRowUsesBridge();
  void rightDropToArchiveRequiresConfirmBeforeUpdate();
  void sevenZipMenuStateHiddenInArchiveView();
  void sevenZipMenuStateTracksSelectionKinds();
  void sevenZipSubmenuContainsCrcShaGroup();
  void shiftDeleteInFilesystemViewBypassesRecycleBin();
  void sortActionsApplyAndUnsortedRestoresLoadOrder();
  void closeShortcutClosesMainWindowFromPanelFocus();
  void splitAndCombineFailureDialogsUseSpecificBackendSummaries();
  void splitAndCombineRunnerExecuteAndReportOperationName();
  void taskProgressDialogTestCancelConfirmationFollowsYesNoCancel();
  void testActionUsesSevenZipBridgeInFilesystemMode();
  void testActionUsesOperSmartItems();
  void timeSubmenuUpdatesPrecisionAndUtcDisplay();
  void toolbarMenuActionsApplyImmediatelyAndPersist();
  void topLevelMenuOrderMatchesOriginalResource();
  void toolsMenuMatchesOriginalVisibleEntriesAndKeepsBenchmark2HiddenExtension();
  void flatViewToggleFollowsActivePanelOnly();
  void twoPanelsToggleAndPanelContextAreIndependent();
  void viewAndEditShowConfigurationMessageWhenCommandIsEmpty();
  void viewAndEditUseConfiguredCommandWithFocusedFileLikeOriginal();
  void viewCommandCalculatesSelectedDirectoryFullSizeLikeOriginal();
  void archiveViewAndEditUnchangedFileCleansTempSession();
  void archiveViewAndEditUseFocusedEntryLikeOriginal();
  void archiveViewAndEditChangedFilePromptsAndUpdatesArchive();
  void archiveViewAndEditDeletedTempFileWarnsAndDoesNotUpdateArchive();
  void viewMenuContainsOriginalActionsAndSubmenus();
  void viewModeActionsSwitchActivePanelView();
};
