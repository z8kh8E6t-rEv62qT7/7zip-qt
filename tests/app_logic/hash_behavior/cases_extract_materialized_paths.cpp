// tests/app_logic/hash_behavior/cases_extract_materialized_paths.cpp
// Role: B1 – ExtractResult.materialized_entries / primary_output_path cases.

#include "internal.h"

using namespace hash_behavior_internal;

namespace {

void write_file(const QString& path, const QByteArray& contents) {
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(contents);
}

// Returns empty string on failure (caller should QVERIFY !result.isEmpty()).
QString build_single_file_archive(const QTemporaryDir& root,
                                  const QString& filename,
                                  const QByteArray& contents,
                                  const QString& archive_name,
                                  const std::optional<std::string>& password =
                                      std::nullopt) {
  const QString file_path = QDir(root.path()).filePath(filename);
  {
    QFile f(file_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(contents);
  }
  const QString archive_path = QDir(root.path()).filePath(archive_name);
  z7::app::AddRequest add;
  add.archive_path = to_std_path(archive_path);
  add.format = "7z";
  add.input_paths = {to_std_path(file_path)};
  if (password.has_value()) {
    add.password = *password;
  }
  std::vector<z7::app::OperationEvent> events;
  const z7::app::AddResult add_result = run_request_sync(add, events);
  if (!add_result.ok) {
    qWarning() << "build_single_file_archive failed:"
               << QString::fromStdString(add_result.summary)
               << QString::fromStdString(add_result.error.message);
    for (const z7::app::OperationEvent& event : events) {
      if (!event.message.empty()) {
        qWarning() << "build_single_file_archive event:"
                   << QString::fromStdString(event.message);
      }
    }
    return {};
  }
  return archive_path;
}

QByteArray read_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

QStringList temp_or_backup_entries(const QString& dir_path) {
  return QDir(dir_path).entryList(
      QStringList{QStringLiteral("*.partial-*"),
                  QStringLiteral("*.z7-extract-backup-*"),
                  QStringLiteral("*.z7-existing-*")},
      QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
}

}  // namespace

// -------------------------------------------------------------------------
// Case 1: single-file extract – basic materialized_entries / primary path.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::extractMaterializedEntriesSingleFile() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  // Build archive with one file.
  const QByteArray payload = QByteArrayLiteral("hello-b1");
  const QString archive_path =
      build_single_file_archive(root, QStringLiteral("payload.txt"),
                                payload, QStringLiteral("single.7z"));
  QVERIFY(!archive_path.isEmpty());

  const QString out_dir = QDir(root.path()).filePath(QStringLiteral("out"));
  QVERIFY(QDir().mkpath(out_dir));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive_path);
  req.output_dir = to_std_path(out_dir);

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(result.ok);

  QCOMPARE(static_cast<int>(result.materialized_entries.size()), 1);
  const auto& entry = result.materialized_entries[0];
  QCOMPARE(QString::fromStdString(entry.archive_entry_path),
           QStringLiteral("payload.txt"));
  QVERIFY(QFileInfo::exists(QString::fromStdString(entry.absolute_output_path)));
  QVERIFY(!entry.is_directory);
  QCOMPARE(entry.bytes_written, static_cast<uint64_t>(payload.size()));
  QVERIFY(!entry.overwrote_existing);
  QVERIFY(!entry.renamed_from_collision);

  // primary_output_path: entries has exactly 1 item (empty = full extract).
  // With entries empty the request covers the whole archive; primary stays empty
  // because there is no single logical entry to report.
  // For a single-entry request we need entries = {"payload.txt"}.
}

// -------------------------------------------------------------------------
// Case 2: single-entry request → primary_output_path is set.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::extractMaterializedEntriesPrimaryPath() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QByteArray payload = QByteArrayLiteral("primary-path-test");
  const QString archive_path =
      build_single_file_archive(root, QStringLiteral("readme.txt"),
                                payload, QStringLiteral("arch.7z"));
  QVERIFY(!archive_path.isEmpty());

  const QString out_dir = QDir(root.path()).filePath(QStringLiteral("out"));
  QVERIFY(QDir().mkpath(out_dir));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive_path);
  req.output_dir = to_std_path(out_dir);
  req.entries = {"readme.txt"};

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QVERIFY(!result.materialized_entries.empty());

  // primary_output_path must point to the extracted file.
  QVERIFY(!result.primary_output_path.empty());
  QVERIFY(QFileInfo::exists(QString::fromStdString(result.primary_output_path)));
  QVERIFY(!result.primary_is_directory);
}

