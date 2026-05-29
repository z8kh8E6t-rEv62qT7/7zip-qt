// tests/support/fake_launcher_binary/main.cpp
// Role: Fake open/xdg-open launcher used by behavior tests.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTextStream>
#include <QThread>

#include "task_ipc_runtime.h"

#if !defined(Q_OS_WIN)
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

constexpr const char* kLogPathEnv = "Z7_FAKE_TRACKER_LOG";
constexpr const char* kFailModeEnv = "Z7_FAKE_TRACKER_FAIL_MODE";
constexpr const char* kSleepMsecsEnv = "Z7_FAKE_TRACKER_SLEEP_MS";
constexpr const char* kNoChildEnv = "Z7_FAKE_TRACKER_NO_CHILD";
constexpr const char* kClaimTaskIpcEnv = "Z7_FAKE_TRACKER_CLAIM_TASK_IPC";

QJsonArray string_list_to_json(const QStringList& values) {
  QJsonArray array;
  for (const QString& value : values) {
    array.append(value);
  }
  return array;
}

QString command_name(z7::task_ipc_runtime::TaskIpcCommandKind command) {
  using z7::task_ipc_runtime::TaskIpcCommandKind;
  switch (command) {
    case TaskIpcCommandKind::kNone:
      return QStringLiteral("none");
    case TaskIpcCommandKind::kAdd:
      return QStringLiteral("add");
    case TaskIpcCommandKind::kExtract:
      return QStringLiteral("extract");
    case TaskIpcCommandKind::kTest:
      return QStringLiteral("test");
    case TaskIpcCommandKind::kHash:
      return QStringLiteral("hash");
    case TaskIpcCommandKind::kBenchmark:
      return QStringLiteral("benchmark");
    case TaskIpcCommandKind::kOpen:
      return QStringLiteral("open");
    case TaskIpcCommandKind::kArchiveExport:
      return QStringLiteral("archive_export");
    case TaskIpcCommandKind::kCli:
      return QStringLiteral("cli");
  }
  return QStringLiteral("unknown");
}

QString path_remap_match_name(
    z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind kind) {
  using z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind;
  switch (kind) {
    case TaskIpcExtractPathRemapMatchKind::kRequestRoot:
      return QStringLiteral("request_root");
    case TaskIpcExtractPathRemapMatchKind::kExactArchivePath:
      return QStringLiteral("exact_archive_path");
    case TaskIpcExtractPathRemapMatchKind::kArchivePrefix:
      return QStringLiteral("archive_prefix");
  }
  return QStringLiteral("unknown");
}

QJsonArray path_remaps_to_json(
    const QVector<z7::task_ipc_runtime::TaskIpcExtractPathRemap>& remaps) {
  QJsonArray array;
  for (const z7::task_ipc_runtime::TaskIpcExtractPathRemap& remap : remaps) {
    QJsonObject object;
    object.insert(QStringLiteral("match_kind"),
                  path_remap_match_name(remap.match_kind));
    object.insert(QStringLiteral("match_kind_value"),
                  static_cast<int>(remap.match_kind));
    object.insert(QStringLiteral("source_path"), remap.source_path);
    object.insert(QStringLiteral("destination_path"), remap.destination_path);
    array.append(object);
  }
  return array;
}

QJsonObject add_payload_to_json(
    const z7::task_ipc_runtime::TaskIpcAddPayload& payload) {
  QJsonObject object;
  object.insert(QStringLiteral("archive_path"), payload.archive_path);
  object.insert(QStringLiteral("archive_type"), payload.archive_type);
  object.insert(QStringLiteral("update_mode"), payload.update_mode);
  object.insert(QStringLiteral("raw_update_switch"), payload.raw_update_switch);
  object.insert(QStringLiteral("raw_update_switches"),
                string_list_to_json(payload.raw_update_switches));
  object.insert(QStringLiteral("path_mode"), payload.path_mode);
  object.insert(QStringLiteral("create_sfx"), payload.create_sfx);
  object.insert(QStringLiteral("share_for_write"), payload.share_for_write);
  object.insert(QStringLiteral("delete_after_compressing"),
                payload.delete_after_compressing);
  object.insert(QStringLiteral("send_by_email"), payload.send_by_email);
  object.insert(QStringLiteral("send_by_email_remove_after"),
                payload.send_by_email_remove_after);
  object.insert(QStringLiteral("send_by_email_address"),
                payload.send_by_email_address);
  object.insert(QStringLiteral("compression_level"), payload.compression_level);
  object.insert(QStringLiteral("method_value"), payload.method_value);
  object.insert(QStringLiteral("dictionary_size"), payload.dictionary_size);
  object.insert(QStringLiteral("word_size"), payload.word_size);
  object.insert(QStringLiteral("solid_block_size"), payload.solid_block_size);
  object.insert(QStringLiteral("thread_count"), payload.thread_count);
  object.insert(QStringLiteral("volume_size"), payload.volume_size);
  object.insert(QStringLiteral("password"), payload.password);
  object.insert(QStringLiteral("encrypt_headers_defined"),
                payload.encrypt_headers_defined);
  object.insert(QStringLiteral("encrypt_headers"), payload.encrypt_headers);
  object.insert(QStringLiteral("encryption_method"), payload.encryption_method);
  object.insert(QStringLiteral("extra_parameters"),
                string_list_to_json(payload.extra_parameters));
  object.insert(QStringLiteral("input_paths"),
                string_list_to_json(payload.input_paths));
  return object;
}

