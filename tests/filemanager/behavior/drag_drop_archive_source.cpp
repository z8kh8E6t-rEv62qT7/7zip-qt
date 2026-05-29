// tests/filemanager/behavior/drag_drop_archive_source.cpp
// Role: Archive drag-source behavior and delayed materialization parity cases.

#include "internal.h"

#include "main_window/model/model.h"

#if defined(Z7_MACOS_INTEGRATION_ENABLED)
#include "macos_native_drag.h"
#include "../../../src/macos_integration/native_drag/src/internal.h"
#endif
#include "drag_drop_policy_qt.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace filemanager_behavior_internal;

namespace {

QStringList archive_temp_dirs_under(const QString& root_path,
                                    const QString& prefix) {
  QDir root(root_path);
  return root.entryList(QStringList{prefix + QStringLiteral("*")},
                        QDir::Dirs | QDir::NoDotAndDotDot,
                        QDir::Name);
}

bool dispatch_drop_with_mime(z7::ui::filemanager::MainWindow* window,
                             QObject* target,
                             QMimeData* mime_data,
                             Qt::MouseButtons buttons = Qt::LeftButton,
                             Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                             QPointF pos = QPointF(2000.0, 2000.0)) {
  if (window == nullptr || target == nullptr || mime_data == nullptr) {
    return false;
  }

  QDropEvent positioned_event(pos,
                              Qt::CopyAction | Qt::MoveAction,
                              mime_data,
                              buttons,
                              modifiers);
  const bool handled = window->eventFilter(target, &positioned_event);
  return handled && positioned_event.isAccepted();
}

z7::ui::filemanager::DragExecutionReport archive_drag_report(
    bool archive_transfer_requested,
    bool internal_archive_drop_handled,
    Qt::DropAction result_action,
    bool native_archive_drag,
    bool native_drag_session_started,
    bool native_drag_completed,
    const QString& native_error_message,
    int promise_write_requests = 0,
    int promise_write_finishes = 0,
    int promise_write_successes = 0,
    int direct_export_attempts = 0,
    int direct_export_successes = 0,
    bool native_ended_in_source_view = false,
    bool native_source_widget_resolved = true,
    bool native_view_available = true,
    bool native_current_event_available = true,
    int native_drag_item_count = 1,
    bool native_promise_writes_settled = true,
    bool native_timed_out = false) {
  z7::ui::filemanager::DragExecutionReport report;
  report.archive_source = true;
  report.archive_transfer_requested = archive_transfer_requested;
  report.internal_archive_drop_handled = internal_archive_drop_handled;
  report.native_archive_drag = native_archive_drag;
  report.native_source_widget_resolved = native_source_widget_resolved;
  report.native_view_available = native_view_available;
  report.native_current_event_available = native_current_event_available;
  report.native_drag_session_started = native_drag_session_started;
  report.native_drag_completed = native_drag_completed;
  report.native_ended_in_source_view = native_ended_in_source_view;
  report.native_promise_writes_settled = native_promise_writes_settled;
  report.native_timed_out = native_timed_out;
  report.native_drag_item_count = native_drag_item_count;
  report.promise_write_requests = promise_write_requests;
  report.promise_write_finishes = promise_write_finishes;
  report.promise_write_successes = promise_write_successes;
  report.direct_export_attempts = direct_export_attempts;
  report.direct_export_successes = direct_export_successes;
  report.native_error_message = native_error_message;
  report.result_action = result_action;
  return report;
}

#if defined(Z7_MACOS_INTEGRATION_ENABLED)
bool wait_for_native_drag_completion(
    const std::shared_ptr<z7::macos_integration::native_drag::detail::SharedState>& shared,
    std::atomic_bool* stop_waiter,
    std::chrono::milliseconds no_promise_grace) {
  quint64 observed_epoch = 0;
  bool grace_active = false;
  auto grace_deadline = std::chrono::steady_clock::time_point{};

  while (stop_waiter == nullptr || !stop_waiter->load(std::memory_order_acquire)) {
    const auto metrics = shared->metrics_snapshot();
    const auto now = std::chrono::steady_clock::now();
    if (metrics.drag_session_ended) {
      if (metrics.promise_requests_started == 0) {
        if (!grace_active) {
          grace_active = true;
          grace_deadline = now + no_promise_grace;
        } else if (now >= grace_deadline) {
          return true;
        }
      } else {
        grace_active = false;
        if (metrics.promise_requests_finished >= metrics.promise_requests_started) {
          return true;
        }
      }
    } else {
      grace_active = false;
    }

    std::unique_lock<std::mutex> lock(shared->completion_wait_mutex);
    if (grace_active) {
      shared->completion_wait_condition.wait_until(
          lock,
          grace_deadline,
          [shared, &observed_epoch, stop_waiter]() {
            return (stop_waiter != nullptr &&
                    stop_waiter->load(std::memory_order_acquire)) ||
                   shared->completion_wait_epoch != observed_epoch;
          });
    } else {
      shared->completion_wait_condition.wait(
          lock,
          [shared, &observed_epoch, stop_waiter]() {
            return (stop_waiter != nullptr &&
                    stop_waiter->load(std::memory_order_acquire)) ||
                   shared->completion_wait_epoch != observed_epoch;
          });
    }
    observed_epoch = shared->completion_wait_epoch;
  }

  return false;
}

bool write_bytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(bytes) == bytes.size();
}

