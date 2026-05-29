// tests/app_logic/hash_behavior/cases_benchmark_split.cpp
// Role: Benchmark and split/combine behavior cases.

#include "internal.h"

using namespace hash_behavior_internal;

namespace {

void write_sparse_file(const QString& path, qint64 bytes) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QVERIFY(file.resize(bytes));
  file.close();
}

class ProgressProbeDelegate final : public z7::app::IArchiveDelegate {
 public:
  explicit ProgressProbeDelegate(std::atomic<uint64_t>& completed_bytes)
      : completed_bytes_(completed_bytes) {}

  void on_progress(const z7::app::ProgressSnapshot& progress) override {
    completed_bytes_.store(progress.completed_bytes, std::memory_order_relaxed);
  }

 private:
  std::atomic<uint64_t>& completed_bytes_;
};

}  // namespace

void AppLogicHashBehaviorTest::benchmarkRequestReturnsSummaryLine() {
    z7::app::BenchmarkRequest request;
    request.iterations = 1;
    request.thread_count = "1";
    request.dictionary_size = "1m";
    request.method_value = "LZMA2";

    std::vector<z7::app::OperationEvent> events;
    const z7::app::BenchmarkResult result = run_request_sync(request, events);
    QVERIFY(result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kNone);
    QVERIFY(!QString::fromStdString(result.summary).trimmed().isEmpty());
    const QString summary_line = QString::fromStdString(result.summary_line).trimmed();
    QVERIFY(!summary_line.isEmpty());
    QVERIFY(result.typed_summary.has_value());
    QVERIFY(result.typed_summary->has_total_rating ||
            result.typed_summary->has_average_metrics);
    if (result.typed_summary->has_total_rating) {
      QVERIFY(summary_line.startsWith(QStringLiteral("Tot:")));
    } else if (result.typed_summary->has_average_metrics) {
      QVERIFY(summary_line.startsWith(QStringLiteral("Avr:")));
    }

    bool has_typed_snapshot = false;
    bool has_text_percent_progress = false;
    for (const z7::app::OperationEvent& event : events) {
      if (event.benchmark_snapshot.has_value()) {
        has_typed_snapshot = true;
      }
      if (event.kind == z7::app::OperationEventKind::kProgress &&
          event.percent >= 0) {
        has_text_percent_progress = true;
      }
    }
    QVERIFY(has_typed_snapshot);
    QVERIFY(!has_text_percent_progress);

    const std::optional<size_t> prepare = first_stage_index(
        events, z7::app::OperationStage::kPrepare);
    const std::optional<size_t> completed = first_stage_index(
        events, z7::app::OperationStage::kCompleted);
    QVERIFY(prepare.has_value());
    QVERIFY(completed.has_value());
    QVERIFY(*prepare < *completed);
}

void AppLogicHashBehaviorTest::backendCapabilitiesExposeSplitCombineAndTypedBenchmark() {
    const z7::app::BackendCapabilities caps = z7::app::ArchiveEngine::query_capabilities();
    QVERIFY(caps.supports_split);
    QVERIFY(caps.supports_combine);
    QVERIFY(caps.supports_typed_benchmark);
}

