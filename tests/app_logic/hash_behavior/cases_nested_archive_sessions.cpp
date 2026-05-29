// tests/app_logic/hash_behavior/cases_nested_archive_sessions.cpp
// Role: Nested archive session strategy and lifetime behavior cases.

#include "internal.h"

#include "native_archive_session_registry.h"

#include <algorithm>

using namespace hash_behavior_internal;

namespace {

bool create_archive_with_options(const QString& working_dir,
                                 const QString& archive_path,
                                 const QStringList& inputs,
                                 const QString& format,
                                 const QString& compression_level,
                                 QString* error) {
  if (error != nullptr) {
    error->clear();
  }

  const QString previous_cwd = QDir::currentPath();
  const bool changed = QDir::setCurrent(working_dir);
  if (!changed) {
    if (error != nullptr) {
      *error = QStringLiteral("failed to switch cwd to working dir");
    }
    return false;
  }

  z7::app::AddRequest request;
  request.archive_path = to_std_path(archive_path);
  request.format = format.trimmed().toStdString();
  if (!compression_level.trimmed().isEmpty()) {
    request.compression_level = compression_level.trimmed().toStdString();
  }
  for (const QString& input : inputs) {
    request.input_paths.push_back(input.toStdString());
  }

  const z7::app::AddResult result = run_request_sync(request);
  QDir::setCurrent(previous_cwd);
  if (!result.ok || !QFileInfo::exists(archive_path)) {
    if (error != nullptr) {
      *error = QString::fromStdString(result.summary);
      if (error->isEmpty()) {
        *error = QString::fromStdString(result.error.message);
      }
      if (error->isEmpty()) {
        *error = QStringLiteral("add() returned failure");
      }
    }
    return false;
  }
  return true;
}

struct EmbeddedZipFixture {
  QString outer_archive;
  QString child_archive_name;
};

struct EmbeddedArchiveInFolderFixture {
  QString outer_archive;
  QString child_archive_entry;
};

struct SameNameEmbeddedArchiveFixture {
  QString outer_archive;
  QString child_archive_entry;
  QString child_leaf_name;
};

struct OpenedNestedArchiveSessions {
  z7::app::OpenArchiveSessionResult parent;
  z7::app::OpenArchiveSessionResult child;
};

class SessionCloseGuard {
 public:
  ~SessionCloseGuard() {
    for (auto it = tokens_.rbegin(); it != tokens_.rend(); ++it) {
      if (it->is_valid()) {
        z7::app::ArchiveSessionRegistry::instance().close(*it);
      }
    }
  }

  void add(z7::app::ArchiveSessionToken token) {
    if (token.is_valid()) {
      tokens_.push_back(token);
    }
  }

  void release(z7::app::ArchiveSessionToken token) {
    tokens_.erase(std::remove(tokens_.begin(), tokens_.end(), token), tokens_.end());
  }

