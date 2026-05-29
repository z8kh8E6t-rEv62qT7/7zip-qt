// tests/app_logic/hash_behavior/setup.cpp
// Role: Archive task/file-operation behavior cases.

#include "internal.h"

using namespace hash_behavior_internal;

void AppLogicHashBehaviorTest::testArchiveUsesOriginalApiPathAndStructuredProgress() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("payload.txt"));
    {
      QFile file(file_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("payload-for-test");
      QVERIFY(file.resize(256 * 1024));
      file.close();
    }

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("payload.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(file_path)};
    std::vector<z7::app::OperationEvent> add_events;
    const z7::app::AddResult add_result = run_request_sync(add_request, add_events);
    QVERIFY(add_result.ok);
    bool add_reported_ratio = false;
    for (const z7::app::OperationEvent& event : add_events) {
      if (event.kind == z7::app::OperationEventKind::kProgress &&
          event.ratio_info.has_value() &&
          event.ratio_info->compressing_mode &&
          (event.ratio_info->input_size_known ||
           event.ratio_info->output_size_known)) {
        add_reported_ratio = true;
        break;
      }
    }
    QVERIFY(add_reported_ratio);

    std::vector<z7::app::OperationEvent> events;

    z7::app::TestRequest test_request;
    test_request.archive_path = to_std_path(archive_path);
    const z7::app::TestResult test_result = run_request_sync(test_request, events);
    QVERIFY(test_result.ok);
    QCOMPARE(test_result.error.domain, z7::app::ArchiveErrorDomain::kNone);
    QCOMPARE(QString::fromStdString(test_result.summary),
             QStringLiteral("There are no errors"));

    bool has_structured_progress = false;
    bool has_current_path = false;
    bool has_ratio_progress = false;
    for (const z7::app::OperationEvent& event : events) {
      if (event.kind != z7::app::OperationEventKind::kProgress ||
          event.stage != z7::app::OperationStage::kRunning) {
        continue;
      }
      if (event.total_files > 0 && event.completed_files > 0) {
        has_structured_progress = true;
      }
      if (!event.current_path.empty()) {
        has_current_path = true;
      }
      if (event.ratio_info.has_value() &&
          !event.ratio_info->compressing_mode &&
          (event.ratio_info->input_size_known ||
           event.ratio_info->output_size_known)) {
        has_ratio_progress = true;
      }
    }
    QVERIFY(has_structured_progress);
    QVERIFY(has_current_path);
    QVERIFY(has_ratio_progress);
  }

void AppLogicHashBehaviorTest::testArchivePathsContinueAfterInvalidInputLikeOriginal() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString payload_path =
        QDir(root.path()).filePath(QStringLiteral("payload.txt"));
    {
      QFile file(payload_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("payload-for-test");
      file.close();
    }

    const QString plain_path =
        QDir(root.path()).filePath(QStringLiteral("plain.txt"));
    {
      QFile file(plain_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("not an archive");
      file.close();
    }

    const QString archive_path =
        QDir(root.path()).filePath(QStringLiteral("payload.7z"));
    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(payload_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    std::vector<z7::app::OperationEvent> events;
    z7::app::TestRequest test_request;
    test_request.archive_paths = {
        to_std_path(plain_path),
        to_std_path(archive_path),
    };

    const z7::app::TestResult test_result = run_request_sync(test_request, events);
    QVERIFY(!test_result.ok);

    const std::string archive_native_path = to_std_path(archive_path);
    bool tested_later_archive = false;
    for (const z7::app::OperationEvent& event : events) {
      if (event.kind != z7::app::OperationEventKind::kProgress ||
          event.stage != z7::app::OperationStage::kRunning) {
        continue;
      }
      if (event.current_path == archive_native_path && event.error_count != 0) {
        tested_later_archive = true;
        break;
      }
    }
    QVERIFY(tested_later_archive);
  }

void AppLogicHashBehaviorTest::testArchivePathsExpandDirectoriesLikeOriginal() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString input_dir = QDir(root.path()).filePath(QStringLiteral("input"));
    QVERIFY(QDir().mkpath(QDir(input_dir).filePath(QStringLiteral("docs/guides"))));

    const auto write_file = [](const QString& path, const QByteArray& bytes) {
      QFile file(path);
      if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
      }
      return file.write(bytes) == bytes.size();
    };

    QVERIFY(write_file(QDir(input_dir).filePath(QStringLiteral(".DS_Store")),
                       QByteArrayLiteral("finder metadata")));
    QVERIFY(write_file(QDir(input_dir).filePath(QStringLiteral("media.m4s")),
                       QByteArrayLiteral("media")));
    QVERIFY(write_file(QDir(input_dir).filePath(QStringLiteral("docs/readme.md")),
                       QByteArrayLiteral("readme")));
    QVERIFY(write_file(QDir(input_dir).filePath(QStringLiteral("docs/guides/install.txt")),
                       QByteArrayLiteral("install")));

    const QString payload_path =
        QDir(root.path()).filePath(QStringLiteral("payload.txt"));
    QVERIFY(write_file(payload_path, QByteArrayLiteral("payload")));

    const QString archive_path =
        QDir(input_dir).filePath(QStringLiteral("valid.7z"));
    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(payload_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    std::vector<z7::app::OperationEvent> events;
    z7::app::TestRequest test_request;
    test_request.archive_paths = {to_std_path(input_dir)};

    const z7::app::TestResult test_result = run_request_sync(test_request, events);
    QVERIFY(!test_result.ok);

    bool saw_final_batch_progress = false;
    int stderr_messages = 0;
    for (const z7::app::OperationEvent& event : events) {
      if (event.kind == z7::app::OperationEventKind::kProgress &&
          event.stage == z7::app::OperationStage::kRunning &&
          event.percent == 100 &&
          event.total_files == 5 &&
          event.completed_files == 5 &&
          event.error_count == 4) {
        saw_final_batch_progress = true;
      }
      if (event.kind == z7::app::OperationEventKind::kLog &&
          event.output_channel == z7::app::OutputChannel::kStdErr) {
        ++stderr_messages;
        QVERIFY(event.message.find('\n') != std::string::npos);
        QVERIFY(event.message.find("Archives:") == std::string::npos);
        QVERIFY(event.message.find("Physical Size") == std::string::npos);
      }
    }
    QVERIFY(saw_final_batch_progress);
    QCOMPARE(stderr_messages, 4);
  }

void AppLogicHashBehaviorTest::extractArchiveUsesOriginalApiPathAndStructuredProgress() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("extract-me.txt"));
    {
      QFile file(file_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("payload-for-extract");
      QVERIFY(file.resize(256 * 1024));
      file.close();
    }

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("extract.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(output_dir));

    std::vector<z7::app::OperationEvent> events;

    z7::app::ExtractRequest extract_request;
    extract_request.archive_path = to_std_path(archive_path);
    extract_request.output_dir = to_std_path(output_dir);
    extract_request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
    const z7::app::ExtractResult extract_result = run_request_sync(extract_request, events);
    QVERIFY(extract_result.ok);
    QCOMPARE(extract_result.error.domain, z7::app::ArchiveErrorDomain::kNone);
    QCOMPARE(QString::fromStdString(extract_result.summary),
             QStringLiteral("Everything is Ok"));

    const QString extracted_file = QDir(output_dir).filePath(QStringLiteral("extract-me.txt"));
    QVERIFY(QFileInfo::exists(extracted_file));
    QFile extracted(extracted_file);
    QVERIFY(extracted.open(QIODevice::ReadOnly));
    const QByteArray extracted_bytes = extracted.readAll();
    QVERIFY(extracted_bytes.startsWith(QByteArray("payload-for-extract")));
    QCOMPARE(extracted_bytes.size(), 256 * 1024);

    bool has_structured_progress = false;
    bool has_known_totals = false;
    bool has_ratio_progress = false;
    for (const z7::app::OperationEvent& event : events) {
      if (event.kind != z7::app::OperationEventKind::kProgress ||
          event.stage != z7::app::OperationStage::kRunning) {
        continue;
      }
      if (event.total_files > 0 && event.completed_files > 0) {
        has_structured_progress = true;
      }
      if (event.totals_known && event.total_bytes > 0) {
        has_known_totals = true;
      }
      if (event.ratio_info.has_value() &&
          !event.ratio_info->compressing_mode &&
          (event.ratio_info->input_size_known ||
           event.ratio_info->output_size_known)) {
        has_ratio_progress = true;
      }
    }
    QVERIFY(has_structured_progress);
    QVERIFY(has_known_totals);
    QVERIFY(has_ratio_progress);
  }

void AppLogicHashBehaviorTest::testArchivePauseResumeStateMachine() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("pause-test.bin"));
    {
      QFile file(file_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QVERIFY(file.resize(1024LL * 1024LL * 1024LL));
      file.close();
    }

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("pause-test.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

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

    std::atomic<uint64_t> completed_bytes{0};
    z7::app::TestRequest request;
    request.archive_path = to_std_path(archive_path);
    auto delegate = std::make_shared<ProgressProbeDelegate>(completed_bytes);
    auto task = start_request(request, delegate);
    QVERIFY(task.valid());

    QVERIFY(wait_until(
        [&completed_bytes, &task]() {
          return completed_bytes.load(std::memory_order_relaxed) > 0 ||
                 is_terminal_state(task.state());
        },
        10000));

    if (is_terminal_state(task.state())) {
      const z7::app::TestResult quick = wait_for_result<z7::app::TestResult>(task);
      QVERIFY(quick.ok);
      QSKIP("test finished too fast to reliably assert pause/resume");
    }

    task.pause();

    if (!wait_until_stable(completed_bytes, 300, 5000)) {
      task.resume();
      const z7::app::TestResult quick = wait_for_result<z7::app::TestResult>(task);
      QVERIFY(quick.ok);
      QSKIP("test progressed too fast to reliably assert paused state");
    }
    const uint64_t paused_bytes = completed_bytes.load(std::memory_order_relaxed);

    task.resume();
    QVERIFY(wait_until(
        [&completed_bytes, &task, paused_bytes]() {
          return completed_bytes.load(std::memory_order_relaxed) > paused_bytes ||
                 is_terminal_state(task.state());
        },
        8000));

    const z7::app::TestResult result = wait_for_result<z7::app::TestResult>(task);
    QVERIFY(result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kNone);
}

void AppLogicHashBehaviorTest::releasingRunningSessionDoesNotSelfJoinWorkerThread() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("self-join-test.bin"));
    {
      QFile file(file_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QVERIFY(file.resize(1024LL * 1024LL * 1024LL));
      file.close();
    }

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("self-join-test.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    class BlockingProgressDelegate final : public z7::app::IArchiveDelegate {
     public:
      BlockingProgressDelegate()
          : saw_progress_future_(saw_progress_.get_future().share()),
            release_future_(release_.get_future().share()) {}

      void on_progress(const z7::app::ProgressSnapshot&) override {
        std::call_once(progress_once_, [this]() {
          saw_progress_.set_value();
          release_future_.wait();
        });
      }

      bool wait_for_progress(std::chrono::milliseconds timeout) const {
        return saw_progress_future_.wait_for(timeout) == std::future_status::ready;
      }

      void release() {
        std::call_once(release_once_, [this]() { release_.set_value(); });
      }

     private:
      std::promise<void> saw_progress_;
      std::shared_future<void> saw_progress_future_;
      std::promise<void> release_;
      std::shared_future<void> release_future_;
      std::once_flag progress_once_;
      std::once_flag release_once_;
    };

    z7::app::TestRequest request;
    request.archive_path = to_std_path(archive_path);
    auto delegate = std::make_shared<BlockingProgressDelegate>();
    auto task = start_request(request, delegate);
    QVERIFY(task.valid());
    QVERIFY(task.completion_delegate != nullptr);

    if (!delegate->wait_for_progress(std::chrono::seconds(15))) {
      const z7::app::TestResult quick = wait_for_result<z7::app::TestResult>(task);
      QVERIFY2(quick.ok,
               "test archive completed before a progress callback could be observed");
      QSKIP("test archive completed too fast to reproduce worker-thread release");
    }

    const std::shared_ptr<WaitableOutcomeDelegate> completion_delegate =
        task.completion_delegate;
    task.session = z7::app::ArchiveSession();
    delegate->release();

    const z7::app::OperationOutcome outcome = completion_delegate->await_outcome();
    const z7::app::TestResult result = result_from_outcome<z7::app::TestResult>(outcome);
    QVERIFY(result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kNone);
}

void AppLogicHashBehaviorTest::testArchiveCancelStateMachine() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("cancel-test.bin"));
    {
      QFile file(file_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QVERIFY(file.resize(1024LL * 1024LL * 1024LL));
      file.close();
    }

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("cancel-test.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

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

    std::atomic<uint64_t> completed_bytes{0};
    z7::app::TestRequest request;
    request.archive_path = to_std_path(archive_path);
    auto delegate = std::make_shared<ProgressProbeDelegate>(completed_bytes);
    auto task = start_request(request, delegate);
    QVERIFY(task.valid());

    QVERIFY(wait_until(
        [&completed_bytes, &task]() {
          return completed_bytes.load(std::memory_order_relaxed) > 0 ||
                 is_terminal_state(task.state());
        },
        10000));

    if (is_terminal_state(task.state())) {
      const z7::app::TestResult quick = wait_for_result<z7::app::TestResult>(task);
      QVERIFY(quick.ok);
      QSKIP("test finished too fast to reliably assert cancel");
    }

    task.cancel();
    const z7::app::TestResult canceled = wait_for_result<z7::app::TestResult>(task);
    if (canceled.ok) {
      QSKIP("test finished too fast to reliably assert cancel domain");
    }
    QVERIFY(!canceled.ok);
    QCOMPARE(canceled.error.domain, z7::app::ArchiveErrorDomain::kCanceled);
}

void AppLogicHashBehaviorTest::deleteArchiveUsesOriginalApiPathAndStructuredProgress() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString alpha_path = QDir(root.path()).filePath(QStringLiteral("alpha.txt"));
    {
      QFile file(alpha_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("alpha");
      file.close();
    }

    const QString beta_path = QDir(root.path()).filePath(QStringLiteral("beta.txt"));
    {
      QFile file(beta_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("beta");
      file.close();
    }

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("delete.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(alpha_path), to_std_path(beta_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    std::vector<z7::app::OperationEvent> events;

    z7::app::DeleteRequest delete_request;
    delete_request.archive_path = to_std_path(archive_path);
    delete_request.entries = {"alpha.txt"};
    const z7::app::DeleteResult delete_result = run_request_sync(delete_request, events);
    QVERIFY(delete_result.ok);
    QCOMPARE(delete_result.error.domain, z7::app::ArchiveErrorDomain::kNone);
    QCOMPARE(QString::fromStdString(delete_result.summary),
             QStringLiteral("Everything is Ok"));

    z7::app::ListRequest list_request;
    list_request.archive_path = to_std_path(archive_path);
    const z7::app::ListResult list_result = run_request_sync(list_request);
    QVERIFY(list_result.ok);

    bool has_alpha = false;
    bool has_beta = false;
    for (const z7::app::ArchiveListEntry& entry : list_result.entries) {
      if (entry.path == "alpha.txt") {
        has_alpha = true;
      }
      if (entry.path == "beta.txt") {
        has_beta = true;
      }
    }
    QVERIFY(!has_alpha);
    QVERIFY(has_beta);

    bool has_structured_progress = false;
    for (const z7::app::OperationEvent& event : events) {
      if (event.kind != z7::app::OperationEventKind::kProgress ||
          event.stage != z7::app::OperationStage::kRunning) {
        continue;
      }
      if (event.total_files > 0 && event.completed_files > 0) {
        has_structured_progress = true;
        break;
      }
    }
    QVERIFY(has_structured_progress);
  }

void AppLogicHashBehaviorTest::deleteFilesystemPathsMovesItemsToRecycleBin() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_name = QStringLiteral("delete-fs-%1.txt").arg(
        QString::number(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const QString source_path = QDir(root.path()).filePath(file_name);
    {
      QFile file(source_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("delete me");
      file.close();
    }

    std::error_code ec;
    const fs::path recycle_dir = recycle_bin_dir_for_test(ec);
    if (ec || recycle_dir.empty()) {
      QSKIP("recycle bin directory is not writable in this test environment");
    }

    const fs::path source_fs = fs::path(to_std_path(source_path));
    const fs::path recycle_target = recycle_dir / source_fs.filename();
    if (fs::exists(recycle_target, ec)) {
      fs::remove_all(recycle_target, ec);
      ec.clear();
    }

    std::vector<z7::app::OperationEvent> events;

    z7::app::DeleteRequest request;
    request.filesystem_paths = {to_std_path(source_path)};
    const z7::app::DeleteResult result = run_request_sync(request, events);
    QVERIFY(result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kNone);
    QVERIFY(!QFileInfo::exists(source_path));
    QVERIFY2(fs::exists(recycle_target, ec), "deleted file was not moved to recycle bin");

    bool has_progress = false;
    for (const z7::app::OperationEvent& event : events) {
      if (event.kind == z7::app::OperationEventKind::kProgress &&
          event.stage == z7::app::OperationStage::kRunning &&
          event.total_files > 0 && event.completed_files > 0) {
        has_progress = true;
        break;
      }
    }
    QVERIFY(has_progress);
    QVERIFY(first_stage_index(events, z7::app::OperationStage::kPrepare).has_value());
    QVERIFY(first_stage_index(events, z7::app::OperationStage::kCompleted).has_value());

    fs::remove_all(recycle_target, ec);
  }

void AppLogicHashBehaviorTest::deleteFilesystemPathsCanBypassRecycleBinWhenRequested() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_name = QStringLiteral("delete-fs-direct-%1.txt").arg(
        QString::number(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const QString source_path = QDir(root.path()).filePath(file_name);
    {
      QFile file(source_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("delete me directly");
      file.close();
    }

    std::error_code ec;
    const fs::path recycle_dir = recycle_bin_dir_for_test(ec);
    const fs::path source_fs = fs::path(to_std_path(source_path));
    const fs::path recycle_target =
        !ec && !recycle_dir.empty() ? recycle_dir / source_fs.filename() : fs::path();
    if (!recycle_target.empty() && fs::exists(recycle_target, ec)) {
      fs::remove_all(recycle_target, ec);
      ec.clear();
    }

    z7::app::DeleteRequest request;
    request.filesystem_paths = {to_std_path(source_path)};
    request.use_recycle_bin = false;
    const z7::app::DeleteResult result = run_request_sync(request);
    QVERIFY(result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kNone);
    QVERIFY(!QFileInfo::exists(source_path));
    if (!recycle_target.empty()) {
      QVERIFY2(!fs::exists(recycle_target, ec),
               "direct delete unexpectedly moved file to recycle bin");
    }
  }

void AppLogicHashBehaviorTest::copyMoveSupportSingleSourceDestinationPathRename() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString copy_src = QDir(root.path()).filePath(QStringLiteral("copy-src.txt"));
    {
      QFile file(copy_src);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("copy-data");
      file.close();
    }

    const QString move_src = QDir(root.path()).filePath(QStringLiteral("move-src.txt"));
    {
      QFile file(move_src);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("move-data");
      file.close();
    }

    const QString copy_dst = QDir(root.path()).filePath(QStringLiteral("copy-renamed.txt"));
    z7::app::CopyRequest copy_request;
    copy_request.source_paths = {to_std_path(copy_src)};
    copy_request.destination_path = to_std_path(copy_dst);
    const z7::app::CopyResult copy_result = run_request_sync(copy_request);
    QVERIFY(copy_result.ok);
    QCOMPARE(copy_result.copied_count, static_cast<size_t>(1));
    QVERIFY(QFileInfo::exists(copy_src));
    QVERIFY(QFileInfo::exists(copy_dst));
    {
      QFile file(copy_dst);
      QVERIFY(file.open(QIODevice::ReadOnly));
      QCOMPARE(file.readAll(), QByteArray("copy-data"));
    }

    const QString move_dst = QDir(root.path()).filePath(QStringLiteral("move-renamed.txt"));
    z7::app::MoveRequest move_request;
    move_request.source_paths = {to_std_path(move_src)};
    move_request.destination_path = to_std_path(move_dst);
    const z7::app::MoveResult move_result = run_request_sync(move_request);
    QVERIFY(move_result.ok);
    QCOMPARE(move_result.moved_count, static_cast<size_t>(1));
    QVERIFY(!QFileInfo::exists(move_src));
    QVERIFY(QFileInfo::exists(move_dst));
    {
      QFile file(move_dst);
      QVERIFY(file.open(QIODevice::ReadOnly));
      QCOMPARE(file.readAll(), QByteArray("move-data"));
    }
  }


// End of setup.cpp