// -------------------------------------------------------------------------
// Case 3: OverwriteMode::kRenameExtracted → renamed_from_collision == true.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::extractMaterializedEntriesRenameCollision() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path =
      build_single_file_archive(root, QStringLiteral("data.txt"),
                                QByteArrayLiteral("v1"), QStringLiteral("arch.7z"));
  QVERIFY(!archive_path.isEmpty());

  const QString out_dir = QDir(root.path()).filePath(QStringLiteral("out"));
  QVERIFY(QDir().mkpath(out_dir));
  // Pre-create the destination so a rename is forced.
  write_file(QDir(out_dir).filePath(QStringLiteral("data.txt")),
             QByteArrayLiteral("original"));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive_path);
  req.output_dir = to_std_path(out_dir);
  req.overwrite_mode = z7::app::OverwriteMode::kRenameExtracted;

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QVERIFY(!result.materialized_entries.empty());

  const auto& entry = result.materialized_entries[0];
  QVERIFY(entry.renamed_from_collision);
  QVERIFY(!entry.overwrote_existing);
  // The renamed file actually exists.
  QVERIFY(QFileInfo::exists(QString::fromStdString(entry.absolute_output_path)));
  // Original is untouched.
  QVERIFY(QFileInfo::exists(QDir(out_dir).filePath(QStringLiteral("data.txt"))));
}

// -------------------------------------------------------------------------
// Case 4: OverwriteMode::kSkip – skipped entries absent from materialized list.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::extractMaterializedEntriesSkipNotRecorded() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  // Archive with two files: skip.txt and keep.txt.
  const QString file_skip = QDir(root.path()).filePath(QStringLiteral("skip.txt"));
  const QString file_keep = QDir(root.path()).filePath(QStringLiteral("keep.txt"));
  write_file(file_skip, QByteArrayLiteral("skip-content"));
  write_file(file_keep, QByteArrayLiteral("keep-content"));

  const QString archive_path =
      QDir(root.path()).filePath(QStringLiteral("two.7z"));
  z7::app::AddRequest add;
  add.archive_path = to_std_path(archive_path);
  add.format = "7z";
  add.input_paths = {to_std_path(file_skip), to_std_path(file_keep)};
  QVERIFY(run_request_sync(add).ok);

  const QString out_dir = QDir(root.path()).filePath(QStringLiteral("out"));
  QVERIFY(QDir().mkpath(out_dir));
  // Pre-create skip.txt so it gets skipped.
  write_file(QDir(out_dir).filePath(QStringLiteral("skip.txt")),
             QByteArrayLiteral("existing"));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive_path);
  req.output_dir = to_std_path(out_dir);
  req.overwrite_mode = z7::app::OverwriteMode::kSkip;

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(result.ok);

  // Only keep.txt should appear.
  QCOMPARE(static_cast<int>(result.materialized_entries.size()), 1);
  QCOMPARE(QString::fromStdString(result.materialized_entries[0].archive_entry_path),
           QStringLiteral("keep.txt"));
}

// -------------------------------------------------------------------------
// Case 5: multi-archive extract → materialized_entries merged; primary empty.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::extractMaterializedEntriesMultiArchiveMerged() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString arch_a =
      build_single_file_archive(root, QStringLiteral("a.txt"),
                                QByteArrayLiteral("aaa"), QStringLiteral("a.7z"));
  QVERIFY(!arch_a.isEmpty());
  const QString arch_b =
      build_single_file_archive(root, QStringLiteral("b.txt"),
                                QByteArrayLiteral("bbb"), QStringLiteral("b.7z"));
  QVERIFY(!arch_b.isEmpty());

  const QString out_dir = QDir(root.path()).filePath(QStringLiteral("out"));
  QVERIFY(QDir().mkpath(out_dir));

  z7::app::ExtractRequest req;
  req.archive_paths = {to_std_path(arch_a), to_std_path(arch_b)};
  req.output_dir = to_std_path(out_dir);

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QCOMPARE(static_cast<int>(result.materialized_entries.size()), 2);
  // primary must be empty for multi-archive requests.
  QVERIFY(result.primary_output_path.empty());
}

