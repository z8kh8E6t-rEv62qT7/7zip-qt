// tests/app_logic/hash_behavior/cases_fileop_models.cpp
// Role: Extended request models and copy/move/delete domain mapping cases.

#include "internal.h"

#include "core/filesystem_replace.h"
#include "descript_ion_store.h"

using namespace hash_behavior_internal;

namespace {

void write_text_file(const QString& path, const QByteArray& contents) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(file.write(contents), contents.size());
}

QByteArray read_text_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  return file.readAll();
}

}  // namespace

void AppLogicHashBehaviorTest::extractTestCopyMoveDeleteRequestsCoverExtendedModels() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_a = QDir(root.path()).filePath(QStringLiteral("a.txt"));
    const QString file_b = QDir(root.path()).filePath(QStringLiteral("b.txt"));
    {
      QFile file(file_a);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("a");
    }
    {
      QFile file(file_b);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("b");
    }

    const QString archive_a = QDir(root.path()).filePath(QStringLiteral("a.7z"));
    const QString archive_b = QDir(root.path()).filePath(QStringLiteral("b.7z"));
    const QString archive_ab = QDir(root.path()).filePath(QStringLiteral("ab.7z"));
    {
      z7::app::AddRequest add;
      add.archive_path = to_std_path(archive_a);
      add.format = "7z";
      add.input_paths = {to_std_path(file_a)};
      QVERIFY(run_request_sync(add).ok);
    }
    {
      z7::app::AddRequest add;
      add.archive_path = to_std_path(archive_b);
      add.format = "7z";
      add.input_paths = {to_std_path(file_b)};
      QVERIFY(run_request_sync(add).ok);
    }
    {
      z7::app::AddRequest add;
      add.archive_path = to_std_path(archive_ab);
      add.format = "7z";
      add.input_paths = {to_std_path(file_a), to_std_path(file_b)};
      QVERIFY(run_request_sync(add).ok);
    }

    const QString extract_dir = QDir(root.path()).filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(extract_dir));
    z7::app::ExtractRequest extract_request;
    extract_request.archive_paths = {to_std_path(archive_a), to_std_path(archive_b)};
    extract_request.output_dir = to_std_path(extract_dir);
    extract_request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
    const z7::app::ExtractResult extract_result = run_request_sync(extract_request);
    QVERIFY(extract_result.ok);
    QVERIFY(QFileInfo::exists(QDir(extract_dir).filePath(QStringLiteral("a.txt"))));
    QVERIFY(QFileInfo::exists(QDir(extract_dir).filePath(QStringLiteral("b.txt"))));

    z7::app::TestRequest test_request;
    test_request.archive_paths = {to_std_path(archive_a), to_std_path(archive_b)};
    const z7::app::TestResult test_result = run_request_sync(test_request);
    QVERIFY(test_result.ok);
    QVERIFY(test_result.hash_summary.has_value());
    const z7::app::HashSummary& test_summary = *test_result.hash_summary;
    QCOMPARE(test_summary.num_archives, static_cast<uint64_t>(2));
    QCOMPARE(test_summary.num_dirs, static_cast<uint64_t>(0));
    QCOMPARE(test_summary.num_files, static_cast<uint64_t>(2));
    QCOMPARE(test_summary.files_size, static_cast<uint64_t>(2));
    QVERIFY(test_summary.physical_size_defined);
    QVERIFY(test_summary.physical_size > 0);

    z7::app::TestRequest entry_test_request;
    entry_test_request.archive_path = to_std_path(archive_ab);
    entry_test_request.entries = {"a.txt"};
    std::vector<z7::app::OperationEvent> entry_test_events;
    const z7::app::TestResult entry_test_result =
        run_request_sync(entry_test_request, entry_test_events);
    QVERIFY(entry_test_result.ok);
    QVERIFY(entry_test_result.hash_summary.has_value());
    const z7::app::HashSummary& entry_test_summary =
        *entry_test_result.hash_summary;
    QCOMPARE(entry_test_summary.num_dirs, static_cast<uint64_t>(0));
    QCOMPARE(entry_test_summary.num_files, static_cast<uint64_t>(1));
    QCOMPARE(entry_test_summary.files_size, static_cast<uint64_t>(1));
    QVERIFY(!entry_test_summary.first_file_name.empty() ||
            !entry_test_summary.main_name.empty());
    bool saw_single_file_log = false;
    bool saw_single_file_progress = false;
    for (const z7::app::OperationEvent& event : entry_test_events) {
      if (event.kind == z7::app::OperationEventKind::kLog &&
          event.message.find("Files: 1") != std::string::npos) {
        saw_single_file_log = true;
      }
      if (event.kind == z7::app::OperationEventKind::kProgress &&
          event.stage == z7::app::OperationStage::kRunning &&
          event.total_files == 1) {
        saw_single_file_progress = true;
      }
    }
    QVERIFY(saw_single_file_log);
    QVERIFY(saw_single_file_progress);

    const QString renamed_copy = QDir(root.path()).filePath(QStringLiteral("copied-name.txt"));
    z7::app::CopyRequest copy_request;
    copy_request.source_paths = {to_std_path(file_a)};
    copy_request.destination_path = to_std_path(renamed_copy);
    copy_request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
    const z7::app::CopyResult copy_result = run_request_sync(copy_request);
    QVERIFY(copy_result.ok);
    QCOMPARE(copy_result.copied_count, static_cast<size_t>(1));
    QVERIFY(QFileInfo::exists(renamed_copy));

    const QString moved_path = QDir(root.path()).filePath(QStringLiteral("moved-name.txt"));
    z7::app::MoveRequest move_request;
    move_request.source_paths = {to_std_path(renamed_copy)};
    move_request.destination_path = to_std_path(moved_path);
    move_request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
    const z7::app::MoveResult move_result = run_request_sync(move_request);
    QVERIFY(move_result.ok);
    QCOMPARE(move_result.moved_count, static_cast<size_t>(1));
    QVERIFY(!QFileInfo::exists(renamed_copy));
    QVERIFY(QFileInfo::exists(moved_path));

    std::vector<z7::app::OperationEvent> delete_events;
    z7::app::DeleteRequest delete_request;
    delete_request.filesystem_paths = {to_std_path(moved_path)};
    delete_request.use_recycle_bin = false;
    const z7::app::DeleteResult delete_result = run_request_sync(delete_request, delete_events);
    QVERIFY(delete_result.ok);
    QVERIFY(!QFileInfo::exists(moved_path));
    QVERIFY(first_stage_index(delete_events, z7::app::OperationStage::kPrepare).has_value());
    QVERIFY(first_stage_index(delete_events, z7::app::OperationStage::kCompleted).has_value());
}