QByteArray read_bytes(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}
#endif

}  // namespace

void FileManagerBehaviorTest::archiveDragMimeDataDefersMaterializationUntilUrlsRequested() {
#if defined(Z7_MACOS_INTEGRATION_ENABLED)
  QTemporaryDir file_root;
  QVERIFY2(file_root.isValid(), "failed to create file fixture temp dir");
  const QString file_archive = create_sample_archive(file_root);
  QVERIFY2(!file_archive.isEmpty(), "failed to prepare sample archive");

  const QStringList before_temp_dirs =
      archive_temp_dirs_under(QDir::tempPath(), QStringLiteral("7zE_"));

  z7::ui::filemanager::MainWindow file_window;
  file_window.open_archive_inside(file_archive);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(file_window) == nullptr, 20000);
  QVERIFY(file_window.in_archive_view());

  const QString file_target_root =
      QDir(file_root.path()).filePath(QStringLiteral("finder-target-file"));
  QVERIFY(QDir().mkpath(file_target_root));
  const QString file_destination =
      QDir(file_target_root).filePath(QStringLiteral("finder-renamed.txt"));
  QVERIFY(!QFileInfo::exists(file_destination));

  QString error;
  QVERIFY2(file_window.export_archive_drag_entry_to_destination_for_panel(
               0, QStringLiteral("sample.txt"), false, file_destination, &error),
           qPrintable(error));
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QVERIFY(QFileInfo::exists(file_destination));
  QVERIFY(QFileInfo(file_destination).isFile());
  QVERIFY(!QFileInfo(QDir(file_target_root).filePath(QStringLiteral("sample.txt"))).exists());

  QTemporaryDir dir_root;
  QVERIFY2(dir_root.isValid(), "failed to create directory fixture temp dir");
  const QString dir_archive = create_nested_archive(dir_root);
  QVERIFY2(!dir_archive.isEmpty(), "failed to prepare nested archive");

  z7::ui::filemanager::MainWindow dir_window;
  dir_window.open_archive_inside(dir_archive);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(dir_window) == nullptr, 20000);
  QVERIFY(dir_window.in_archive_view());

  const QString dir_target_root =
      QDir(dir_root.path()).filePath(QStringLiteral("finder-target-dir"));
  QVERIFY(QDir().mkpath(dir_target_root));
  const QString dir_destination =
      QDir(dir_target_root).filePath(QStringLiteral("top"));
  QVERIFY2(dir_window.export_archive_drag_entry_to_destination_for_panel(
               0, QStringLiteral("top"), true, dir_destination, &error),
           qPrintable(error));
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QVERIFY(QFileInfo(dir_destination).isDir());
  QVERIFY(QFileInfo(QDir(dir_destination).filePath(QStringLiteral("inner/leaf.txt")))
              .exists());
  QVERIFY(!QFileInfo(QDir(dir_destination).filePath(QStringLiteral("top"))).exists());

  const QString root_target_root =
      QDir(dir_root.path()).filePath(QStringLiteral("finder-target-root"));
  QVERIFY(QDir().mkpath(root_target_root));
  const QString root_destination =
      QDir(root_target_root).filePath(QStringLiteral("archive-root"));
  QVERIFY2(dir_window.export_archive_drag_entry_to_destination_for_panel(
               0, QString(), true, root_destination, &error),
           qPrintable(error));
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QVERIFY(QFileInfo(root_destination).isDir());
  QVERIFY(QFileInfo(QDir(root_destination).filePath(QStringLiteral("top/inner/leaf.txt")))
              .exists());

  const QStringList after_temp_dirs =
      archive_temp_dirs_under(QDir::tempPath(), QStringLiteral("7zE_"));
  QCOMPARE(after_temp_dirs, before_temp_dirs);
  return;
