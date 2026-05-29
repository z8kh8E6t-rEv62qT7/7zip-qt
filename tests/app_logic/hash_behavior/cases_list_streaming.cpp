// tests/app_logic/hash_behavior/cases_list_streaming.cpp
// Role: B4 – ListRequest streaming batch callback behaviour cases.

#include "internal.h"

using namespace hash_behavior_internal;

namespace {

class BatchCollectorDelegate final : public z7::app::IArchiveDelegate {
 public:
  explicit BatchCollectorDelegate(std::optional<size_t> stop_after_batches = std::nullopt)
      : stop_after_(stop_after_batches) {}

  bool on_list_entries_batch(std::vector<z7::app::ArchiveListEntry>&& batch) override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++batch_count_;
    for (auto& e : batch) {
      all_entries_.push_back(std::move(e));
    }
    if (stop_after_.has_value() && batch_count_ >= *stop_after_) {
      return false;
    }
    return true;
  }

  size_t batch_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return batch_count_;
  }

  std::vector<z7::app::ArchiveListEntry> take_entries() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(all_entries_);
  }

 private:
  mutable std::mutex mutex_;
  size_t batch_count_ = 0;
  std::vector<z7::app::ArchiveListEntry> all_entries_;
  std::optional<size_t> stop_after_;
};

// Build a flat archive containing `count` small files named file_0.txt … file_{N-1}.txt.
// Returns the archive path, or empty string on failure.
QString make_flat_archive(const QTemporaryDir& root, int count) {
  const QString src_dir = QDir(root.path()).filePath(QStringLiteral("src"));
  if (!QDir().mkpath(src_dir)) {
    return {};
  }
  for (int i = 0; i < count; ++i) {
    const QString name = QStringLiteral("file_%1.txt").arg(i);
    QFile f(QDir(src_dir).filePath(name));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return {};
    }
    f.write(QByteArray("data-") + QByteArray::number(i));
    f.close();
  }

  const QString arc_path = QDir(root.path()).filePath(QStringLiteral("flat.7z"));
  z7::app::AddRequest add;
  add.archive_path = arc_path.toStdString();
  add.format = "7z";
  add.input_paths = {src_dir.toStdString()};
  if (!run_request_sync(add).ok) {
    return {};
  }
  return arc_path;
}

}  // namespace

// B4 test 1: streaming_mode=false → old behaviour, entries in ListResult.
void AppLogicHashBehaviorTest::listStreamingModeOffPreservesOldBehaviour() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString arc_path = make_flat_archive(root, 5);
  QVERIFY(!arc_path.isEmpty());

  z7::app::ListRequest req;
  req.archive_path = arc_path.toStdString();
  // streaming_mode defaults to false

  const z7::app::ListResult result = run_request_sync(req);
  QVERIFY(result.ok);
  // The 5 files are inside a "src" subdirectory; root lists "src".
  QVERIFY(!result.entries.empty());
}

// B4 test 2: streaming_mode=true → batches delivered, ListResult.entries empty.
void AppLogicHashBehaviorTest::listStreamingModeBatchesDeliveredAndEntriesEmpty() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  // Create 12 files so that with batch_size_hint=4 we get at least 3 batches.
  const QString src_dir = QDir(root.path()).filePath(QStringLiteral("src2"));
  QVERIFY(QDir().mkpath(src_dir));
  for (int i = 0; i < 12; ++i) {
    QFile f(QDir(src_dir).filePath(QStringLiteral("f%1.txt").arg(i)));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("x");
    f.close();
  }
  const QString arc_path = QDir(root.path()).filePath(QStringLiteral("batch.7z"));
  {
    z7::app::AddRequest add;
    add.archive_path = arc_path.toStdString();
    add.format = "7z";
    add.input_paths = {src_dir.toStdString()};
    QVERIFY(run_request_sync(add).ok);
  }

  // List the src2 subdirectory so we see the 12 files directly.
  z7::app::ListRequest req;
  req.archive_path = arc_path.toStdString();
  req.directory = "src2";
  req.streaming_mode = true;
  req.batch_size_hint = 4;

  // Default delegate (no override) — entries must be empty, result ok.
  {
    const z7::app::ListResult result = run_request_sync(req);
    QVERIFY(result.ok);
    QVERIFY(result.entries.empty());
  }

  // Custom batch-collector delegate.
  auto collector2 = std::make_shared<BatchCollectorDelegate>();
  StartedSession task = start_request(req, collector2);
  QVERIFY(task.valid());
  const z7::app::ListResult result2 = wait_for_result<z7::app::ListResult>(task);
  QVERIFY(result2.ok);
  QVERIFY(result2.entries.empty());

  const size_t batches = collector2->batch_count();
  QVERIFY2(batches >= 3,
           qPrintable(QStringLiteral("expected >= 3 batches, got %1").arg(batches)));

  const std::vector<z7::app::ArchiveListEntry> entries = collector2->take_entries();
  QCOMPARE(static_cast<int>(entries.size()), 12);

  // Verify all entries have non-empty paths.
  for (const auto& e : entries) {
    QVERIFY(!e.path.empty());
  }
}