// -------------------------------------------------------------------------
// Case 6: OverwriteMode::kOverwrite → overwrote_existing == true.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::extractMaterializedEntriesOverwriteFlag() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path =
      build_single_file_archive(root, QStringLiteral("file.txt"),
                                QByteArrayLiteral("new-content"),
                                QStringLiteral("arch.7z"));
  QVERIFY(!archive_path.isEmpty());

  const QString out_dir = QDir(root.path()).filePath(QStringLiteral("out"));
  QVERIFY(QDir().mkpath(out_dir));
  write_file(QDir(out_dir).filePath(QStringLiteral("file.txt")),
             QByteArrayLiteral("old-content"));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive_path);
  req.output_dir = to_std_path(out_dir);
  req.overwrite_mode = z7::app::OverwriteMode::kOverwrite;

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QVERIFY(!result.materialized_entries.empty());
  QVERIFY(result.materialized_entries[0].overwrote_existing);
  QVERIFY(!result.materialized_entries[0].renamed_from_collision);
}

void AppLogicHashBehaviorTest::extractMaterializedEntriesOverwriteLeavesNoTempResidue() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path =
      build_single_file_archive(root, QStringLiteral("file.txt"),
                                QByteArrayLiteral("new-content"),
                                QStringLiteral("overwrite-clean.7z"));
  QVERIFY(!archive_path.isEmpty());

  const QString out_dir = QDir(root.path()).filePath(QStringLiteral("out_clean"));
  QVERIFY(QDir().mkpath(out_dir));
  const QString existing_path = QDir(out_dir).filePath(QStringLiteral("file.txt"));
  write_file(existing_path, QByteArrayLiteral("old-content"));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive_path);
  req.output_dir = to_std_path(out_dir);
  req.overwrite_mode = z7::app::OverwriteMode::kOverwrite;

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QCOMPARE(read_file(existing_path), QByteArrayLiteral("new-content"));
  QVERIFY(temp_or_backup_entries(out_dir).isEmpty());
}

void AppLogicHashBehaviorTest::extractMaterializedEntriesPathRemapsDirectWrite() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString report_path =
      QDir(root.path()).filePath(QStringLiteral("report.txt"));
  write_file(report_path, QByteArrayLiteral("report-body"));

  const QString docs_dir = QDir(root.path()).filePath(QStringLiteral("docs"));
  QVERIFY(QDir().mkpath(docs_dir));
  const QString readme_path = QDir(docs_dir).filePath(QStringLiteral("readme.md"));
  write_file(readme_path, QByteArrayLiteral("docs-body"));

  const QString archive_path =
      QDir(root.path()).filePath(QStringLiteral("remaps.7z"));
  z7::app::AddRequest add;
  add.archive_path = to_std_path(archive_path);
  add.format = "7z";
  add.input_items.push_back(
      z7::app::AddInputItem{to_std_path(report_path), "report.txt"});
  add.input_items.push_back(
      z7::app::AddInputItem{to_std_path(docs_dir), "docs"});
  QVERIFY(run_request_sync(add).ok);

  const QString exact_destination =
      QDir(root.path()).filePath(QStringLiteral("out/exact/final-report.txt"));
  z7::app::ExtractRequest exact_request;
  exact_request.archive_path = to_std_path(archive_path);
  exact_request.output_dir =
      to_std_path(QFileInfo(exact_destination).absolutePath());
  exact_request.entries = {"report.txt"};
  exact_request.path_remaps.push_back(
      z7::app::ExtractPathRemap{
          z7::app::ExtractPathRemapMatchKind::kExactArchivePath,
          "report.txt",
          to_std_path(exact_destination)});

  const z7::app::ExtractResult exact_result = run_request_sync(exact_request);
  QVERIFY(exact_result.ok);
  QCOMPARE(QString::fromStdString(exact_result.primary_output_path),
           QFileInfo(exact_destination).absoluteFilePath());
  QVERIFY(!exact_result.primary_is_directory);
  QCOMPARE(static_cast<int>(exact_result.materialized_entries.size()), 1);
  QCOMPARE(QString::fromStdString(exact_result.materialized_entries[0].absolute_output_path),
           QFileInfo(exact_destination).absoluteFilePath());
  QFile exact_file(exact_destination);
  QVERIFY(exact_file.open(QIODevice::ReadOnly));
  QCOMPARE(exact_file.readAll(), QByteArrayLiteral("report-body"));

  const QString prefix_destination =
      QDir(root.path()).filePath(QStringLiteral("out/prefix/docs-export"));
  z7::app::ExtractRequest prefix_request;
  prefix_request.archive_path = to_std_path(archive_path);
  prefix_request.output_dir =
      to_std_path(QFileInfo(prefix_destination).absolutePath());
  prefix_request.entries = {"docs"};
  prefix_request.path_remaps.push_back(
      z7::app::ExtractPathRemap{
          z7::app::ExtractPathRemapMatchKind::kArchivePrefix,
          "docs",
          to_std_path(prefix_destination)});

  const z7::app::ExtractResult prefix_result = run_request_sync(prefix_request);
  QVERIFY(prefix_result.ok);
  QCOMPARE(QString::fromStdString(prefix_result.primary_output_path),
           QFileInfo(prefix_destination).absoluteFilePath());
  QVERIFY(prefix_result.primary_is_directory);
  QVERIFY(QFileInfo(prefix_destination).isDir());
  QFile prefix_file(QDir(prefix_destination).filePath(QStringLiteral("readme.md")));
  QVERIFY(prefix_file.open(QIODevice::ReadOnly));
  QCOMPARE(prefix_file.readAll(), QByteArrayLiteral("docs-body"));

  const QString root_destination =
      QDir(root.path()).filePath(QStringLiteral("out/root/archive-root"));
  z7::app::ExtractRequest root_request;
  root_request.archive_path = to_std_path(archive_path);
  root_request.output_dir =
      to_std_path(QFileInfo(root_destination).absolutePath());
  root_request.path_remaps.push_back(
      z7::app::ExtractPathRemap{
          z7::app::ExtractPathRemapMatchKind::kRequestRoot,
          {},
          to_std_path(root_destination)});

  const z7::app::ExtractResult root_result = run_request_sync(root_request);
  QVERIFY(root_result.ok);
  QCOMPARE(QString::fromStdString(root_result.primary_output_path),
           QFileInfo(root_destination).absoluteFilePath());
  QVERIFY(root_result.primary_is_directory);
  QFile root_report(QDir(root_destination).filePath(QStringLiteral("report.txt")));
  QVERIFY(root_report.open(QIODevice::ReadOnly));
  QCOMPARE(root_report.readAll(), QByteArrayLiteral("report-body"));
  QFile root_readme(QDir(root_destination).filePath(QStringLiteral("docs/readme.md")));
  QVERIFY(root_readme.open(QIODevice::ReadOnly));
  QCOMPARE(root_readme.readAll(), QByteArrayLiteral("docs-body"));
}