void AppLogicHashBehaviorTest::splitAndCombineRoundTripMatchesSourceBytes() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("payload.bin"));
    QByteArray payload;
    payload.resize(3 * 1024 * 1024 + 123);
    for (int i = 0; i < payload.size(); ++i) {
      payload[i] = static_cast<char>(i % 251);
    }
    {
      QFile file(source_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QCOMPARE(file.write(payload), payload.size());
    }

    const QString split_dir = QDir(root.path()).filePath(QStringLiteral("split"));
    const QString combine_dir = QDir(root.path()).filePath(QStringLiteral("combine"));
    QVERIFY(QDir().mkpath(split_dir));
    QVERIFY(QDir().mkpath(combine_dir));

    z7::app::SplitRequest split_request;
    split_request.source_file_path = to_std_path(source_file);
    split_request.output_dir = to_std_path(split_dir);
    split_request.volume_size_spec = "1M";
    const z7::app::SplitResult split_result = run_request_sync(split_request);
    QVERIFY(split_result.ok);
    QVERIFY(split_result.volume_count > 1);
    QCOMPARE(static_cast<int>(split_result.generated_volume_paths.size()),
             static_cast<int>(split_result.volume_count));

    const QString first_part =
        QFile::decodeName(split_result.generated_volume_paths.front().c_str());
    QVERIFY(QFileInfo::exists(first_part));

    z7::app::CombineRequest combine_request;
    combine_request.source_part_path = to_std_path(first_part);
    combine_request.output_dir = to_std_path(combine_dir);
    const z7::app::CombineResult combine_result = run_request_sync(combine_request);
    QVERIFY(combine_result.ok);
    QCOMPARE(static_cast<int>(combine_result.input_volume_paths.size()),
             static_cast<int>(split_result.volume_count));

    const QString combined_file = QDir(combine_dir).filePath(QStringLiteral("payload.bin"));
    QFile combined(combined_file);
    QVERIFY(combined.open(QIODevice::ReadOnly));
    QCOMPARE(combined.readAll(), payload);
}

void AppLogicHashBehaviorTest::splitAndCombineReportExpectedErrors() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("piece.bin"));
    {
      QFile file(source_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QVERIFY(file.resize(2 * 1024 * 1024));
    }

    const QString split_dir = QDir(root.path()).filePath(QStringLiteral("split"));
    const QString combine_dir = QDir(root.path()).filePath(QStringLiteral("combine"));
    QVERIFY(QDir().mkpath(split_dir));
    QVERIFY(QDir().mkpath(combine_dir));

    z7::app::SplitRequest bad_split;
    bad_split.source_file_path = to_std_path(source_file);
    bad_split.output_dir = to_std_path(split_dir);
    bad_split.volume_size_spec = "abc";
    const z7::app::SplitResult bad_split_result = run_request_sync(bad_split);
    QVERIFY(!bad_split_result.ok);
    QCOMPARE(bad_split_result.error.domain,
             z7::app::ArchiveErrorDomain::kInvalidArguments);

    z7::app::SplitRequest good_split = bad_split;
    good_split.volume_size_spec = "1M";
    const z7::app::SplitResult good_split_result = run_request_sync(good_split);
    QVERIFY(good_split_result.ok);
    QVERIFY(good_split_result.generated_volume_paths.size() >= 2);

    const QString first_part =
        QFile::decodeName(good_split_result.generated_volume_paths.front().c_str());
    const QString second_part =
        QFile::decodeName(good_split_result.generated_volume_paths.at(1).c_str());
    QVERIFY(QFile::remove(second_part));

    z7::app::CombineRequest missing_part_combine;
    missing_part_combine.source_part_path = to_std_path(first_part);
    missing_part_combine.output_dir = to_std_path(combine_dir);
    const z7::app::CombineResult missing_result = run_request_sync(missing_part_combine);
    QVERIFY(!missing_result.ok);
    QCOMPARE(missing_result.error.domain,
             z7::app::ArchiveErrorDomain::kInvalidArguments);

    const QString split_dir_2 = QDir(root.path()).filePath(QStringLiteral("split2"));
    QVERIFY(QDir().mkpath(split_dir_2));
    z7::app::SplitRequest second_split = good_split;
    second_split.output_dir = to_std_path(split_dir_2);
    const z7::app::SplitResult second_split_result = run_request_sync(second_split);
    QVERIFY(second_split_result.ok);
    QVERIFY(second_split_result.generated_volume_paths.size() >= 2);
    const QString first_part_2 =
        QFile::decodeName(second_split_result.generated_volume_paths.front().c_str());

    QFile existing(QDir(combine_dir).filePath(QStringLiteral("piece.bin")));
    QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
    existing.close();

    z7::app::CombineRequest existing_target_combine;
    existing_target_combine.source_part_path = to_std_path(first_part_2);
    existing_target_combine.output_dir = to_std_path(combine_dir);
    const z7::app::CombineResult existing_result = run_request_sync(existing_target_combine);
    QVERIFY(!existing_result.ok);
    QCOMPARE(existing_result.error.domain,
             z7::app::ArchiveErrorDomain::kIo);
}