QJsonObject extract_payload_to_json(
    const z7::task_ipc_runtime::TaskIpcExtractPayload& payload) {
  QJsonObject object;
  object.insert(QStringLiteral("output_dir"), payload.output_dir);
  object.insert(QStringLiteral("split_dest_enabled"),
                payload.split_dest_enabled);
  object.insert(QStringLiteral("split_dest_name"), payload.split_dest_name);
  object.insert(QStringLiteral("overwrite_switch"), payload.overwrite_switch);
  object.insert(QStringLiteral("archive_type"), payload.archive_type);
  object.insert(QStringLiteral("eliminate_root_duplication"),
                payload.eliminate_root_duplication);
  object.insert(QStringLiteral("path_remaps"),
                path_remaps_to_json(payload.path_remaps));
  object.insert(QStringLiteral("restore_file_security"),
                payload.restore_file_security);
  object.insert(QStringLiteral("zone_id_mode"), payload.zone_id_mode);
  object.insert(QStringLiteral("password"), payload.password);
  object.insert(QStringLiteral("archive_inputs"),
                string_list_to_json(payload.archive_inputs));
  return object;
}

QJsonObject archive_export_payload_to_json(
    const z7::task_ipc_runtime::TaskIpcArchiveExportPayload& payload) {
  QJsonObject object;
  object.insert(QStringLiteral("root_archive_path"),
                payload.root_archive_path);
  object.insert(QStringLiteral("root_archive_type"),
                payload.root_archive_type);
  object.insert(QStringLiteral("nested_archive_entries"),
                string_list_to_json(payload.nested_archive_entries));
  object.insert(QStringLiteral("archive_entry_paths"),
                string_list_to_json(payload.archive_entry_paths));
  object.insert(QStringLiteral("output_dir"), payload.output_dir);
  object.insert(QStringLiteral("overwrite_mode"), payload.overwrite_mode);
  object.insert(QStringLiteral("path_mode"), payload.path_mode);
  object.insert(QStringLiteral("eliminate_root_duplication"),
                payload.eliminate_root_duplication);
  object.insert(QStringLiteral("path_remaps"),
                path_remaps_to_json(payload.path_remaps));
  object.insert(QStringLiteral("restore_file_security"),
                payload.restore_file_security);
  object.insert(QStringLiteral("zone_id_mode"), payload.zone_id_mode);
  object.insert(QStringLiteral("password"), payload.password);
  return object;
}

QJsonObject test_payload_to_json(
    const z7::task_ipc_runtime::TaskIpcTestPayload& payload) {
  QJsonObject object;
  object.insert(QStringLiteral("archive_inputs"),
                string_list_to_json(payload.archive_inputs));
  return object;
}

QJsonObject hash_payload_to_json(
    const z7::task_ipc_runtime::TaskIpcHashPayload& payload) {
  QJsonObject object;
  object.insert(QStringLiteral("hash_method"), payload.hash_method);
  object.insert(QStringLiteral("input_paths"),
                string_list_to_json(payload.input_paths));
  return object;
}

QJsonObject benchmark_payload_to_json(
    const z7::task_ipc_runtime::TaskIpcBenchmarkPayload& payload) {
  QJsonObject object;
  object.insert(QStringLiteral("method_value"), payload.method_value);
  object.insert(QStringLiteral("dictionary_size"), payload.dictionary_size);
  object.insert(QStringLiteral("thread_count"), payload.thread_count);
  object.insert(QStringLiteral("operands"),
                string_list_to_json(payload.operands));
  return object;
}