 private:
  std::vector<z7::app::ArchiveSessionToken> tokens_;
};

EmbeddedZipFixture create_embedded_zip_fixture(const QTemporaryDir& root,
                                               const QString& outer_format,
                                               const QString& outer_compression_level) {
  EmbeddedZipFixture fixture;
  fixture.child_archive_name = QStringLiteral("child.zip");

  const QString child_file_name = QStringLiteral("leaf.txt");
  const QString child_file_path = QDir(root.path()).filePath(child_file_name);
  {
    QFile file(child_file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return fixture;
    }
    file.write("nested session payload\n");
    file.close();
  }

  const QString child_archive_path = QDir(root.path()).filePath(fixture.child_archive_name);
  {
    QString error;
    if (!create_archive_with_options(root.path(),
                                     child_archive_path,
                                     QStringList{child_file_name},
                                     QStringLiteral("zip"),
                                     QString(),
                                     &error)) {
      qWarning() << "create_embedded_zip_fixture child failed:" << error;
      fixture.outer_archive.clear();
      return fixture;
    }
  }

  const QString outer_note_name = QStringLiteral("outer-note.txt");
  const QString outer_note_path = QDir(root.path()).filePath(outer_note_name);
  {
    QFile file(outer_note_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return fixture;
    }
    file.write("outer note\n");
    file.close();
  }

  fixture.outer_archive =
      QDir(root.path()).filePath(QStringLiteral("outer.%1").arg(outer_format));
  QString error;
  if (!create_archive_with_options(root.path(),
                                   fixture.outer_archive,
                                   QStringList{fixture.child_archive_name, outer_note_name},
                                   outer_format,
                                   outer_compression_level,
                                   &error)) {
    qWarning() << "create_embedded_zip_fixture outer failed:" << error;
    fixture.outer_archive.clear();
  }
  return fixture;
}

EmbeddedArchiveInFolderFixture create_embedded_archive_in_folder_fixture(
    const QTemporaryDir& root) {
  EmbeddedArchiveInFolderFixture fixture;
  fixture.child_archive_entry = QStringLiteral("pack/child2.7z");

  const QString child_file_name = QStringLiteral("child2.txt");
  const QString child_file_path = QDir(root.path()).filePath(child_file_name);
  {
    QFile file(child_file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return fixture;
    }
    file.write("embedded in folder\n");
    file.close();
  }

  const QString child_archive_name = QStringLiteral("child2.7z");
  const QString child_archive_path = QDir(root.path()).filePath(child_archive_name);
  {
    QString error;
    if (!create_archive_with_options(root.path(),
                                     child_archive_path,
                                     QStringList{child_file_name},
                                     QStringLiteral("7z"),
                                     QString(),
                                     &error)) {
      qWarning() << "create_embedded_archive_in_folder_fixture child failed:" << error;
      fixture.outer_archive.clear();
      return fixture;
    }
  }

  const QString pack_dir = QDir(root.path()).filePath(QStringLiteral("pack"));
  if (!QDir().mkpath(pack_dir)) {
    return fixture;
  }
  if (!QFile::copy(child_archive_path, QDir(pack_dir).filePath(child_archive_name))) {
    return fixture;
  }

  fixture.outer_archive =
      QDir(root.path()).filePath(QStringLiteral("outer-folder.7z"));
  QString error;
  if (!create_archive_with_options(root.path(),
                                   fixture.outer_archive,
                                   QStringList{QStringLiteral("pack")},
                                   QStringLiteral("7z"),
                                   QString(),
                                   &error)) {
    qWarning() << "create_embedded_archive_in_folder_fixture outer failed:" << error;
    fixture.outer_archive.clear();
  }
  return fixture;
}

SameNameEmbeddedArchiveFixture create_same_name_embedded_archive_fixture(
    const QTemporaryDir& root) {
  SameNameEmbeddedArchiveFixture fixture;
  const QString shared_base = QStringLiteral("apps.apple.com-main");
  fixture.child_leaf_name = QStringLiteral("same-name-leaf.txt");
  fixture.child_archive_entry =
      QStringLiteral("%1/%1.7z").arg(shared_base);

  const QString child_file_path = QDir(root.path()).filePath(fixture.child_leaf_name);
  {
    QFile file(child_file_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return fixture;
    }
    file.write("same-name nested session payload\n");
    file.close();
  }

  const QString child_archive_source =
      QDir(root.path()).filePath(QStringLiteral("child-source.7z"));
  {
    QString error;
    if (!create_archive_with_options(root.path(),
                                     child_archive_source,
                                     QStringList{fixture.child_leaf_name},
                                     QStringLiteral("7z"),
                                     QString(),
                                     &error)) {
      qWarning() << "create_same_name_embedded_archive_fixture child failed:" << error;
      fixture.outer_archive.clear();
      return fixture;
    }
  }

  const QString container_dir = QDir(root.path()).filePath(shared_base);
  if (!QDir().mkpath(container_dir)) {
    return fixture;
  }
  const QString packed_child =
      QDir(container_dir).filePath(QStringLiteral("%1.7z").arg(shared_base));
  if (!QFile::copy(child_archive_source, packed_child)) {
    return fixture;
  }

  fixture.outer_archive =
      QDir(root.path()).filePath(QStringLiteral("%1.7z").arg(shared_base));
  QString error;
  if (!create_archive_with_options(root.path(),
                                   fixture.outer_archive,
                                   QStringList{shared_base},
                                   QStringLiteral("7z"),
                                   QString(),
                                   &error)) {
    qWarning() << "create_same_name_embedded_archive_fixture outer failed:" << error;
    fixture.outer_archive.clear();
  }
  return fixture;
}

std::vector<std::string> listed_entry_paths(z7::app::ArchiveSessionToken token) {
  z7::app::ListRequest list_request;
  list_request.session_token = token;
  const z7::app::ListResult list_result = run_request_sync(list_request);
  if (!list_result.ok) {
    return {};
  }
  std::vector<std::string> paths;
  paths.reserve(list_result.entries.size());
  for (const z7::app::ArchiveListEntry& entry : list_result.entries) {
    paths.push_back(entry.path);
  }
  return paths;
}

bool contains_entry_path(const std::vector<std::string>& paths,
                        const std::string& needle) {
  return std::find(paths.begin(), paths.end(), needle) != paths.end();
}

QString extract_entry_text_from_session(z7::app::ArchiveSessionToken token,
                                        const QString& entry_name,
                                        const QTemporaryDir& output_root) {
  z7::app::ExtractRequest request;
  request.session_token = token;
  request.output_dir = to_std_path(output_root.path());
  request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
  request.entries.push_back(entry_name.toStdString());
  const z7::app::ExtractResult result = run_request_sync(request);
  if (!result.ok) {
    return QString();
  }

  QFile file(QDir(output_root.path()).filePath(entry_name));
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  return QString::fromUtf8(file.readAll());
}

OpenedNestedArchiveSessions open_nested_archive_sessions(
    const QString& outer_archive,
    const QString& child_entry_path,
    const std::string& child_type_hint,
    SessionCloseGuard* guard) {
  OpenedNestedArchiveSessions sessions;

  z7::app::OpenArchiveFromPathRequest open_parent_request;
  open_parent_request.archive_path = to_std_path(outer_archive);
  sessions.parent = run_request_sync(open_parent_request);
  if (!sessions.parent.ok || !sessions.parent.token.is_valid()) {
    return sessions;
  }
  if (guard != nullptr) {
    guard->add(sessions.parent.token);
  }

  z7::app::OpenArchiveFromParentRequest open_child_request;
  open_child_request.parent = sessions.parent.token;
  open_child_request.entry_path = child_entry_path.toStdString();
  open_child_request.archive_type_hint = child_type_hint;
  sessions.child = run_request_sync(open_child_request);
  if (!sessions.child.ok || !sessions.child.token.is_valid()) {
    return sessions;
  }
  if (guard != nullptr) {
    guard->add(sessions.child.token);
  }

  return sessions;
}

}  // namespace

