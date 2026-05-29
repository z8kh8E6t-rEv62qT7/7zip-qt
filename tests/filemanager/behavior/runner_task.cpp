// tests/filemanager/behavior/runner_task.cpp
// Role: Runner task and progress behavior cases.

#include "internal.h"

#include "gui_task_runner_helpers.h"
#include "hash_progress_dialog.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <system_error>

using namespace filemanager_behavior_internal;

namespace {

namespace fs = std::filesystem;

QString create_bridge_worker_stub(const QTemporaryDir& root) {
#ifdef Q_OS_WIN
    const QString exe_name = QStringLiteral("7zG.exe");
#else
    const QString exe_name = QStringLiteral("7zG");
#endif
    Q_UNUSED(root);
    const QString path = QDir(QCoreApplication::applicationDirPath()).filePath(exe_name);
    return QFileInfo(path).isExecutable() ? path : QString();
}

z7::ui::gui::BridgeTaskPayload make_bridge_payload(const QString& caption) {
    z7::ui::gui::BridgeTaskPayload payload;
    payload.command = z7::ui::gui::BridgeCommandKind::kHash;
    payload.caption = caption;
    payload.hash_method = QStringLiteral("SHA256");
    payload.input_paths = {
        QStringLiteral("/tmp/a.txt"),
        QStringLiteral("/tmp/b.txt"),
    };
    return payload;
}

void seed_dispatch_slot_for_current_worker(
    QSharedMemory* bootstrap_memory,
    int slot_index,
    quint64 session_id,
    quint32 generation,
    const QString& owner_instance_id,
    quint32 payload_size,
    z7::ui::gui::BridgeCommandKind command) {
    using namespace z7::ui::gui::bridge_internal;

    auto* raw = bootstrap_raw(bootstrap_memory);
    QVERIFY(raw != nullptr);
    SharedMemoryLock lock(&raw->lock);
    QVERIFY(lock.ok());
    BridgeSlotRaw& slot = raw->slot_records[slot_index];
    slot = BridgeSlotRaw{};
    slot.generation = generation;
    slot.state = static_cast<quint32>(z7::ui::gui::BridgeSlotState::kDispatched);
    slot.session_id = session_id;
    slot.command_kind = static_cast<quint32>(command);
    slot.launcher_pid = static_cast<qint64>(QCoreApplication::applicationPid());
    slot.worker_pid = static_cast<qint64>(QCoreApplication::applicationPid());
    slot.request_pool_slot = static_cast<quint32>(slot_index);
    slot.request_payload_size = payload_size;
    slot.updated_msecs = now_msecs();
    write_fixed_utf8(owner_instance_id, slot.owner_instance_id, 64);
}

fs::path recycle_bin_dir_for_test(std::error_code& ec) {
    ec.clear();
    fs::path recycle_dir;

#if defined(_WIN32)
    if (const char* profile = std::getenv("USERPROFILE");
        profile != nullptr && profile[0] != '\0') {
      recycle_dir = fs::path(profile) / ".Recycle.Bin";
    } else {
      recycle_dir = fs::temp_directory_path(ec) / ".Recycle.Bin";
      if (ec) {
        return {};
      }
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
      recycle_dir = fs::path(home) / ".Trash";
    } else {
      recycle_dir = fs::temp_directory_path(ec) / ".Trash";
      if (ec) {
        return {};
      }
    }
#else
    fs::path base;
    if (const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
        xdg_data_home != nullptr && xdg_data_home[0] != '\0') {
      base = fs::path(xdg_data_home);
    } else if (const char* home = std::getenv("HOME");
               home != nullptr && home[0] != '\0') {
      base = fs::path(home) / ".local" / "share";
    } else {
      base = fs::temp_directory_path(ec);
      if (ec) {
        return {};
      }
    }
    recycle_dir = base / "Trash" / "files";
#endif

    fs::create_directories(recycle_dir, ec);
    if (ec) {
      return {};
    }

    const fs::path probe = recycle_dir / fs::path(
        ".z7-fm-delete-probe-" + std::to_string(
            static_cast<long long>(
                std::chrono::steady_clock::now().time_since_epoch().count())));
    std::ofstream out(probe, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
      ec = std::make_error_code(std::errc::permission_denied);
      return {};
    }
    out << "probe";
    out.close();

    std::error_code remove_ec;
    fs::remove(probe, remove_ec);
    ec.clear();
    return recycle_dir;
}

QWidgetList dialog_interaction_candidates() {
    QWidgetList candidates;
    if (QWidget* modal = QApplication::activeModalWidget()) {
      candidates << modal;
    }
    candidates << QApplication::topLevelWidgets();
    candidates << QApplication::allWidgets();
    return candidates;
}

bool click_child_button(QDialog* dialog, const QString& object_name) {
    if (dialog == nullptr) {
      return false;
    }
    auto* button = dialog->findChild<QPushButton*>(object_name);
    if (button == nullptr || !button->isEnabled()) {
      return false;
    }
    button->click();
    return true;
}

bool click_dialog_ok(QDialog* dialog) {
    if (dialog == nullptr) {
      return false;
    }
    if (auto* buttons = dialog->findChild<QDialogButtonBox*>()) {
      if (QPushButton* ok_button = buttons->button(QDialogButtonBox::Ok)) {
        ok_button->click();
        return true;
      }
    }
    dialog->accept();
    return true;
}

bool click_message_box_button_text(QDialog* dialog, const QString& expected_text) {
    auto* box = qobject_cast<QMessageBox*>(dialog);
    if (box == nullptr) {
      return false;
    }
    for (QAbstractButton* button : box->buttons()) {
      if (button != nullptr && button->text() == expected_text) {
        button->click();
        return true;
      }
    }
    return false;
}

void reject_named_or_active_dialog(const QString& object_name) {
    QSet<QWidget*> seen;
    for (QWidget* widget : dialog_interaction_candidates()) {
      if (widget == nullptr || seen.contains(widget)) {
        continue;
      }
      seen.insert(widget);
      auto* dialog = qobject_cast<QDialog*>(widget);
      if (dialog == nullptr || !dialog->isVisible()) {
        continue;
      }
      if (dialog->objectName() == object_name) {
        dialog->reject();
        return;
      }
    }

    auto* active_dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    if (active_dialog != nullptr && active_dialog->isVisible()) {
      active_dialog->reject();
    }
}

void schedule_named_dialog_interaction(
    const QString& object_name,
    const std::function<bool(QDialog*)>& handler,
    bool* seen_dialog = nullptr,
    int duration_ms = 3000,
    int interval_ms = 10) {
    if (seen_dialog != nullptr) {
      *seen_dialog = false;
    }
    auto* timer = new QTimer(QApplication::instance());
    timer->setInterval(interval_ms);
    QObject::connect(timer, &QTimer::timeout, [timer, object_name, handler, seen_dialog]() {
      QSet<QWidget*> seen;
      for (QWidget* widget : dialog_interaction_candidates()) {
        if (widget == nullptr || seen.contains(widget)) {
          continue;
        }
        seen.insert(widget);
        auto* dialog = qobject_cast<QDialog*>(widget);
        if (dialog == nullptr || !dialog->isVisible() ||
            dialog->objectName() != object_name) {
          continue;
        }
        if (seen_dialog != nullptr) {
          *seen_dialog = true;
        }
        if (handler(dialog)) {
          timer->stop();
          timer->deleteLater();
        }
        return;
      }
    });
    timer->start();
    QTimer::singleShot(duration_ms, timer, [timer, object_name]() {
      reject_named_or_active_dialog(object_name);
      timer->stop();
      timer->deleteLater();
    });
}

}  // namespace