QJsonObject open_payload_to_json(
    const z7::task_ipc_runtime::TaskIpcOpenPayload& payload) {
  QJsonObject object;
  object.insert(QStringLiteral("archive_path"), payload.archive_path);
  object.insert(QStringLiteral("archive_type"), payload.archive_type);
  object.insert(QStringLiteral("nested_archive_entries"),
                string_list_to_json(payload.nested_archive_entries));
  object.insert(QStringLiteral("entry_path"), payload.entry_path);
  return object;
}

QJsonObject cli_payload_to_json(
    const z7::task_ipc_runtime::TaskIpcCliPayload& payload) {
  QJsonObject object;
  object.insert(QStringLiteral("argv"), string_list_to_json(payload.argv));
  object.insert(QStringLiteral("working_dir"), payload.working_dir);
  return object;
}

QJsonObject task_payload_to_json(
    const z7::task_ipc_runtime::TaskIpcPayload& payload) {
  QJsonObject object;
  object.insert(QStringLiteral("command"), command_name(payload.command));
  object.insert(QStringLiteral("command_value"),
                static_cast<int>(payload.command));
  object.insert(QStringLiteral("show_dialog"), payload.show_dialog);
  object.insert(QStringLiteral("refresh_after_finish"),
                payload.refresh_after_finish);
  object.insert(QStringLiteral("complete_on_claim"), payload.complete_on_claim);
  object.insert(QStringLiteral("caption"), payload.caption);
  if (payload.add.has_value()) {
    object.insert(QStringLiteral("add"), add_payload_to_json(*payload.add));
  }
  if (payload.extract.has_value()) {
    object.insert(QStringLiteral("extract"),
                  extract_payload_to_json(*payload.extract));
  }
  if (payload.test.has_value()) {
    object.insert(QStringLiteral("test"), test_payload_to_json(*payload.test));
  }
  if (payload.hash.has_value()) {
    object.insert(QStringLiteral("hash"), hash_payload_to_json(*payload.hash));
  }
  if (payload.benchmark.has_value()) {
    object.insert(QStringLiteral("benchmark"),
                  benchmark_payload_to_json(*payload.benchmark));
  }
  if (payload.open.has_value()) {
    object.insert(QStringLiteral("open"), open_payload_to_json(*payload.open));
  }
  if (payload.archive_export.has_value()) {
    object.insert(QStringLiteral("archive_export"),
                  archive_export_payload_to_json(*payload.archive_export));
  }
  if (payload.cli.has_value()) {
    object.insert(QStringLiteral("cli"), cli_payload_to_json(*payload.cli));
  }
  return object;
}

bool claim_task_ipc_enabled() {
  return qEnvironmentVariable(kClaimTaskIpcEnv).trimmed() ==
         QStringLiteral("1");
}

QString fail_mode() {
  const QString mode =
      qEnvironmentVariable(kFailModeEnv, QStringLiteral("normal")).trimmed();
  return mode.isEmpty() ? QStringLiteral("normal") : mode;
}

int sleep_msecs() {
  bool ok = false;
  const int value =
      qEnvironmentVariableIntValue(kSleepMsecsEnv, &ok);
  return ok && value > 0 ? value : 0;
}

bool no_child_enabled() {
  return qEnvironmentVariable(kNoChildEnv).trimmed() == QStringLiteral("1");
}

bool write_log(const QStringList& args,
               const QString& mode,
               const QJsonObject& task_ipc = QJsonObject()) {
  const QString log_path =
      QDir::cleanPath(qEnvironmentVariable(kLogPathEnv).trimmed());
  if (log_path.isEmpty()) {
    return true;
  }

  const QFileInfo info(log_path);
  QDir parent_dir = info.dir();
  if (!parent_dir.exists() && !parent_dir.mkpath(QStringLiteral("."))) {
    return false;
  }

  QFile log_file(log_path);
  if (!log_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    return false;
  }

  QJsonObject root;
  root.insert(QStringLiteral("program"),
              QCoreApplication::applicationFilePath());
  root.insert(QStringLiteral("cwd"), QDir::currentPath());
  root.insert(QStringLiteral("mode"), mode);
#if !defined(Q_OS_WIN)
  root.insert(QStringLiteral("pid"), static_cast<qint64>(::getpid()));
  root.insert(QStringLiteral("ppid"), static_cast<qint64>(::getppid()));
#endif

  QJsonArray args_json;
  for (const QString& arg : args) {
    args_json.append(arg);
  }
  root.insert(QStringLiteral("args"), args_json);
  if (!task_ipc.isEmpty()) {
    root.insert(QStringLiteral("task_ipc"), task_ipc);
  }

  log_file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  log_file.write("\n");
  return log_file.flush();
}

