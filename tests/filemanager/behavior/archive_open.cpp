// tests/filemanager/behavior/archive_open.cpp
// Role: Archive open behavior cases.

#include "internal.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <vector>

using namespace filemanager_behavior_internal;

namespace {

class ScopedEnvVar final {
 public:
  ScopedEnvVar(const QByteArray& key, const QByteArray& value)
      : key_(key),
        old_value_(qgetenv(key.constData())) {
    qputenv(key_.constData(), value);
  }

  ~ScopedEnvVar() {
    if (old_value_.isNull()) {
      qunsetenv(key_.constData());
    } else {
      qputenv(key_.constData(), old_value_);
    }
  }

 private:
  QByteArray key_;
  QByteArray old_value_;
};

QString fake_launcher_dir() {
  return QStringLiteral(Z7_FAKE_LAUNCHER_BIN_DIR);
}

QByteArray prepend_to_path(const QString& dir_path) {
  const QByteArray encoded_dir = QFile::encodeName(dir_path);
  const QByteArray current_path = qgetenv("PATH");
#ifdef Q_OS_WIN
  const QByteArray separator(";");
#else
  const QByteArray separator(":");
#endif
  if (current_path.isEmpty()) {
    return encoded_dir;
  }
  return encoded_dir + separator + current_path;
}

QJsonObject read_tracker_log(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return QJsonObject{};
  }

  QJsonParseError error{};
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return QJsonObject{};
  }
  return document.object();
}

QString first_tracked_sample_path(const QJsonObject& log) {
  const QJsonArray args = log.value(QStringLiteral("args")).toArray();
  for (const QJsonValue& value : args) {
    const QString arg = value.toString();
    if (QFileInfo(arg).fileName() == QStringLiteral("sample.txt")) {
      return arg;
    }
  }
  return QString();
}

QStringList archive_temp_dirs_under_system_root(const QString& prefix) {
  QDir root(QDir::tempPath());
  return root.entryList(QStringList{prefix + QStringLiteral("*")},
                        QDir::Dirs | QDir::NoDotAndDotDot,
                        QDir::Name);
}

QString extract_archive_entry_text(const QString& archive_path,
                                   const QString& entry_name,
                                   const QString& output_root) {
  z7::app::ExtractRequest request;
  request.archive_path = to_native_path_string(archive_path);
  request.output_dir = to_native_path_string(output_root);
  request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
  request.entries = std::vector<std::string>{to_native_path_string(entry_name)};

  z7::app::ArchiveRequest archive_request;
  archive_request.payload = request;
  const z7::app::OperationOutcome outcome =
      run_archive_request_and_await(archive_request);
  z7::app::ExtractResult result;
  if (const std::optional<z7::app::ExtractResult> typed =
          z7::app::outcome_payload_as<z7::app::ExtractResult>(outcome);
      typed.has_value()) {
    result = *typed;
  } else {
    result.ok = outcome.ok;
    result.error = outcome.error;
    result.native_exit_code = outcome.native_code;
    result.native_execution = outcome.native_execution;
    result.summary = outcome.summary;
  }
  if (!result.ok) {
    return QString();
  }

  QFile output_file(QDir(output_root).filePath(entry_name));
  if (!output_file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  return QString::fromUtf8(output_file.readAll());
}

}  // namespace

void FileManagerBehaviorTest::openFromArchiveViewFileShowsErrorWhenNotArchive() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  // Use a .dat extension: not in always_start_externally, not a valid archive,
  // so on_open_requested() must go through open_archive_file_inside_for_panel
  // and fail — old code fell back to external, new code shows an error dialog.
  const QString data_file = QDir(root.path()).filePath(QStringLiteral("payload.dat"));
  {
    QFile f(data_file);
    QVERIFY2(f.open(QIODevice::WriteOnly), "failed to create payload.dat");
    f.write("not an archive\n");
  }
  const QString archive_path =
      QDir(root.path()).filePath(QStringLiteral("test.7z"));
  QString error;
  QVERIFY2(create_archive_via_backend(root.path(),
                                      archive_path,
                                      QStringList{QStringLiteral("payload.dat")},
                                      &error),
           qPrintable(QStringLiteral("failed to create archive: %1").arg(error)));

  z7::ui::filemanager::MainWindow window;
  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QVERIFY(window.in_archive_view());

  const int file_row = row_by_name(window, QStringLiteral("payload.dat"));
  QVERIFY(file_row >= 0);
  select_rows_in_active_panel(&window, {file_row});

  int external_open_count = 0;
  window.external_opener_ = [&external_open_count](const QString&) {
    ++external_open_count;
    return true;
  };

  schedule_message_box_autoclose();
  window.on_open_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  close_message_boxes();

  QCOMPARE(external_open_count, 0);
  QVERIFY(window.in_archive_view());
  QCOMPARE(window.active_panel_controller().archive.virtual_dir, QString());
}