void FileManagerBehaviorTest::extractAndTestRunnerSupportMultiArchiveAndEntryPaths() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_a = create_sample_archive(root);
    QVERIFY2(!archive_a.isEmpty(), "failed to prepare archive_a");
    const QString archive_b = QDir(root.path()).filePath(QStringLiteral("sample2.7z"));
    QVERIFY(QFile::copy(archive_a, archive_b));

    QTemporaryDir out_root;
    QVERIFY2(out_root.isValid(), "failed to create output temp dir");

    {
      z7::ui::filemanager::ArchiveProcessRunner runner;
      QSignalSpy started_spy(&runner, SIGNAL(started(QString,QString,QStringList)));
      QSignalSpy finished_spy(&runner, SIGNAL(finished(bool,int,int,QString)));

      QVERIFY(runner.start_extract_many(QStringList{archive_a, archive_b},
                                        out_root.path(),
                                        z7::ui::filemanager::OverwriteMode::kOverwrite));
      QTRY_VERIFY_WITH_TIMEOUT(started_spy.count() > 0, 5000);
      const QList<QVariant> started_args = started_spy.takeFirst();
      QCOMPARE(started_args.at(1).toString(), QStringLiteral("Extract"));
      const QStringList targets = started_args.at(2).toStringList();
      QVERIFY(targets.contains(out_root.path()));
      QVERIFY(targets.contains(archive_a));
      QVERIFY(targets.contains(archive_b));
      QTRY_VERIFY_WITH_TIMEOUT(finished_spy.count() > 0, 20000);
    }

    {
      z7::ui::filemanager::ArchiveProcessRunner runner;
      QSignalSpy started_spy(&runner, SIGNAL(started(QString,QString,QStringList)));
      QSignalSpy finished_spy(&runner, SIGNAL(finished(bool,int,int,QString)));

      QVERIFY(runner.start_test_many(QStringList{archive_a, archive_b}));
      QTRY_VERIFY_WITH_TIMEOUT(started_spy.count() > 0, 5000);
      const QList<QVariant> started_args = started_spy.takeFirst();
      QCOMPARE(started_args.at(1).toString(), QStringLiteral("Test"));
      const QStringList targets = started_args.at(2).toStringList();
      QVERIFY(targets.contains(archive_a));
      QVERIFY(targets.contains(archive_b));
      QTRY_VERIFY_WITH_TIMEOUT(finished_spy.count() > 0, 20000);
    }

    {
      z7::ui::filemanager::ArchiveProcessRunner runner;
      QSignalSpy started_spy(&runner, SIGNAL(started(QString,QString,QStringList)));
      QSignalSpy finished_spy(&runner, SIGNAL(finished(bool,int,int,QString)));

      QVERIFY(runner.start_test_entries(archive_a, QStringList{QStringLiteral("sample.txt")}));
      QTRY_VERIFY_WITH_TIMEOUT(started_spy.count() > 0, 5000);
      const QList<QVariant> started_args = started_spy.takeFirst();
      QCOMPARE(started_args.at(1).toString(), QStringLiteral("Test"));
      QCOMPARE(started_args.at(2).toStringList(), QStringList{archive_a});
      QTRY_VERIFY_WITH_TIMEOUT(finished_spy.count() > 0, 20000);
    }
  }

void FileManagerBehaviorTest::extractActionAcceptsTypeHints() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    QTemporaryDir out_star;
    QVERIFY2(out_star.isValid(), "failed to create output temp dir for *");
    {
      z7::ui::filemanager::ArchiveProcessRunner runner;
      QSignalSpy finished_spy(&runner, SIGNAL(finished(bool,int,int,QString)));
      QVERIFY(runner.start_extract(
          archive_path,
          out_star.path(),
          z7::ui::filemanager::OverwriteMode::kOverwrite,
          QStringLiteral("*")));
      QTRY_VERIFY_WITH_TIMEOUT(finished_spy.count() > 0, 20000);
    }

    QTemporaryDir out_parser;
    QVERIFY2(out_parser.isValid(), "failed to create output temp dir for #");
    {
      z7::ui::filemanager::ArchiveProcessRunner runner;
      QSignalSpy finished_spy(&runner, SIGNAL(finished(bool,int,int,QString)));
      QVERIFY(runner.start_extract(
          archive_path,
          out_parser.path(),
          z7::ui::filemanager::OverwriteMode::kOverwrite,
          QStringLiteral("#")));
      QTRY_VERIFY_WITH_TIMEOUT(finished_spy.count() > 0, 20000);
    }
  }

void FileManagerBehaviorTest::benchmarkRunnerExecutesAndReportsOperationName() {
    z7::ui::filemanager::ArchiveProcessRunner runner;
    QSignalSpy started_spy(
        &runner,
        SIGNAL(started(QString,QString,QStringList)));
    QSignalSpy finished_spy(
        &runner,
        SIGNAL(finished(bool,int,int,QString)));

    QVERIFY(runner.start_benchmark(
        1,
        QStringLiteral("1"),
        QStringLiteral("1m"),
        false));

    QTRY_VERIFY_WITH_TIMEOUT(started_spy.count() > 0, 5000);
    const QList<QVariant> started_args = started_spy.takeFirst();
    QCOMPARE(started_args.at(1).toString(), QStringLiteral("Benchmark"));

    QTRY_VERIFY_WITH_TIMEOUT(finished_spy.count() > 0, 60000);
    const QList<QVariant> finished_args = finished_spy.takeLast();
    QVERIFY(finished_args.at(0).toBool());
  }