void AppLogicHashBehaviorTest::nestedOpenChoosesExpectedStrategiesForEmbeddedZipArchives() {
    QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(0));

    {
      QTemporaryDir root;
      QVERIFY2(root.isValid(), "failed to create temp dir");

      const EmbeddedZipFixture fixture = create_embedded_zip_fixture(
          root,
          QStringLiteral("tar"),
          QString());
      QVERIFY2(!fixture.outer_archive.isEmpty(), "failed to create outer tar fixture");

      SessionCloseGuard guard;

      z7::app::OpenArchiveFromPathRequest open_parent_request;
      open_parent_request.archive_path = to_std_path(fixture.outer_archive);
      open_parent_request.archive_type_hint = "tar";
      const z7::app::OpenArchiveSessionResult parent_result = run_request_sync(open_parent_request);
      QVERIFY(parent_result.ok);
      QVERIFY(parent_result.token.is_valid());
      guard.add(parent_result.token);

      z7::app::OpenArchiveFromParentRequest open_child_request;
      open_child_request.parent = parent_result.token;
      open_child_request.entry_path = fixture.child_archive_name.toStdString();
      open_child_request.archive_type_hint = "zip";
      const z7::app::OpenArchiveSessionResult child_result = run_request_sync(open_child_request);
      QVERIFY(child_result.ok);
      QVERIFY(child_result.token.is_valid());
      guard.add(child_result.token);
      QCOMPARE(child_result.used_strategy,
               z7::app::OpenArchiveSessionResult::Strategy::kStream);

      const z7::app::OperationResult close_child =
          run_request_sync(z7::app::CloseArchiveSessionRequest{child_result.token});
      QVERIFY(close_child.ok);
      guard.release(child_result.token);
      const z7::app::OperationResult close_parent =
          run_request_sync(z7::app::CloseArchiveSessionRequest{parent_result.token});
      QVERIFY(close_parent.ok);
      guard.release(parent_result.token);
    }

    QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(0));

    {
      QTemporaryDir root;
      QVERIFY2(root.isValid(), "failed to create temp dir");

      const EmbeddedZipFixture fixture = create_embedded_zip_fixture(
          root,
          QStringLiteral("7z"),
          QString());
      QVERIFY2(!fixture.outer_archive.isEmpty(), "failed to create outer 7z fixture");

      SessionCloseGuard guard;

      z7::app::OpenArchiveFromPathRequest open_parent_request;
      open_parent_request.archive_path = to_std_path(fixture.outer_archive);
      const z7::app::OpenArchiveSessionResult parent_result = run_request_sync(open_parent_request);
      QVERIFY(parent_result.ok);
      QVERIFY(parent_result.token.is_valid());
      guard.add(parent_result.token);

      z7::app::OpenArchiveFromParentRequest open_child_request;
      open_child_request.parent = parent_result.token;
      open_child_request.entry_path = fixture.child_archive_name.toStdString();
      open_child_request.archive_type_hint = "zip";
      const z7::app::OpenArchiveSessionResult child_result = run_request_sync(open_child_request);
      QVERIFY(child_result.ok);
      QVERIFY(child_result.token.is_valid());
      guard.add(child_result.token);
      QCOMPARE(child_result.used_strategy,
               z7::app::OpenArchiveSessionResult::Strategy::kMemory);

      const z7::app::OperationResult close_child =
          run_request_sync(z7::app::CloseArchiveSessionRequest{child_result.token});
      QVERIFY(close_child.ok);
      guard.release(child_result.token);
      const z7::app::OperationResult close_parent =
          run_request_sync(z7::app::CloseArchiveSessionRequest{parent_result.token});
      QVERIFY(close_parent.ok);
      guard.release(parent_result.token);
    }

    QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(0));
}