#endif

  clear_runtime_settings();

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");
  QTemporaryDir working_root;
  QVERIFY2(working_root.isValid(), "failed to create working dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  z7::platform::qt::PortableSettings settings;
  settings.setValue(QStringLiteral("Options/WorkDirType"), 2);
  settings.setValue(QStringLiteral("Options/WorkDirPath"), working_root.path());
  settings.setValue(QStringLiteral("Options/TempRemovableOnly"), false);

  z7::ui::filemanager::MainWindow window;
  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QVERIFY(window.in_archive_view());

  const int file_row = row_by_name(window, QStringLiteral("sample.txt"));
  QVERIFY(file_row >= 0);
  auto* model = window.panels_[0].ui.details_view != nullptr
                    ? window.panels_[0].ui.details_view->model()
                    : nullptr;
  QVERIFY(model != nullptr);

  const QStringList before_dirs =
      archive_temp_dirs_under(working_root.path(), QStringLiteral("7zE_"));
  std::unique_ptr<QMimeData> mime(model->mimeData(
      QModelIndexList{model->index(file_row, 0), model->index(file_row, 1)}));
  QVERIFY(mime != nullptr);
  QVERIFY(mime->hasFormat(QStringLiteral("application/x-z7-filemanager-archive-source")));

  const QStringList after_create_dirs =
      archive_temp_dirs_under(working_root.path(), QStringLiteral("7zE_"));
  QCOMPARE(after_create_dirs, before_dirs);

  QVERIFY(QFile::remove(archive_path));
  QVERIFY(!QFileInfo::exists(archive_path));

  const QList<QUrl> urls = mime->urls();
  QVERIFY(!urls.isEmpty());
  const QString extracted_path = urls.front().toLocalFile();
  QVERIFY(QFileInfo::exists(extracted_path));

  const QString normalized_extracted = QDir::cleanPath(QDir::fromNativeSeparators(extracted_path));
  const QString normalized_working_root =
      QDir::cleanPath(QDir::fromNativeSeparators(working_root.path()));
  QVERIFY2(!(normalized_extracted.startsWith(normalized_working_root + QLatin1Char('/')) ||
             normalized_extracted.startsWith(normalized_working_root + QLatin1Char('\\'))),
           qPrintable(QStringLiteral("extracted path '%1' unexpectedly under configured working root '%2'")
                          .arg(normalized_extracted, normalized_working_root)));
  const QString normalized_system_temp =
      QDir::cleanPath(QDir::fromNativeSeparators(QDir::tempPath()));
  QVERIFY2(normalized_extracted.startsWith(normalized_system_temp + QLatin1Char('/')) ||
               normalized_extracted.startsWith(normalized_system_temp + QLatin1Char('\\')),
           qPrintable(QStringLiteral("extracted path '%1' is not under system temp root '%2'")
                          .arg(normalized_extracted, normalized_system_temp)));
  const QFileInfo extracted_info(normalized_extracted);
  QVERIFY2(extracted_info.dir().dirName().startsWith(QStringLiteral("7zE_")),
           qPrintable(QStringLiteral("drag temp dir '%1' should use 7zE_ prefix")
                          .arg(extracted_info.dir().dirName())));

  const QStringList after_urls_dirs =
      archive_temp_dirs_under(working_root.path(), QStringLiteral("7zE_"));
  QCOMPARE(after_urls_dirs, before_dirs);

  clear_runtime_settings();
}