void FileManagerBehaviorTest::splitAndCombineRunnerExecuteAndReportOperationName() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("split.bin"));
    {
      QFile file(source_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QVERIFY(file.resize(2LL * 1024LL * 1024LL + 17LL));
    }

    const QString split_dir = QDir(root.path()).filePath(QStringLiteral("split_out"));
    const QString combine_dir = QDir(root.path()).filePath(QStringLiteral("combine_out"));
    QVERIFY(QDir().mkpath(split_dir));
    QVERIFY(QDir().mkpath(combine_dir));

    z7::ui::filemanager::ArchiveProcessRunner split_runner;
    QSignalSpy split_started(
        &split_runner,
        SIGNAL(started(QString,QString,QStringList)));
    QSignalSpy split_finished(
        &split_runner,
        SIGNAL(finished(bool,int,int,QString)));

    QVERIFY(split_runner.start_split(source_file, split_dir, QStringLiteral("1M")));
    QTRY_VERIFY_WITH_TIMEOUT(split_started.count() > 0, 5000);
    QCOMPARE(split_started.takeFirst().at(1).toString(), QStringLiteral("Split"));
    QTRY_VERIFY_WITH_TIMEOUT(split_finished.count() > 0, 20000);
    QVERIFY(split_finished.takeLast().at(0).toBool());

    const QString part1 = QDir(split_dir).filePath(QStringLiteral("split.bin.001"));
    QVERIFY(QFileInfo::exists(part1));

    z7::ui::filemanager::ArchiveProcessRunner combine_runner;
    QSignalSpy combine_started(
        &combine_runner,
        SIGNAL(started(QString,QString,QStringList)));
    QSignalSpy combine_finished(
        &combine_runner,
        SIGNAL(finished(bool,int,int,QString)));

    QVERIFY(combine_runner.start_combine(part1, combine_dir));
    QTRY_VERIFY_WITH_TIMEOUT(combine_started.count() > 0, 5000);
    QCOMPARE(combine_started.takeFirst().at(1).toString(), QStringLiteral("Combine"));
    QTRY_VERIFY_WITH_TIMEOUT(combine_finished.count() > 0, 20000);
    QVERIFY(combine_finished.takeLast().at(0).toBool());
    QVERIFY(QFileInfo::exists(QDir(combine_dir).filePath(QStringLiteral("split.bin"))));
  }

void FileManagerBehaviorTest::splitAndCombineFailureDialogsUseSpecificBackendSummaries() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_file = QDir(root.path()).filePath(QStringLiteral("tiny.bin"));
    {
      QFile file(source_file);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("tiny");
    }

    const QString split_dir = QDir(root.path()).filePath(QStringLiteral("split_fail_out"));
    const QString combine_dir = QDir(root.path()).filePath(QStringLiteral("combine_fail_out"));
    QVERIFY(QDir().mkpath(split_dir));
    QVERIFY(QDir().mkpath(combine_dir));

    {
      z7::ui::filemanager::MainWindow window;
      window.set_current_directory(root.path());

      QString warning_text;
      schedule_message_box_capture_and_click(QMessageBox::Ok,
                                             nullptr,
                                             &warning_text,
                                             10000);
      window.start_split_task(source_file, split_dir, QStringLiteral("10M"));
      QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr,
                               20000);
      QVERIFY2(warning_text.contains(
                   QStringLiteral("Split volume size must be smaller")),
               qPrintable(warning_text));
      QVERIFY2(!warning_text.contains(QStringLiteral("Invalid request arguments")),
               qPrintable(warning_text));
    }

    {
      z7::ui::filemanager::MainWindow window;
      window.set_current_directory(root.path());

      QString warning_text;
      schedule_message_box_capture_and_click(QMessageBox::Ok,
                                             nullptr,
                                             &warning_text,
                                             10000);
      window.start_combine_task(source_file, combine_dir);
      QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr,
                               20000);
      QVERIFY2(warning_text.contains(
                   QStringLiteral("Cannot detect split file sequence")),
               qPrintable(warning_text));
      QVERIFY2(!warning_text.contains(QStringLiteral("Invalid request arguments")),
               qPrintable(warning_text));
    }
  }