void AppLogicHashBehaviorTest::splitAndCombineCancelStateMachine() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("cancel-payload.bin"));
    write_sparse_file(source_file, 1024LL * 1024LL * 1024LL);

    bool split_cancel_asserted = false;
    {
      const QString split_dir = QDir(root.path()).filePath(QStringLiteral("split-cancel"));
      QVERIFY(QDir().mkpath(split_dir));

      z7::app::SplitRequest split_request;
      split_request.source_file_path = to_std_path(source_file);
      split_request.output_dir = to_std_path(split_dir);
      split_request.volume_size_spec = "4M";

      std::atomic<uint64_t> split_progress{0};
      auto split_delegate = std::make_shared<ProgressProbeDelegate>(split_progress);
      auto split_task = start_request(split_request, split_delegate);
      QVERIFY(split_task.valid());
      QVERIFY(wait_until(
          [&split_progress, &split_task]() {
            return split_progress.load(std::memory_order_relaxed) > 0 ||
                   is_terminal_state(split_task.state());
          },
          12000));

      if (!is_terminal_state(split_task.state())) {
        split_task.cancel();
      }
      const z7::app::SplitResult split_result = wait_for_result<z7::app::SplitResult>(split_task);
      if (!split_result.ok) {
        QCOMPARE(split_result.error.domain, z7::app::ArchiveErrorDomain::kCanceled);
        split_cancel_asserted = true;
      }
    }

    const QString split_for_combine_dir =
        QDir(root.path()).filePath(QStringLiteral("split-for-combine"));
    QVERIFY(QDir().mkpath(split_for_combine_dir));
    z7::app::SplitRequest prep_split_request;
    prep_split_request.source_file_path = to_std_path(source_file);
    prep_split_request.output_dir = to_std_path(split_for_combine_dir);
    prep_split_request.volume_size_spec = "4M";
    const z7::app::SplitResult prep_split_result = run_request_sync(prep_split_request);
    QVERIFY(prep_split_result.ok);
    QVERIFY(prep_split_result.generated_volume_paths.size() > 1);

    bool combine_cancel_asserted = false;
    {
      const QString combine_out_dir = QDir(root.path()).filePath(QStringLiteral("combine-cancel"));
      QVERIFY(QDir().mkpath(combine_out_dir));

      z7::app::CombineRequest combine_request;
      combine_request.source_part_path = prep_split_result.generated_volume_paths.front();
      combine_request.output_dir = to_std_path(combine_out_dir);

      std::atomic<uint64_t> combine_progress{0};
      auto combine_delegate = std::make_shared<ProgressProbeDelegate>(combine_progress);
      auto combine_task = start_request(combine_request, combine_delegate);
      QVERIFY(combine_task.valid());
      QVERIFY(wait_until(
          [&combine_progress, &combine_task]() {
            return combine_progress.load(std::memory_order_relaxed) > 0 ||
                   is_terminal_state(combine_task.state());
          },
          12000));

      if (!is_terminal_state(combine_task.state())) {
        combine_task.cancel();
      }
      const z7::app::CombineResult combine_result =
          wait_for_result<z7::app::CombineResult>(combine_task);
      if (!combine_result.ok) {
        QCOMPARE(combine_result.error.domain, z7::app::ArchiveErrorDomain::kCanceled);
        combine_cancel_asserted = true;
      }
    }

    if (!split_cancel_asserted && !combine_cancel_asserted) {
      QSKIP("split/combine finished too fast to reliably assert cancel domain");
    }
}