void FileManagerBehaviorTest::archiveDragMimeDataIncludesMacPromiseMarkerWhenEnabled() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  z7::ui::filemanager::MainWindow window;
  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QVERIFY(window.in_archive_view());

  const int file_row = row_by_name(window, QStringLiteral("sample.txt"));
  QVERIFY(file_row >= 0);
  auto* model = window.panels_[0].ui.details_view != nullptr
                    ? window.panels_[0].ui.details_view->model()
                    : nullptr;
  QVERIFY(model != nullptr);

  std::unique_ptr<QMimeData> mime(model->mimeData(
      QModelIndexList{model->index(file_row, 0)}));
  QVERIFY(mime != nullptr);

  const QString promise_mime =
      z7::platform::qt::mac_archive_promise_mime_type();
  const bool native_enabled =
      z7::platform::qt::mac_archive_native_drag_enabled();
  QCOMPARE(mime->hasFormat(promise_mime), native_enabled);

  if (native_enabled) {
    const QByteArray payload = mime->data(promise_mime);
    QVERIFY2(!payload.isEmpty(), "mac archive promise payload should not be empty");
    QVERIFY(mime->hasFormat(
        QStringLiteral("application/x-z7-filemanager-archive-transfer-requested")));
  }
}

void FileManagerBehaviorTest::macArchiveNativePromiseBackendRejectsInvalidRequest() {
#if defined(Z7_MACOS_INTEGRATION_ENABLED)
  const z7::macos_integration::native_drag::MacOSIntegrationNativeDragRequest request;
  const z7::macos_integration::native_drag::MacOSIntegrationNativeDragResult result =
      z7::macos_integration::native_drag::run_macos_integration_native_drag(request);
  QVERIFY(!result.handled);
#else
  QSKIP("macOS integration disabled at build time");
#endif
}

void FileManagerBehaviorTest::macNativeDragPolicyAndItemNamesFollowPlatformContract() {
#if defined(Z7_MACOS_INTEGRATION_ENABLED)
  using z7::macos_integration::native_drag::MacOSIntegrationNativeDragItem;
  using z7::macos_integration::native_drag::detail::default_item_name;
  using z7::macos_integration::native_drag::detail::qt_action_from_ns_operation;

  QCOMPARE(qt_action_from_ns_operation(NSDragOperationCopy), Qt::CopyAction);
  QCOMPARE(qt_action_from_ns_operation(NSDragOperationMove), Qt::MoveAction);
  QCOMPARE(qt_action_from_ns_operation(NSDragOperationCopy | NSDragOperationMove),
           Qt::MoveAction);
  QCOMPARE(qt_action_from_ns_operation(static_cast<NSDragOperation>(0)),
           Qt::IgnoreAction);

  MacOSIntegrationNativeDragItem item;
  item.archive_entry_path = QStringLiteral("docs/original.txt");
  item.suggested_file_name = QStringLiteral("  promised.txt  ");
  QCOMPARE(default_item_name(item), QStringLiteral("promised.txt"));

  item.suggested_file_name.clear();
  QCOMPARE(default_item_name(item), QStringLiteral("original.txt"));

  item.archive_entry_path = QStringLiteral("   ");
  QCOMPARE(default_item_name(item), QStringLiteral("7zFM-item"));
#else
  QSKIP("macOS integration disabled at build time");
#endif
}

void FileManagerBehaviorTest::macNativeDragFilesystemPromiseCopiesSourceWithoutWriter() {
#if defined(Z7_MACOS_INTEGRATION_ENABLED)
  using z7::macos_integration::native_drag::MacOSIntegrationNativeDragItem;
  using z7::macos_integration::native_drag::detail::write_to_destination_if_needed;

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString source_file =
      QDir(root.path()).filePath(QStringLiteral("source.txt"));
  const QByteArray source_bytes("filesystem native drag file payload");
  QVERIFY(write_bytes(source_file, source_bytes));

  MacOSIntegrationNativeDragItem file_item;
  file_item.source_path = source_file;
  file_item.suggested_file_name = QStringLiteral("source.txt");
  const QString file_destination =
      QDir(root.path()).filePath(QStringLiteral("finder-target/renamed.txt"));
  QString error;
  QVERIFY2(write_to_destination_if_needed(&file_item, file_destination, &error),
           qPrintable(error));
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QCOMPARE(read_bytes(file_destination), source_bytes);
  QVERIFY(QFileInfo::exists(source_file));

  const QString source_dir =
      QDir(root.path()).filePath(QStringLiteral("source-folder"));
  QVERIFY(QDir().mkpath(QDir(source_dir).filePath(QStringLiteral("nested"))));
  const QString source_leaf =
      QDir(source_dir).filePath(QStringLiteral("nested/leaf.txt"));
  const QByteArray leaf_bytes("filesystem native drag directory payload");
  QVERIFY(write_bytes(source_leaf, leaf_bytes));

  MacOSIntegrationNativeDragItem dir_item;
  dir_item.source_path = source_dir;
  dir_item.suggested_file_name = QStringLiteral("source-folder");
  dir_item.is_dir = true;
  const QString dir_destination =
      QDir(root.path()).filePath(QStringLiteral("finder-target/exported-dir"));
  QVERIFY2(write_to_destination_if_needed(&dir_item, dir_destination, &error),
           qPrintable(error));
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QCOMPARE(read_bytes(QDir(dir_destination).filePath(QStringLiteral("nested/leaf.txt"))),
           leaf_bytes);
  QVERIFY(QFileInfo(source_dir).isDir());
#else
  QSKIP("macOS integration disabled at build time");
#endif
}