void FileManagerBehaviorTest::deleteRunnerDefaultsToRecycleBinAndCanBypassIt() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    std::error_code ec;
    const fs::path recycle_dir = recycle_bin_dir_for_test(ec);
    if (ec || recycle_dir.empty()) {
      QSKIP("recycle bin directory is not writable in this test environment");
    }

    const QString recycle_name = QStringLiteral("fm-delete-recycle-%1.txt").arg(
        QString::number(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const QString recycle_source = QDir(root.path()).filePath(recycle_name);
    {
      QFile file(recycle_source);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("normal delete");
    }

    const fs::path recycle_target =
        recycle_dir / fs::path(to_native_path_string(recycle_source)).filename();
    if (fs::exists(recycle_target, ec)) {
      fs::remove_all(recycle_target, ec);
      ec.clear();
    }

    {
      z7::ui::filemanager::ArchiveProcessRunner runner;
      QSignalSpy finished_spy(&runner, SIGNAL(finished(bool,int,int,QString)));
      QVERIFY(runner.start_delete_paths(QStringList{recycle_source}));
      QTRY_VERIFY_WITH_TIMEOUT(finished_spy.count() > 0, 20000);
      QVERIFY(finished_spy.takeLast().at(0).toBool());
    }
    QVERIFY(!QFileInfo::exists(recycle_source));
    QVERIFY2(fs::exists(recycle_target, ec),
             "runner default delete did not move file to recycle bin");
    fs::remove_all(recycle_target, ec);
    ec.clear();

    const QString direct_name = QStringLiteral("fm-delete-direct-%1.txt").arg(
        QString::number(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const QString direct_source = QDir(root.path()).filePath(direct_name);
    {
      QFile file(direct_source);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("shift delete");
    }

    const fs::path direct_target =
        recycle_dir / fs::path(to_native_path_string(direct_source)).filename();
    if (fs::exists(direct_target, ec)) {
      fs::remove_all(direct_target, ec);
      ec.clear();
    }

    {
      z7::ui::filemanager::ArchiveProcessRunner runner;
      QSignalSpy finished_spy(&runner, SIGNAL(finished(bool,int,int,QString)));
      QVERIFY(runner.start_delete_paths(QStringList{direct_source}, false));
      QTRY_VERIFY_WITH_TIMEOUT(finished_spy.count() > 0, 20000);
      QVERIFY(finished_spy.takeLast().at(0).toBool());
    }
    QVERIFY(!QFileInfo::exists(direct_source));
    QVERIFY2(!fs::exists(direct_target, ec),
             "runner direct delete unexpectedly moved file to recycle bin");
  }

void FileManagerBehaviorTest::deleteInFilesystemViewMovesItemToRecycleBin() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    std::error_code ec;
    const fs::path recycle_dir = recycle_bin_dir_for_test(ec);
    if (ec || recycle_dir.empty()) {
      QSKIP("recycle bin directory is not writable in this test environment");
    }

    const QString file_name = QStringLiteral("fm-normal-delete-%1.txt").arg(
        QString::number(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const QString source_path = QDir(root.path()).filePath(file_name);
    {
      QFile file(source_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("normal delete from ui");
    }

    const fs::path recycle_target =
        recycle_dir / fs::path(to_native_path_string(source_path)).filename();
    if (fs::exists(recycle_target, ec)) {
      fs::remove_all(recycle_target, ec);
      ec.clear();
    }

    z7::ui::filemanager::MainWindow window;
    window.confirm_delete_ = false;
    window.set_current_directory(root.path());

    const int row = row_by_name(window, file_name);
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});
    QVERIFY(window.active_panel_controller().ui.details_view != nullptr);

    QTest::keyClick(window.active_panel_controller().ui.details_view, Qt::Key_Delete);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr,
                             20000);

    QVERIFY(!QFileInfo::exists(source_path));
    QVERIFY2(fs::exists(recycle_target, ec),
             "normal Delete did not move file to recycle bin");
    fs::remove_all(recycle_target, ec);
  }

void FileManagerBehaviorTest::shiftDeleteInFilesystemViewBypassesRecycleBin() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    std::error_code ec;
    const fs::path recycle_dir = recycle_bin_dir_for_test(ec);
    if (ec || recycle_dir.empty()) {
      QSKIP("recycle bin directory is not writable in this test environment");
    }

    const QString file_name = QStringLiteral("fm-shift-delete-%1.txt").arg(
        QString::number(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    const QString source_path = QDir(root.path()).filePath(file_name);
    {
      QFile file(source_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("shift delete from ui");
    }

    const fs::path recycle_target =
        recycle_dir / fs::path(to_native_path_string(source_path)).filename();
    if (fs::exists(recycle_target, ec)) {
      fs::remove_all(recycle_target, ec);
      ec.clear();
    }

    z7::ui::filemanager::MainWindow window;
    window.confirm_delete_ = false;
    window.set_current_directory(root.path());

    const int row = row_by_name(window, file_name);
    QVERIFY(row >= 0);
    select_rows_in_active_panel(&window, {row});
    QVERIFY(window.active_panel_controller().ui.details_view != nullptr);

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_Delete,
                    Qt::ShiftModifier);
    QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr,
                             20000);

    QVERIFY(!QFileInfo::exists(source_path));
    QVERIFY2(!fs::exists(recycle_target, ec),
             "Shift+Delete unexpectedly moved file to recycle bin");
  }

void FileManagerBehaviorTest::progressAndCancelSignalsAreVisible() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    QStringList files;
    for (int i = 0; i < 200; ++i) {
      const QString file_path =
          QDir(root.path()).filePath(QStringLiteral("f_%1.txt").arg(i));
      QFile file(file_path);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QByteArray payload(8192, static_cast<char>('A' + (i % 26)));
      file.write(payload);
      file.close();
      files << file_path;
    }

    z7::ui::filemanager::ArchiveProcessRunner runner;
    QSignalSpy log_spy(&runner, SIGNAL(log_line(QString)));
    QSignalSpy stage_spy(&runner, SIGNAL(stage_changed(QString)));
    QSignalSpy progress_spy(&runner, SIGNAL(progress_changed(int)));
    QSignalSpy finished_spy(&runner, SIGNAL(finished(bool,int,int,QString)));

    QVERIFY(runner.start_hash(files, QStringLiteral("SHA256")));
    QTest::qWait(30);
    runner.cancel();

    QTRY_VERIFY_WITH_TIMEOUT(finished_spy.count() > 0, 20000);
    QVERIFY(progress_spy.count() > 0);
    QVERIFY(log_spy.count() > 0);
    QVERIFY(stage_spy.count() > 0);
    QVERIFY(spy_contains_stage(stage_spy, QStringLiteral("Preparing")));
    QVERIFY(spy_contains_stage(stage_spy, QStringLiteral("Running")));
    QVERIFY(spy_contains_stage(stage_spy, QStringLiteral("Completed")));
  }

void FileManagerBehaviorTest::hashRunnerPauseResumeAndFinish() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString large_file = QDir(root.path()).filePath(QStringLiteral("large_sparse.bin"));
    QFile file(large_file);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(file.resize(128LL * 1024LL * 1024LL));
    file.close();

    z7::ui::filemanager::ArchiveProcessRunner runner;
    QSignalSpy stage_spy(&runner, SIGNAL(stage_changed(QString)));
    QSignalSpy progress_spy(
        &runner,
        &z7::ui::filemanager::ArchiveProcessRunner::detailed_progress_changed);
    QSignalSpy finished_spy(&runner, SIGNAL(finished(bool,int,int,QString)));

    QVERIFY(runner.start_hash(QStringList{large_file}, QStringLiteral("SHA256")));

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000 &&
           max_completed_bytes_from_progress(progress_spy) == 0 &&
           finished_spy.count() == 0) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QTest::qWait(20);
    }

    if (finished_spy.count() > 0) {
      QSKIP("hash finished too fast to reliably assert pause/resume");
    }

    const quint64 before_pause = max_completed_bytes_from_progress(progress_spy);
    QVERIFY(before_pause > 0);
    runner.pause();
    QTest::qWait(120);
    const quint64 paused_value = max_completed_bytes_from_progress(progress_spy);
    QVERIFY(paused_value <= before_pause + (8ULL << 20));

    runner.resume();
    QElapsedTimer resume_timer;
    resume_timer.start();
    while (resume_timer.elapsed() < 3000 &&
           max_completed_bytes_from_progress(progress_spy) <= paused_value + (8ULL << 20) &&
           finished_spy.count() == 0) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QTest::qWait(20);
    }
    QVERIFY(max_completed_bytes_from_progress(progress_spy) > paused_value ||
            finished_spy.count() > 0);

    QTRY_VERIFY_WITH_TIMEOUT(finished_spy.count() > 0, 8000);
    const QList<QVariant> finished_args = finished_spy.takeLast();
    QVERIFY(finished_args.at(0).toBool());
    QVERIFY(spy_contains_stage(stage_spy, QStringLiteral("Preparing")));
    QVERIFY(spy_contains_stage(stage_spy, QStringLiteral("Running")));
    QVERIFY(spy_contains_stage(stage_spy, QStringLiteral("Completed")));
  }