void AppLogicHashBehaviorTest::nestedOpenSupportsSameNameEmbeddedArchiveEntries() {
    QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(0));

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const SameNameEmbeddedArchiveFixture fixture =
        create_same_name_embedded_archive_fixture(root);
    QVERIFY2(!fixture.outer_archive.isEmpty(),
             "failed to create same-name embedded archive fixture");

    SessionCloseGuard guard;

    z7::app::OpenArchiveFromPathRequest open_parent_request;
    open_parent_request.archive_path = to_std_path(fixture.outer_archive);
    const z7::app::OpenArchiveSessionResult parent_result = run_request_sync(open_parent_request);
    QVERIFY(parent_result.ok);
    QVERIFY(parent_result.token.is_valid());
    guard.add(parent_result.token);

    z7::app::OpenArchiveFromParentRequest open_child_request;
    open_child_request.parent = parent_result.token;
    open_child_request.entry_path = fixture.child_archive_entry.toStdString();
    open_child_request.archive_type_hint = "7z";
    const z7::app::OpenArchiveSessionResult child_result = run_request_sync(open_child_request);
    QVERIFY(child_result.ok);
    QVERIFY(child_result.token.is_valid());
    guard.add(child_result.token);

    const std::vector<std::string> child_entries = listed_entry_paths(child_result.token);
    QVERIFY(contains_entry_path(child_entries,
                                fixture.child_leaf_name.toStdString()));
}

