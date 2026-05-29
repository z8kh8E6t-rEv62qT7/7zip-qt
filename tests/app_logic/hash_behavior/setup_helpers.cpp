// tests/app_logic/hash_behavior/setup_helpers.cpp
// Role: Shared fixtures and helper utilities for hash behavior tests.

#include "internal.h"

namespace hash_behavior_internal {

namespace fs = std::filesystem;

std::string to_std_path(const QString& path) {
  const QByteArray encoded = QFile::encodeName(path);
  return std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
}

fs::path recycle_bin_dir_for_test(std::error_code& ec) {
  ec.clear();
  fs::path recycle_dir;

#if defined(_WIN32)
  if (const char* profile = std::getenv("USERPROFILE"); profile != nullptr && profile[0] != '\0') {
    recycle_dir = fs::path(profile) / ".Recycle.Bin";
  } else {
    recycle_dir = fs::temp_directory_path(ec) / ".Recycle.Bin";
    if (ec) {
      return {};
    }
  }
#elif defined(__APPLE__)
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    recycle_dir = fs::path(home) / ".Trash";
  } else {
    recycle_dir = fs::temp_directory_path(ec) / ".Trash";
    if (ec) {
      return {};
    }
  }
#else
  fs::path base;
  if (const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
      xdg_data_home != nullptr && xdg_data_home[0] != '\0') {
    base = fs::path(xdg_data_home);
  } else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    base = fs::path(home) / ".local" / "share";
  } else {
    base = fs::temp_directory_path(ec);
    if (ec) {
      return {};
    }
  }
  recycle_dir = base / "Trash" / "files";
#endif

  auto ensure_writable_directory = [](const fs::path& dir, std::error_code& inner_ec) {
    inner_ec.clear();
    fs::create_directories(dir, inner_ec);
    if (inner_ec) {
      return false;
    }

    const fs::path probe = dir / (".z7-probe-" + std::to_string(
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())));
    std::ofstream out(probe, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
      inner_ec = std::make_error_code(std::errc::permission_denied);
      return false;
    }
    out << "probe";
    out.close();

    std::error_code remove_ec;
    fs::remove(probe, remove_ec);
    inner_ec.clear();
    return true;
  };

  if (!ensure_writable_directory(recycle_dir, ec)) {
    return {};
  }
  return recycle_dir;
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate()) {
    if (timer.elapsed() >= timeout_ms) {
      return false;
    }
    QTest::qWait(20);
  }
  return true;
}

bool wait_until_stable(const std::atomic<uint64_t>& value,
                       int stable_ms,
                       int timeout_ms) {
  QElapsedTimer timer;
  timer.start();

  uint64_t last = value.load(std::memory_order_relaxed);
  int stable_for_ms = 0;
  while (timer.elapsed() < timeout_ms) {
    QTest::qWait(50);
    const uint64_t current = value.load(std::memory_order_relaxed);
    if (current == last) {
      stable_for_ms += 50;
      if (stable_for_ms >= stable_ms) {
        return true;
      }
    } else {
      last = current;
      stable_for_ms = 0;
    }
  }
  return false;
}

std::optional<size_t> first_stage_index(
    const std::vector<z7::app::OperationEvent>& events,
    z7::app::OperationStage stage) {
  for (size_t i = 0; i < events.size(); ++i) {
    const z7::app::OperationEvent& event = events.at(i);
    if (event.kind == z7::app::OperationEventKind::kLifecycle &&
        event.stage == stage) {
      return i;
    }
  }
  return std::nullopt;
}

bool is_terminal_state(z7::app::ArchiveSessionState state) {
  switch (state) {
    case z7::app::ArchiveSessionState::kCompleted:
    case z7::app::ArchiveSessionState::kFailed:
    case z7::app::ArchiveSessionState::kCancelled:
      return true;
    case z7::app::ArchiveSessionState::kPending:
    case z7::app::ArchiveSessionState::kRunning:
    case z7::app::ArchiveSessionState::kPaused:
    case z7::app::ArchiveSessionState::kCancelling:
      return false;
  }
  return false;
}

OperationEventCollector::OperationEventCollector(
    std::vector<z7::app::OperationEvent>* sink)
    : sink_(sink) {}

void OperationEventCollector::on_lifecycle(z7::app::OperationStage stage,
                                           std::string_view message) {
  z7::app::OperationEvent event;
  event.kind = z7::app::OperationEventKind::kLifecycle;
  event.stage = stage;
  event.message.assign(message.data(), message.size());
  push_event(event);
}

void OperationEventCollector::on_log(const z7::app::ArchiveLog& log) {
  z7::app::OperationEvent event;
  event.kind = z7::app::OperationEventKind::kLog;
  event.stage = log.stage;
  event.output_channel = log.channel;
  event.message = log.message;
  event.benchmark_snapshot = log.benchmark_snapshot;
  event.benchmark_summary = log.benchmark_summary;
  push_event(event);
}

void OperationEventCollector::on_progress(const z7::app::ProgressSnapshot& progress) {
  z7::app::OperationEvent event;
  event.kind = z7::app::OperationEventKind::kProgress;
  event.stage = progress.stage;
  event.percent = progress.percent;
  event.totals_known = progress.totals_known;
  event.total_bytes = progress.total_bytes;
  event.completed_bytes = progress.completed_bytes;
  event.total_files = progress.total_files;
  event.completed_files = progress.completed_files;
  event.error_count = progress.error_count;
  event.current_path = progress.current_path;
  event.message = progress.message;
  event.ratio_info = progress.ratio_info;
  event.benchmark_snapshot = progress.benchmark_snapshot;
  event.benchmark_summary = progress.benchmark_summary;
  push_event(event);
}

void OperationEventCollector::push_event(const z7::app::OperationEvent& event) {
  if (sink_ == nullptr) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  sink_->push_back(event);
}

}  // namespace hash_behavior_internal