void FileManagerBehaviorTest::hashProgressDialogPauseBackgroundCancelButtonsAreDeterministic() {
    z7::ui::filemanager::HashProgressDialog dialog;
    dialog.set_operation_name(QStringLiteral("SHA256"));
    dialog.set_stage(QStringLiteral("Running"));
    dialog.set_progress(true,
                        4096,
                        1024,
                        4,
                        1,
                        0,
                        QStringLiteral("/tmp/source.bin"));
    dialog.set_running(true);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* background_button =
        dialog.findChild<QPushButton*>(QStringLiteral("hashProgressBackgroundButton"));
    auto* pause_button =
        dialog.findChild<QPushButton*>(QStringLiteral("hashProgressPauseButton"));
    auto* cancel_button =
        dialog.findChild<QPushButton*>(QStringLiteral("hashProgressCancelButton"));
    QVERIFY(background_button != nullptr);
    QVERIFY(pause_button != nullptr);
    QVERIFY(cancel_button != nullptr);
    QVERIFY(background_button->isEnabled());
    QVERIFY(pause_button->isEnabled());
    QVERIFY(cancel_button->isEnabled());

    QSignalSpy background_spy(&dialog, SIGNAL(background_requested(bool)));
    QSignalSpy pause_spy(&dialog, SIGNAL(pause_requested()));
    QSignalSpy resume_spy(&dialog, SIGNAL(resume_requested()));
    QSignalSpy cancel_spy(&dialog, SIGNAL(cancel_requested()));

    pause_button->click();
    QVERIFY(dialog.is_paused());
    QCOMPARE(pause_spy.count(), 1);
    QCOMPARE(resume_spy.count(), 0);
    pause_button->click();
    QVERIFY(!dialog.is_paused());
    QCOMPARE(resume_spy.count(), 1);

    background_button->click();
    QVERIFY(dialog.is_backgrounded());
    QCOMPARE(background_spy.count(), 1);
    QVERIFY(background_spy.takeFirst().at(0).toBool());
    background_button->click();
    QVERIFY(!dialog.is_backgrounded());
    QCOMPARE(background_spy.count(), 1);
    QVERIFY(!background_spy.takeFirst().at(0).toBool());

    cancel_button->click();
    QCOMPARE(cancel_spy.count(), 1);

    dialog.set_running(false);
    QVERIFY(!background_button->isEnabled());
    QVERIFY(!pause_button->isEnabled());
    QVERIFY(!cancel_button->isEnabled());
    dialog.close();
  }

void FileManagerBehaviorTest::guiPromptDialogsCanRunConsecutiveScriptedInteractions() {
    clear_runtime_settings();

    bool saw_password_dialog = false;
    schedule_named_dialog_interaction(
        QStringLiteral("passwordPromptDialog"),
        [](QDialog* dialog) {
          auto* edit = dialog->findChild<QLineEdit*>(QStringLiteral("passwordPromptEdit"));
          if (edit == nullptr) {
            return false;
          }
          edit->setText(QStringLiteral("secret"));
          return click_dialog_ok(dialog);
        },
        &saw_password_dialog);
    z7::app::PasswordPrompt password_prompt;
    password_prompt.archive_path = "/tmp/scripted.7z";
    password_prompt.reason_kind = z7::app::PasswordPromptReason::kWrongPassword;
    const z7::app::PasswordReply password_reply =
        z7::ui::gui::show_password_prompt_dialog(nullptr, password_prompt);
    QVERIFY(saw_password_dialog);
    QVERIFY(password_reply.kind == z7::app::PasswordReplyKind::kProvide);
    QCOMPARE(QString::fromStdString(password_reply.password), QStringLiteral("secret"));

    bool saw_overwrite_dialog = false;
    schedule_overwrite_prompt_submit(OverwritePromptChoice::kAutoRename,
                                     &saw_overwrite_dialog);
    z7::app::OverwritePrompt overwrite_prompt;
    overwrite_prompt.existing_path = "/tmp/existing.txt";
    overwrite_prompt.incoming_path = "/tmp/incoming.txt";
    overwrite_prompt.existing_size_defined = true;
    overwrite_prompt.existing_size = 12;
    overwrite_prompt.incoming_size_defined = true;
    overwrite_prompt.incoming_size = 34;
    const z7::app::OverwriteDecision overwrite_reply =
        z7::ui::gui::show_overwrite_prompt_dialog(nullptr, overwrite_prompt);
    QVERIFY(saw_overwrite_dialog);
    QVERIFY(overwrite_reply == z7::app::OverwriteDecision::kAutoRename);

    bool saw_choice_dialog = false;
    schedule_named_dialog_interaction(
        QStringLiteral("choicePromptDialog"),
        [](QDialog* dialog) {
          return click_message_box_button_text(dialog, QStringLiteral("second"));
        },
        &saw_choice_dialog);
    z7::app::ChoicePrompt choice_prompt;
    choice_prompt.title = "Scripted choice";
    choice_prompt.message = "Pick one.";
    choice_prompt.choices = {"first", "second"};
    choice_prompt.default_index = 1;
    const z7::app::ChoiceReply choice_reply =
        z7::ui::gui::show_choice_prompt_dialog(nullptr, choice_prompt);
    QVERIFY(saw_choice_dialog);
    QVERIFY(choice_reply.kind == z7::app::ChoiceReplyKind::kSelect);
    QCOMPARE(choice_reply.selected_index, 1);

    bool saw_memory_dialog = false;
    schedule_named_dialog_interaction(
        QStringLiteral("memoryLimitPromptDialog"),
        [](QDialog* dialog) {
          return click_child_button(dialog,
                                    QStringLiteral("memoryLimitUpdateLimitButton"));
        },
        &saw_memory_dialog);
    schedule_input_dialog_submit(QStringLiteral("256"));
    z7::app::MemoryLimitPrompt memory_prompt;
    memory_prompt.required_usage_bytes = 128ULL * 1024ULL * 1024ULL;
    memory_prompt.current_limit_defined = true;
    memory_prompt.current_limit_bytes = 64ULL * 1024ULL * 1024ULL;
    memory_prompt.archive_path = "/tmp/scripted.7z";
    memory_prompt.file_path = "large.bin";
    memory_prompt.test_mode = true;
    memory_prompt.skip_archive_supported = true;
    const z7::app::MemoryLimitReply memory_reply =
        z7::ui::gui::show_memory_limit_prompt_dialog(nullptr, memory_prompt);
    QVERIFY(saw_memory_dialog);
    QVERIFY(memory_reply.action ==
            z7::app::MemoryLimitAction::kUpdateLimitAndContinue);
    QCOMPARE(memory_reply.updated_limit_bytes, 256ULL * 1024ULL * 1024ULL);

    clear_runtime_settings();
  }

