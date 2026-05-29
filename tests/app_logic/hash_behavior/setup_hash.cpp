// tests/app_logic/hash_behavior/setup_hash.cpp
// Role: Hash-specific behavior cases.

#include "internal.h"

using namespace hash_behavior_internal;

void AppLogicHashBehaviorTest::singleMethodAndAllMethodsProduceDigestSummary() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("sample.bin"));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("sample-data");
    file.close();

    z7::app::HashRequest single_request;
    single_request.hash_method = "CRC32";
    single_request.input_paths = {to_std_path(file_path)};

    const z7::app::HashResult single_result = run_request_sync(single_request);
    QVERIFY(single_result.ok);
    QVERIFY(single_result.hash_summary.has_value());

    const z7::app::HashSummary& single_summary = *single_result.hash_summary;
    QCOMPARE(single_summary.num_dirs, static_cast<uint64_t>(0));
    QCOMPARE(single_summary.num_files, static_cast<uint64_t>(1));
    QCOMPARE(single_summary.methods.size(), static_cast<size_t>(1));
    QCOMPARE(QString::fromStdString(single_summary.methods[0].method_name),
             QStringLiteral("CRC32"));
    QVERIFY(single_summary.methods[0].has_data_sum);
    QVERIFY(!single_summary.methods[0].data_sum.empty());

    z7::app::HashRequest all_request = single_request;
    all_request.hash_method = "*";

    const z7::app::HashResult all_result = run_request_sync(all_request);
    QVERIFY(all_result.ok);
    QVERIFY(all_result.hash_summary.has_value());

    const z7::app::HashSummary& all_summary = *all_result.hash_summary;
    QVERIFY(all_summary.methods.size() >= 2);
    std::set<std::string> methods;
    for (const z7::app::HashMethodDigest& digest : all_summary.methods) {
      methods.insert(digest.method_name);
      QVERIFY(digest.has_data_sum);
      QVERIFY(!digest.data_sum.empty());
    }
    QVERIFY(methods.find("CRC32") != methods.end());
    QVERIFY(methods.find("SHA256") != methods.end());
  }

void AppLogicHashBehaviorTest::hashRequestRejectsUnknownMethod() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("unknown.bin"));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("unknown-method");
    file.close();

    z7::app::HashRequest request;
    request.hash_method = "NOT_A_REAL_HASH";
    request.input_paths = {to_std_path(file_path)};

    const z7::app::HashResult result = run_request_sync(request);
    QVERIFY(!result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);
}

void AppLogicHashBehaviorTest::hashRequestReturnsPartialSuccessWhenInputMissing() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString good_path = QDir(root.path()).filePath(QStringLiteral("good.bin"));
    {
      QFile file(good_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("hash-partial");
      file.close();
    }
    const QString missing_path = QDir(root.path()).filePath(QStringLiteral("missing.bin"));
    QVERIFY(!QFileInfo::exists(missing_path));

    z7::app::HashRequest request;
    request.hash_method = "CRC32";
    request.input_paths = {to_std_path(good_path), to_std_path(missing_path)};

    const z7::app::HashResult result = run_request_sync(request);
    QVERIFY(result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kNone);
    QVERIFY(result.hash_summary.has_value());
    QVERIFY(result.hash_summary->num_errors >= 1);
}