void AppLogicHashBehaviorTest::commentRequestsCoverArchiveAndFilesystemModels() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_name = QStringLiteral("item.txt");
    const QString file_path = QDir(root.path()).filePath(file_name);
    write_text_file(file_path, QByteArrayLiteral("body"));

    const QString archive_path =
        QDir(root.path()).filePath(QStringLiteral("commented.7z"));
    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(file_path)};
    QVERIFY(run_request_sync(add_request).ok);

    z7::app::ArchiveCommentRequest invalid_archive_comment;
    invalid_archive_comment.archive_path = to_std_path(archive_path);
    invalid_archive_comment.entry_path = "missing.txt";
    invalid_archive_comment.comment = "archive-side comment";
    const z7::app::ArchiveCommentResult invalid_archive_comment_result =
        run_request_sync(invalid_archive_comment);
    QVERIFY(!invalid_archive_comment_result.ok);
    QCOMPARE(invalid_archive_comment_result.error.domain,
             z7::app::ArchiveErrorDomain::kInvalidArguments);

    z7::app::ArchiveCommentRequest archive_comment;
    archive_comment.archive_path = to_std_path(archive_path);
    archive_comment.entry_path = file_name.toStdString();
    archive_comment.comment = "archive-side comment";
    const z7::app::ArchiveCommentResult archive_comment_result =
        run_request_sync(archive_comment);
    QVERIFY(archive_comment_result.ok);

    z7::app::FilesystemCommentRequest filesystem_comment;
    filesystem_comment.directory_path = to_std_path(root.path());
    filesystem_comment.entry_name = file_name.toStdString();
    filesystem_comment.comment = "filesystem-side comment";
    const z7::app::FilesystemCommentResult filesystem_comment_result =
        run_request_sync(filesystem_comment);
    QVERIFY(filesystem_comment_result.ok);

    z7::app::DescriptIonDocument document;
    QVERIFY(z7::app::load_descript_ion_document(to_std_path(root.path()), &document));
    const std::optional<std::string> stored_comment =
        z7::app::read_descript_ion_comment_for_display(
            document, file_name.toStdString());
    QVERIFY(stored_comment.has_value());
    QCOMPARE(QString::fromStdString(*stored_comment),
             QStringLiteral("filesystem-side comment"));
}