void FileManagerBehaviorTest::macNativeDragArchivePromiseUsesDirectExportWriter() {
#if defined(Z7_MACOS_INTEGRATION_ENABLED)
  using z7::macos_integration::native_drag::MacOSIntegrationNativeDragItem;
  using z7::macos_integration::native_drag::detail::write_to_destination_if_needed;

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QByteArray archive_bytes("archive native drag direct export payload");
  int write_calls = 0;
  MacOSIntegrationNativeDragItem item;
  item.archive_entry_path = QStringLiteral("docs/readme.md");
  item.write_to_destination =
      [&archive_bytes, &write_calls](const QString& destination, QString* error) {
        ++write_calls;
        if (!write_bytes(destination, archive_bytes)) {
          if (error != nullptr) {
            *error = QStringLiteral("failed to write destination");
          }
          return false;
        }
        return true;
      };

  QString error;
  const QString destination =
      QDir(root.path()).filePath(QStringLiteral("finder-target/readme.md"));
  QVERIFY(QDir().mkpath(QFileInfo(destination).absolutePath()));
  QVERIFY2(write_to_destination_if_needed(&item, destination, &error),
           qPrintable(error));
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QCOMPARE(write_calls, 1);
  QCOMPARE(read_bytes(destination), archive_bytes);

  item.write_to_destination = {};
  QVERIFY(!write_to_destination_if_needed(&item, destination, &error));
  QVERIFY(error.contains(QStringLiteral("Missing direct-export writer")));
#else
  QSKIP("macOS integration disabled at build time");
#endif
}

void FileManagerBehaviorTest::macArchiveNativePromiseGraceWaitsForLateRequest() {
#if defined(Z7_MACOS_INTEGRATION_ENABLED)
  using z7::macos_integration::native_drag::MacOSIntegrationNativeDragRequest;
  using z7::macos_integration::native_drag::detail::PromiseWriteConcurrencyPolicy;
  using z7::macos_integration::native_drag::detail::SharedState;

  MacOSIntegrationNativeDragRequest request;
  request.items.resize(1);
  auto shared = std::make_shared<SharedState>(
      request, PromiseWriteConcurrencyPolicy::from_defaults());

  std::atomic_bool stop_waiter{false};
  std::atomic_bool completed{false};
  std::thread waiter([shared, &stop_waiter, &completed]() {
    completed.store(
        wait_for_native_drag_completion(shared,
                                        &stop_waiter,
                                        std::chrono::milliseconds(150)),
        std::memory_order_release);
  });

  shared->mark_drag_session_ended(Qt::IgnoreAction, false, false);
  QTest::qWait(50);
  shared->on_promise_request_started(0);
  QTest::qWait(200);
  QVERIFY(!completed.load(std::memory_order_acquire));

  shared->on_write_finish(0, 0, true, false, false, 0, false, 0);
  QTRY_VERIFY_WITH_TIMEOUT(completed.load(std::memory_order_acquire), 1000);

  stop_waiter.store(true, std::memory_order_release);
  shared->notify_completion_waiters();
  waiter.join();
#else
  QSKIP("macOS integration disabled at build time");
#endif
}

