// tests/app_logic/hash_behavior/cases_get_entry_info.cpp
// Role: B2 – GetEntryInfoRequest / GetEntryInfoResult behavior cases.

#include "internal.h"
#include "native_archive_session_registry.h"

using namespace hash_behavior_internal;

namespace {

// Returns empty string on failure (caller should QVERIFY !result.isEmpty()).
QString build_multientry_archive(const QTemporaryDir& root) {
  const QString file1 = QDir(root.path()).filePath(QStringLiteral("hello.txt"));
  const QString docs  = QDir(root.path()).filePath(QStringLiteral("docs"));
  const QString file2 = QDir(docs).filePath(QStringLiteral("readme.md"));

  if (!QDir().mkpath(docs)) return {};
  {
    QFile f(file1);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write("hello world");
  }
  {
    QFile f(file2);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write("readme content");
  }

  const QString archive = QDir(root.path()).filePath(QStringLiteral("multi.7z"));
  z7::app::AddRequest add;
  add.archive_path = to_std_path(archive);
  add.format = "7z";
  add.input_paths = {to_std_path(file1), to_std_path(docs)};
  if (!run_request_sync(add).ok) return {};
  return archive;
}

}  // namespace

// -------------------------------------------------------------------------
// Case 1: direct-open path, single file entry.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::getEntryInfoDirectOpenSingleFile() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive = build_multientry_archive(root);
  QVERIFY(!archive.isEmpty());

  z7::app::GetEntryInfoRequest req;
  req.archive_path = to_std_path(archive);
  req.entry_path = "hello.txt";

  const z7::app::GetEntryInfoResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QVERIFY(result.exists);
  QVERIFY(!result.is_directory);
  QCOMPARE(result.size, static_cast<uint64_t>(11));  // "hello world"
  QVERIFY(!result.subtree_file_count.has_value());
}

// -------------------------------------------------------------------------
// Case 2: directory entry – subtree stats populated.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::getEntryInfoDirectoryReturnsSubtreeStats() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive = build_multientry_archive(root);
  QVERIFY(!archive.isEmpty());

  z7::app::GetEntryInfoRequest req;
  req.archive_path = to_std_path(archive);
  req.entry_path = "docs";

  const z7::app::GetEntryInfoResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QVERIFY(result.exists);
  QVERIFY(result.is_directory);
  QVERIFY(result.subtree_file_count.has_value());
  QCOMPARE(*result.subtree_file_count, static_cast<uint64_t>(1));  // readme.md
  QVERIFY(result.subtree_total_size.has_value());
  QCOMPARE(*result.subtree_total_size, static_cast<uint64_t>(14));  // "readme content"
}

// -------------------------------------------------------------------------
// Case 3: entry not in archive → exists=false, ok=true.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::getEntryInfoNotFoundReturnsFalse() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive = build_multientry_archive(root);
  QVERIFY(!archive.isEmpty());

  z7::app::GetEntryInfoRequest req;
  req.archive_path = to_std_path(archive);
  req.entry_path = "nonexistent_file.txt";

  const z7::app::GetEntryInfoResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QVERIFY(!result.exists);
}

// -------------------------------------------------------------------------
// Case 4: empty entry_path = archive root → always exists as directory.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::getEntryInfoRootPathIsDirectory() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive = build_multientry_archive(root);
  QVERIFY(!archive.isEmpty());

  z7::app::GetEntryInfoRequest req;
  req.archive_path = to_std_path(archive);
  req.entry_path = "";

  const z7::app::GetEntryInfoResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QVERIFY(result.exists);
  QVERIFY(result.is_directory);
  QVERIFY(result.subtree_file_count.has_value());
  QVERIFY(*result.subtree_file_count >= 2u);  // hello.txt + docs/readme.md
}

// -------------------------------------------------------------------------
// Case 5: session-token reuse – no second archive open.
// -------------------------------------------------------------------------
void AppLogicHashBehaviorTest::getEntryInfoSessionReuseNoReopen() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive = build_multientry_archive(root);
  QVERIFY(!archive.isEmpty());

  z7::app::OpenArchiveFromPathRequest open_req;
  open_req.archive_path = to_std_path(archive);
  const z7::app::OpenArchiveSessionResult open_result = run_request_sync(open_req);
  QVERIFY(open_result.ok);
  QVERIFY(open_result.token.is_valid());

  z7::app::GetEntryInfoRequest req;
  req.session_token = open_result.token;
  req.entry_path = "hello.txt";

  const z7::app::GetEntryInfoResult result = run_request_sync(req);
  QVERIFY(result.ok);
  QVERIFY(result.exists);
  QVERIFY(!result.is_directory);

  run_request_sync(z7::app::CloseArchiveSessionRequest{open_result.token});
  QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(0));
}