void AppLogicHashBehaviorTest::extractMaterializedEntriesRollbackRestoresOverwrittenFile() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path =
      build_single_file_archive(root, QStringLiteral("file.txt"),
                                QByteArrayLiteral("new-content"),
                                QStringLiteral("arch.7z"));
  QVERIFY(!archive_path.isEmpty());

  const QString out_dir = QDir(root.path()).filePath(QStringLiteral("out"));
  QVERIFY(QDir().mkpath(out_dir));
  const QString existing_path = QDir(out_dir).filePath(QStringLiteral("file.txt"));
  write_file(existing_path, QByteArrayLiteral("old-content"));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive_path);
  req.output_dir = to_std_path(out_dir);
  req.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
  z7::app::ExtractBudget budget;
  budget.max_bytes = 1;
  budget.on_exceeded = z7::app::BudgetExceededAction::kFailAndRollback;
  req.budget = budget;

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(!result.ok);
  QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kBudgetExceeded);

  QFile existing(existing_path);
  QVERIFY(existing.open(QIODevice::ReadOnly));
  QCOMPARE(existing.readAll(), QByteArrayLiteral("old-content"));
}

void AppLogicHashBehaviorTest::extractMaterializedEntriesWrongPasswordNewFileLeavesNoResidue() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path =
      build_single_file_archive(root,
                                QStringLiteral("secret.txt"),
                                QByteArrayLiteral("classified"),
                                QStringLiteral("secret.7z"),
                                std::string("test-password"));
  QVERIFY(!archive_path.isEmpty());

  const QString out_dir = QDir(root.path()).filePath(QStringLiteral("out_wrong_new"));
  QVERIFY(QDir().mkpath(out_dir));
  const QString output_path = QDir(out_dir).filePath(QStringLiteral("secret.txt"));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive_path);
  req.output_dir = to_std_path(out_dir);
  DelegateInteraction interaction;
  interaction.request_password = [](const z7::app::PasswordPrompt&) {
    z7::app::PasswordReply reply;
    reply.kind = z7::app::PasswordReplyKind::kProvide;
    reply.password = "wrong-password";
    return reply;
  };

  const z7::app::ExtractResult result = run_request_sync(req, interaction);
  QVERIFY(!result.ok);
  QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kPassword);
  QVERIFY(!QFileInfo::exists(output_path));
  QVERIFY(temp_or_backup_entries(out_dir).isEmpty());
}