void FileManagerBehaviorTest::archiveDragStrictFailureShowsWarningWhenDropRejected() {
#if !defined(Q_OS_MAC)
  QSKIP("Strict failure is only active on macOS.");
#endif
  z7::ui::filemanager::MainWindow window;

  QString captured_title;
  QString captured_text;
  schedule_message_box_capture_and_click(
      QMessageBox::Ok,
      &captured_title,
      &captured_text,
      3000,
      10);

  window.on_panel_drag_finished(
      archive_drag_report(false,
                          false,
                          Qt::IgnoreAction,
                          true,
                          true,
                          true,
                          QString()));

  QTRY_VERIFY_WITH_TIMEOUT(!captured_text.isEmpty(), 3000);
  QVERIFY(!captured_title.isEmpty() || !captured_text.isEmpty());
  QVERIFY(captured_text.contains(
      QStringLiteral("did not request archive data")));
  QCOMPARE(window.property("z7.fm.drag.archive.last.result_action").toInt(),
           static_cast<int>(Qt::IgnoreAction));
  QCOMPARE(window.property("z7.fm.drag.archive.last.transfer_requested")
               .toBool(),
           false);
  QCOMPARE(window.property("z7.fm.drag.archive.last.internal_drop_handled")
               .toBool(),
           false);
  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("TargetDidNotRequestPromise"));

  close_message_boxes();
}

