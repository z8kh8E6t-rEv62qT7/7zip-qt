// tests/filemanager/behavior/archive_open_platform_tracking.cpp
// Role: Platform-specific external-open process tracking behavior cases.

#include "internal.h"

#include <QJsonArray>

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

QString fake_tracker_program_path() {
#ifdef Q_OS_WIN
  const QString program_name = QStringLiteral("z7_fake_open_tracker.exe");
#else
  const QString program_name = QStringLiteral("z7_fake_open_tracker");
#endif
  return QDir(fake_launcher_dir()).filePath(program_name);
}

QString fake_wrapper_path() {
#if defined(Q_OS_MAC)
  return QDir(fake_launcher_dir()).filePath(QStringLiteral("open"));
#elif defined(Q_OS_LINUX)
  return QDir(fake_launcher_dir()).filePath(QStringLiteral("xdg-open"));
#else
  return QString();
#endif
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

void verify_fake_launcher_fixture_ready() {
  const QFileInfo tracker_info(fake_tracker_program_path());
  QVERIFY2(tracker_info.isFile(),
           qPrintable(QStringLiteral("missing fake tracker executable: %1")
                          .arg(tracker_info.absoluteFilePath())));
  QVERIFY2(tracker_info.isExecutable(),
           qPrintable(QStringLiteral("fake tracker is not executable: %1")
                          .arg(tracker_info.absoluteFilePath())));

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
  const QFileInfo wrapper_info(fake_wrapper_path());
  QVERIFY2(wrapper_info.isFile(),
           qPrintable(QStringLiteral("missing launcher wrapper: %1")
                          .arg(wrapper_info.absoluteFilePath())));
  QVERIFY2(wrapper_info.isExecutable(),
           qPrintable(QStringLiteral("launcher wrapper is not executable: %1")
                          .arg(wrapper_info.absoluteFilePath())));
#endif
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

bool tracker_log_contains_sample_entry(const QJsonObject& log) {
  const QJsonArray args = log.value(QStringLiteral("args")).toArray();
  for (const QJsonValue& value : args) {
    const QString arg = value.toString();
    if (QFileInfo(arg).fileName() == QStringLiteral("sample.txt")) {
      return true;
    }
  }
  return false;
}

void prepare_archive_entry_for_open_outside(z7::ui::filemanager::MainWindow* window,
                                            const QString& archive_path) {
  QVERIFY(window != nullptr);
  window->open_archive_inside(archive_path);
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(*window) == nullptr, 20000);
  QVERIFY(window->in_archive_view());

  const int file_row = row_by_name(*window, QStringLiteral("sample.txt"));
  QVERIFY(file_row >= 0);
  select_rows_in_active_panel(window, {file_row});
}

class ScopedFakeTrackerEnvironment final {
 public:
  ScopedFakeTrackerEnvironment(const QString& log_path,
                               const QString& fail_mode,
                               int sleep_msecs)
      : path_env_(QByteArrayLiteral("PATH"),
                  prepend_to_path(fake_launcher_dir())),
        log_env_(QByteArrayLiteral("Z7_FAKE_TRACKER_LOG"),
                 log_path.toUtf8()),
        fail_mode_env_(QByteArrayLiteral("Z7_FAKE_TRACKER_FAIL_MODE"),
                       fail_mode.toUtf8()),
        sleep_env_(QByteArrayLiteral("Z7_FAKE_TRACKER_SLEEP_MS"),
                   QByteArray::number(sleep_msecs)) {}

 private:
  ScopedEnvVar path_env_;
  ScopedEnvVar log_env_;
  ScopedEnvVar fail_mode_env_;
  ScopedEnvVar sleep_env_;
};

}  // namespace