void FileManagerBehaviorTest::bridgeSharedMemoryNativeKeysMatchPlatformConventions() {
#ifdef Q_OS_WIN
    const QString expected_bootstrap = QStringLiteral("Local\\z7.bridge.bootstrap.v1");
    const QString expected_request_pool = QStringLiteral("Local\\z7.bridge.reqpool.v1");
#elif defined(Q_OS_MACOS)
    const QString expected_bootstrap;
    const QString expected_request_pool;
#else
    const QString expected_bootstrap = QStringLiteral("/z7.bridge.bootstrap.v1");
    const QString expected_request_pool = QStringLiteral("/z7.bridge.reqpool.v1");
#endif

    QCOMPARE(z7::ui::gui::bridge_bootstrap_key(), expected_bootstrap);
    QCOMPARE(z7::ui::gui::bridge_request_pool_key(), expected_request_pool);

#if defined(Q_OS_MACOS)
    return;
#endif

    QSharedMemory bootstrap_memory;
    bootstrap_memory.setNativeKey(z7::ui::gui::bridge_bootstrap_key(),
#ifdef Q_OS_WIN
                                  QNativeIpcKey::Type::Windows);
#else
                                  QNativeIpcKey::Type::PosixRealtime);
#endif
    QCOMPARE(bootstrap_memory.nativeKey(), expected_bootstrap);

    QSharedMemory request_pool_memory;
    request_pool_memory.setNativeKey(z7::ui::gui::bridge_request_pool_key(),
#ifdef Q_OS_WIN
                                     QNativeIpcKey::Type::Windows);
#else
                                     QNativeIpcKey::Type::PosixRealtime);
#endif
    QCOMPARE(request_pool_memory.nativeKey(), expected_request_pool);
  }

void FileManagerBehaviorTest::bridgeExistingSharedMemorySegmentsAttachWithoutCreatePermission() {
    using namespace z7::ui::gui;
    namespace bridge = z7::ui::gui::bridge_internal;

    reset_bridge_segments_for_test();
    QString bridge_error;
    QVERIFY2(ensure_bridge_bootstrap_ready(&bridge_error), qPrintable(bridge_error));

#if defined(Q_OS_MACOS)
    std::shared_ptr<QSharedMemory> mac_bootstrap_memory;
    QVERIFY(!bridge::open_bootstrap_memory(false, &mac_bootstrap_memory, &bridge_error));
    QVERIFY(bridge_error.contains(QStringLiteral("not used on macOS")));
    std::shared_ptr<QSharedMemory> mac_request_pool_memory;
    QVERIFY(!bridge::open_request_pool_memory(false, &mac_request_pool_memory, &bridge_error));
    QVERIFY(bridge_error.contains(QStringLiteral("not used on macOS")));
    return;
#endif

    z7::task_ipc_runtime::task_ipc_internal::update_bootstrap_memory_lease({});
    z7::task_ipc_runtime::task_ipc_internal::update_request_pool_memory_lease({});

    std::shared_ptr<QSharedMemory> bootstrap_memory;
    QVERIFY2(bridge::open_bootstrap_memory(false, &bootstrap_memory, &bridge_error),
             qPrintable(bridge_error));
    QVERIFY(bootstrap_memory != nullptr);
    QVERIFY(bootstrap_memory->isAttached());
    const auto* bootstrap = bridge::bootstrap_raw(bootstrap_memory.get());
    QVERIFY(bootstrap != nullptr);
    QCOMPARE(bootstrap->magic, bridge::kBridgeMagic);
    QCOMPARE(bootstrap->version, bridge::kBridgeVersion);
    QCOMPARE(bootstrap->slot_count, static_cast<quint16>(bridge::kBridgeSlotCount));

    std::shared_ptr<QSharedMemory> request_pool_memory;
    QVERIFY2(bridge::open_request_pool_memory(false, &request_pool_memory, &bridge_error),
             qPrintable(bridge_error));
    QVERIFY(request_pool_memory != nullptr);
    QVERIFY(request_pool_memory->isAttached());
    const auto* request_pool = bridge::request_pool_raw(request_pool_memory.get());
    QVERIFY(request_pool != nullptr);
    QCOMPARE(request_pool->magic, bridge::kBridgeRequestPoolMagic);
    QCOMPARE(request_pool->version, bridge::kBridgeRequestPoolVersion);
    QCOMPARE(request_pool->slot_count, static_cast<quint16>(bridge::kBridgeSlotCount));
  }