void AppLogicHashBehaviorTest::nestedArchiveSessionsSupportParentCloseBeforeChildClose() {
    QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(0));

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const EmbeddedZipFixture fixture = create_embedded_zip_fixture(
        root,
        QStringLiteral("7z"),
        QString());
    QVERIFY2(!fixture.outer_archive.isEmpty(), "failed to create embedded zip fixture");

    SessionCloseGuard guard;

    z7::app::OpenArchiveFromPathRequest open_parent_request;
    open_parent_request.archive_path = to_std_path(fixture.outer_archive);
    const z7::app::OpenArchiveSessionResult parent_result = run_request_sync(open_parent_request);
    QVERIFY(parent_result.ok);
    QVERIFY(parent_result.token.is_valid());
    guard.add(parent_result.token);

    z7::app::OpenArchiveFromParentRequest open_child_request;
    open_child_request.parent = parent_result.token;
    open_child_request.entry_path = fixture.child_archive_name.toStdString();
    open_child_request.archive_type_hint = "zip";
    const z7::app::OpenArchiveSessionResult child_result = run_request_sync(open_child_request);
    QVERIFY(child_result.ok);
    QVERIFY(child_result.token.is_valid());
    guard.add(child_result.token);

    QCOMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(2));

    const z7::app::OperationResult close_parent =
        run_request_sync(z7::app::CloseArchiveSessionRequest{parent_result.token});
    QVERIFY(close_parent.ok);
    guard.release(parent_result.token);
    QCOMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(1));

    const std::vector<std::string> child_entries = listed_entry_paths(child_result.token);
    QVERIFY(!child_entries.empty());
    QVERIFY(contains_entry_path(child_entries, std::string("leaf.txt")));

    const z7::app::OperationResult close_child =
        run_request_sync(z7::app::CloseArchiveSessionRequest{child_result.token});
    QVERIFY(close_child.ok);
    guard.release(child_result.token);

    QTRY_COMPARE(z7::app::ArchiveSessionRegistry::instance().session_count(), size_t(0));
}

void AppLogicHashBehaviorTest::nestedDeleteWritebackRemovesEmbeddedArchiveEntry() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const EmbeddedZipFixture fixture = create_embedded_zip_fixture(
        root,
        QStringLiteral("7z"),
        QString());
    QVERIFY2(!fixture.outer_archive.isEmpty(), "failed to create embedded zip fixture");

    SessionCloseGuard guard;
    const OpenedNestedArchiveSessions initial_sessions = open_nested_archive_sessions(
        fixture.outer_archive,
        fixture.child_archive_name,
        "zip",
        &guard);
    QVERIFY(initial_sessions.parent.ok);
    QVERIFY(initial_sessions.child.ok);

    z7::app::DeleteRequest delete_request;
    delete_request.session_token = initial_sessions.child.token;
    delete_request.entries.push_back(std::string("leaf.txt"));
    const z7::app::DeleteResult delete_result = run_request_sync(delete_request);
    QVERIFY(delete_result.ok);

    const z7::app::OperationResult close_child =
        run_request_sync(z7::app::CloseArchiveSessionRequest{initial_sessions.child.token});
    QVERIFY(close_child.ok);
    guard.release(initial_sessions.child.token);

    const z7::app::OpenArchiveSessionResult reopened_child = run_request_sync(
        z7::app::OpenArchiveFromParentRequest{
            initial_sessions.parent.token,
            std::nullopt,
            fixture.child_archive_name.toStdString(),
            "zip",
            0,
            {}});
    QVERIFY(reopened_child.ok);
    QVERIFY(reopened_child.token.is_valid());
    guard.add(reopened_child.token);

    const std::vector<std::string> child_entries = listed_entry_paths(reopened_child.token);
    QVERIFY(!contains_entry_path(child_entries, std::string("leaf.txt")));

    const z7::app::OperationResult close_reopened_child =
        run_request_sync(z7::app::CloseArchiveSessionRequest{reopened_child.token});
    QVERIFY(close_reopened_child.ok);
    guard.release(reopened_child.token);

    const z7::app::OperationResult close_parent =
        run_request_sync(z7::app::CloseArchiveSessionRequest{initial_sessions.parent.token});
    QVERIFY(close_parent.ok);
    guard.release(initial_sessions.parent.token);

    SessionCloseGuard persisted_guard;
    const OpenedNestedArchiveSessions persisted_sessions = open_nested_archive_sessions(
        fixture.outer_archive,
        fixture.child_archive_name,
        "zip",
        &persisted_guard);
    QVERIFY(persisted_sessions.parent.ok);
    QVERIFY(persisted_sessions.child.ok);

    const std::vector<std::string> persisted_entries =
        listed_entry_paths(persisted_sessions.child.token);
    QVERIFY(!contains_entry_path(persisted_entries, std::string("leaf.txt")));
}