bool parse_task_ipc_identity(const QStringList& args,
                             quint64* out_session_id,
                             quint32* out_generation,
                             QString* out_shm_name,
                             QString* out_sem_name) {
  if (out_session_id != nullptr) {
    *out_session_id = 0;
  }
  if (out_generation != nullptr) {
    *out_generation = 0;
  }
  if (out_shm_name != nullptr) {
    out_shm_name->clear();
  }
  if (out_sem_name != nullptr) {
    out_sem_name->clear();
  }

  for (const QString& arg : args) {
    if (arg.startsWith(QStringLiteral("--task-ipc-session="))) {
      bool ok = false;
      const quint64 parsed =
          arg.mid(QStringLiteral("--task-ipc-session=").size()).toULongLong(&ok);
      if (!ok) {
        return false;
      }
      if (out_session_id != nullptr) {
        *out_session_id = parsed;
      }
      continue;
    }
    if (arg.startsWith(QStringLiteral("--task-ipc-generation="))) {
      bool ok = false;
      const quint32 parsed =
          arg.mid(QStringLiteral("--task-ipc-generation=").size()).toUInt(&ok);
      if (!ok) {
        return false;
      }
      if (out_generation != nullptr) {
        *out_generation = parsed;
      }
      continue;
    }
    if (arg.startsWith(QStringLiteral("--task-ipc-shm="))) {
      if (out_shm_name != nullptr) {
        *out_shm_name =
            arg.mid(QStringLiteral("--task-ipc-shm=").size()).trimmed();
      }
      continue;
    }
    if (arg.startsWith(QStringLiteral("--task-ipc-sem="))) {
      if (out_sem_name != nullptr) {
        *out_sem_name =
            arg.mid(QStringLiteral("--task-ipc-sem=").size()).trimmed();
      }
    }
  }

  const bool has_identity = out_session_id != nullptr &&
                            *out_session_id != 0 &&
                            out_generation != nullptr &&
                            *out_generation != 0U;
#if defined(Q_OS_MACOS)
  return has_identity && out_shm_name != nullptr && !out_shm_name->isEmpty() &&
         out_sem_name != nullptr && !out_sem_name->isEmpty();
#else
  Q_UNUSED(out_shm_name);
  Q_UNUSED(out_sem_name);
  return has_identity;
#endif
}

bool publish_task_ipc_completion_with_fallback(
    const z7::task_ipc_runtime::TaskIpcClaimedTask& task,
    int result_code,
    const QString& summary) {
  QString error_message;
  if (z7::task_ipc_runtime::publish_task_ipc_completion(
          task, result_code, summary, &error_message)) {
    return true;
  }
  return z7::task_ipc_runtime::publish_task_ipc_completion_minimal(
      task, result_code, nullptr);
}

