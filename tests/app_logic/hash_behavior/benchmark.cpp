// tests/app_logic/hash_behavior/benchmark.cpp
// Role: Benchmark typed snapshot behavior cases.

#include "internal.h"

using namespace hash_behavior_internal;

void AppLogicHashBehaviorTest::benchmarkTypedSnapshotsExposeStructuredSizes() {
    z7::app::BenchmarkRequest request;
    request.iterations = 1;
    request.thread_count = "1";
    request.dictionary_size = "1m";
    request.method_value = "LZMA2";
    request.total_mode = false;

    std::vector<z7::app::OperationEvent> events;
    const z7::app::BenchmarkResult result = run_request_sync(request, events);
    QVERIFY(result.ok);
    QVERIFY(result.typed_summary.has_value());
    QVERIFY(result.typed_summary->has_average_metrics);

    bool saw_dictionary_or_average = false;
    bool saw_compress_size = false;
    bool saw_decompress_size = false;
    for (const z7::app::OperationEvent& event : events) {
      if (!event.benchmark_snapshot.has_value()) {
        continue;
      }
      const z7::app::BenchmarkTypedSnapshot& snapshot = *event.benchmark_snapshot;
      if (snapshot.kind != z7::app::BenchmarkSnapshotKind::kDictionaryPass &&
          snapshot.kind != z7::app::BenchmarkSnapshotKind::kAveragePass) {
        continue;
      }
      saw_dictionary_or_average = true;
      saw_compress_size = saw_compress_size ||
                          (snapshot.metrics.has_compress_size &&
                           snapshot.metrics.compress_size_bytes > 0);
      saw_decompress_size = saw_decompress_size ||
                            (snapshot.metrics.has_decompress_size &&
                             snapshot.metrics.decompress_size_bytes > 0);
    }

    QVERIFY(saw_dictionary_or_average);
    QVERIFY(saw_compress_size);
    QVERIFY(saw_decompress_size);
}

void AppLogicHashBehaviorTest::benchmarkRequestRejectsInvalidDictionarySize() {
    z7::app::BenchmarkRequest request;
    request.iterations = 1;
    request.thread_count = "1";
    request.dictionary_size = "bad-size";
    request.method_value = "LZMA2";
    request.total_mode = false;

    const z7::app::BenchmarkResult result = run_request_sync(request);
    QVERIFY(!result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);
}

void AppLogicHashBehaviorTest::benchmarkMemoryLimitRequiresTypedHandler() {
    z7::app::BenchmarkRequest request;
    request.iterations = 1;
    request.thread_count = "1";
    request.dictionary_size = "1024g";
    request.method_value = "LZMA2";
    request.total_mode = false;

    DelegateInteraction interaction;

    const z7::app::BenchmarkResult result = run_request_sync(request, interaction);
    QVERIFY(!result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);
    QVERIFY(QString::fromStdString(result.error.message)
                .contains(QStringLiteral("memory limit")));
    QCOMPARE(result.native_execution.termination_reason,
             z7::app::NativeTerminationReason::kCompleted);
}

void AppLogicHashBehaviorTest::benchmarkMemoryLimitExplicitHandlerTakesPriorityOverChoice() {
    z7::app::BenchmarkRequest request;
    request.iterations = 1;
    request.thread_count = "1";
    request.dictionary_size = "1024g";
    request.method_value = "LZMA2";
    request.total_mode = false;

    std::atomic<int> memory_calls{0};
    std::atomic<int> choice_calls{0};
    DelegateInteraction interaction;
    interaction.request_memory_limit = [&memory_calls](const z7::app::MemoryLimitPrompt&) {
      ++memory_calls;
      z7::app::MemoryLimitReply reply;
      reply.action = z7::app::MemoryLimitAction::kSkipOperation;
      return reply;
    };
    interaction.request_choice = [&choice_calls](const z7::app::ChoicePrompt&) {
      ++choice_calls;
      z7::app::ChoiceReply reply;
      reply.kind = z7::app::ChoiceReplyKind::kSelect;
      reply.selected_index = 1;
      return reply;
    };

    const z7::app::BenchmarkResult result = run_request_sync(request, interaction);
    QCOMPARE(memory_calls.load(), 1);
    QCOMPARE(choice_calls.load(), 0);
    QVERIFY(!result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kCanceled);
}

void AppLogicHashBehaviorTest::benchmarkMemoryLimitUpdatedLimitBelowEstimateCancelsOperation() {
    z7::app::BenchmarkRequest request;
    request.iterations = 1;
    request.thread_count = "1";
    request.dictionary_size = "1024g";
    request.method_value = "LZMA2";
    request.total_mode = false;

    std::atomic<int> memory_calls{0};
    DelegateInteraction interaction;
    interaction.request_memory_limit = [&memory_calls](const z7::app::MemoryLimitPrompt&) {
      ++memory_calls;
      z7::app::MemoryLimitReply reply;
      reply.action = z7::app::MemoryLimitAction::kUpdateLimitAndContinue;
      reply.updated_limit_bytes = 1;
      return reply;
    };

    const z7::app::BenchmarkResult result = run_request_sync(request, interaction);
    QCOMPARE(memory_calls.load(), 1);
    QVERIFY(!result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kCanceled);
    QCOMPARE(result.native_execution.termination_reason,
             z7::app::NativeTerminationReason::kCanceled);
}