void FileManagerBehaviorTest::openInsideFromArchiveViewFileDoesNotFallbackToExternalLaunch() {
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
  select_rows_in_active_panel(&window, {file_row});

  int external_open_count = 0;
  window.external_opener_ = [&external_open_count](const QString&) {
    ++external_open_count;
    return true;
  };

  schedule_message_box_autoclose();
  window.on_open_inside_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  close_message_boxes();

  QCOMPARE(external_open_count, 0);
  QVERIFY(window.in_archive_view());
  QCOMPARE(window.active_panel_controller().archive.virtual_dir, QString());
}

void FileManagerBehaviorTest::openFromArchiveViewUsesSystemTempForTemporaryExtraction() {
  clear_runtime_settings();
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");
  QTemporaryDir working_root;
  QVERIFY2(working_root.isValid(), "failed to create configured working dir");

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
  select_rows_in_active_panel(&window, {file_row});

  const QString tracker_log_path =
      QDir(root.path()).filePath(QStringLiteral("open-temp-log.json"));
  ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                        prepend_to_path(fake_launcher_dir()));
  ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                       tracker_log_path.toUtf8());
  ScopedEnvVar fail_mode_env(QByteArrayLiteral("Z7_FAKE_TRACKER_FAIL_MODE"),
                             QByteArrayLiteral("normal"));
  ScopedEnvVar sleep_env(QByteArrayLiteral("Z7_FAKE_TRACKER_SLEEP_MS"),
                         QByteArrayLiteral("0"));

  QVERIFY(QFile::remove(archive_path));
  QVERIFY(!QFileInfo::exists(archive_path));

  window.on_open_outside_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QJsonObject tracker_log;
  QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                            !tracker_log.isEmpty()),
                           5000);

  const QString tracked_path = first_tracked_sample_path(tracker_log);
  QVERIFY(!tracked_path.isEmpty());
  const QString extracted_path =
      QDir::cleanPath(QDir::fromNativeSeparators(tracked_path));
  const QString configured_root =
      QDir::cleanPath(QDir::fromNativeSeparators(working_root.path()));
  const QString system_temp_root =
      QDir::cleanPath(QDir::fromNativeSeparators(QDir::tempPath()));
  QVERIFY2(!(extracted_path.startsWith(configured_root + QLatin1Char('/')) ||
             extracted_path.startsWith(configured_root + QLatin1Char('\\'))),
           qPrintable(QStringLiteral("extracted path '%1' unexpectedly under configured working root '%2'")
                          .arg(extracted_path, configured_root)));
  QVERIFY2(extracted_path.startsWith(system_temp_root + QLatin1Char('/')) ||
               extracted_path.startsWith(system_temp_root + QLatin1Char('\\')),
           qPrintable(QStringLiteral("extracted path '%1' not under system temp root '%2'")
                          .arg(extracted_path, system_temp_root)));
  const QFileInfo extracted_info(extracted_path);
  QVERIFY2(extracted_info.dir().dirName().startsWith(QStringLiteral("7zO_")),
           qPrintable(QStringLiteral("temporary open dir '%1' should use 7zO_ prefix")
                          .arg(extracted_info.dir().dirName())));

  clear_runtime_settings();
}