void AppLogicHashBehaviorTest::directoryRecursionFlagControlsSummaryCounts() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString folder_path = QDir(root.path()).filePath(QStringLiteral("folder"));
    const QString nested_dir = QDir(folder_path).filePath(QStringLiteral("nested"));
    QVERIFY(QDir().mkpath(nested_dir));

    const QString top_file = QDir(folder_path).filePath(QStringLiteral("top.txt"));
    {
      QFile file(top_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("abc");
      file.close();
    }

    const QString nested_file = QDir(nested_dir).filePath(QStringLiteral("leaf.txt"));
    {
      QFile file(nested_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("12345");
      file.close();
    }

    z7::app::HashRequest non_recursive;
    non_recursive.hash_method = "CRC32";
    non_recursive.input_paths = {to_std_path(folder_path)};
    non_recursive.recursive_dirs = false;

    const z7::app::HashResult non_recursive_result = run_request_sync(non_recursive);
    QVERIFY(non_recursive_result.ok);
    QVERIFY(non_recursive_result.hash_summary.has_value());
    const z7::app::HashSummary& non_recursive_summary =
        *non_recursive_result.hash_summary;
    QCOMPARE(non_recursive_summary.num_dirs, static_cast<uint64_t>(1));
    QCOMPARE(non_recursive_summary.num_files, static_cast<uint64_t>(0));
    QCOMPARE(non_recursive_summary.files_size, static_cast<uint64_t>(0));

    z7::app::HashRequest recursive = non_recursive;
    recursive.recursive_dirs = true;

    const z7::app::HashResult recursive_result = run_request_sync(recursive);
    QVERIFY(recursive_result.ok);
    QVERIFY(recursive_result.hash_summary.has_value());
    const z7::app::HashSummary& recursive_summary = *recursive_result.hash_summary;
    QVERIFY(recursive_summary.num_dirs >= 2);
    QCOMPARE(recursive_summary.num_files, static_cast<uint64_t>(2));
    QCOMPARE(recursive_summary.files_size, static_cast<uint64_t>(8));
  }

void AppLogicHashBehaviorTest::hashPauseResumeCancelStateMachine() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    QStringList inputs;
    for (int i = 0; i < 4; ++i) {
      const QString path =
          QDir(root.path()).filePath(QStringLiteral("large_%1.bin").arg(i));
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QVERIFY(file.resize(1024LL * 1024LL * 1024LL));
      file.close();
      inputs.push_back(path);
    }

    z7::app::HashRequest request;
    request.hash_method = "SHA256";
    for (const QString& path : inputs) {
      request.input_paths.push_back(to_std_path(path));
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

    const auto launch_task = [&request](std::atomic<uint64_t>& completed_bytes) {
      auto delegate = std::make_shared<ProgressProbeDelegate>(completed_bytes);
      auto task = start_request(request, std::move(delegate));
      return task;
    };

    std::atomic<uint64_t> completed_bytes{0};
    auto task = launch_task(completed_bytes);
    QVERIFY(task.valid());

    QVERIFY(wait_until(
        [&completed_bytes, &task]() {
          return completed_bytes.load(std::memory_order_relaxed) > 0 ||
                 is_terminal_state(task.state());
        },
        5000));
    if (is_terminal_state(task.state())) {
      const z7::app::HashResult quick = wait_for_result<z7::app::HashResult>(task);
      QVERIFY(quick.ok);
      QSKIP("hash finished too fast to reliably assert pause/resume");
    }

    task.pause();
    QVERIFY(wait_until_stable(completed_bytes, 300, 5000));
    const uint64_t paused_bytes = completed_bytes.load(std::memory_order_relaxed);

    task.resume();
    QVERIFY(wait_until(
        [&completed_bytes, paused_bytes, &task]() {
          return completed_bytes.load(std::memory_order_relaxed) > paused_bytes ||
                 is_terminal_state(task.state());
        },
        5000));

    const z7::app::HashResult finished = wait_for_result<z7::app::HashResult>(task);
    QVERIFY(finished.ok);
    QVERIFY(finished.hash_summary.has_value());
    QCOMPARE(finished.hash_summary->num_files, static_cast<uint64_t>(inputs.size()));

    std::atomic<uint64_t> cancel_progress{0};
    auto cancel_task = launch_task(cancel_progress);
    QVERIFY(cancel_task.valid());

    QVERIFY(wait_until(
        [&cancel_progress, &cancel_task]() {
          return cancel_progress.load(std::memory_order_relaxed) > 0 ||
                 is_terminal_state(cancel_task.state());
        },
        5000));
    if (is_terminal_state(cancel_task.state())) {
      const z7::app::HashResult quick =
          wait_for_result<z7::app::HashResult>(cancel_task);
      QVERIFY(quick.ok);
      QSKIP("hash finished too fast to reliably assert cancel");
    }

    cancel_task.cancel();
    const z7::app::HashResult canceled =
        wait_for_result<z7::app::HashResult>(cancel_task);
    QVERIFY(!canceled.ok);
    QCOMPARE(canceled.error.domain, z7::app::ArchiveErrorDomain::kCanceled);
  }