// B4 test 3: delegate returns false on 3rd batch → backend cancels.
void AppLogicHashBehaviorTest::listStreamingModeDelegateEarlyStopCancels() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString src_dir = QDir(root.path()).filePath(QStringLiteral("src3"));
  QVERIFY(QDir().mkpath(src_dir));
  for (int i = 0; i < 12; ++i) {
    QFile f(QDir(src_dir).filePath(QStringLiteral("g%1.txt").arg(i)));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("y");
    f.close();
  }
  const QString arc_path = QDir(root.path()).filePath(QStringLiteral("stop.7z"));
  {
    z7::app::AddRequest add;
    add.archive_path = arc_path.toStdString();
    add.format = "7z";
    add.input_paths = {src_dir.toStdString()};
    QVERIFY(run_request_sync(add).ok);
  }

  // Stop after 2nd batch (returns false on 2nd callback).
  auto collector = std::make_shared<BatchCollectorDelegate>(/*stop_after_batches=*/2);

  z7::app::ListRequest req;
  req.archive_path = arc_path.toStdString();
  req.directory = "src3";
  req.streaming_mode = true;
  req.batch_size_hint = 4;

  StartedSession task = start_request(req, collector);
  QVERIFY(task.valid());
  const z7::app::ListResult result = wait_for_result<z7::app::ListResult>(task);

  // Backend aborted — result must not be ok.
  QVERIFY(!result.ok);
  QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kCanceled);

  // Entries delivered before the stop are retained by the collector.
  const std::vector<z7::app::ArchiveListEntry> entries = collector->take_entries();
  QVERIFY(entries.size() <= 12);
  QVERIFY(entries.size() >= 1);
}

// B4 test 4: session token reuse + streaming → no second archive open needed.
void AppLogicHashBehaviorTest::listStreamingModeSessionTokenReuse() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString src_dir = QDir(root.path()).filePath(QStringLiteral("src4"));
  QVERIFY(QDir().mkpath(src_dir));
  for (int i = 0; i < 8; ++i) {
    QFile f(QDir(src_dir).filePath(QStringLiteral("h%1.txt").arg(i)));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("z");
    f.close();
  }
  const QString arc_path = QDir(root.path()).filePath(QStringLiteral("session.7z"));
  {
    z7::app::AddRequest add;
    add.archive_path = arc_path.toStdString();
    add.format = "7z";
    add.input_paths = {src_dir.toStdString()};
    QVERIFY(run_request_sync(add).ok);
  }

  // Open a session.
  z7::app::OpenArchiveFromPathRequest open_req;
  open_req.archive_path = arc_path.toStdString();
  const z7::app::OpenArchiveSessionResult open_result = run_request_sync(open_req);
  QVERIFY(open_result.ok);
  const z7::app::ArchiveSessionToken token = open_result.token;

  // Streaming list using the session token.
  auto collector = std::make_shared<BatchCollectorDelegate>();
  z7::app::ListRequest req;
  req.session_token = token;
  req.directory = "src4";
  req.streaming_mode = true;
  req.batch_size_hint = 3;

  StartedSession task = start_request(req, collector);
  QVERIFY(task.valid());
  const z7::app::ListResult result = wait_for_result<z7::app::ListResult>(task);
  QVERIFY(result.ok);
  QVERIFY(result.entries.empty());

  const std::vector<z7::app::ArchiveListEntry> entries = collector->take_entries();
  QCOMPARE(static_cast<int>(entries.size()), 8);

  // Non-streaming list via same token should still return full entries.
  z7::app::ListRequest non_stream_req;
  non_stream_req.session_token = token;
  non_stream_req.directory = "src4";
  const z7::app::ListResult non_stream = run_request_sync(non_stream_req);
  QVERIFY(non_stream.ok);
  QCOMPARE(static_cast<int>(non_stream.entries.size()), 8);

  // Close the session.
  z7::app::CloseArchiveSessionRequest close_req;
  close_req.token = token;
  QVERIFY(run_request_sync(close_req).ok);
}
