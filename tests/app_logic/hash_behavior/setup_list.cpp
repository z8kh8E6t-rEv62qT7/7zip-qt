// tests/app_logic/hash_behavior/setup_list.cpp
// Role: List and archive metadata behavior cases.

#include "internal.h"

using namespace hash_behavior_internal;

namespace {

class ActivityProbeDelegate final : public z7::app::IArchiveDelegate {
 public:
  explicit ActivityProbeDelegate(std::atomic<uint64_t>& activity)
      : activity_(activity) {}

  void on_lifecycle(z7::app::OperationStage stage, std::string_view) override {
    if (stage == z7::app::OperationStage::kRunning) {
      activity_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  void on_progress(const z7::app::ProgressSnapshot&) override {
    activity_.fetch_add(1, std::memory_order_relaxed);
  }

 private:
  std::atomic<uint64_t>& activity_;
};

}  // namespace

void AppLogicHashBehaviorTest::listReturnsTypedEntriesFromOriginalApi() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("alpha.txt"));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("alpha-content");
    file.close();

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("sample.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(file_path)};

    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    z7::app::ListRequest list_request;
    list_request.archive_path = to_std_path(archive_path);
    const z7::app::ListResult list_result = run_request_sync(list_request);
    QVERIFY(list_result.ok);
    QVERIFY(!list_result.entries.empty());

    bool has_named_file_entry = false;
    for (const z7::app::ArchiveListEntry& entry : list_result.entries) {
      QVERIFY(!entry.path.empty());
      if (entry.is_dir) {
        QCOMPARE(entry.size, static_cast<uint64_t>(0));
      }
      if (entry.path.find("alpha.txt") != std::string::npos) {
        has_named_file_entry = true;
        QVERIFY(!entry.is_dir);
        QCOMPARE(entry.size, static_cast<uint64_t>(13));
      }
    }
    QVERIFY(has_named_file_entry);
  }

void AppLogicHashBehaviorTest::addRequestZipPasswordWithDisabledEncryptedHeadersSucceeds() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("alpha.txt"));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("alpha-content");
    file.close();

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("sample.zip"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "zip";
    add_request.input_paths = {to_std_path(file_path)};
    add_request.password = "test-password";
    add_request.encryption_method = "AES-256";
    add_request.encrypt_headers_defined = true;
    add_request.encrypt_headers = false;

    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY2(add_result.ok, add_result.summary.c_str());

    z7::app::ListRequest list_request;
    list_request.archive_path = to_std_path(archive_path);
    const z7::app::ListResult list_result = run_request_sync(list_request);
    QVERIFY2(list_result.ok, list_result.summary.c_str());

    bool has_named_file_entry = false;
    for (const z7::app::ArchiveListEntry& entry : list_result.entries) {
      if (entry.path.find("alpha.txt") != std::string::npos) {
        has_named_file_entry = true;
        break;
      }
    }
    QVERIFY(has_named_file_entry);
  }