void FileManagerBehaviorTest::openFromArchiveViewIgnoresRemovableOnlyWorkDirOnFixedDisk() {
  clear_runtime_settings();
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");
  QTemporaryDir working_root;
  QVERIFY2(working_root.isValid(), "failed to create configured working dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  z7::platform::qt::PortableSettings settings;
  settings.setValue(QStringLiteral("Options/WorkDirType"), 2);
  settings.setValue(QStringLiteral("Options/WorkDirPath"), working_root.path());
  settings.setValue(QStringLiteral("Options/TempRemovableOnly"), true);

  z7::ui::filemanager::MainWindow window;
  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QVERIFY(window.in_archive_view());

  const int file_row = row_by_name(window, QStringLiteral("sample.txt"));
  QVERIFY(file_row >= 0);
  select_rows_in_active_panel(&window, {file_row});

  const QString tracker_log_path =
      QDir(root.path()).filePath(QStringLiteral("open-removable-fixed-log.json"));
  ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                        prepend_to_path(fake_launcher_dir()));
  ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                       tracker_log_path.toUtf8());
  ScopedEnvVar fail_mode_env(QByteArrayLiteral("Z7_FAKE_TRACKER_FAIL_MODE"),
                             QByteArrayLiteral("normal"));
  ScopedEnvVar sleep_env(QByteArrayLiteral("Z7_FAKE_TRACKER_SLEEP_MS"),
                         QByteArrayLiteral("0"));

  QVERIFY(QFile::remove(archive_path));
  QVERIFY(!QFileInfo::exists(archive_path));

  window.on_open_outside_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QJsonObject tracker_log;
  QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                            !tracker_log.isEmpty()),
                           5000);

  const QString extracted_path =
      QDir::cleanPath(QDir::fromNativeSeparators(first_tracked_sample_path(tracker_log)));
  QVERIFY(!extracted_path.isEmpty());
  const QString configured_root =
      QDir::cleanPath(QDir::fromNativeSeparators(working_root.path()));
  const QString system_temp_root =
      QDir::cleanPath(QDir::fromNativeSeparators(QDir::tempPath()));

  QVERIFY2(!(extracted_path.startsWith(configured_root + QLatin1Char('/')) ||
             extracted_path.startsWith(configured_root + QLatin1Char('\\'))),
           qPrintable(QStringLiteral("extracted path '%1' unexpectedly used configured work root '%2'")
                          .arg(extracted_path, configured_root)));
  QVERIFY2(extracted_path.startsWith(system_temp_root + QLatin1Char('/')) ||
               extracted_path.startsWith(system_temp_root + QLatin1Char('\\')),
           qPrintable(QStringLiteral("extracted path '%1' not under system temp root '%2'")
                          .arg(extracted_path, system_temp_root)));
  const QFileInfo extracted_info(extracted_path);
  QVERIFY2(extracted_info.dir().dirName().startsWith(QStringLiteral("7zO_")),
           qPrintable(QStringLiteral("temporary open dir '%1' should use 7zO_ prefix")
                          .arg(extracted_info.dir().dirName())));

  clear_runtime_settings();
}

void FileManagerBehaviorTest::openFromArchiveViewIgnoresRemovableOnlyWorkDirOnRemovableSource() {
  clear_runtime_settings();
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");
  QTemporaryDir working_root;
  QVERIFY2(working_root.isValid(), "failed to create configured working dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  z7::platform::qt::PortableSettings settings;
  settings.setValue(QStringLiteral("Options/WorkDirType"), 2);
  settings.setValue(QStringLiteral("Options/WorkDirPath"), working_root.path());
  settings.setValue(QStringLiteral("Options/TempRemovableOnly"), true);

  z7::ui::filemanager::MainWindow window;
  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QVERIFY(window.in_archive_view());

  const int file_row = row_by_name(window, QStringLiteral("sample.txt"));
  QVERIFY(file_row >= 0);
  select_rows_in_active_panel(&window, {file_row});

  const QString tracker_log_path =
      QDir(root.path()).filePath(QStringLiteral("open-removable-source-log.json"));
  ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                        prepend_to_path(fake_launcher_dir()));
  ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                       tracker_log_path.toUtf8());
  ScopedEnvVar fail_mode_env(QByteArrayLiteral("Z7_FAKE_TRACKER_FAIL_MODE"),
                             QByteArrayLiteral("normal"));
  ScopedEnvVar sleep_env(QByteArrayLiteral("Z7_FAKE_TRACKER_SLEEP_MS"),
                         QByteArrayLiteral("0"));

  QVERIFY(QFile::remove(archive_path));
  QVERIFY(!QFileInfo::exists(archive_path));

  window.on_open_outside_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QJsonObject tracker_log;
  QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                            !tracker_log.isEmpty()),
                           5000);

  const QString extracted_path =
      QDir::cleanPath(QDir::fromNativeSeparators(first_tracked_sample_path(tracker_log)));
  QVERIFY(!extracted_path.isEmpty());
  const QString configured_root =
      QDir::cleanPath(QDir::fromNativeSeparators(working_root.path()));
  const QString system_temp_root =
      QDir::cleanPath(QDir::fromNativeSeparators(QDir::tempPath()));
  QVERIFY2(!(extracted_path.startsWith(configured_root + QLatin1Char('/')) ||
             extracted_path.startsWith(configured_root + QLatin1Char('\\'))),
           qPrintable(QStringLiteral("extracted path '%1' unexpectedly under configured work root '%2'")
                          .arg(extracted_path, configured_root)));
  QVERIFY2(extracted_path.startsWith(system_temp_root + QLatin1Char('/')) ||
               extracted_path.startsWith(system_temp_root + QLatin1Char('\\')),
           qPrintable(QStringLiteral("extracted path '%1' not under system temp root '%2'")
                          .arg(extracted_path, system_temp_root)));
  const QFileInfo extracted_info(extracted_path);
  QVERIFY2(extracted_info.dir().dirName().startsWith(QStringLiteral("7zO_")),
           qPrintable(QStringLiteral("temporary open dir '%1' should use 7zO_ prefix")
                          .arg(extracted_info.dir().dirName())));

  clear_runtime_settings();
}