void FileManagerBehaviorTest::bridgeDispatchWritesPayloadIntoFixedRequestPoolSlot() {
    using namespace z7::ui::gui;

    reset_bridge_segments_for_test();
    QString bootstrap_error;
    QVERIFY2(ensure_bridge_bootstrap_ready(&bootstrap_error), qPrintable(bootstrap_error));

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");
    const QString worker_program = create_bridge_worker_stub(root);
    QVERIFY2(!worker_program.isEmpty(), "failed to create worker stub");

    const BridgeTaskPayload payload = make_bridge_payload(QStringLiteral("dispatch-slot"));
    BridgeDispatchResult dispatch_result;
    QString dispatch_error;
    QVERIFY2(dispatch_bridge_task(worker_program,
                                  QDir::tempPath(),
                                  QStringLiteral("dispatch-owner"),
                                  payload,
                                  &dispatch_result,
                                  &dispatch_error),
             qPrintable(dispatch_error));
    QVERIFY(dispatch_result.session_id != 0);
    QVERIFY(dispatch_result.worker_pid > 0);

#if defined(Q_OS_MACOS)
    const auto mappings =
        z7::task_ipc_runtime::task_ipc_internal::posix_task_mappings_snapshot();
    std::shared_ptr<z7::task_ipc_runtime::task_ipc_internal::PosixTaskIpcMapping>
        dispatch_mapping;
    for (const auto& mapping : mappings) {
      if (mapping != nullptr && mapping->raw() != nullptr &&
          mapping->raw()->slot.session_id == dispatch_result.session_id &&
          mapping->raw()->slot.generation == dispatch_result.generation) {
        dispatch_mapping = mapping;
        break;
      }
    }
    QVERIFY(dispatch_mapping != nullptr);
    z7::task_ipc_runtime::set_task_ipc_worker_endpoint(dispatch_mapping->shm_name(),
                                                       dispatch_mapping->sem_name());

    BridgeClaimedTask claimed_task;
    QString claim_error;
    QVERIFY2(claim_bridge_task_for_worker(dispatch_result.session_id,
                                          dispatch_result.generation,
                                          &claimed_task,
                                          &claim_error),
             qPrintable(claim_error));
    QCOMPARE(claimed_task.payload.command, payload.command);
    QCOMPARE(claimed_task.payload.caption, payload.caption);
    QCOMPARE(claimed_task.payload.hash_method, payload.hash_method);
    QCOMPARE(claimed_task.payload.input_paths, payload.input_paths);
    QVERIFY(!claimed_task.ipc_shm_name.trimmed().isEmpty());
    QVERIFY(!claimed_task.ipc_sem_name.trimmed().isEmpty());

    QString release_error;
    QVERIFY2(release_bridge_task_slot(claimed_task, &release_error),
             qPrintable(release_error));
    return;
#else
    namespace bridge = z7::ui::gui::bridge_internal;

    QSharedMemory bootstrap_memory;
    QString attach_error;
    QVERIFY2(attach_shared_memory_for_test(bridge_bootstrap_key(),
                                           &bootstrap_memory,
                                           &attach_error),
             qPrintable(attach_error));
    QSharedMemory request_pool_memory;
    QVERIFY2(attach_shared_memory_for_test(bridge_request_pool_key(),
                                           &request_pool_memory,
                                           &attach_error),
             qPrintable(attach_error));

    int slot_index = -1;
    quint32 payload_size = 0;
    {
      auto* raw = bridge::bootstrap_raw(&bootstrap_memory);
      QVERIFY(raw != nullptr);
      bridge::SharedMemoryLock lock(&raw->lock);
      QVERIFY(lock.ok());
      slot_index = find_slot_index_for_session(*raw, dispatch_result.session_id);
      QVERIFY(slot_index >= 0);

      const bridge::BridgeSlotRaw& slot = raw->slot_records[slot_index];
      QCOMPARE(slot.request_pool_slot, static_cast<quint32>(slot_index));
      QVERIFY(slot.request_payload_size > 0);
      QVERIFY(slot.request_payload_size <= static_cast<quint32>(bridge::kBridgeRequestPoolSlotSize));
      payload_size = slot.request_payload_size;
    }

    BridgeTaskPayload decoded_payload;
    QString read_error;
    auto* request_pool = bridge::request_pool_raw(&request_pool_memory);
    QVERIFY(request_pool != nullptr);
    bridge::SharedMemoryLock slot_lock(
        bridge::request_pool_slot_lock(request_pool, slot_index));
    QVERIFY(slot_lock.ok());
    QVERIFY2(bridge::read_request_payload_from_slot(&request_pool_memory,
                                                    slot_index,
                                                    payload_size,
                                                    &decoded_payload,
                                                    &read_error),
             qPrintable(read_error));
    QCOMPARE(decoded_payload.command, payload.command);
    QCOMPARE(decoded_payload.caption, payload.caption);
    QCOMPARE(decoded_payload.hash_method, payload.hash_method);
    QCOMPARE(decoded_payload.input_paths, payload.input_paths);

    {
      auto* raw = bridge::bootstrap_raw(&bootstrap_memory);
      QVERIFY(raw != nullptr);
      bridge::SharedMemoryLock lock(&raw->lock);
      QVERIFY(lock.ok());
      bridge::clear_slot(&raw->slot_records[slot_index], true);
    }
#endif
  }

void FileManagerBehaviorTest::bridgeClaimReadsPayloadFromFixedRequestPoolSlot() {
    using namespace z7::ui::gui;
    namespace bridge = z7::ui::gui::bridge_internal;

    reset_bridge_segments_for_test();
    QString bootstrap_error;
    QVERIFY2(ensure_bridge_bootstrap_ready(&bootstrap_error), qPrintable(bootstrap_error));

#if defined(Q_OS_MACOS)
    QSKIP("Raw request-pool slot seeding is non-macOS-only; macOS dispatch/claim is covered by bridgeDispatchWritesPayloadIntoFixedRequestPoolSlot.");
#endif

    std::shared_ptr<QSharedMemory> bootstrap_memory;
    QVERIFY2(bridge::open_bootstrap_memory(true, &bootstrap_memory, &bootstrap_error),
             qPrintable(bootstrap_error));
    std::shared_ptr<QSharedMemory> request_pool_memory;
    QVERIFY2(bridge::open_request_pool_memory(true, &request_pool_memory, &bootstrap_error),
             qPrintable(bootstrap_error));

    const BridgeTaskPayload source_payload = make_bridge_payload(QStringLiteral("claim-slot"));
    QString encode_error;
    const QByteArray encoded_payload =
        bridge::serialize_task_payload(source_payload, &encode_error);
    QVERIFY2(!encoded_payload.isEmpty(), qPrintable(encode_error));
    {
      auto* request_pool = bridge::request_pool_raw(request_pool_memory.get());
      QVERIFY(request_pool != nullptr);
      bridge::SharedMemoryLock slot_lock(
          bridge::request_pool_slot_lock(request_pool, 0));
      QVERIFY(slot_lock.ok());
      QVERIFY2(bridge::write_request_payload_to_slot(request_pool_memory.get(),
                                                     0,
                                                     encoded_payload,
                                                     &encode_error),
               qPrintable(encode_error));
    }

    seed_dispatch_slot_for_current_worker(bootstrap_memory.get(),
                                          0,
                                          41,
                                          7,
                                          QStringLiteral("claim-owner"),
                                          static_cast<quint32>(encoded_payload.size()),
                                          source_payload.command);

    BridgeClaimedTask claimed_task;
    QString claim_error;
    QVERIFY2(claim_bridge_task_for_worker(41, 7, &claimed_task, &claim_error),
             qPrintable(claim_error));
    QCOMPARE(claimed_task.slot_index, 0);
    QCOMPARE(claimed_task.session_id, 41ULL);
    QCOMPARE(claimed_task.generation, 7U);
    QCOMPARE(claimed_task.payload.command, source_payload.command);
    QCOMPARE(claimed_task.payload.caption, source_payload.caption);
    QCOMPARE(claimed_task.payload.hash_method, source_payload.hash_method);
    QCOMPARE(claimed_task.payload.input_paths, source_payload.input_paths);

    QString release_error;
    QVERIFY2(release_bridge_task_slot(claimed_task, &release_error),
             qPrintable(release_error));
  }