void FileManagerBehaviorTest::archiveDragFailureClassTracksRejectWriteFailureAndCancel() {
  z7::ui::filemanager::MainWindow window;
  // On macOS strict failure is always active; auto-close any warning dialogs.
  schedule_message_box_autoclose(5000, 10);

  auto native_start_failed =
      archive_drag_report(false,
                          false,
                          Qt::IgnoreAction,
                          true,
                          false,
                          false,
                          QStringLiteral("Failed to begin native drag session."));
  native_start_failed.native_drag_item_count = 2;
  native_start_failed.native_promise_writes_settled = false;
  window.on_panel_drag_finished(native_start_failed);
  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("NativeStartFailed"));
  QCOMPARE(window.property("z7.fm.drag.archive.last.native_drag_session_started")
               .toBool(),
           false);
  QCOMPARE(window.property("z7.fm.drag.archive.last.native_drag_item_count")
               .toInt(),
           2);

  window.on_panel_drag_finished(archive_drag_report(false,
                                                    false,
                                                    Qt::IgnoreAction,
                                                    true,
                                                    true,
                                                    true,
                                                    QString()));
  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("TargetDidNotRequestPromise"));
  QCOMPARE(window.property("z7.fm.drag.archive.last.error_message").toString(),
           QString());

  auto direct_export_failed =
      archive_drag_report(true,
                          false,
                          Qt::IgnoreAction,
                          true,
                          true,
                          true,
                          QStringLiteral("direct export failed"));
  direct_export_failed.promise_write_requests = 1;
  direct_export_failed.promise_write_finishes = 1;
  direct_export_failed.direct_export_attempts = 1;
  window.on_panel_drag_finished(direct_export_failed);
  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("DirectExportFailed"));
  QCOMPARE(window.property("z7.fm.drag.archive.last.direct_export_attempts")
               .toInt(),
           1);
  QCOMPARE(window.property("z7.fm.drag.archive.last.direct_export_successes")
               .toInt(),
           0);

  auto direct_write_failed =
      archive_drag_report(true,
                          false,
                          Qt::IgnoreAction,
                          true,
                          true,
                          true,
                          QStringLiteral("simulated write failure"));
  direct_write_failed.promise_write_requests = 1;
  direct_write_failed.promise_write_finishes = 1;
  direct_write_failed.direct_export_attempts = 1;
  window.on_panel_drag_finished(direct_write_failed);
  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("DirectExportFailed"));
  QCOMPARE(window.property("z7.fm.drag.archive.last.error_message").toString(),
           QStringLiteral("simulated write failure"));
  QCOMPARE(window.property("z7.fm.drag.archive.last.direct_export_attempts").toInt(),
           1);
  QCOMPARE(window.property("z7.fm.drag.archive.last.direct_export_successes").toInt(),
           0);

  auto partial_direct_success =
      archive_drag_report(true,
                          false,
                          Qt::IgnoreAction,
                          true,
                          true,
                          true,
                          QStringLiteral("partial direct export success"));
  partial_direct_success.promise_write_requests = 2;
  partial_direct_success.promise_write_finishes = 2;
  partial_direct_success.promise_write_successes = 1;
  partial_direct_success.direct_export_attempts = 2;
  partial_direct_success.direct_export_successes = 1;
  partial_direct_success.native_ended_in_source_view = true;
  partial_direct_success.native_drag_item_count = 2;
  window.on_panel_drag_finished(partial_direct_success);
  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("DirectExportFailed"));
  QCOMPARE(window.property("z7.fm.drag.archive.last.direct_export_successes").toInt(),
           1);
  QCOMPARE(window.property("z7.fm.drag.archive.last.native_drag_item_count").toInt(),
           2);

  auto canceled = archive_drag_report(true,
                                      false,
                                      Qt::IgnoreAction,
                                      true,
                                      true,
                                      true,
                                      QString());
  canceled.promise_write_requests = 1;
  canceled.promise_write_finishes = 1;
  window.on_panel_drag_finished(canceled);
  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("Canceled"));

  auto partial_promise_write =
      archive_drag_report(true,
                          false,
                          Qt::IgnoreAction,
                          true,
                          true,
                          true,
                          QStringLiteral("partial promise write"));
  partial_promise_write.promise_write_requests = 1;
  partial_promise_write.native_ended_in_source_view = true;
  partial_promise_write.native_drag_item_count = 2;
  partial_promise_write.native_promise_writes_settled = false;
  window.on_panel_drag_finished(partial_promise_write);
  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("PromiseRequestedButNoWrite"));

  auto target_no_promise =
      archive_drag_report(true,
                          false,
                          Qt::IgnoreAction,
                          true,
                          true,
                          false,
                          QString());
  target_no_promise.native_ended_in_source_view = true;
  target_no_promise.native_drag_item_count = 1;
  window.on_panel_drag_finished(target_no_promise);
  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("TargetDidNotRequestPromise"));
  QCOMPARE(window.property("z7.fm.drag.archive.last.native_timed_out").toBool(),
           false);

  auto timed_out_promise =
      archive_drag_report(true,
                          false,
                          Qt::IgnoreAction,
                          true,
                          true,
                          false,
                          QStringLiteral("timed out waiting for remaining writes"));
  timed_out_promise.promise_write_requests = 1;
  timed_out_promise.native_ended_in_source_view = true;
  timed_out_promise.native_drag_item_count = 2;
  timed_out_promise.native_promise_writes_settled = false;
  timed_out_promise.native_timed_out = true;
  window.on_panel_drag_finished(timed_out_promise);
  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("PromiseRequestedButNoWrite"));
  QCOMPARE(window.property("z7.fm.drag.archive.last.native_timed_out").toBool(),
           true);

  auto accepted = archive_drag_report(true,
                                      true,
                                      Qt::CopyAction,
                                      true,
                                      true,
                                      true,
                                      QString());
  accepted.promise_write_requests = 2;
  accepted.promise_write_finishes = 2;
  accepted.promise_write_successes = 2;
  accepted.direct_export_attempts = 2;
  accepted.direct_export_successes = 2;
  accepted.native_ended_in_source_view = true;
  accepted.native_drag_item_count = 2;
  window.on_panel_drag_finished(accepted);
  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("Accepted"));
  QCOMPARE(window.property("z7.fm.drag.archive.last.promise_write_finishes")
               .toInt(),
           2);
}