void AppLogicHashBehaviorTest::addRequest7zEncryptedHeadersRequirePasswordOnOpen() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString alpha_path = QDir(root.path()).filePath(QStringLiteral("alpha.txt"));
    {
      QFile file(alpha_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("alpha-content");
      file.close();
    }
    const QString beta_path = QDir(root.path()).filePath(QStringLiteral("beta.txt"));
    {
      QFile file(beta_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("beta-content");
      file.close();
    }

    const QString archive_path =
        QDir(root.path()).filePath(QStringLiteral("encrypted-headers.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(alpha_path), to_std_path(beta_path)};
    add_request.password = "test-password";
    add_request.encrypt_headers_defined = true;
    add_request.encrypt_headers = true;

    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY2(add_result.ok, add_result.summary.c_str());

    z7::app::OpenArchiveFromPathRequest open_request;
    open_request.archive_path = to_std_path(archive_path);

    int cancel_prompt_count = 0;
    DelegateInteraction cancel_interaction;
    cancel_interaction.request_password =
        [&cancel_prompt_count](const z7::app::PasswordPrompt&) {
          ++cancel_prompt_count;
          z7::app::PasswordReply reply;
          reply.kind = z7::app::PasswordReplyKind::kCancel;
          return reply;
        };
    const z7::app::OpenArchiveSessionResult canceled_open =
        run_request_sync(open_request, cancel_interaction);
    QVERIFY(!canceled_open.ok);
    QCOMPARE(canceled_open.error.domain, z7::app::ArchiveErrorDomain::kPassword);
    QVERIFY(cancel_prompt_count > 0);

    int provide_prompt_count = 0;
    DelegateInteraction provide_interaction;
    provide_interaction.request_password =
        [&provide_prompt_count](const z7::app::PasswordPrompt&) {
          ++provide_prompt_count;
          z7::app::PasswordReply reply;
          reply.kind = z7::app::PasswordReplyKind::kProvide;
          reply.password = "test-password";
          return reply;
        };
    const z7::app::OpenArchiveSessionResult opened =
        run_request_sync(open_request, provide_interaction);
    QVERIFY2(opened.ok, opened.summary.c_str());
    QVERIFY(opened.token.is_valid());
    QVERIFY(provide_prompt_count > 0);

    z7::app::ListRequest list_request;
    list_request.session_token = opened.token;
    const z7::app::ListResult list_result = run_request_sync(list_request);
    QVERIFY2(list_result.ok, list_result.summary.c_str());

    bool has_alpha = false;
    bool has_beta = false;
    for (const z7::app::ArchiveListEntry& entry : list_result.entries) {
      has_alpha = has_alpha || entry.path.find("alpha.txt") != std::string::npos;
      has_beta = has_beta || entry.path.find("beta.txt") != std::string::npos;
    }
    QVERIFY(has_alpha);
    QVERIFY(has_beta);

    const z7::app::OperationResult close_result =
        run_request_sync(z7::app::CloseArchiveSessionRequest{opened.token});
    QVERIFY(close_result.ok);
  }

void AppLogicHashBehaviorTest::listUsesFileManagerRootViewSemantics() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString nested_root = QDir(root.path()).filePath(QStringLiteral("top"));
    const QString nested_dir = QDir(nested_root).filePath(QStringLiteral("inner"));
    QVERIFY(QDir().mkpath(nested_dir));

    const QString nested_file = QDir(nested_dir).filePath(QStringLiteral("leaf.txt"));
    QFile file(nested_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("leaf-content");
    file.close();

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("nested.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(nested_root)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    z7::app::ListRequest list_request;
    list_request.archive_path = to_std_path(archive_path);
    const z7::app::ListResult list_result = run_request_sync(list_request);
    QVERIFY(list_result.ok);
    QVERIFY(!list_result.entries.empty());

    bool has_top_dir = false;
    bool has_nested_full_path = false;
    for (const z7::app::ArchiveListEntry& entry : list_result.entries) {
      if (entry.path == "top") {
        has_top_dir = true;
        QVERIFY(entry.is_dir);
      }
      if (entry.path.find('/') != std::string::npos ||
          entry.path.find('\\') != std::string::npos) {
        has_nested_full_path = true;
      }
    }
    QVERIFY(has_top_dir);
    QVERIFY(!has_nested_full_path);
  }

void AppLogicHashBehaviorTest::listSupportsVirtualDirectoryTraversal() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString top_dir = QDir(root.path()).filePath(QStringLiteral("top"));
    const QString inner_dir = QDir(top_dir).filePath(QStringLiteral("inner"));
    QVERIFY(QDir().mkpath(inner_dir));

    const QString leaf_file = QDir(inner_dir).filePath(QStringLiteral("leaf.txt"));
    QFile file(leaf_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("leaf");
    file.close();

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("tree.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(top_dir)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    z7::app::ListRequest root_list;
    root_list.archive_path = to_std_path(archive_path);
    const z7::app::ListResult root_result = run_request_sync(root_list);
    QVERIFY(root_result.ok);

    bool has_top = false;
    for (const z7::app::ArchiveListEntry& entry : root_result.entries) {
      if (entry.path == "top" && entry.is_dir) {
        has_top = true;
        break;
      }
    }
    QVERIFY(has_top);

    z7::app::ListRequest top_list;
    top_list.archive_path = to_std_path(archive_path);
    top_list.directory = "top";
    const z7::app::ListResult top_result = run_request_sync(top_list);
    QVERIFY(top_result.ok);
    QVERIFY(!top_result.entries.empty());

    bool has_inner = false;
    for (const z7::app::ArchiveListEntry& entry : top_result.entries) {
      if (entry.path == "inner" && entry.is_dir) {
        has_inner = true;
        break;
      }
    }
    QVERIFY(has_inner);

    z7::app::ListRequest inner_list;
    inner_list.archive_path = to_std_path(archive_path);
    inner_list.directory = "top/inner";
    const z7::app::ListResult inner_result = run_request_sync(inner_list);
    QVERIFY(inner_result.ok);
    QVERIFY(!inner_result.entries.empty());

    bool has_leaf = false;
    for (const z7::app::ArchiveListEntry& entry : inner_result.entries) {
      if (entry.path == "leaf.txt" && !entry.is_dir) {
        has_leaf = true;
        break;
      }
    }
    QVERIFY(has_leaf);
  }

void AppLogicHashBehaviorTest::listCancelStateMachineWithHugeDirectoryFilter() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("list-cancel.txt"));
    QFile file(source_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("list-cancel");
    file.close();

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("list-cancel.7z"));
    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(source_file)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    std::string huge_directory_filter;
    huge_directory_filter.reserve(2 * 1000000);
    for (size_t i = 0; i < 1000000; ++i) {
      huge_directory_filter.push_back('x');
      if (i + 1 != 1000000) {
        huge_directory_filter.push_back('/');
      }
    }

    z7::app::ListRequest list_request;
    list_request.archive_path = to_std_path(archive_path);
    list_request.directory = huge_directory_filter;

    std::atomic<uint64_t> activity{0};
    auto delegate = std::make_shared<ActivityProbeDelegate>(activity);
    auto task = start_request(list_request, delegate);
    QVERIFY(task.valid());

    QElapsedTimer timer;
    timer.start();
    while (!is_terminal_state(task.state()) && timer.elapsed() < 4000) {
      task.cancel();
      QTest::qWait(1);
    }

    const z7::app::ListResult result = wait_for_result<z7::app::ListResult>(task);
    QVERIFY(!result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kCanceled);
}

void AppLogicHashBehaviorTest::addRequestDirectoryTargetsArchiveVirtualDirectory() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_file =
        QDir(root.path()).filePath(QStringLiteral("dropped.txt"));
    QFile file(source_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("drop");
    file.close();

    const QString archive_path =
        QDir(root.path()).filePath(QStringLiteral("target-dir.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.directory = "folderA/folderB";
    add_request.input_paths = {to_std_path(source_file)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    z7::app::ListRequest root_list;
    root_list.archive_path = to_std_path(archive_path);
    const z7::app::ListResult root_result = run_request_sync(root_list);
    QVERIFY(root_result.ok);
    bool has_folder_a = false;
    for (const z7::app::ArchiveListEntry& entry : root_result.entries) {
      if (entry.path == "folderA" && entry.is_dir) {
        has_folder_a = true;
        break;
      }
    }
    QVERIFY(has_folder_a);

    z7::app::ListRequest nested_list;
    nested_list.archive_path = to_std_path(archive_path);
    nested_list.directory = "folderA/folderB";
    const z7::app::ListResult nested_result = run_request_sync(nested_list);
    QVERIFY(nested_result.ok);
    bool has_dropped_file = false;
    for (const z7::app::ArchiveListEntry& entry : nested_result.entries) {
      if (entry.path == "dropped.txt" && !entry.is_dir) {
        has_dropped_file = true;
        break;
      }
    }
    QVERIFY(has_dropped_file);
  }

void AppLogicHashBehaviorTest::listDetailedPropsIncludeArchivePreviewFields() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString top_dir = QDir(root.path()).filePath(QStringLiteral("top"));
    const QString inner_dir = QDir(top_dir).filePath(QStringLiteral("inner"));
    QVERIFY(QDir().mkpath(inner_dir));

    const QString leaf_file = QDir(inner_dir).filePath(QStringLiteral("leaf.txt"));
    QFile file(leaf_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("leaf-with-details");
    file.close();

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("details.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(top_dir)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    z7::app::ListRequest root_list;
    root_list.archive_path = to_std_path(archive_path);
    root_list.include_detailed_props = true;
    const z7::app::ListResult root_result = run_request_sync(root_list);
    QVERIFY(root_result.ok);

    std::optional<z7::app::ArchiveListEntry> top_entry;
    for (const z7::app::ArchiveListEntry& entry : root_result.entries) {
      if (entry.path == "top" && entry.is_dir) {
        top_entry = entry;
        break;
      }
    }
    QVERIFY(top_entry.has_value());
    QVERIFY(top_entry->num_sub_dirs.has_value() || top_entry->num_sub_files.has_value());

    z7::app::ListRequest inner_list;
    inner_list.archive_path = to_std_path(archive_path);
    inner_list.directory = "top/inner";
    inner_list.include_detailed_props = true;
    const z7::app::ListResult inner_result = run_request_sync(inner_list);
    QVERIFY(inner_result.ok);

    std::optional<z7::app::ArchiveListEntry> leaf_entry;
    for (const z7::app::ArchiveListEntry& entry : inner_result.entries) {
      if (entry.path == "leaf.txt" && !entry.is_dir) {
        leaf_entry = entry;
        break;
      }
    }
    QVERIFY(leaf_entry.has_value());
    QVERIFY(leaf_entry->packed_size.has_value());
    QVERIFY(leaf_entry->mtime_msecs_utc.has_value());
    QVERIFY(leaf_entry->crc.has_value());
    QVERIFY(!leaf_entry->method.empty());
  }

void AppLogicHashBehaviorTest::archivePropertiesReturnStructuredLines() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("alpha.txt"));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("alpha-content");
    file.close();

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("sample.7z"));

    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    z7::app::ArchivePropertiesRequest properties_request;
    properties_request.archive_path = to_std_path(archive_path);
    properties_request.entries = {"alpha.txt"};

    const z7::app::ArchivePropertiesResult properties_result =
        run_request_sync(properties_request);
    QVERIFY(properties_result.ok);
    QVERIFY(!properties_result.lines.empty());

    bool has_separator = false;
    bool has_archive_path = false;
    bool has_non_empty_pair = false;
    for (const z7::app::ArchivePropertyLine& line : properties_result.lines) {
      if (line.kind != z7::app::PropertyLineKind::kPair) {
        has_separator = true;
        continue;
      }
      if (!line.value.empty()) {
        has_non_empty_pair = true;
      }
      if (line.value.find("sample.7z") != std::string::npos) {
        has_archive_path = true;
      }
    }

    QVERIFY(has_separator);
    QVERIFY(has_non_empty_pair);
    QVERIFY(has_archive_path);
  }