void FileManagerBehaviorTest::openFromArchiveViewOutsideQuickObservableExitWaitsForCloseEvent() {
#if defined(Q_OS_WIN)
  QSKIP("Path-based fake launcher covers the non-Windows tracked path.");
#endif

  verify_fake_launcher_fixture_ready();

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  const QString tracker_log_path =
      QDir(root.path()).filePath(QStringLiteral("quick-exit-log.json"));
  ScopedFakeTrackerEnvironment fake_env(
      tracker_log_path,
      QStringLiteral("normal"),
      0);

  z7::ui::filemanager::MainWindow window;
  prepare_archive_entry_for_open_outside(&window, archive_path);

  window.on_open_outside_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QTRY_VERIFY_WITH_TIMEOUT(!window.archive_temp_sessions_.isEmpty(), 3000);

  QJsonObject tracker_log;
  QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                            !tracker_log.isEmpty()),
                           5000);
  QVERIFY(tracker_log_contains_sample_entry(tracker_log));

  QTest::qWait(250);
  QVERIFY(window.close());
  QTRY_VERIFY_WITH_TIMEOUT(window.archive_temp_sessions_.isEmpty(), 3000);
}

void FileManagerBehaviorTest::
    openFromArchiveViewOutsideQuickObservableExitOnWindowsWaitsForCloseEvent() {
  QSKIP("Retired after removing the tracked launcher seam; Windows ShellExecuteExW cannot be intercepted via PATH-based fake launcher.");
}

void FileManagerBehaviorTest::
    openFromArchiveViewOutsideOnMacTracksChildProcessUntilExit() {
#if !defined(Q_OS_MAC)
  QSKIP("macOS-only process tracking behavior.");
#endif

  verify_fake_launcher_fixture_ready();

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  const QString tracker_log_path =
      QDir(root.path()).filePath(QStringLiteral("child-tracking-log.json"));
  ScopedFakeTrackerEnvironment fake_env(
      tracker_log_path,
      QStringLiteral("normal"),
      2000);

  z7::ui::filemanager::MainWindow window;
  prepare_archive_entry_for_open_outside(&window, archive_path);

  window.on_open_outside_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QTRY_VERIFY_WITH_TIMEOUT(!window.archive_temp_sessions_.isEmpty(), 3000);

  QJsonObject tracker_log;
  QTRY_VERIFY_WITH_TIMEOUT((tracker_log = read_tracker_log(tracker_log_path),
                            !tracker_log.isEmpty()),
                           5000);
  QVERIFY(tracker_log_contains_sample_entry(tracker_log));
  QCOMPARE(tracker_log.value(QStringLiteral("mode")).toString(),
           QStringLiteral("normal"));

  QTest::qWait(1300);
  QVERIFY2(!window.archive_temp_sessions_.isEmpty(),
           "session should stay alive while tracked child process is still running");
  QTRY_VERIFY_WITH_TIMEOUT(window.archive_temp_sessions_.isEmpty(), 5000);
}

void FileManagerBehaviorTest::
    openFromArchiveViewOutsideOnMacTrackingInitFailureFailsStrictly() {
#if !defined(Q_OS_MAC)
  QSKIP("macOS-only process tracking behavior.");
#endif

  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString archive_path = create_sample_archive(root);
  QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

  QString warning_title;
  QString warning_text;
  schedule_message_box_capture_and_click(QMessageBox::Ok,
                                         &warning_title,
                                         &warning_text,
                                         5000,
                                         10);

  z7::ui::filemanager::MainWindow window;
  prepare_archive_entry_for_open_outside(&window, archive_path);

  ScopedEnvVar path_env(QByteArrayLiteral("PATH"),
                        QFile::encodeName(root.path()));

  window.on_open_outside_requested();
  QTRY_VERIFY_WITH_TIMEOUT(filemanager_behavior_internal::current_runner(window) == nullptr, 20000);
  QTRY_VERIFY_WITH_TIMEOUT(!warning_text.trimmed().isEmpty(), 5000);
  QVERIFY(warning_text.contains(QStringLiteral("Open outside"),
                                Qt::CaseInsensitive));
  QVERIFY(warning_text.contains(QStringLiteral("Failed"),
                                Qt::CaseInsensitive));
  QTRY_VERIFY_WITH_TIMEOUT(window.archive_temp_sessions_.isEmpty(), 3000);
}

void FileManagerBehaviorTest::
    openFromArchiveViewOutsideOnMacTrackingRuntimeFailureFailsStrictly() {
  QSKIP("Retired after removing the runtime snapshot failure test seam; the remaining system-level fake launcher does not simulate mid-session libproc enumeration failure.");
}