void FileManagerBehaviorTest::openFromArchiveViewOutsideTrackedProcessExitCleansSession() {
#if defined(Q_OS_WIN)
  QSKIP("Path-based fake launcher covers the current tracked open-outside path on non-Windows.");
#endif

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
  select_rows_in_active_panel(&window, {file_row});

  const QStringList temp_dirs_before =
      archive_temp_dirs_under_system_root(QStringLiteral("7zO_"));
  const QString tracker_log_path =
      QDir(root.path()).filePath(QStringLiteral("open-outside-session-log.json"));
  ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                        prepend_to_path(fake_launcher_dir()));
  ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                       tracker_log_path.toUtf8());
  ScopedEnvVar fail_mode_env(QByteArrayLiteral("Z7_FAKE_TRACKER_FAIL_MODE"),
                             QByteArrayLiteral("normal"));
  ScopedEnvVar sleep_env(QByteArrayLiteral("Z7_FAKE_TRACKER_SLEEP_MS"),
                         QByteArrayLiteral("1200"));
  ScopedEnvVar no_child_env(QByteArrayLiteral("Z7_FAKE_TRACKER_NO_CHILD"),
                            QByteArrayLiteral("1"));

  window.on_open_outside_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QJsonObject tracker_log;
  QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                            !tracker_log.isEmpty()),
                           5000);
  QTRY_VERIFY_WITH_TIMEOUT(!window.archive_temp_sessions_.isEmpty(), 5000);
  QTRY_VERIFY_WITH_TIMEOUT(window.archive_temp_sessions_.isEmpty(), 5000);

  QVERIFY(window.close());

  const QStringList temp_dirs_after =
      archive_temp_dirs_under_system_root(QStringLiteral("7zO_"));
  QCOMPARE(temp_dirs_after.size(), temp_dirs_before.size());
}

void FileManagerBehaviorTest::openFromArchiveViewOutsideChangedFileDoesNotWriteBack() {
#if defined(Q_OS_WIN)
  QSKIP("Path-based fake launcher covers the current tracked open-outside path on non-Windows.");
#endif

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  z7::ui::filemanager::MainWindow window;

  int prompt_count = 0;
  window.question_box_ = [&prompt_count](const QString&,
                                         const QString&,
                                         QMessageBox::StandardButtons,
                                         QMessageBox::StandardButton) {
    ++prompt_count;
    return QMessageBox::Yes;
  };

  window.open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QVERIFY(window.in_archive_view());

  const int file_row = row_by_name(window, QStringLiteral("sample.txt"));
  QVERIFY(file_row >= 0);
  select_rows_in_active_panel(&window, {file_row});

  const QStringList temp_dirs_before =
      archive_temp_dirs_under_system_root(QStringLiteral("7zO_"));
  const QString tracker_log_path =
      QDir(root.path()).filePath(QStringLiteral("open-outside-changed-log.json"));
  ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                        prepend_to_path(fake_launcher_dir()));
  ScopedEnvVar log_env(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                       tracker_log_path.toUtf8());
  ScopedEnvVar fail_mode_env(QByteArrayLiteral("Z7_FAKE_TRACKER_FAIL_MODE"),
                             QByteArrayLiteral("normal"));
  ScopedEnvVar sleep_env(QByteArrayLiteral("Z7_FAKE_TRACKER_SLEEP_MS"),
                         QByteArrayLiteral("1200"));
  ScopedEnvVar no_child_env(QByteArrayLiteral("Z7_FAKE_TRACKER_NO_CHILD"),
                            QByteArrayLiteral("1"));

  window.on_open_outside_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QJsonObject tracker_log;
  QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                            !tracker_log.isEmpty()),
                           5000);
  QTRY_VERIFY_WITH_TIMEOUT(!window.archive_temp_sessions_.isEmpty(), 5000);

  const QString tracked_path = first_tracked_sample_path(tracker_log);
  QVERIFY(!tracked_path.isEmpty());
  QFile rewritten(tracked_path);
  QVERIFY(rewritten.open(QIODevice::WriteOnly | QIODevice::Truncate));
  rewritten.write("updated from open outside\n");
  rewritten.close();

  QTRY_VERIFY_WITH_TIMEOUT(window.archive_temp_sessions_.isEmpty(), 5000);
  QCOMPARE(prompt_count, 0);
  QVERIFY(window.close());

  const QStringList temp_dirs_after =
      archive_temp_dirs_under_system_root(QStringLiteral("7zO_"));
  QCOMPARE(temp_dirs_after.size(), temp_dirs_before.size());

  QTemporaryDir verify_root;
  QVERIFY2(verify_root.isValid(), "failed to create archive verification root");
  const QString extracted_text = extract_archive_entry_text(
      archive_path,
      QStringLiteral("sample.txt"),
      verify_root.path());
  QCOMPARE(extracted_text, QStringLiteral("7zfm behavior test\n"));
}