void AppLogicHashBehaviorTest::atomicReplaceHelperRestoresOriginalAndPreservesTempOnPromoteFailure() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString original_path =
        QDir(root.path()).filePath(QStringLiteral("comment.7z"));
    const QString temp_path =
        QDir(root.path()).filePath(QStringLiteral("comment.tmp"));
    write_text_file(original_path, QByteArrayLiteral("original-archive"));
    write_text_file(temp_path, QByteArrayLiteral("updated-archive"));

    const QString backup_path =
        QDir(root.path()).filePath(QStringLiteral("comment.7z.backup"));
    bool failed_promote = false;
    z7::app::AtomicReplaceFileOps ops;
    ops.exists = [](const std::filesystem::path& path, std::error_code& ec) {
      return std::filesystem::exists(path, ec);
    };
    ops.rename =
        [&failed_promote, &temp_path, &original_path](
            const std::filesystem::path& from,
            const std::filesystem::path& to,
            std::error_code& ec) {
          if (!failed_promote &&
              QString::fromStdString(from.string()) == temp_path &&
              QString::fromStdString(to.string()) == original_path) {
            failed_promote = true;
            ec = std::make_error_code(std::errc::permission_denied);
            return;
          }
          std::filesystem::rename(from, to, ec);
        };
    ops.remove = [](const std::filesystem::path& path, std::error_code& ec) {
      std::filesystem::remove(path, ec);
    };
    ops.make_unique_sibling_path =
        [&backup_path](const std::filesystem::path&, std::string_view) {
          return std::filesystem::path(to_std_path(backup_path));
        };

    const z7::app::AtomicReplaceResult result = z7::app::replace_file_atomically(
        std::filesystem::path(to_std_path(temp_path)),
        std::filesystem::path(to_std_path(original_path)),
        ".unused",
        &ops);
    QVERIFY(!result.success);
    QVERIFY(result.original_restored);
    QVERIFY(result.source_exists);
    QVERIFY(result.error.has_value());
    QVERIFY(QString::fromStdString(result.error->summary).contains(temp_path));

    QCOMPARE(read_text_file(original_path), QByteArrayLiteral("original-archive"));
    QCOMPARE(read_text_file(temp_path), QByteArrayLiteral("updated-archive"));
}

void AppLogicHashBehaviorTest::atomicReplaceHelperFailsWhenBackupPathProbeFails() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString original_path =
        QDir(root.path()).filePath(QStringLiteral("comment.7z"));
    const QString temp_path =
        QDir(root.path()).filePath(QStringLiteral("comment.tmp"));
    write_text_file(original_path, QByteArrayLiteral("original-archive"));
    write_text_file(temp_path, QByteArrayLiteral("updated-archive"));

    z7::app::AtomicReplaceFileOps ops;
    ops.exists =
        [&original_path, &temp_path](
            const std::filesystem::path& path,
            std::error_code& ec) {
          const QString probed = QString::fromStdString(path.string());
          if (probed == original_path || probed == temp_path) {
            ec.clear();
            return true;
          }
          ec = std::make_error_code(std::errc::permission_denied);
          return false;
        };
    ops.rename = [](const std::filesystem::path&,
                    const std::filesystem::path&,
                    std::error_code& ec) {
      ec = std::make_error_code(std::errc::operation_not_permitted);
    };

    const z7::app::AtomicReplaceResult result = z7::app::replace_file_atomically(
        std::filesystem::path(to_std_path(temp_path)),
        std::filesystem::path(to_std_path(original_path)),
        ".probe-error-",
        &ops);
    QVERIFY(!result.success);
    QVERIFY(result.error.has_value());
    QVERIFY(QString::fromStdString(result.error->summary)
                .contains(QStringLiteral("permission"), Qt::CaseInsensitive));
    QCOMPARE(read_text_file(original_path), QByteArrayLiteral("original-archive"));
    QCOMPARE(read_text_file(temp_path), QByteArrayLiteral("updated-archive"));
}

