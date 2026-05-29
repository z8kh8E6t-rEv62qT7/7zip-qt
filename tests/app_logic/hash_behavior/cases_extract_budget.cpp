// tests/app_logic/hash_behavior/cases_extract_budget.cpp
// Role: B3 – ExtractBudget early-stop and rollback behavior cases.

#include "internal.h"

using namespace hash_behavior_internal;

namespace {

// Build a 7z archive containing `n` small files named file_0.txt … file_N-1.txt.
// Returns "" on failure.
QString build_nfile_archive(const QTemporaryDir& root,
                            int n,
                            const QString& archive_name) {
  // Write source files into a subdirectory so they don't clutter root.
  const QString src_dir = QDir(root.path()).filePath(QStringLiteral("src"));
  if (!QDir().mkpath(src_dir)) return {};

  for (int i = 0; i < n; ++i) {
    const QString path =
        QDir(src_dir).filePath(QStringLiteral("file_%1.txt").arg(i));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(QByteArrayLiteral("content"));  // 7 bytes each
  }

  const QString archive = QDir(root.path()).filePath(archive_name);
  z7::app::AddRequest add;
  add.archive_path = to_std_path(archive);
  add.format = "7z";
  add.input_paths = {to_std_path(src_dir)};
  if (!run_request_sync(add).ok) return {};
  return archive;
}

// Count regular files (non-directory) in a directory tree.
int count_files(const QString& dir_path) {
  const QDir dir(dir_path);
  if (!dir.exists()) return 0;
  const QFileInfoList entries =
      dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Dirs);
  int count = 0;
  for (const QFileInfo& fi : entries) {
    if (fi.isFile()) {
      ++count;
    } else if (fi.isDir()) {
      count += count_files(fi.absoluteFilePath());
    }
  }
  return count;
}

QByteArray read_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

void write_file(const QString& path, const QByteArray& contents) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(contents);
}

}  // namespace

// -------------------------------------------------------------------------
// Case 1: max_files + kFailAndRollback → output dir empty after.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::extractBudgetMaxFilesRollback() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive = build_nfile_archive(root, 5, QStringLiteral("five.7z"));
  QVERIFY(!archive.isEmpty());

  const QString out_dir =
      QDir(root.path()).filePath(QStringLiteral("out_rollback"));
  QVERIFY(QDir().mkpath(out_dir));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive);
  req.output_dir = to_std_path(out_dir);
  z7::app::ExtractBudget budget;
  budget.max_files = 2;
  budget.on_exceeded = z7::app::BudgetExceededAction::kFailAndRollback;
  req.budget = budget;

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(!result.ok);
  QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kBudgetExceeded);
  QVERIFY(result.materialized_entries.empty());
  // All files should have been rolled back.
  QCOMPARE(count_files(out_dir), 0);
}

void AppLogicHashBehaviorTest::extractBudgetRollbackRestoresOverwrittenFile() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive = build_nfile_archive(root, 5, QStringLiteral("five.7z"));
  QVERIFY(!archive.isEmpty());

  const QString out_dir =
      QDir(root.path()).filePath(QStringLiteral("out_restore_existing"));
  QVERIFY(QDir().mkpath(out_dir));
  const QString existing_path =
      QDir(out_dir).filePath(QStringLiteral("src/file_0.txt"));
  QVERIFY(QDir().mkpath(QFileInfo(existing_path).absolutePath()));
  write_file(existing_path, QByteArrayLiteral("original-existing-content"));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive);
  req.output_dir = to_std_path(out_dir);
  req.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
  z7::app::ExtractBudget budget;
  budget.max_files = 2;
  budget.on_exceeded = z7::app::BudgetExceededAction::kFailAndRollback;
  req.budget = budget;

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(!result.ok);
  QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kBudgetExceeded);
  QCOMPARE(read_file(existing_path), QByteArrayLiteral("original-existing-content"));
}

// -------------------------------------------------------------------------
// Case 2: max_files + kFailAndKeepPartial → some files remain.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::extractBudgetMaxFilesKeepPartial() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive = build_nfile_archive(root, 5, QStringLiteral("five.7z"));
  QVERIFY(!archive.isEmpty());

  const QString out_dir =
      QDir(root.path()).filePath(QStringLiteral("out_partial"));
  QVERIFY(QDir().mkpath(out_dir));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive);
  req.output_dir = to_std_path(out_dir);
  z7::app::ExtractBudget budget;
  budget.max_files = 2;
  budget.on_exceeded = z7::app::BudgetExceededAction::kFailAndKeepPartial;
  req.budget = budget;

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(!result.ok);
  QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kBudgetExceeded);
  // materialized_entries contains up to 2 files (the ones successfully written).
  QVERIFY(!result.materialized_entries.empty());
  // Files that were successfully extracted are still on disk.
  QVERIFY(count_files(out_dir) > 0);
}

// -------------------------------------------------------------------------
// Case 3: max_files + kTruncate → ok=true, partial kept.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::extractBudgetMaxFilesTruncate() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive = build_nfile_archive(root, 5, QStringLiteral("five.7z"));
  QVERIFY(!archive.isEmpty());

  const QString out_dir =
      QDir(root.path()).filePath(QStringLiteral("out_truncate"));
  QVERIFY(QDir().mkpath(out_dir));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive);
  req.output_dir = to_std_path(out_dir);
  z7::app::ExtractBudget budget;
  budget.max_files = 2;
  budget.on_exceeded = z7::app::BudgetExceededAction::kTruncate;
  req.budget = budget;

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QVERIFY(!result.materialized_entries.empty());
  QVERIFY(count_files(out_dir) > 0);
}

// -------------------------------------------------------------------------
// Case 4: generous limit well above entry count → no trigger.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::extractBudgetExactLimitNoTrigger() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive = build_nfile_archive(root, 3, QStringLiteral("three.7z"));
  QVERIFY(!archive.isEmpty());

  const QString out_dir =
      QDir(root.path()).filePath(QStringLiteral("out_exact"));
  QVERIFY(QDir().mkpath(out_dir));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive);
  req.output_dir = to_std_path(out_dir);
  z7::app::ExtractBudget budget;
  budget.max_files = 1000;  // far above any realistic count
  budget.on_exceeded = z7::app::BudgetExceededAction::kFailAndRollback;
  req.budget = budget;

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QVERIFY(!result.materialized_entries.empty());
}

// -------------------------------------------------------------------------
// Case 5: no budget (std::nullopt) → behaviour identical to old semantics.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::extractBudgetNulloptNoBehaviourChange() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive = build_nfile_archive(root, 3, QStringLiteral("three.7z"));
  QVERIFY(!archive.isEmpty());

  const QString out_dir =
      QDir(root.path()).filePath(QStringLiteral("out_nobudget"));
  QVERIFY(QDir().mkpath(out_dir));

  z7::app::ExtractRequest req;
  req.archive_path = to_std_path(archive);
  req.output_dir = to_std_path(out_dir);
  // budget = std::nullopt (default)

  const z7::app::ExtractResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kNone);
  QVERIFY(!result.materialized_entries.empty());
}