void FileManagerBehaviorTest::genericQtArchiveDragMaterializationFailureIsReported() {
  const QString failure_text =
      QStringLiteral("simulated generic Qt materialization failure");

  z7::ui::filemanager::DirectoryListModel model;
  QVector<z7::ui::filemanager::DirectoryListModel::VirtualEntry> entries;
  z7::ui::filemanager::DirectoryListModel::VirtualEntry entry;
  entry.path = QStringLiteral("docs/readme.txt");
  entry.display_name = QStringLiteral("readme.txt");
  entries.push_back(entry);
  model.set_virtual_entries(QString(), entries);
  model.set_archive_drag_source(QStringLiteral("/tmp/missing-source.7z"),
                                QStringLiteral("7z"),
                                42);

  bool materializer_called = false;
  model.set_archive_drag_materializer(
      [&materializer_called, failure_text](
          const QStringList& requested_entries,
          z7::ui::filemanager::DirectoryListModel::ArchiveDragMaterializedCallback
              finished_cb) {
        materializer_called = true;
        QCOMPARE(requested_entries, QStringList{QStringLiteral("docs/readme.txt")});
        finished_cb({}, failure_text);
      });

  std::unique_ptr<QMimeData> mime(
      model.mimeData(QModelIndexList{model.index(0, 0)}));
  QVERIFY(mime != nullptr);
  QVERIFY(mime->hasFormat(
      QString::fromLatin1(z7::ui::filemanager::kMimeTypeZ7FmArchiveSource)));
  QVERIFY(!mime->hasFormat(QString::fromLatin1(
      z7::ui::filemanager::kMimeTypeZ7FmArchiveMaterializationError)));

  const QList<QUrl> urls = mime->urls();
  QVERIFY(materializer_called);
  QVERIFY(urls.isEmpty());
  QVERIFY(mime->hasFormat(QString::fromLatin1(
      z7::ui::filemanager::kMimeTypeZ7FmArchiveTransferRequested)));
  QVERIFY(mime->hasFormat(QString::fromLatin1(
      z7::ui::filemanager::kMimeTypeZ7FmArchiveMaterializationError)));
  QCOMPARE(QString::fromUtf8(mime->data(QString::fromLatin1(
               z7::ui::filemanager::kMimeTypeZ7FmArchiveMaterializationError))),
           failure_text);

  z7::ui::filemanager::MainWindow window;
  schedule_message_box_autoclose(5000, 10);

  z7::ui::filemanager::DragExecutionReport report;
  report.archive_source = true;
  report.archive_transfer_requested = true;
  report.result_action = Qt::CopyAction;
  report.materialization_error_message = failure_text;
  window.on_panel_drag_finished(report);

  QCOMPARE(window.property("z7.fm.drag.archive.last.failure_class").toString(),
           QStringLiteral("MaterializationFailed"));
  QCOMPARE(window.property("z7.fm.drag.archive.last.error_message").toString(),
           failure_text);
  QCOMPARE(window.property("z7.fm.drag.archive.last.materialization_error_message")
               .toString(),
           failure_text);
}

void FileManagerBehaviorTest::dropInternalArchiveSourceToFilesystemExtractsIntoTargetDirectory() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  const QString target_dir = QDir(root.path()).filePath(QStringLiteral("drop-target"));
  QVERIFY(QDir().mkpath(target_dir));

  z7::ui::filemanager::MainWindow window;
  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QVERIFY(window.in_archive_view());

  const int file_row = row_by_name(window, QStringLiteral("sample.txt"));
  QVERIFY(file_row >= 0);
  auto* source_model = window.panels_[0].ui.details_view != nullptr
                           ? window.panels_[0].ui.details_view->model()
                           : nullptr;
  QVERIFY(source_model != nullptr);

  if (!window.two_panels_visible_) {
    window.on_two_panels_action_triggered();
  }
  window.set_current_directory_for_panel(1, target_dir);
  window.set_active_panel(1);
  std::unique_ptr<QMimeData> mime(
      source_model->mimeData(QModelIndexList{source_model->index(file_row, 0)}));
  QVERIFY(mime != nullptr);
  QVERIFY(mime->hasFormat(QStringLiteral("application/x-z7-filemanager-archive-source")));

  QVERIFY(QFile::remove(archive_path));
  QVERIFY(!QFileInfo::exists(archive_path));

  bool sevenzip_launched = false;
  window.external_command_launcher_ =
      [&sevenzip_launched](const QString&,
                           const QStringList&,
                           const QString&,
                           qint64*) {
        sevenzip_launched = true;
        return true;
      };

  QVERIFY(window.panels_[1].ui.details_view != nullptr);
  QVERIFY(window.panels_[1].ui.details_view->viewport() != nullptr);
  QVERIFY(dispatch_drop_with_mime(&window,
                                  window.panels_[1].ui.details_view->viewport(),
                                  mime.get()));

  const QString extracted_file = QDir(target_dir).filePath(QStringLiteral("sample.txt"));
  QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(extracted_file), 20000);
  QVERIFY(!sevenzip_launched);
}