int run_claim_task_ipc_mode(const QStringList& args, const QString& mode) {
  QJsonObject task_ipc_log;
  task_ipc_log.insert(QStringLiteral("enabled"), true);

  quint64 session_id = 0;
  quint32 generation = 0;
  QString shm_name;
  QString sem_name;
  if (!parse_task_ipc_identity(args, &session_id, &generation, &shm_name,
                               &sem_name)) {
    task_ipc_log.insert(QStringLiteral("claimed"), false);
    task_ipc_log.insert(QStringLiteral("completed"), false);
    task_ipc_log.insert(QStringLiteral("error"),
                        QStringLiteral("Invalid or missing task IPC identity."));
    write_log(args, mode, task_ipc_log);
    return 1;
  }
  task_ipc_log.insert(QStringLiteral("session_id"),
                      QString::number(session_id));
  task_ipc_log.insert(QStringLiteral("generation"),
                      static_cast<int>(generation));
  task_ipc_log.insert(QStringLiteral("shm_name"), shm_name);
  task_ipc_log.insert(QStringLiteral("sem_name"), sem_name);

#if defined(Q_OS_MACOS)
  z7::task_ipc_runtime::set_task_ipc_worker_endpoint(shm_name, sem_name);
#else
  Q_UNUSED(shm_name);
  Q_UNUSED(sem_name);
#endif

  z7::task_ipc_runtime::TaskIpcClaimedTask task;
  QString claim_error;
  if (!z7::task_ipc_runtime::claim_task_ipc_task_for_worker(
          session_id, generation, &task, &claim_error)) {
    task_ipc_log.insert(QStringLiteral("claimed"), false);
    task_ipc_log.insert(QStringLiteral("completed"), false);
    task_ipc_log.insert(QStringLiteral("error"), claim_error);
    write_log(args, mode, task_ipc_log);
    return 1;
  }

  task_ipc_log.insert(QStringLiteral("claimed"), true);
  task_ipc_log.insert(QStringLiteral("slot_index"), task.slot_index);
  task_ipc_log.insert(QStringLiteral("owner_instance_id"),
                      task.owner_instance_id);
  task_ipc_log.insert(QStringLiteral("launcher_pid"),
                      static_cast<qint64>(task.launcher_pid));
  task_ipc_log.insert(QStringLiteral("worker_pid"),
                      static_cast<qint64>(task.worker_pid));
  task_ipc_log.insert(QStringLiteral("payload"),
                      task_payload_to_json(task.payload));

  const bool completed =
      publish_task_ipc_completion_with_fallback(task, 0, QString());
  task_ipc_log.insert(QStringLiteral("completed"), completed);
  if (!completed) {
    task_ipc_log.insert(QStringLiteral("error"),
                        QStringLiteral("Failed to publish task IPC completion."));
  }
  if (!write_log(args, mode, task_ipc_log)) {
    return 1;
  }
  return completed ? 0 : 1;
}

#if !defined(Q_OS_WIN)
[[noreturn]] void run_child_sleep_process(int sleep_msecs_value) {
  const unsigned int sleep_secs =
      static_cast<unsigned int>((sleep_msecs_value + 999) / 1000);
  ::sleep(sleep_secs);
  ::_exit(0);
}

bool spawn_sleeping_child(int sleep_msecs_value) {
  if (sleep_msecs_value <= 0 || no_child_enabled()) {
    return true;
  }

  const pid_t child = ::fork();
  if (child < 0) {
    return false;
  }
  if (child == 0) {
    run_child_sleep_process(sleep_msecs_value);
  }
  return true;
}
#endif

int normal_exit_delay_msecs(int sleep_msecs_value) {
  if (sleep_msecs_value > 0) {
    return sleep_msecs_value;
  }
  return 250;
}

int run_normal_mode(const QStringList& args, const QString& mode) {
  if (mode != QStringLiteral("no_log") &&
      !write_log(args, mode)) {
    return 1;
  }

#if !defined(Q_OS_WIN)
  if (!spawn_sleeping_child(sleep_msecs())) {
    return 1;
  }
  QThread::msleep(static_cast<unsigned long>(normal_exit_delay_msecs(sleep_msecs())));
  return 0;
#else
  Q_UNUSED(args);
  Q_UNUSED(mode);
  return 0;
#endif
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  const QStringList args = QCoreApplication::arguments().mid(1);

  if (args.contains(QStringLiteral("--help"))) {
    QTextStream(stdout)
        << "z7_fake_open_tracker simulates open/xdg-open behavior for tests.\n";
    return 0;
  }

  const QString mode = fail_mode();
  if (claim_task_ipc_enabled()) {
    return run_claim_task_ipc_mode(args, mode);
  }

  if (mode == QStringLiteral("hang")) {
    if (!write_log(args, mode)) {
      return 1;
    }
    const int sleep_value = sleep_msecs();
    const unsigned int sleep_secs =
        static_cast<unsigned int>((sleep_value > 0 ? sleep_value : 5000) + 999) / 1000;
#if !defined(Q_OS_WIN)
    ::sleep(sleep_secs);
#else
    QThread::sleep(sleep_secs);
#endif
    return 0;
  }

  if (mode == QStringLiteral("crash_signal")) {
    if (!write_log(args, mode)) {
      return 1;
    }
#if !defined(Q_OS_WIN)
    ::raise(SIGKILL);
#endif
    return 1;
  }

  if (mode == QStringLiteral("fail_exit")) {
    if (!write_log(args, mode)) {
      return 1;
    }
    return 1;
  }

  if (mode == QStringLiteral("no_log")) {
    return run_normal_mode(args, mode);
  }

  return run_normal_mode(args, mode);
}
