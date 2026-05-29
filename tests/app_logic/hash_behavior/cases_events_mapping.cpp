// tests/app_logic/hash_behavior/cases_events_mapping.cpp
// Role: Operation lifecycle and error-domain mapping behavior cases.

#include "internal.h"

using namespace hash_behavior_internal;

void AppLogicHashBehaviorTest::progressLifecycleEventsAreTypedAndOrdered() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("progress.bin"));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(file.resize(32LL * 1024LL * 1024LL));
    file.close();

    z7::app::HashRequest request;
    request.hash_method = "SHA256";
    request.input_paths = {to_std_path(file_path)};

    std::vector<z7::app::OperationEvent> events;
    const z7::app::HashResult result = run_request_sync(request, events);
    QVERIFY(result.ok);
    QVERIFY(!events.empty());

    const std::optional<size_t> prepare = first_stage_index(
        events, z7::app::OperationStage::kPrepare);
    const std::optional<size_t> running = first_stage_index(
        events, z7::app::OperationStage::kRunning);
    const std::optional<size_t> completed = first_stage_index(
        events, z7::app::OperationStage::kCompleted);
    QVERIFY(prepare.has_value());
    QVERIFY(running.has_value());
    QVERIFY(completed.has_value());
    QVERIFY(*prepare < *running);
    QVERIFY(*running < *completed);

    bool has_running_progress = false;
    for (const z7::app::OperationEvent& event : events) {
      if (event.kind == z7::app::OperationEventKind::kProgress &&
          event.stage == z7::app::OperationStage::kRunning) {
        has_running_progress = true;
        break;
      }
    }
    QVERIFY(has_running_progress);

    const auto assert_ordered_lifecycle = [](const std::vector<z7::app::OperationEvent>& op_events) {
      const std::optional<size_t> prepare =
          first_stage_index(op_events, z7::app::OperationStage::kPrepare);
      const std::optional<size_t> running =
          first_stage_index(op_events, z7::app::OperationStage::kRunning);
      const std::optional<size_t> finalizing =
          first_stage_index(op_events, z7::app::OperationStage::kFinalizing);
      const std::optional<size_t> completed =
          first_stage_index(op_events, z7::app::OperationStage::kCompleted);
      QVERIFY(prepare.has_value());
      QVERIFY(running.has_value());
      QVERIFY(finalizing.has_value());
      QVERIFY(completed.has_value());
      QVERIFY(*prepare < *running);
      QVERIFY(*running < *finalizing);
      QVERIFY(*finalizing < *completed);
    };

    const QString created_dir_name = QStringLiteral("created-dir");
    z7::app::CreateRequest create_request;
    create_request.parent_dir = to_std_path(root.path());
    create_request.name = to_std_path(created_dir_name);
    create_request.kind = z7::app::CreateNodeKind::kDirectory;
    std::vector<z7::app::OperationEvent> create_events;
    const z7::app::CreateResult create_result = run_request_sync(create_request, create_events);
    QVERIFY(create_result.ok);
    assert_ordered_lifecycle(create_events);
    bool create_has_progress = false;
    for (const z7::app::OperationEvent& event : create_events) {
      if (event.kind == z7::app::OperationEventKind::kProgress) {
        create_has_progress = true;
        break;
      }
    }
    QVERIFY(create_has_progress);

    const QString rename_source = QDir(root.path()).filePath(QStringLiteral("rename-src.txt"));
    {
      QFile rename_file(rename_source);
      QVERIFY(rename_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      rename_file.write("rename");
      rename_file.close();
    }

    z7::app::RenameRequest rename_request;
    rename_request.source_path = to_std_path(rename_source);
    rename_request.new_name = "rename-dst.txt";
    std::vector<z7::app::OperationEvent> rename_events;
    const z7::app::RenameResult rename_result = run_request_sync(rename_request, rename_events);
    QVERIFY(rename_result.ok);
    assert_ordered_lifecycle(rename_events);
    bool rename_has_progress = false;
    for (const z7::app::OperationEvent& event : rename_events) {
      if (event.kind == z7::app::OperationEventKind::kProgress) {
        rename_has_progress = true;
        break;
      }
    }
    QVERIFY(rename_has_progress);

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("events.7z"));
    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    z7::app::ListRequest list_request;
    list_request.archive_path = to_std_path(archive_path);
    list_request.include_detailed_props = true;
    std::vector<z7::app::OperationEvent> list_events;
    const z7::app::ListResult list_result = run_request_sync(list_request, list_events);
    QVERIFY(list_result.ok);
    QVERIFY(!list_result.entries.empty());
    assert_ordered_lifecycle(list_events);
}

void AppLogicHashBehaviorTest::errorDomainMappingIgnoresDiagnosticKeywords() {
    const z7::app::ArchiveError invalid_args = z7::app::map_native_exit_code(
        7,
        z7::app::NativeTerminationReason::kCompleted,
        "password unsupported i/o random diagnostic");
    QCOMPARE(invalid_args.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);

    const z7::app::ArchiveError success = z7::app::map_native_exit_code(
        0,
        z7::app::NativeTerminationReason::kCompleted,
        "fatal password error text");
    QCOMPARE(success.domain, z7::app::ArchiveErrorDomain::kNone);

    const z7::app::ArchiveError unknown = z7::app::map_native_exit_code(
        2,
        z7::app::NativeTerminationReason::kCompleted,
        "unsupported format i/o permission denied password");
    QCOMPARE(unknown.domain, z7::app::ArchiveErrorDomain::kUnknown);
}

void AppLogicHashBehaviorTest::createAndRenameRequestsReportInvalidArguments() {
    z7::app::CreateRequest bad_create;
    bad_create.parent_dir = "";
    bad_create.name = "";
    bad_create.kind = z7::app::CreateNodeKind::kFile;
    const z7::app::CreateResult create_result = run_request_sync(bad_create);
    QVERIFY(!create_result.ok);
    QCOMPARE(create_result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);

    z7::app::RenameRequest bad_rename;
    bad_rename.source_path = "";
    bad_rename.new_name = "";
    const z7::app::RenameResult rename_result = run_request_sync(bad_rename);
    QVERIFY(!rename_result.ok);
    QCOMPARE(rename_result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);
}

void AppLogicHashBehaviorTest::createFileRequestDoesNotOverwriteExistingFile() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString existing_path =
        QDir(root.path()).filePath(QStringLiteral("existing.txt"));
    {
      QFile existing(existing_path);
      QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
      existing.write("original-content");
      existing.close();
    }

    z7::app::CreateRequest request;
    request.parent_dir = to_std_path(root.path());
    request.name = "existing.txt";
    request.kind = z7::app::CreateNodeKind::kFile;

    const z7::app::CreateResult result = run_request_sync(request);
    QVERIFY(!result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kIo);

    QFile existing(existing_path);
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArrayLiteral("original-content"));
}