void FileManagerBehaviorTest::bridgeDispatchRejectsPayloadLargerThanFixedRequestPoolSlot() {
    using namespace z7::ui::gui;
    namespace bridge = z7::ui::gui::bridge_internal;

    reset_bridge_segments_for_test();
    QString bootstrap_error;
    QVERIFY2(ensure_bridge_bootstrap_ready(&bootstrap_error), qPrintable(bootstrap_error));

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");
    const QString worker_program = create_bridge_worker_stub(root);
    QVERIFY2(!worker_program.isEmpty(), "failed to create worker stub");

    BridgeTaskPayload payload = make_bridge_payload(QStringLiteral("too-large"));
    payload.caption = QString(bridge::kBridgeRequestPoolSlotSize + 4096, QLatin1Char('X'));

    BridgeDispatchResult dispatch_result;
    QString dispatch_error;
    QVERIFY(!dispatch_bridge_task(worker_program,
                                  QDir::tempPath(),
                                  QStringLiteral("oversized-owner"),
                                  payload,
                                  &dispatch_result,
                                  &dispatch_error));
    QVERIFY(dispatch_error.contains(QStringLiteral("fixed request-pool slot capacity")));
  }

void FileManagerBehaviorTest::bridgeReusedSlotOverwritesPriorRequestPoolPayload() {
    using namespace z7::ui::gui;
    namespace bridge = z7::ui::gui::bridge_internal;

    reset_bridge_segments_for_test();
    QString bootstrap_error;
    QVERIFY2(ensure_bridge_bootstrap_ready(&bootstrap_error), qPrintable(bootstrap_error));

#if defined(Q_OS_MACOS)
    QSKIP("Request-pool slot reuse is non-macOS-only; macOS uses per-task POSIX mappings.");
#endif

    std::shared_ptr<QSharedMemory> bootstrap_memory;
    QVERIFY2(bridge::open_bootstrap_memory(true, &bootstrap_memory, &bootstrap_error),
             qPrintable(bootstrap_error));
    std::shared_ptr<QSharedMemory> request_pool_memory;
    QVERIFY2(bridge::open_request_pool_memory(true, &request_pool_memory, &bootstrap_error),
             qPrintable(bootstrap_error));

    const BridgeTaskPayload first_payload = make_bridge_payload(QStringLiteral("slot-A"));
    QString encode_error;
    const QByteArray first_encoded =
        bridge::serialize_task_payload(first_payload, &encode_error);
    QVERIFY2(!first_encoded.isEmpty(), qPrintable(encode_error));
    {
      auto* request_pool = bridge::request_pool_raw(request_pool_memory.get());
      QVERIFY(request_pool != nullptr);
      bridge::SharedMemoryLock first_slot_lock(
          bridge::request_pool_slot_lock(request_pool, 0));
      QVERIFY(first_slot_lock.ok());
      QVERIFY2(bridge::write_request_payload_to_slot(request_pool_memory.get(),
                                                     0,
                                                     first_encoded,
                                                     &encode_error),
               qPrintable(encode_error));
    }
    seed_dispatch_slot_for_current_worker(bootstrap_memory.get(),
                                          0,
                                          71,
                                          3,
                                          QStringLiteral("reuse-owner"),
                                          static_cast<quint32>(first_encoded.size()),
                                          first_payload.command);

    BridgeClaimedTask first_claim;
    QString claim_error;
    QVERIFY2(claim_bridge_task_for_worker(71, 3, &first_claim, &claim_error),
             qPrintable(claim_error));
    QCOMPARE(first_claim.payload.caption, first_payload.caption);
    QString release_error;
    QVERIFY2(release_bridge_task_slot(first_claim, &release_error),
             qPrintable(release_error));

    const BridgeTaskPayload second_payload = make_bridge_payload(QStringLiteral("slot-B"));
    const QByteArray second_encoded =
        bridge::serialize_task_payload(second_payload, &encode_error);
    QVERIFY2(!second_encoded.isEmpty(), qPrintable(encode_error));
    {
      auto* request_pool = bridge::request_pool_raw(request_pool_memory.get());
      QVERIFY(request_pool != nullptr);
      bridge::SharedMemoryLock second_slot_lock(
          bridge::request_pool_slot_lock(request_pool, 0));
      QVERIFY(second_slot_lock.ok());
      QVERIFY2(bridge::write_request_payload_to_slot(request_pool_memory.get(),
                                                     0,
                                                     second_encoded,
                                                     &encode_error),
               qPrintable(encode_error));
    }
    seed_dispatch_slot_for_current_worker(bootstrap_memory.get(),
                                          0,
                                          72,
                                          4,
                                          QStringLiteral("reuse-owner"),
                                          static_cast<quint32>(second_encoded.size()),
                                          second_payload.command);

    BridgeClaimedTask second_claim;
    QVERIFY2(claim_bridge_task_for_worker(72, 4, &second_claim, &claim_error),
             qPrintable(claim_error));
    QCOMPARE(second_claim.payload.caption, second_payload.caption);
    QVERIFY(second_claim.payload.caption != first_payload.caption);
    QVERIFY2(release_bridge_task_slot(second_claim, &release_error),
             qPrintable(release_error));
  }


// End of runner_task.cpp
