// tests/filemanager/behavior/main.cpp
// Role: File manager behavior test entrypoint.

#include "internal.h"

#include "file_open_support.h"

using namespace filemanager_behavior_internal;

namespace {

QString test_7zg_path() {
#ifdef Q_OS_WIN
  const QString exe_name = QStringLiteral("7zG.exe");
#else
  const QString exe_name = QStringLiteral("7zG");
#endif
  return QDir(QCoreApplication::applicationDirPath()).filePath(exe_name);
}

bool ensure_test_7zg_stub(QString* error) {
  const QString path = test_7zg_path();
  const QFileInfo existing(path);
  if (existing.isFile() && existing.isExecutable()) {
    return true;
  }
  if (existing.exists() && !existing.isFile()) {
    if (error != nullptr) {
      *error = QStringLiteral("Path exists but is not a file: %1").arg(path);
    }
    return false;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    if (error != nullptr) {
      *error =
          QStringLiteral("Failed to create local 7zG test stub: %1").arg(path);
    }
    return false;
  }
#ifdef Q_OS_WIN
  // Placeholder for path resolution checks in tests that inject launcher.
  file.write("@echo off\r\nexit /b 0\r\n");
#else
  file.write("#!/bin/sh\nexit 0\n");
#endif
  file.close();

#ifndef Q_OS_WIN
  if (!QFile::setPermissions(path,
                             QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                 QFileDevice::ExeOwner |
                                 QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                 QFileDevice::ReadOther | QFileDevice::ExeOther)) {
    if (error != nullptr) {
      *error = QStringLiteral("Failed to make local 7zG test stub executable: %1")
                   .arg(path);
    }
    return false;
  }
#endif

  const QFileInfo created(path);
  if (!created.isFile() || !created.isExecutable()) {
    if (error != nullptr) {
      *error = QStringLiteral("Local 7zG test stub is not executable: %1").arg(path);
    }
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  Q_INIT_RESOURCE(generated_filemanager_resources);
  z7::apps::filemanager::FileOpenApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("z7-tests"));
  QCoreApplication::setApplicationName(QStringLiteral("filemanager-behavior"));
  QString stub_error;
  if (!ensure_test_7zg_stub(&stub_error)) {
    qCritical().noquote() << stub_error;
    return 1;
  }
  QTemporaryDir settings_dir;
  z7::platform::qt::set_portable_settings_root(settings_dir.path());
  QString init_error;
  if (!z7::platform::qt::initialize_portable_settings(&init_error)) {
    qCritical().noquote() << init_error;
    return 1;
  }
  clear_runtime_settings();
  FileManagerBehaviorTest test;
  return QTest::qExec(&test, argc, argv);
}