void AppLogicHashBehaviorTest::addRequestUpdateModeAppliesOriginalActionSetSemantics() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_name = QStringLiteral("item.txt");
    const QString source_path = QDir(root.path()).filePath(file_name);
    write_text_file(source_path, QByteArray("archive-new"));

    const QString archive_update = QDir(root.path()).filePath(QStringLiteral("update.7z"));
    const QString archive_add = QDir(root.path()).filePath(QStringLiteral("add.7z"));
    for (const QString& archive_path : {archive_update, archive_add}) {
      z7::app::AddRequest add_request;
      add_request.archive_path = to_std_path(archive_path);
      add_request.format = "7z";
      add_request.input_paths = {to_std_path(source_path)};
      QVERIFY2(run_request_sync(add_request).ok, "initial add failed");
    }

    write_text_file(source_path, QByteArray("disk-old"));
    QFile source_file(source_path);
    QVERIFY(source_file.open(QIODevice::ReadWrite));
    QVERIFY(source_file.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2),
                                    QFileDevice::FileModificationTime));
    source_file.close();

    z7::app::AddRequest update_request;
    update_request.archive_path = to_std_path(archive_update);
    update_request.format = "7z";
    update_request.update_mode = "update";
    update_request.input_paths = {to_std_path(source_path)};
    const z7::app::AddResult update_result = run_request_sync(update_request);
    QVERIFY(update_result.ok);

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_add);
    add_request.format = "7z";
    add_request.update_mode = "add";
    add_request.input_paths = {to_std_path(source_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    const auto extract_file_text = [&root, &file_name](const QString& archive_path,
                                                       const QString& out_dir_name)
                                       -> QByteArray {
      const QString output_dir = QDir(root.path()).filePath(out_dir_name);
      if (!QDir().mkpath(output_dir)) {
        return QByteArray();
      }
      z7::app::ExtractRequest extract_request;
      extract_request.archive_path = to_std_path(archive_path);
      extract_request.output_dir = to_std_path(output_dir);
      extract_request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
      const z7::app::ExtractResult extract_result = run_request_sync(extract_request);
      if (!extract_result.ok) {
        return QByteArray();
      }
      return read_text_file(QDir(output_dir).filePath(file_name));
    };

    QCOMPARE(extract_file_text(archive_update, QStringLiteral("out-update")),
             QByteArray("archive-new"));
    QCOMPARE(extract_file_text(archive_add, QStringLiteral("out-add")),
             QByteArray("disk-old"));
}

void AppLogicHashBehaviorTest::copyMoveDeleteInvalidRequestsAndCancelPathsMapCorrectDomains() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_a = QDir(root.path()).filePath(QStringLiteral("a.txt"));
    const QString file_b = QDir(root.path()).filePath(QStringLiteral("b.txt"));
    {
      QFile file(file_a);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("a");
    }
    {
      QFile file(file_b);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("b");
    }

    z7::app::CopyRequest invalid_copy;
    const z7::app::CopyResult invalid_copy_result = run_request_sync(invalid_copy);
    QVERIFY(!invalid_copy_result.ok);
    QCOMPARE(invalid_copy_result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);

    z7::app::MoveRequest invalid_move;
    invalid_move.source_paths = {to_std_path(file_a), to_std_path(file_b)};
    invalid_move.destination_path = to_std_path(QDir(root.path()).filePath(QStringLiteral("x.txt")));
    const z7::app::MoveResult invalid_move_result = run_request_sync(invalid_move);
    QVERIFY(!invalid_move_result.ok);
    QCOMPARE(invalid_move_result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);

    z7::app::DeleteRequest invalid_delete;
    const z7::app::DeleteResult invalid_delete_result = run_request_sync(invalid_delete);
    QVERIFY(!invalid_delete_result.ok);
    QCOMPARE(invalid_delete_result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);

    QStringList many_sources;
    for (int i = 0; i < 48; ++i) {
      const QString path = QDir(root.path()).filePath(QStringLiteral("c_%1.txt").arg(i));
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("c");
      many_sources.push_back(path);
    }
    const QString copy_out_dir = QDir(root.path()).filePath(QStringLiteral("copy_out"));
    QVERIFY(QDir().mkpath(copy_out_dir));

    z7::app::CopyRequest cancel_copy_request;
    for (const QString& path : many_sources) {
      cancel_copy_request.source_paths.push_back(to_std_path(path));
    }
    cancel_copy_request.destination_dir = to_std_path(copy_out_dir);

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

    std::atomic<uint64_t> cancel_progress{0};
    auto delegate = std::make_shared<ProgressProbeDelegate>(cancel_progress);
    auto task = start_request(cancel_copy_request, delegate);
    QVERIFY(task.valid());

    QVERIFY(wait_until(
        [&cancel_progress, &task]() {
          return cancel_progress.load(std::memory_order_relaxed) > 0 ||
                 is_terminal_state(task.state());
        },
        5000));

    if (!is_terminal_state(task.state())) {
      task.cancel();
    }
    const z7::app::CopyResult canceled_copy = wait_for_result<z7::app::CopyResult>(task);
    if (canceled_copy.ok) {
      QSKIP("copy finished too fast to reliably assert cancel domain");
    }
    QVERIFY(!canceled_copy.ok);
    QCOMPARE(canceled_copy.error.domain, z7::app::ArchiveErrorDomain::kCanceled);
}