void AppLogicHashBehaviorTest::nestedUpdateWritebackReplacesEmbeddedArchiveContent() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const EmbeddedZipFixture fixture = create_embedded_zip_fixture(
        root,
        QStringLiteral("7z"),
        QString());
    QVERIFY2(!fixture.outer_archive.isEmpty(), "failed to create embedded zip fixture");

    const QString replacement_path =
        QDir(root.path()).filePath(QStringLiteral("leaf.txt"));
    {
      QFile file(replacement_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("updated nested payload\n");
      file.close();
    }

    SessionCloseGuard guard;
    const OpenedNestedArchiveSessions initial_sessions = open_nested_archive_sessions(
        fixture.outer_archive,
        fixture.child_archive_name,
        "zip",
        &guard);
    QVERIFY(initial_sessions.parent.ok);
    QVERIFY(initial_sessions.child.ok);

    z7::app::AddRequest update_request;
    update_request.session_token = initial_sessions.child.token;
    update_request.update_mode = "update";
    update_request.input_paths.push_back(to_std_path(replacement_path));
    const z7::app::AddResult update_result = run_request_sync(update_request);
    QVERIFY(update_result.ok);

    const z7::app::OperationResult close_child =
        run_request_sync(z7::app::CloseArchiveSessionRequest{initial_sessions.child.token});
    QVERIFY(close_child.ok);
    guard.release(initial_sessions.child.token);

    const z7::app::OpenArchiveSessionResult reopened_child = run_request_sync(
        z7::app::OpenArchiveFromParentRequest{
            initial_sessions.parent.token,
            std::nullopt,
            fixture.child_archive_name.toStdString(),
            "zip",
            0,
            {}});
    QVERIFY(reopened_child.ok);
    QVERIFY(reopened_child.token.is_valid());
    guard.add(reopened_child.token);

    QTemporaryDir output_root;
    QVERIFY2(output_root.isValid(), "failed to create extraction verification dir");
    const QString extracted_text = extract_entry_text_from_session(
        reopened_child.token,
        QStringLiteral("leaf.txt"),
        output_root);
    QCOMPARE(extracted_text, QStringLiteral("updated nested payload\n"));

    const z7::app::OperationResult close_reopened_child =
        run_request_sync(z7::app::CloseArchiveSessionRequest{reopened_child.token});
    QVERIFY(close_reopened_child.ok);
    guard.release(reopened_child.token);

    const z7::app::OperationResult close_parent =
        run_request_sync(z7::app::CloseArchiveSessionRequest{initial_sessions.parent.token});
    QVERIFY(close_parent.ok);
    guard.release(initial_sessions.parent.token);

    SessionCloseGuard persisted_guard;
    const OpenedNestedArchiveSessions persisted_sessions = open_nested_archive_sessions(
        fixture.outer_archive,
        fixture.child_archive_name,
        "zip",
        &persisted_guard);
    QVERIFY(persisted_sessions.parent.ok);
    QVERIFY(persisted_sessions.child.ok);

    QTemporaryDir persisted_output_root;
    QVERIFY2(persisted_output_root.isValid(),
             "failed to create persisted extraction verification dir");
    const QString persisted_text = extract_entry_text_from_session(
        persisted_sessions.child.token,
        QStringLiteral("leaf.txt"),
        persisted_output_root);
    QCOMPARE(persisted_text, QStringLiteral("updated nested payload\n"));
}