void AppLogicHashBehaviorTest::extractMaterializedEntriesWrongPasswordExistingFileRestoresOriginal() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path =
      build_single_file_archive(root,
                                QStringLiteral("file.txt"),
                                QByteArrayLiteral("new-content"),
                                QStringLiteral("wrong-existing.7z"),
                                std::string("test-password"));
  QVERIFY(!archive_path.isEmpty());

  const QString out_dir =
      QDir(root.path()).filePath(QStringLiteral("out_wrong_existing"));
  QVERIFY(QDir().mkpath(out_dir));
  const QString existing_path = QDir(out_dir).filePath(QStringLiteral("file.txt"));
  write_file(existing_path, QByteArrayLiteral("old-content"));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive_path);
  req.output_dir = to_std_path(out_dir);
  req.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
  DelegateInteraction interaction;
  interaction.request_password = [](const z7::app::PasswordPrompt&) {
    z7::app::PasswordReply reply;
    reply.kind = z7::app::PasswordReplyKind::kProvide;
    reply.password = "wrong-password";
    return reply;
  };

  const z7::app::ExtractResult result = run_request_sync(req, interaction);
  QVERIFY(!result.ok);
  QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kPassword);
  QCOMPARE(read_file(existing_path), QByteArrayLiteral("old-content"));
  QVERIFY(temp_or_backup_entries(out_dir).isEmpty());
}

void AppLogicHashBehaviorTest::extractMaterializedEntriesRenameExistingSuccessKeepsReadableBackup() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path =
      build_single_file_archive(root, QStringLiteral("file.txt"),
                                QByteArrayLiteral("new-content"),
                                QStringLiteral("rename-existing-success.7z"));
  QVERIFY(!archive_path.isEmpty());

  const QString out_dir =
      QDir(root.path()).filePath(QStringLiteral("out_rename_existing"));
  QVERIFY(QDir().mkpath(out_dir));
  const QString existing_path = QDir(out_dir).filePath(QStringLiteral("file.txt"));
  const QString renamed_existing_path =
      QDir(out_dir).filePath(QStringLiteral("file_1.txt"));
  write_file(existing_path, QByteArrayLiteral("old-content"));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive_path);
  req.output_dir = to_std_path(out_dir);
  req.overwrite_mode = z7::app::OverwriteMode::kRenameExisting;

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QCOMPARE(read_file(existing_path), QByteArrayLiteral("new-content"));
  QCOMPARE(read_file(renamed_existing_path), QByteArrayLiteral("old-content"));
  QVERIFY(QDir(out_dir)
              .entryList(QStringList{QStringLiteral("*.partial-*"),
                                     QStringLiteral("*.z7-extract-backup-*")},
                         QDir::Files | QDir::NoDotAndDotDot)
              .isEmpty());
}

void AppLogicHashBehaviorTest::extractMaterializedEntriesRenameExistingFailureRestoresOriginal() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path =
      build_single_file_archive(root,
                                QStringLiteral("file.txt"),
                                QByteArrayLiteral("new-content"),
                                QStringLiteral("rename-existing-failure.7z"),
                                std::string("test-password"));
  QVERIFY(!archive_path.isEmpty());

  const QString out_dir =
      QDir(root.path()).filePath(QStringLiteral("out_rename_existing_fail"));
  QVERIFY(QDir().mkpath(out_dir));
  const QString existing_path = QDir(out_dir).filePath(QStringLiteral("file.txt"));
  const QString renamed_existing_path =
      QDir(out_dir).filePath(QStringLiteral("file_1.txt"));
  write_file(existing_path, QByteArrayLiteral("old-content"));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive_path);
  req.output_dir = to_std_path(out_dir);
  req.overwrite_mode = z7::app::OverwriteMode::kRenameExisting;
  DelegateInteraction interaction;
  interaction.request_password = [](const z7::app::PasswordPrompt&) {
    z7::app::PasswordReply reply;
    reply.kind = z7::app::PasswordReplyKind::kProvide;
    reply.password = "wrong-password";
    return reply;
  };

  const z7::app::ExtractResult result = run_request_sync(req, interaction);
  QVERIFY(!result.ok);
  QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kPassword);
  QCOMPARE(read_file(existing_path), QByteArrayLiteral("old-content"));
  QVERIFY(!QFileInfo::exists(renamed_existing_path));
  QVERIFY(temp_or_backup_entries(out_dir).isEmpty());
}
