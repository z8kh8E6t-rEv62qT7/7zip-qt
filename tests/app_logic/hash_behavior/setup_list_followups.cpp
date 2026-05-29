// tests/app_logic/hash_behavior/setup_list_followups.cpp
// Role: Additional list/properties open-flow behavior cases.

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

void AppLogicHashBehaviorTest::openListPropertiesCancelStateMachine() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("props-cancel.txt"));
    QFile source(source_file);
    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    source.write("properties-cancel");
    source.close();

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("props-cancel.7z"));
    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(source_file)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    auto try_cancel_properties = [&](size_t entry_count) -> bool {
      std::atomic<uint64_t> activity{0};
      auto delegate = std::make_shared<ActivityProbeDelegate>(activity);

      z7::app::ArchivePropertiesRequest request;
      request.archive_path = to_std_path(archive_path);
      request.entries.reserve(entry_count);
      for (size_t i = 0; i < entry_count; ++i) {
        request.entries.push_back("missing-entry-" + std::to_string(i) + ".txt");
      }

      auto task = start_request(request, delegate);
      if (!task.valid()) {
        return false;
      }

      if (!wait_until(
              [&activity, &task]() {
                return activity.load(std::memory_order_relaxed) > 0 ||
                       is_terminal_state(task.state());
              },
              10000)) {
        return false;
      }

      if (!is_terminal_state(task.state())) {
        task.cancel();
      }
      const z7::app::ArchivePropertiesResult result =
          wait_for_result<z7::app::ArchivePropertiesResult>(task);
      return !result.ok && result.error.domain == z7::app::ArchiveErrorDomain::kCanceled;
    };

    bool canceled = false;
    for (const size_t entries : {size_t{200000}, size_t{400000}, size_t{800000}}) {
      if (try_cancel_properties(entries)) {
        canceled = true;
        break;
      }
    }

    QVERIFY2(canceled,
             "archive properties did not enter canceled domain under cancellation attempts");
}

void AppLogicHashBehaviorTest::openListPropertiesNavigateRequestsCoverSuccessAndFailure() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("open.txt"));
    {
      QFile file(file_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("open-list-properties");
      file.close();
    }

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("open.7z"));
    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY(add_result.ok);

    z7::app::OpenArchiveRequest open_request;
    open_request.archive_path = to_std_path(archive_path);
    const z7::app::OpenArchiveResult open_result = run_request_sync(open_request);
    QVERIFY(open_result.ok);
    QVERIFY(!open_result.archive_path.empty());

    z7::app::OpenArchiveRequest bad_open_request;
    const z7::app::OpenArchiveResult bad_open_result = run_request_sync(bad_open_request);
    QVERIFY(!bad_open_result.ok);
    QCOMPARE(bad_open_result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);

    z7::app::ListRequest bad_list_request;
    const z7::app::ListResult bad_list_result = run_request_sync(bad_list_request);
    QVERIFY(!bad_list_result.ok);
    QCOMPARE(bad_list_result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);

    z7::app::ArchivePropertiesRequest bad_properties_request;
    const z7::app::ArchivePropertiesResult bad_properties_result =
        run_request_sync(bad_properties_request);
    QVERIFY(!bad_properties_result.ok);
    QCOMPARE(bad_properties_result.error.domain,
             z7::app::ArchiveErrorDomain::kInvalidArguments);

    z7::app::NavigateRequest navigate_request;
    navigate_request.from_path = to_std_path(root.path());
    navigate_request.to_path = to_std_path(QDir(root.path()).filePath(QStringLiteral("next")));
    const z7::app::NavigateResult navigate_result = run_request_sync(navigate_request);
    QVERIFY(navigate_result.ok);
    QCOMPARE(QString::fromStdString(navigate_result.resolved_path),
             QDir(root.path()).filePath(QStringLiteral("next")));

    z7::app::NavigateRequest bad_navigate_request;
    bad_navigate_request.from_path = to_std_path(root.path());
    const z7::app::NavigateResult bad_navigate_result = run_request_sync(bad_navigate_request);
    QVERIFY(!bad_navigate_result.ok);
    QCOMPARE(bad_navigate_result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);
}