void AppLogicHashBehaviorTest::nestedDeleteWritebackSupportsEmbeddedArchivePathsWithDirectories() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const EmbeddedArchiveInFolderFixture fixture =
        create_embedded_archive_in_folder_fixture(root);
    QVERIFY2(!fixture.outer_archive.isEmpty(),
             "failed to create embedded archive-in-folder fixture");

    SessionCloseGuard guard;
    const OpenedNestedArchiveSessions initial_sessions = open_nested_archive_sessions(
        fixture.outer_archive,
        fixture.child_archive_entry,
        "7z",
        &guard);
    QVERIFY(initial_sessions.parent.ok);
    QVERIFY(initial_sessions.child.ok);

    z7::app::DeleteRequest delete_request;
    delete_request.session_token = initial_sessions.child.token;
    delete_request.entries.push_back(std::string("child2.txt"));
    std::vector<z7::app::OperationEvent> events;
    const z7::app::DeleteResult delete_result = run_request_sync(delete_request, events);
    QStringList event_messages;
    for (const z7::app::OperationEvent& event : events) {
      if (!event.message.empty()) {
        event_messages << QString::fromStdString(event.message);
      }
    }
    QVERIFY2(delete_result.ok,
             qPrintable(QString::fromStdString(delete_result.summary) +
                        QStringLiteral("\nEvents:\n") +
                        event_messages.join(QLatin1Char('\n'))));

    const z7::app::OperationResult close_child =
        run_request_sync(z7::app::CloseArchiveSessionRequest{initial_sessions.child.token});
    QVERIFY(close_child.ok);
    guard.release(initial_sessions.child.token);

    const z7::app::OpenArchiveSessionResult reopened_child = run_request_sync(
        z7::app::OpenArchiveFromParentRequest{
            initial_sessions.parent.token,
            std::nullopt,
            fixture.child_archive_entry.toStdString(),
            "7z",
            0,
            {}});
    QVERIFY(reopened_child.ok);
    QVERIFY(reopened_child.token.is_valid());
    guard.add(reopened_child.token);

    const std::vector<std::string> child_entries = listed_entry_paths(reopened_child.token);
    QVERIFY(!contains_entry_path(child_entries, std::string("child2.txt")));

    const z7::app::OperationResult close_reopened_child =
        run_request_sync(z7::app::CloseArchiveSessionRequest{reopened_child.token});
    QVERIFY(close_reopened_child.ok);
    guard.release(reopened_child.token);

    const z7::app::OperationResult close_parent =
        run_request_sync(z7::app::CloseArchiveSessionRequest{initial_sessions.parent.token});
    QVERIFY(close_parent.ok);
    guard.release(initial_sessions.parent.token);

    SessionCloseGuard persisted_guard;
    const OpenedNestedArchiveSessions persisted_sessions = open_nested_archive_sessions(
        fixture.outer_archive,
        fixture.child_archive_entry,
        "7z",
        &persisted_guard);
    QVERIFY(persisted_sessions.parent.ok);
    QVERIFY(persisted_sessions.child.ok);

    const std::vector<std::string> persisted_entries =
        listed_entry_paths(persisted_sessions.child.token);
    QVERIFY(!contains_entry_path(persisted_entries, std::string("child2.txt")));
}
