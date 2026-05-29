#include <QAbstractButton>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSize>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QUuid>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "app_startup_qt.h"
#include "archive_session.h"
#include "archive_string_codec_qt.h"
#include "portable_settings.h"
#include "portable_settings_internal.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "main_window.h"
#include "options_dialog.h"
#undef private
#include "official_lang_catalog.h"
#include "compress_dialog.h"
#include "extract_dialog.h"
#include "gui_app_controller.h"
#include "gui_task_progress_dialog.h"
#include "gui_task_runner.h"
#include "path_history_utils.h"
#include "task_ipc_runtime.h"

namespace {

class ScopedEnvironmentVariable final {
 public:
  explicit ScopedEnvironmentVariable(const char* name)
      : name_(name),
        had_value_(qEnvironmentVariableIsSet(name)),
        value_(qgetenv(name)) {}

  ScopedEnvironmentVariable(const char* name, const QByteArray& temporary_value)
      : ScopedEnvironmentVariable(name) {
    qputenv(name_, temporary_value);
  }

  ~ScopedEnvironmentVariable() {
    if (had_value_) {
      qputenv(name_, value_);
    } else {
      qunsetenv(name_);
    }
  }

 private:
  const char* name_;
  bool had_value_;
  QByteArray value_;
};

class ScopedCatalogLanguage final {
 public:
  explicit ScopedCatalogLanguage(const QString& language_id)
      : catalog_(z7::ui::runtime_support::OfficialLangCatalog::instance()),
        previous_language_(catalog_.current_language()) {
    changed_ = catalog_.set_language(language_id);
  }

  ~ScopedCatalogLanguage() {
    if (changed_) {
      catalog_.set_language(previous_language_);
    }
  }

  bool changed() const { return changed_; }

 private:
  z7::ui::runtime_support::OfficialLangCatalog& catalog_;
  QString previous_language_;
  bool changed_ = false;
};

class ScopedPortableSettingsRoot final {
 public:
  explicit ScopedPortableSettingsRoot(QString root) : root_(std::move(root)) {}

  ~ScopedPortableSettingsRoot() {
    z7::platform::qt::set_portable_settings_root(root_);
    QString ignored_error;
    z7::platform::qt::initialize_portable_settings(&ignored_error);
  }

 private:
  QString root_;
};

class WaitableOutcomeDelegate final : public z7::app::IArchiveDelegate {
 public:
  using PasswordHandler =
      std::function<std::optional<z7::app::PasswordReply>(
          const z7::app::PasswordPrompt&)>;

  explicit WaitableOutcomeDelegate(PasswordHandler password_handler = {})
      : password_handler_(std::move(password_handler)) {}

  std::optional<z7::app::PasswordReply> request_password(
      const z7::app::PasswordPrompt& prompt) override {
    if (password_handler_) {
      return password_handler_(prompt);
    }
    return std::nullopt;
  }

  void on_finished(const z7::app::OperationOutcome& outcome) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      outcome_ = outcome;
      done_ = true;
    }
    cv_.notify_all();
  }

  z7::app::OperationOutcome await_outcome() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return done_; });
    return outcome_.value_or(z7::app::OperationOutcome{});
  }

 private:
  PasswordHandler password_handler_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::optional<z7::app::OperationOutcome> outcome_;
  bool done_ = false;
};

std::string to_native_path_string(const QString& path) {
  const QByteArray encoded = QFile::encodeName(path);
  return std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
}

z7::app::OperationOutcome run_archive_request_and_await(
    const z7::app::ArchiveRequest& request,
    WaitableOutcomeDelegate::PasswordHandler password_handler = {}) {
  auto delegate = std::make_shared<WaitableOutcomeDelegate>(
      std::move(password_handler));
  z7::app::ArchiveEngine engine;
  z7::app::ArchiveSession session = engine.start(request, delegate);
  if (session.valid()) {
    return delegate->await_outcome();
  }

  z7::app::OperationOutcome outcome;
  outcome.status = z7::app::OperationStatus::kFailed;
  outcome.error_domain = z7::app::ArchiveErrorDomain::kBackendUnavailable;
  outcome.native_code = 2;
  outcome.ok = false;
  outcome.summary = "No archive backend available.";
  return outcome;
}

bool create_sample_archive(const QTemporaryDir& root, QString* archive_path) {
  if (archive_path == nullptr) {
    return false;
  }

  const QString sample_file = QDir(root.path()).filePath(QStringLiteral("sample.txt"));
  QFile file(sample_file);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  file.write("7zg test result lifecycle smoke\n");
  file.close();

  const QString previous_dir = QDir::currentPath();
  if (!QDir::setCurrent(root.path())) {
    return false;
  }

  z7::app::AddRequest add;
  add.archive_path = to_native_path_string(
      QDir(root.path()).filePath(QStringLiteral("sample.7z")));
  add.format = "7z";
  add.input_paths.push_back("sample.txt");

  z7::app::ArchiveRequest request;
  request.payload = add;
  const z7::app::OperationOutcome outcome = run_archive_request_and_await(request);
  QDir::setCurrent(previous_dir);

  *archive_path = z7::ui::archive_support::from_native_string(add.archive_path);
  return outcome.ok && QFileInfo::exists(*archive_path);
}

bool write_file(const QString& path, const QByteArray& contents) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(contents) == contents.size();
}

bool set_file_modified_time(const QString& path, const QDateTime& modified_at) {
  QFile file(path);
  if (!file.open(QIODevice::ReadWrite)) {
    return false;
  }
  return file.setFileTime(modified_at, QFileDevice::FileModificationTime);
}

QByteArray read_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

QString sha256_hex(const QByteArray& data) {
  return QString::fromLatin1(
      QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QStringList direct_child_files_with_contents(const QString& dir_path,
                                             const QByteArray& contents) {
  QStringList matches;
  const QFileInfoList files =
      QDir(dir_path).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                   QDir::Name);
  for (const QFileInfo& file : files) {
    if (read_file(file.absoluteFilePath()) == contents) {
      matches.push_back(file.fileName());
    }
  }
  return matches;
}

QStringList combo_data_values(const QComboBox* combo) {
  QStringList values;
  if (combo == nullptr) {
    return values;
  }
  for (int i = 0; i < combo->count(); ++i) {
    values.push_back(combo->itemData(i).toString());
  }
  return values;
}

void set_combo_data(QComboBox* combo, const QString& value) {
  QVERIFY(combo != nullptr);
  const int index = combo->findData(value);
  QVERIFY2(index >= 0, qPrintable(value));
  combo->setCurrentIndex(index);
  QApplication::processEvents();
}

bool create_single_file_archive(const QTemporaryDir& root,
                                const QString& archive_name,
                                const QString& entry_name,
                                const QByteArray& contents,
                                QString* archive_path,
                                QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (archive_path == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Missing archive path output.");
    }
    return false;
  }

  const QString source_dir =
      QDir(root.path()).filePath(archive_name + QStringLiteral(".src"));
  const QString source_path = QDir(source_dir).filePath(entry_name);
  if (!QDir().mkpath(QFileInfo(source_path).absolutePath())) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Failed to create archive source dir.");
    }
    return false;
  }
  if (!write_file(source_path, contents)) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Failed to write archive source file: %1")
              .arg(source_path);
    }
    return false;
  }

  const QString target_archive = QDir(root.path()).filePath(archive_name);
  z7::app::AddRequest add;
  add.archive_path = to_native_path_string(target_archive);
  add.format = "7z";
  add.input_items.push_back(
      z7::app::AddInputItem{to_native_path_string(source_path),
                            entry_name.toStdString()});

  z7::app::ArchiveRequest request;
  request.payload = add;
  const z7::app::OperationOutcome outcome =
      run_archive_request_and_await(request);
  if (!outcome.ok || !QFileInfo::exists(target_archive)) {
    if (error_message != nullptr) {
      *error_message = QString::fromStdString(outcome.summary).trimmed();
      if (error_message->isEmpty()) {
        *error_message =
            QStringLiteral("Failed to create single-file archive.");
      }
    }
    return false;
  }

  *archive_path = target_archive;
  return true;
}

bool create_archive_with_entries(
    const QTemporaryDir& root,
    const QString& archive_name,
    const QString& archive_format,
    const QVector<QPair<QString, QByteArray>>& entries,
    QString* archive_path,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (archive_path == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Missing archive path output.");
    }
    return false;
  }

  const QString source_dir =
      QDir(root.path()).filePath(archive_name + QStringLiteral(".src"));
  std::vector<z7::app::AddInputItem> input_items;
  input_items.reserve(static_cast<size_t>(entries.size()));
  for (const auto& entry : entries) {
    const QString source_path = QDir(source_dir).filePath(entry.first);
    if (!QDir().mkpath(QFileInfo(source_path).absolutePath())) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Failed to create source directory for %1.")
                .arg(entry.first);
      }
      return false;
    }
    if (!write_file(source_path, entry.second)) {
      if (error_message != nullptr) {
        *error_message =
            QStringLiteral("Failed to write archive source file: %1")
                .arg(source_path);
      }
      return false;
    }
    input_items.push_back(
        z7::app::AddInputItem{to_native_path_string(source_path),
                              entry.first.toStdString()});
  }

  const QString target_archive = QDir(root.path()).filePath(archive_name);
  z7::app::AddRequest add;
  add.archive_path = to_native_path_string(target_archive);
  add.format = archive_format.toStdString();
  add.input_items = std::move(input_items);

  z7::app::ArchiveRequest request;
  request.payload = add;
  const z7::app::OperationOutcome outcome =
      run_archive_request_and_await(request);
  if (!outcome.ok || !QFileInfo::exists(target_archive)) {
    if (error_message != nullptr) {
      *error_message = QString::fromStdString(outcome.summary).trimmed();
      if (error_message->isEmpty()) {
        *error_message = QStringLiteral("Failed to create archive entries.");
      }
    }
    return false;
  }

  *archive_path = target_archive;
  return true;
}

bool create_archive_with_entries(
    const QTemporaryDir& root,
    const QString& archive_name,
    const QVector<QPair<QString, QByteArray>>& entries,
    QString* archive_path,
    QString* error_message) {
  return create_archive_with_entries(root,
                                     archive_name,
                                     QStringLiteral("7z"),
                                     entries,
                                     archive_path,
                                     error_message);
}

struct RealArchiveFormatSpec {
  QString id;
  QString extension;
  bool supports_directory_input = false;
};

QVector<RealArchiveFormatSpec> single_file_format_matrix() {
  return {
      {QStringLiteral("7z"), QStringLiteral("7z"), true},
      {QStringLiteral("bzip2"), QStringLiteral("bz2"), false},
      {QStringLiteral("gzip"), QStringLiteral("gz"), false},
      {QStringLiteral("tar"), QStringLiteral("tar"), true},
      {QStringLiteral("wim"), QStringLiteral("wim"), true},
      {QStringLiteral("xz"), QStringLiteral("xz"), false},
      {QStringLiteral("zip"), QStringLiteral("zip"), true},
  };
}

QVector<RealArchiveFormatSpec> directory_format_matrix() {
  QVector<RealArchiveFormatSpec> out;
  for (const RealArchiveFormatSpec& format : single_file_format_matrix()) {
    if (format.supports_directory_input) {
      out.push_back(format);
    }
  }
  return out;
}

struct RealArchivePasswordVariant {
  QString label;
  QString format_id;
  QString extension;
  QString password;
  QString encryption_method;
  bool encrypt_headers_defined = false;
  bool encrypt_headers = false;
};

QVector<RealArchivePasswordVariant> password_format_matrix() {
  return {
      {QStringLiteral("7z-password"),
       QStringLiteral("7z"),
       QStringLiteral("7z"),
       QStringLiteral("test-password"),
       QString(),
       false,
       false},
      {QStringLiteral("7z-password-he"),
       QStringLiteral("7z"),
       QStringLiteral("7z"),
       QStringLiteral("test-password"),
       QString(),
       true,
       true},
      {QStringLiteral("zip-zipcrypto"),
       QStringLiteral("zip"),
       QStringLiteral("zip"),
       QStringLiteral("test-password"),
       QStringLiteral("ZipCrypto"),
       false,
       false},
      {QStringLiteral("zip-aes"),
       QStringLiteral("zip"),
       QStringLiteral("zip"),
       QStringLiteral("test-password"),
       QStringLiteral("AES-256"),
       false,
       false},
  };
}

QString real_7zg_program() {
  return QFileInfo(QStringLiteral(Z7_REAL_7ZG_PATH)).absoluteFilePath();
}

QString side_by_side_7zg_program() {
#ifdef Q_OS_WIN
  const QString exe_name = QStringLiteral("7zG.exe");
#else
  const QString exe_name = QStringLiteral("7zG");
#endif
  return QDir(QCoreApplication::applicationDirPath()).filePath(exe_name);
}

QString side_by_side_sfx_module() {
  return QDir(QCoreApplication::applicationDirPath()).filePath(
      QStringLiteral("7z.sfx"));
}

bool append_archive_payload_to_sfx(const QString& sfx_module,
                                   const QString& payload_archive,
                                   const QString& output_sfx,
                                   QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (!QFileInfo::exists(sfx_module)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Missing SFX module: %1").arg(sfx_module);
    }
    return false;
  }
  if (!QFileInfo::exists(payload_archive)) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Missing SFX payload archive: %1").arg(payload_archive);
    }
    return false;
  }

  QFile::remove(output_sfx);
  if (!QFile::copy(sfx_module, output_sfx)) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Failed to copy SFX module to: %1").arg(output_sfx);
    }
    return false;
  }

  QFile out(output_sfx);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Append)) {
    if (error_message != nullptr) {
      *error_message = out.errorString();
    }
    return false;
  }
  QFile in(payload_archive);
  if (!in.open(QIODevice::ReadOnly)) {
    if (error_message != nullptr) {
      *error_message = in.errorString();
    }
    return false;
  }
  while (!in.atEnd()) {
    const QByteArray chunk = in.read(64 * 1024);
    if (chunk.isEmpty() && in.error() != QFileDevice::NoError) {
      if (error_message != nullptr) {
        *error_message = in.errorString();
      }
      return false;
    }
    if (out.write(chunk) != chunk.size()) {
      if (error_message != nullptr) {
        *error_message = out.errorString();
      }
      return false;
    }
  }

  const QFileDevice::Permissions executable_permissions =
      QFile::permissions(output_sfx) |
      QFileDevice::ExeOwner |
      QFileDevice::ExeUser |
      QFileDevice::ExeGroup |
      QFileDevice::ExeOther;
  if (!QFile::setPermissions(output_sfx, executable_permissions)) {
    if (error_message != nullptr) {
      *error_message =
          QStringLiteral("Failed to make SFX executable: %1").arg(output_sfx);
    }
    return false;
  }
  return true;
}

bool collect_task_ipc_events_for_test(
    const QString& owner_instance_id,
    QVector<z7::task_ipc_runtime::TaskIpcEvent>* out_events,
    QString* error_message) {
  if (out_events == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Missing task IPC event output.");
    }
    return false;
  }
  return z7::task_ipc_runtime::collect_task_ipc_events(
      owner_instance_id,
      out_events,
      error_message);
}

bool contains_task_ipc_event_kind(
    const QVector<z7::task_ipc_runtime::TaskIpcEvent>& events,
    z7::task_ipc_runtime::TaskIpcEventKind kind) {
  for (const z7::task_ipc_runtime::TaskIpcEvent& event : events) {
    if (event.event_kind == kind) {
      return true;
    }
  }
  return false;
}

bool task_ipc_events_empty_for_owner(const QString& owner_instance_id,
                                     QString* error_message) {
  QVector<z7::task_ipc_runtime::TaskIpcEvent> events;
  if (!collect_task_ipc_events_for_test(owner_instance_id,
                                        &events,
                                        error_message)) {
    return false;
  }
  return events.isEmpty();
}

bool create_path_remap_archive(const QTemporaryDir& root,
                               QString* archive_path,
                               QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (archive_path == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Missing archive path output.");
    }
    return false;
  }

  const QString docs_dir = QDir(root.path()).filePath(QStringLiteral("docs"));
  if (!QDir().mkpath(docs_dir)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Failed to create docs directory.");
    }
    return false;
  }

  if (!write_file(QDir(docs_dir).filePath(QStringLiteral("readme.md")),
                  QByteArrayLiteral("docs-body"))) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Failed to write docs/readme.md.");
    }
    return false;
  }
  if (!write_file(QDir(root.path()).filePath(QStringLiteral("report.txt")),
                  QByteArrayLiteral("report-body"))) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Failed to write report.txt.");
    }
    return false;
  }

  const QString target_archive =
      QDir(root.path()).filePath(QStringLiteral("remaps.7z"));
  z7::app::AddRequest add;
  add.archive_path = to_native_path_string(target_archive);
  add.format = "7z";
  add.input_items.push_back(
      z7::app::AddInputItem{to_native_path_string(QDir(root.path()).filePath(
                                QStringLiteral("report.txt"))),
                            "report.txt"});
  add.input_items.push_back(
      z7::app::AddInputItem{to_native_path_string(docs_dir), "docs"});

  z7::app::ArchiveRequest request;
  request.payload = add;
  const z7::app::OperationOutcome outcome =
      run_archive_request_and_await(request);
  if (!outcome.ok || !QFileInfo::exists(target_archive)) {
    if (error_message != nullptr) {
      *error_message =
          QString::fromStdString(outcome.summary).trimmed();
      if (error_message->isEmpty()) {
        *error_message =
            QStringLiteral("Failed to create path remap archive.");
      }
    }
    return false;
  }

  *archive_path = target_archive;
  return true;
}

z7::task_ipc_runtime::TaskIpcPayload make_extract_remap_payload(
    const QString& archive_path,
    const QString& output_dir,
    const QString& remap_destination) {
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
  payload.show_dialog = false;
  payload.refresh_after_finish = false;
  payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
  payload.extract->archive_inputs = {archive_path};
  payload.extract->output_dir = output_dir;
  payload.extract->overwrite_switch = QStringLiteral("-aoa");

  z7::task_ipc_runtime::TaskIpcExtractPathRemap remap;
  remap.match_kind =
      z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind::kArchivePrefix;
  remap.source_path = QStringLiteral("docs");
  remap.destination_path = remap_destination;
  payload.extract->path_remaps.push_back(remap);
  return payload;
}

z7::task_ipc_runtime::TaskIpcPayload make_add_payload(
    const QString& archive_path,
    const QString& archive_type,
    const QStringList& input_paths) {
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kAdd;
  payload.show_dialog = false;
  payload.refresh_after_finish = false;
  payload.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
  payload.add->archive_path = archive_path;
  payload.add->archive_type = archive_type;
  payload.add->input_paths = input_paths;
  return payload;
}

void apply_password_variant_to_add_payload(
    z7::task_ipc_runtime::TaskIpcPayload& payload,
    const RealArchivePasswordVariant& variant) {
  payload.add->password = variant.password;
  if (!variant.encryption_method.isEmpty()) {
    payload.add->encryption_method = variant.encryption_method;
  }
  payload.add->encrypt_headers_defined = variant.encrypt_headers_defined;
  payload.add->encrypt_headers = variant.encrypt_headers;
}

z7::task_ipc_runtime::TaskIpcPayload make_extract_payload(
    const QString& archive_path,
    const QString& output_dir,
    const QString& password = QString(),
    const QString& overwrite_switch = QStringLiteral("-aoa")) {
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
  payload.show_dialog = false;
  payload.refresh_after_finish = false;
  payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
  payload.extract->archive_inputs = {archive_path};
  payload.extract->output_dir = output_dir;
  payload.extract->overwrite_switch = overwrite_switch;
  payload.extract->password = password;
  return payload;
}

z7::task_ipc_runtime::TaskIpcPayload make_archive_export_remap_payload(
    const QString& archive_path,
    const QString& output_dir,
    const QString& remap_destination) {
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport;
  payload.show_dialog = false;
  payload.refresh_after_finish = false;
  payload.archive_export =
      z7::task_ipc_runtime::TaskIpcArchiveExportPayload{};
  payload.archive_export->root_archive_path = archive_path;
  payload.archive_export->root_archive_type = QStringLiteral("7z");
  payload.archive_export->archive_entry_paths =
      QStringList{QStringLiteral("docs")};
  payload.archive_export->output_dir = output_dir;
  payload.archive_export->overwrite_mode = QStringLiteral("-aoa");
  payload.archive_export->path_mode = QStringLiteral("full");

  z7::task_ipc_runtime::TaskIpcExtractPathRemap remap;
  remap.match_kind =
      z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind::kArchivePrefix;
  remap.source_path = QStringLiteral("docs");
  remap.destination_path = remap_destination;
  payload.archive_export->path_remaps.push_back(remap);
  return payload;
}

z7::task_ipc_runtime::TaskIpcPayload make_test_payload(
    const QStringList& archive_inputs) {
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kTest;
  payload.show_dialog = false;
  payload.refresh_after_finish = false;
  payload.test = z7::task_ipc_runtime::TaskIpcTestPayload{};
  payload.test->archive_inputs = archive_inputs;
  return payload;
}

z7::task_ipc_runtime::TaskIpcPayload make_archive_test_payload(
    const QString& archive_path,
    const QStringList& archive_entry_paths,
    const QString& archive_type = QStringLiteral("7z"),
    const QStringList& nested_archive_entries = {}) {
  z7::task_ipc_runtime::TaskIpcPayload payload =
      make_test_payload(archive_entry_paths);
  payload.open = z7::task_ipc_runtime::TaskIpcOpenPayload{};
  payload.open->archive_path = archive_path;
  payload.open->archive_type = archive_type;
  payload.open->nested_archive_entries = nested_archive_entries;
  return payload;
}

z7::task_ipc_runtime::TaskIpcPayload make_hash_payload(
    const QStringList& input_paths,
    const QString& hash_method) {
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kHash;
  payload.show_dialog = false;
  payload.refresh_after_finish = false;
  payload.hash = z7::task_ipc_runtime::TaskIpcHashPayload{};
  payload.hash->hash_method = hash_method;
  payload.hash->input_paths = input_paths;
  return payload;
}

z7::task_ipc_runtime::TaskIpcPayload make_archive_hash_payload(
    const QString& archive_path,
    const QStringList& archive_entry_paths,
    const QString& hash_method,
    const QString& archive_type = QStringLiteral("7z"),
    const QStringList& nested_archive_entries = {}) {
  z7::task_ipc_runtime::TaskIpcPayload payload =
      make_hash_payload(archive_entry_paths, hash_method);
  payload.open = z7::task_ipc_runtime::TaskIpcOpenPayload{};
  payload.open->archive_path = archive_path;
  payload.open->archive_type = archive_type;
  payload.open->nested_archive_entries = nested_archive_entries;
  return payload;
}

z7::task_ipc_runtime::TaskIpcPayload make_email_add_payload(
    const QString& input_path,
    const QString& archive_path) {
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kAdd;
  payload.show_dialog = false;
  payload.refresh_after_finish = false;
  payload.add = z7::task_ipc_runtime::TaskIpcAddPayload{};
  payload.add->archive_path = archive_path;
  payload.add->archive_type = QStringLiteral("7z");
  payload.add->send_by_email = true;
  payload.add->send_by_email_address =
      QStringLiteral("test@example.invalid");
  payload.add->input_paths = {input_path};
  return payload;
}

z7::task_ipc_runtime::TaskIpcPayload make_cli_payload(
    const QString& working_dir,
    const QStringList& argv) {
  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kCli;
  payload.show_dialog = false;
  payload.refresh_after_finish = false;
  payload.cli = z7::task_ipc_runtime::TaskIpcCliPayload{};
  payload.cli->working_dir = working_dir;
  payload.cli->argv = argv;
  return payload;
}

struct RealTaskIpcRunResult {
  bool dispatched = false;
  bool completed = false;
  QString error_message;
  z7::task_ipc_runtime::TaskIpcDispatchResult dispatch;
  z7::task_ipc_runtime::TaskIpcEvent completion;
};

QString describe_task_ipc_run(const QString& context,
                              const RealTaskIpcRunResult& result) {
  return QStringLiteral(
             "%1 dispatched=%2 completed=%3 result=%4 error=%5 summary=%6")
      .arg(context)
      .arg(result.dispatched)
      .arg(result.completed)
      .arg(result.completion.result_code)
      .arg(result.error_message)
      .arg(result.completion.summary);
}

bool task_ipc_succeeded(const RealTaskIpcRunResult& result,
                        QString* error_message) {
  if (!result.dispatched || !result.completed ||
      !result.error_message.trimmed().isEmpty() ||
      result.completion.result_code != 0) {
    if (error_message != nullptr) {
      *error_message = describe_task_ipc_run(QStringLiteral("task"), result);
    }
    return false;
  }
  return true;
}

bool task_ipc_failed_with_summary(const RealTaskIpcRunResult& result,
                                  QString* error_message) {
  if (!result.dispatched || !result.completed ||
      !result.error_message.trimmed().isEmpty() ||
      result.completion.result_code == 0 ||
      result.completion.summary.trimmed().isEmpty()) {
    if (error_message != nullptr) {
      *error_message = describe_task_ipc_run(QStringLiteral("task"), result);
    }
    return false;
  }
  return true;
}

RealTaskIpcRunResult run_real_7zg_task_ipc_and_wait(
    const QString& worker_program,
    const QString& working_dir,
    const QString& owner_instance_id,
    const z7::task_ipc_runtime::TaskIpcPayload& payload) {
  RealTaskIpcRunResult result;
  const QString child_settings_root =
      QDir(working_dir).filePath(QStringLiteral(".z7-real-7zg-settings"));
  const ScopedEnvironmentVariable child_settings_root_env(
      z7::platform::qt::portable_settings_internal::kTestRootEnv,
      QFile::encodeName(child_settings_root));
  QString dispatch_error;
  QProcess* worker_process_raw = nullptr;
  const z7::task_ipc_runtime::TaskIpcManagedProcessOptions process_options;
  result.dispatched = z7::task_ipc_runtime::dispatch_task_ipc_task_managed_process(
      worker_program,
      working_dir,
      owner_instance_id,
      payload,
      process_options,
      &result.dispatch,
      &worker_process_raw,
      &dispatch_error);
  std::unique_ptr<QProcess> worker_process(worker_process_raw);
  const auto wait_for_worker_exit = [&worker_process]() {
    if (worker_process == nullptr ||
        worker_process->state() == QProcess::NotRunning) {
      return;
    }
    if (!worker_process->waitForFinished(30000)) {
      worker_process->kill();
      worker_process->waitForFinished(5000);
    }
  };
  if (!result.dispatched) {
    result.error_message = dispatch_error;
    return result;
  }

  QString collect_error;
  const bool saw_completed = QTest::qWaitFor(
      [&result, &collect_error, &owner_instance_id]() {
        QVector<z7::task_ipc_runtime::TaskIpcEvent> events;
        collect_error.clear();
        if (!z7::task_ipc_runtime::collect_task_ipc_events(
                owner_instance_id,
                &events,
                &collect_error)) {
          return false;
        }
        for (const z7::task_ipc_runtime::TaskIpcEvent& event : events) {
          if (event.session_id != result.dispatch.session_id ||
              event.generation != result.dispatch.generation ||
              event.event_kind !=
                  z7::task_ipc_runtime::TaskIpcEventKind::kCompleted) {
            continue;
          }
          result.completion = event;
          return true;
        }
        return false;
      },
      30000);

  if (!saw_completed) {
    wait_for_worker_exit();
    result.error_message =
        collect_error.trimmed().isEmpty()
            ? QStringLiteral("Timed out waiting for real 7zG task IPC completion "
                             "(session=%1 generation=%2 pid=%3).")
                  .arg(result.dispatch.session_id)
                  .arg(result.dispatch.generation)
                  .arg(result.dispatch.worker_pid)
            : collect_error.trimmed();
    return result;
  }

  result.completed = true;
  QString ack_error;
  if (!z7::task_ipc_runtime::acknowledge_task_ipc_event(result.completion,
                                                        &ack_error)) {
    result.error_message = ack_error;
  }
  wait_for_worker_exit();
  return result;
}

bool wait_for_task_ipc_completion(
    const QString& owner_instance_id,
    z7::task_ipc_runtime::TaskIpcCommandKind command,
    z7::task_ipc_runtime::TaskIpcEvent* out_completion,
    QString* error_message) {
  if (out_completion == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Missing task IPC completion output.");
    }
    return false;
  }

  QString collect_error;
  const bool saw_completed = QTest::qWaitFor(
      [&]() {
        QVector<z7::task_ipc_runtime::TaskIpcEvent> events;
        collect_error.clear();
        if (!collect_task_ipc_events_for_test(owner_instance_id,
                                              &events,
                                              &collect_error)) {
          return false;
        }
        for (const z7::task_ipc_runtime::TaskIpcEvent& event : events) {
          if (event.event_kind !=
                  z7::task_ipc_runtime::TaskIpcEventKind::kCompleted ||
              event.payload.command != command) {
            continue;
          }
          *out_completion = event;
          return true;
        }
        return false;
      },
      30000);

  if (saw_completed) {
    return true;
  }
  if (error_message != nullptr) {
    *error_message =
        collect_error.trimmed().isEmpty()
            ? QStringLiteral("Timed out waiting for task IPC completion.")
            : collect_error.trimmed();
  }
  return false;
}

RealTaskIpcRunResult run_real_7zg_task_ipc_and_wait(
    const QString& working_dir,
    const z7::task_ipc_runtime::TaskIpcPayload& payload) {
  return run_real_7zg_task_ipc_and_wait(
      real_7zg_program(),
      working_dir,
      QUuid::createUuid().toString(QUuid::WithoutBraces),
      payload);
}

struct DirectCliRunResult {
  bool started = false;
  bool finished = false;
  int exit_code = -1;
  QByteArray stdout_data;
  QByteArray stderr_data;
  QString error_message;
};

DirectCliRunResult run_real_7zg_direct_cli(const QString& working_dir,
                                           const QStringList& args,
                                           int timeout_msecs = 60000,
                                           bool suppress_gui_dialogs = false,
                                           const QByteArray& stdin_data = {}) {
  DirectCliRunResult result;
  QProcess process;
  process.setProgram(real_7zg_program());
  process.setArguments(args);
  process.setWorkingDirectory(working_dir);

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  env.insert(
      QString::fromLatin1(
          z7::platform::qt::portable_settings_internal::kTestRootEnv),
      QDir(working_dir).filePath(QStringLiteral(".z7-direct-cli-settings")));
  if (suppress_gui_dialogs) {
    env.insert(QStringLiteral("Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS"),
               QStringLiteral("1"));
  }
  process.setProcessEnvironment(env);

  process.start();
  result.started = process.waitForStarted(timeout_msecs);
  if (!result.started) {
    result.error_message = process.errorString();
    return result;
  }
  if (!stdin_data.isEmpty()) {
    process.write(stdin_data);
  }
  process.closeWriteChannel();

  result.finished = process.waitForFinished(timeout_msecs);
  if (!result.finished) {
    process.kill();
    process.waitForFinished(5000);
    result.error_message =
        QStringLiteral("Timed out waiting for direct 7zG CLI: %1")
            .arg(args.join(QLatin1Char(' ')));
    result.stdout_data = process.readAllStandardOutput();
    result.stderr_data = process.readAllStandardError();
    return result;
  }

  result.exit_code = process.exitCode();
  result.stdout_data = process.readAllStandardOutput();
  result.stderr_data = process.readAllStandardError();
  if (process.exitStatus() != QProcess::NormalExit) {
    result.error_message =
        QStringLiteral("Direct 7zG CLI crashed: %1")
            .arg(args.join(QLatin1Char(' ')));
  }
  return result;
}

DirectCliRunResult run_sfx_module_for_test(const QString& sfx_path,
                                           const QString& output_dir,
                                           const QString& password = QString(),
                                           bool yes_to_all = true,
                                           int timeout_msecs = 60000) {
  DirectCliRunResult result;
  QProcess process;
  process.setProgram(sfx_path);
  QStringList args = {
      QStringLiteral("--z7-sfx-test-auto-extract"),
      QStringLiteral("--z7-sfx-test-output=%1").arg(output_dir)};
  if (!password.isNull()) {
    args.push_back(QStringLiteral("--z7-sfx-test-password=%1").arg(password));
  }
  if (yes_to_all) {
    args.push_back(QStringLiteral("--z7-sfx-test-yes"));
  }
  process.setArguments(args);
  process.setWorkingDirectory(QFileInfo(sfx_path).absolutePath());

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  process.setProcessEnvironment(env);

  process.start();
  result.started = process.waitForStarted(timeout_msecs);
  if (!result.started) {
    result.error_message = process.errorString();
    return result;
  }
  result.finished = process.waitForFinished(timeout_msecs);
  if (!result.finished) {
    process.kill();
    process.waitForFinished(5000);
    result.error_message =
        QStringLiteral("Timed out waiting for SFX module: %1").arg(sfx_path);
  }
  result.exit_code = process.exitCode();
  result.stdout_data = process.readAllStandardOutput();
  result.stderr_data = process.readAllStandardError();
  if (process.exitStatus() != QProcess::NormalExit) {
    result.error_message = QStringLiteral("SFX module crashed: %1").arg(sfx_path);
  }
  return result;
}

void verify_direct_cli_success(const DirectCliRunResult& result,
                               const QString& context) {
  QVERIFY2(result.started, qPrintable(result.error_message));
  QVERIFY2(result.finished, qPrintable(result.error_message));
  QVERIFY2(result.error_message.trimmed().isEmpty(),
           qPrintable(result.error_message));
  QVERIFY2(result.exit_code == 0,
           qPrintable(QStringLiteral("%1 failed with exit=%2\nstdout:\n%3\nstderr:\n%4")
                          .arg(context)
                          .arg(result.exit_code)
                          .arg(QString::fromLocal8Bit(result.stdout_data))
                          .arg(QString::fromLocal8Bit(result.stderr_data))));
}

void verify_button_box_baseline(QDialogButtonBox* box, const QWidget* reference) {
  QVERIFY(box != nullptr);
  const int expected_width = z7::platform::qt::dialog_button_min_width(reference);
  const int expected_height = z7::platform::qt::dialog_button_min_height(reference);

  const QList<QAbstractButton*> buttons = box->buttons();
  QVERIFY(!buttons.isEmpty());
  for (QAbstractButton* button : buttons) {
    auto* push = qobject_cast<QPushButton*>(button);
    if (push == nullptr) {
      continue;
    }
    QVERIFY(push->minimumWidth() >= expected_width);
    QVERIFY(push->minimumHeight() >= expected_height);
  }
}

bool has_visible_task_progress_dialog();

bool capture_test_result_dialog(QDialog* dialog,
                                QString* captured_text,
                                bool* progress_visible) {
  if (dialog == nullptr ||
      dialog->objectName() != QStringLiteral("testResultDialog")) {
    return false;
  }
  if (captured_text != nullptr) {
    if (auto* label =
            dialog->findChild<QLabel*>(QStringLiteral("testResultTextLabel"));
        label != nullptr) {
      *captured_text = label->text();
    }
  }
  if (progress_visible != nullptr) {
    *progress_visible = has_visible_task_progress_dialog();
  }
  return true;
}

bool request_close_test_result_dialog(QDialog* dialog,
                                      QString* captured_text,
                                      bool* progress_visible) {
  if (!capture_test_result_dialog(dialog, captured_text, progress_visible)) {
    return false;
  }
  QTimer::singleShot(0, dialog, &QDialog::accept);
  return true;
}

bool request_close_visible_test_result_dialog(QString* captured_text,
                                             bool* progress_visible) {
  if (auto* active =
          qobject_cast<QDialog*>(QApplication::activeModalWidget());
      active != nullptr &&
      request_close_test_result_dialog(active,
                                       captured_text,
                                       progress_visible)) {
    return true;
  }

  const QWidgetList widgets = QApplication::allWidgets();
  for (QWidget* widget : widgets) {
    auto* dialog = qobject_cast<QDialog*>(widget);
    if (dialog == nullptr || !dialog->isVisible()) {
      continue;
    }
    if (request_close_test_result_dialog(dialog,
                                         captured_text,
                                         progress_visible)) {
      return true;
    }
  }
  return false;
}

QString visible_dialog_snapshot() {
  QStringList rows;
  const QWidgetList widgets = QApplication::allWidgets();
  for (QWidget* widget : widgets) {
    auto* dialog = qobject_cast<QDialog*>(widget);
    if (dialog == nullptr || !dialog->isVisible()) {
      continue;
    }
    rows << QStringLiteral("%1:%2")
                .arg(dialog->objectName(), dialog->windowTitle());
  }
  return rows.join(QStringLiteral(", "));
}

bool has_visible_task_progress_dialog() {
  const QWidgetList widgets = QApplication::allWidgets();
  for (QWidget* widget : widgets) {
    auto* dialog = qobject_cast<QDialog*>(widget);
    if (dialog != nullptr &&
        dialog->objectName() == QStringLiteral("taskProgressDialog") &&
        dialog->isVisible()) {
      return true;
    }
  }
  return false;
}

QDialog* visible_task_progress_dialog() {
  const QWidgetList widgets = QApplication::allWidgets();
  for (QWidget* widget : widgets) {
    auto* dialog = qobject_cast<QDialog*>(widget);
    if (dialog != nullptr &&
        dialog->objectName() == QStringLiteral("taskProgressDialog") &&
        dialog->isVisible()) {
      return dialog;
    }
  }
  return nullptr;
}

bool has_visible_test_result_dialog() {
  const QWidgetList widgets = QApplication::allWidgets();
  for (QWidget* widget : widgets) {
    auto* dialog = qobject_cast<QDialog*>(widget);
    if (dialog != nullptr &&
        dialog->objectName() == QStringLiteral("testResultDialog") &&
        dialog->isVisible()) {
      return true;
    }
  }
  return false;
}

bool has_visible_message_box() {
  const QWidgetList widgets = QApplication::allWidgets();
  for (QWidget* widget : widgets) {
    auto* box = qobject_cast<QMessageBox*>(widget);
    if (box != nullptr && box->isVisible()) {
      return true;
    }
  }
  return false;
}

QString localized_label(uint32_t id) {
  return z7::ui::runtime_support::strip_mnemonic(
      z7::ui::runtime_support::L(id));
}

QStringList dialog_label_texts(const QWidget& dialog) {
  QStringList texts;
  const QList<QLabel*> labels = dialog.findChildren<QLabel*>();
  for (const QLabel* label : labels) {
    if (label != nullptr && !label->text().isEmpty()) {
      texts.push_back(label->text());
    }
  }
  return texts;
}

struct DialogLifecycleRunResult {
  bool finished = false;
  z7::ui::gui::GuiTaskCompletion completion;
  bool saw_progress_dialog = false;
  bool saw_progress_behind_summary = false;
  QString summary_text;
  QStringList dialog_history;
};

struct FailureProgressRunResult {
  bool opened = false;
  bool finished_before_close = false;
  bool finished_after_close = false;
  z7::ui::gui::GuiTaskCompletion completion;
  QString title;
  QString log_text;
  QStringList message_numbers;
  QStringList message_text_rows;
  int progress_minimum = 0;
  int progress_maximum = 0;
  int progress_value = 0;
  bool saw_test_result_dialog = false;
  bool saw_message_box = false;
};

class TestResultDialogCloser final : public QObject {
 public:
  explicit TestResultDialogCloser(
      std::shared_ptr<DialogLifecycleRunResult> state)
      : state_(std::move(state)) {
    QApplication::instance()->installEventFilter(this);
    fallback_.setInterval(10);
    fallback_.setParent(this);
    QObject::connect(&fallback_, &QTimer::timeout, [this]() { scan(); });
    fallback_.start();
  }

  ~TestResultDialogCloser() override {
    fallback_.stop();
    if (QApplication::instance() != nullptr) {
      QApplication::instance()->removeEventFilter(this);
    }
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event != nullptr && event->type() == QEvent::Show) {
      if (auto* dialog = qobject_cast<QDialog*>(watched);
          dialog != nullptr && request_close(dialog)) {
        return false;
      }
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  void scan() {
    const QString snapshot = visible_dialog_snapshot();
    if (!snapshot.isEmpty() &&
        (state_->dialog_history.isEmpty() ||
         state_->dialog_history.back() != snapshot)) {
      state_->dialog_history << snapshot;
    }
    state_->saw_progress_dialog =
        state_->saw_progress_dialog || has_visible_task_progress_dialog();

    QString captured;
    bool progress_visible = false;
    if (!request_close_visible_test_result_dialog(&captured,
                                                  &progress_visible)) {
      return;
    }
    state_->summary_text = captured;
    state_->saw_progress_dialog =
        state_->saw_progress_dialog || progress_visible;
    state_->saw_progress_behind_summary = progress_visible;
    fallback_.stop();
  }

  bool request_close(QDialog* dialog) {
    QString captured;
    bool progress_visible = false;
    if (!request_close_test_result_dialog(dialog,
                                          &captured,
                                          &progress_visible)) {
      return false;
    }
    state_->summary_text = captured;
    state_->saw_progress_dialog =
        state_->saw_progress_dialog || progress_visible;
    state_->saw_progress_behind_summary = progress_visible;
    fallback_.stop();
    return true;
  }

  std::shared_ptr<DialogLifecycleRunResult> state_;
  QTimer fallback_;
};

void close_all_visible_dialogs() {
  const QWidgetList top_levels = QApplication::topLevelWidgets();
  for (QWidget* widget : top_levels) {
    auto* dialog = qobject_cast<QDialog*>(widget);
    if (dialog != nullptr && dialog->isVisible()) {
      dialog->reject();
    }
  }
}

DialogLifecycleRunResult run_test_result_lifecycle_case(
    const z7::ui::gui::GuiTaskSpec& spec) {
  auto state = std::make_shared<DialogLifecycleRunResult>();
  z7::ui::gui::GuiAppController controller;
  TestResultDialogCloser closer(state);

  controller.run_task_spec_async(
      spec,
      QStringLiteral("Testing"),
      {},
      [state](const z7::ui::gui::GuiTaskCompletion& result) {
        state->completion = result;
        state->finished = true;
      });

  const bool completed =
      QTest::qWaitFor([state]() { return state->finished; }, 20000);
  if (!completed) {
    close_all_visible_dialogs();
    QTest::qWait(100);
  }
  return *state;
}

FailureProgressRunResult run_failure_progress_lifecycle_case(
    const z7::ui::gui::GuiTaskSpec& spec,
    const QString& title) {
  FailureProgressRunResult state;
  z7::ui::gui::GuiAppController controller;
  bool finished = false;

  controller.run_task_spec_async(
      spec,
      title,
      {},
      [&state, &finished](const z7::ui::gui::GuiTaskCompletion& result) {
        state.completion = result;
        finished = true;
      });

  QDialog* progress_dialog = nullptr;
  state.opened = QTest::qWaitFor(
      [&]() {
        progress_dialog = visible_task_progress_dialog();
        if (progress_dialog == nullptr) {
          return false;
        }
        auto* close_button = progress_dialog->findChild<QPushButton*>(
            QStringLiteral("taskProgressCloseButton"));
        return close_button != nullptr && close_button->isVisible();
      },
      20000);

  if (!state.opened || progress_dialog == nullptr) {
    close_all_visible_dialogs();
    QTest::qWait(100);
    return state;
  }

  state.finished_before_close = finished;
  state.saw_test_result_dialog = has_visible_test_result_dialog();
  state.saw_message_box = has_visible_message_box();
  state.title = progress_dialog->windowTitle();
  if (auto* message_table = progress_dialog->findChild<QTableWidget*>(
          QStringLiteral("taskProgressMessagesList"));
      message_table != nullptr && message_table->isVisible()) {
    for (int row = 0; row < message_table->rowCount(); ++row) {
      QTableWidgetItem* number_item = message_table->item(row, 0);
      QTableWidgetItem* message_item = message_table->item(row, 1);
      state.message_numbers << (number_item == nullptr ? QString() : number_item->text());
      state.message_text_rows << (message_item == nullptr ? QString() : message_item->text());
    }
    state.log_text = state.message_text_rows.join(QLatin1Char('\n'));
  }
  if (auto* progress_bar = progress_dialog->findChild<QProgressBar*>();
      progress_bar != nullptr) {
    state.progress_minimum = progress_bar->minimum();
    state.progress_maximum = progress_bar->maximum();
    state.progress_value = progress_bar->value();
  }

  auto* close_button = progress_dialog->findChild<QPushButton*>(
      QStringLiteral("taskProgressCloseButton"));
  if (close_button != nullptr && close_button->isEnabled()) {
    QTimer::singleShot(0, close_button, &QPushButton::click);
    state.finished_after_close =
        QTest::qWaitFor([&]() { return finished; }, 5000);
  }
  return state;
}

void clear_shared_startup_settings() {
  z7::platform::qt::PortableSettings shared(
      QCoreApplication::organizationName(),
      QStringLiteral("7z-shared"));
  shared.clear();
  shared.sync();
}

void clear_all_smoke_settings() {
  z7::platform::qt::PortableSettings app_settings;
  app_settings.clear();
  app_settings.sync();
  clear_shared_startup_settings();
}

void accept_next_modal_message_box() {
  QTimer::singleShot(0, []() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
      auto* box = qobject_cast<QMessageBox*>(widget);
      if (box != nullptr && box->isVisible()) {
        box->done(QMessageBox::Ok);
        return;
      }
    }
  });
}

struct FileManagerRealExtractResult {
  bool launched = false;
  bool completed = false;
  QString error_message;
  z7::task_ipc_runtime::TaskIpcEvent completion;
};

FileManagerRealExtractResult launch_file_manager_real_7zg_extract_and_wait(
    const QString& current_dir,
    const QStringList& archive_inputs,
    const QString& output_dir,
    const QString& settings_root) {
  FileManagerRealExtractResult result;

  const QFileInfo side_by_side_7zg(side_by_side_7zg_program());
  if (!side_by_side_7zg.exists() || !side_by_side_7zg.isExecutable()) {
    result.error_message =
        QStringLiteral("Missing executable side-by-side 7zG: %1")
            .arg(side_by_side_7zg.absoluteFilePath());
    return result;
  }

  const ScopedEnvironmentVariable child_settings_root_env(
      z7::platform::qt::portable_settings_internal::kTestRootEnv,
      QFile::encodeName(settings_root));
  const ScopedEnvironmentVariable suppress_dialogs(
      "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
      QByteArrayLiteral("1"));

  z7::ui::filemanager::MainWindow window;
  window.set_current_directory(current_dir);
  const QString located_worker =
      QFileInfo(window.locate_7zg_program()).absoluteFilePath();
  if (located_worker != side_by_side_7zg.absoluteFilePath()) {
    result.error_message =
        QStringLiteral("MainWindow located %1 instead of side-by-side 7zG %2.")
            .arg(located_worker, side_by_side_7zg.absoluteFilePath());
    return result;
  }

  const QString owner_instance_id =
      QUuid::createUuid().toString(QUuid::WithoutBraces);
  // Keep completion collection deterministic: the test owns the ack instead
  // of racing MainWindow's queued completion notifier.
  window.task_ipc_owner_instance_id_ = owner_instance_id;
  window.task_ipc_completion_notifier_registered_ = true;

  z7::task_ipc_runtime::TaskIpcPayload payload;
  payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
  payload.show_dialog = false;
  payload.refresh_after_finish = false;
  payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
  payload.extract->archive_inputs = archive_inputs;
  payload.extract->output_dir = output_dir;
  payload.extract->overwrite_switch = QStringLiteral("-aoa");

  accept_next_modal_message_box();
  result.launched =
      window.launch_gui_subprocess_task(QStringLiteral("Smoke Extract"),
                                        payload);
  if (!result.launched) {
    result.error_message =
        QStringLiteral("MainWindow failed to launch real 7zG extract task.");
    return result;
  }

  if (!wait_for_task_ipc_completion(
          owner_instance_id,
          z7::task_ipc_runtime::TaskIpcCommandKind::kExtract,
          &result.completion,
          &result.error_message)) {
    return result;
  }

  result.completed = true;
  QString ack_error;
  if (!z7::task_ipc_runtime::acknowledge_task_ipc_event(result.completion,
                                                        &ack_error)) {
    result.error_message = ack_error;
  }
  return result;
}

}  // namespace

class HiDpiSmokeTest final : public QObject {
  Q_OBJECT

 private slots:
  void startupPolicyIsSharedAcrossEntrypoints() {
    const z7::platform::qt::AppStartupConfig fm =
        z7::platform::qt::default_startup_config(
            z7::platform::qt::StartupAppKind::kFileManager);
    const z7::platform::qt::AppStartupConfig gui =
        z7::platform::qt::default_startup_config(
            z7::platform::qt::StartupAppKind::kGui);

    QCOMPARE(fm.organization_name, gui.organization_name);
    QCOMPARE(fm.preferred_style, gui.preferred_style);
    QCOMPARE(fm.window_icon_resource, gui.window_icon_resource);
    QCOMPARE(fm.hidpi.scale_factor_rounding, gui.hidpi.scale_factor_rounding);
    QCOMPARE(QGuiApplication::highDpiScaleFactorRoundingPolicy(),
             fm.hidpi.scale_factor_rounding);
    QVERIFY(QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs));
  }

  void messageBoxesUseQtDialogBackendByGlobalStartupPolicy() {
    QMessageBox box;
    box.setText(QStringLiteral("Qt dialog backend smoke"));
    box.setStandardButtons(QMessageBox::Ok);
    QVERIFY(QCoreApplication::testAttribute(Qt::AA_DontUseNativeDialogs));
  }

  void guiTaskProgressDialogUsesCompactSizeAndVisiblePercentText() {
    z7::ui::gui::TaskProgressDialog dialog;
    QCOMPARE(dialog.size(), QSize(840, 500));

    QProgressBar* progress_bar = dialog.findChild<QProgressBar*>();
    QVERIFY(progress_bar != nullptr);
    QVERIFY(progress_bar->isTextVisible());
  }

  void guiTaskProgressDialogPauseBackgroundAndCancelInteractions() {
    z7::ui::gui::TaskProgressDialog dialog;
    dialog.set_header(QStringLiteral("Adding: sample.7z"));
    dialog.set_stage(QStringLiteral("Adding"));
    dialog.set_percent(17);
    dialog.set_running(true);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* background_button =
        dialog.findChild<QPushButton*>(QStringLiteral("taskProgressBackgroundButton"));
    auto* pause_button =
        dialog.findChild<QPushButton*>(QStringLiteral("taskProgressPauseButton"));
    auto* cancel_button =
        dialog.findChild<QPushButton*>(QStringLiteral("taskProgressCancelButton"));
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
    QCOMPARE(pause_spy.count(), 1);
    QCOMPARE(resume_spy.count(), 0);
    pause_button->click();
    QCOMPARE(resume_spy.count(), 1);

    background_button->click();
    QCOMPARE(background_spy.count(), 1);
    QVERIFY(background_spy.takeFirst().at(0).toBool());
    background_button->click();
    QCOMPARE(background_spy.count(), 1);
    QVERIFY(!background_spy.takeFirst().at(0).toBool());

    int confirmation_count = 0;
    dialog.set_cancel_confirmation_handler([&confirmation_count]() -> int {
      ++confirmation_count;
      if (confirmation_count == 1) {
        return static_cast<int>(QMessageBox::No);
      }
      if (confirmation_count == 2) {
        return static_cast<int>(QMessageBox::Cancel);
      }
      return static_cast<int>(QMessageBox::Yes);
    });

    cancel_button->click();
    QCOMPARE(confirmation_count, 1);
    QCOMPARE(cancel_spy.count(), 0);
    cancel_button->click();
    QCOMPARE(confirmation_count, 2);
    QCOMPARE(cancel_spy.count(), 0);
    cancel_button->click();
    QCOMPARE(confirmation_count, 3);
    QCOMPARE(cancel_spy.count(), 1);
    QCOMPARE(pause_spy.count(), 4);
    QCOMPARE(resume_spy.count(), 4);

    dialog.set_cancel_confirmation_handler([&confirmation_count]() -> int {
      ++confirmation_count;
      return static_cast<int>(QMessageBox::Yes);
    });
    dialog.close();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(confirmation_count, 4);
    QCOMPARE(cancel_spy.count(), 2);
    QVERIFY(dialog.isVisible());

    dialog.set_running(false);
    dialog.close();
  }

  void taskProgressDialogShowsFailureMessagesWhileRunning() {
    z7::ui::gui::TaskProgressDialog dialog;
    dialog.set_header(QStringLiteral("Testing: sample.7z"));
    dialog.set_test_mode(true);
    dialog.set_running(true);
    dialog.set_percent(42);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    dialog.append_failure_result_message(
        QStringLiteral("/tmp/z7-sample-archive-structure.pZDUyF/root/readme-root/readme.txt\n"
                       "Cannot open file as archive"));

    auto* message_table =
        dialog.findChild<QTableWidget*>(QStringLiteral("taskProgressMessagesList"));
    QVERIFY(message_table != nullptr);
    QVERIFY(message_table->isVisible());
    QTRY_COMPARE_WITH_TIMEOUT(message_table->rowCount(), 2, 1000);
    QCOMPARE(message_table->item(0, 0)->text(), QStringLiteral("1"));
    QCOMPARE(message_table->item(1, 0)->text(), QString());
    const int compact_row_height = message_table->fontMetrics().lineSpacing() * 2;
    QTRY_VERIFY_WITH_TIMEOUT(message_table->rowHeight(0) <= compact_row_height,
                             1000);
    QVERIFY(message_table->rowHeight(1) <= compact_row_height);

    auto* close_button =
        dialog.findChild<QPushButton*>(QStringLiteral("taskProgressCloseButton"));
    auto* cancel_button =
        dialog.findChild<QPushButton*>(QStringLiteral("taskProgressCancelButton"));
    QVERIFY(close_button != nullptr);
    QVERIFY(cancel_button != nullptr);
    QVERIFY(!close_button->isVisible());
    QVERIFY(cancel_button->isVisible());
  }

  void taskProgressDialogDisplaysOriginalRatioMetrics() {
    z7::ui::gui::TaskProgressDialog no_ratio_dialog;
    no_ratio_dialog.set_detailed_progress(true,
                                          1000,
                                          123,
                                          0,
                                          0,
                                          0,
                                          std::nullopt,
                                          QString());
    const QStringList no_ratio_texts = dialog_label_texts(no_ratio_dialog);
    QCOMPARE(no_ratio_texts.count(QStringLiteral("123 B")), 1);
    QVERIFY(!no_ratio_texts.contains(QStringLiteral("100%")));

    z7::ui::runtime_support::TaskProgressRatioInfo compress_ratio;
    compress_ratio.input_size_known = true;
    compress_ratio.input_size = 200;
    compress_ratio.output_size_known = true;
    compress_ratio.output_size = 50;
    compress_ratio.compressing_mode = true;

    z7::ui::gui::TaskProgressDialog compress_dialog;
    compress_dialog.set_detailed_progress(true,
                                          1000,
                                          123,
                                          0,
                                          0,
                                          0,
                                          compress_ratio,
                                          QString());
    const QStringList compress_texts = dialog_label_texts(compress_dialog);
    QVERIFY(compress_texts.contains(QStringLiteral("200 B")));
    QVERIFY(compress_texts.contains(QStringLiteral("50 B")));
    QVERIFY(compress_texts.contains(QStringLiteral("25%")));

    z7::ui::runtime_support::TaskProgressRatioInfo extract_ratio;
    extract_ratio.input_size_known = true;
    extract_ratio.input_size = 80;
    extract_ratio.output_size_known = true;
    extract_ratio.output_size = 320;
    extract_ratio.compressing_mode = false;

    z7::ui::gui::TaskProgressDialog extract_dialog;
    extract_dialog.set_detailed_progress(true,
                                         1000,
                                         123,
                                         0,
                                         0,
                                         0,
                                         extract_ratio,
                                         QString());
    const QStringList extract_texts = dialog_label_texts(extract_dialog);
    QVERIFY(extract_texts.contains(QStringLiteral("320 B")));
    QVERIFY(extract_texts.contains(QStringLiteral("80 B")));
    QVERIFY(extract_texts.contains(QStringLiteral("25%")));
  }

  void externalLanguageResourcesArePresentAndParseable() {
    QString error;
    QVERIFY2(z7::ui::runtime_support::OfficialLangCatalog::validate_required_language_resources(&error),
             qPrintable(error));

    const QString en_path =
        z7::ui::runtime_support::OfficialLangCatalog::required_english_language_file_path();
    const QFileInfo en_info(en_path);
    QVERIFY(en_info.exists());
    QVERIFY(en_info.isFile());
  }

  void persistedStartupOverridesAreSharedAcrossEntrypoints() {
    clear_shared_startup_settings();

    z7::platform::qt::AppStartupConfig startup =
        z7::platform::qt::default_startup_config(
            z7::platform::qt::StartupAppKind::kFileManager);
    startup.preferred_style = QStringLiteral("totally-invalid-style");
    startup.hidpi.scale_factor_rounding =
        Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor;
    z7::platform::qt::persist_startup_overrides(startup);

    const z7::platform::qt::AppStartupConfig fm =
        z7::platform::qt::startup_config_with_persisted_overrides(
            z7::platform::qt::StartupAppKind::kFileManager);
    const z7::platform::qt::AppStartupConfig gui =
        z7::platform::qt::startup_config_with_persisted_overrides(
            z7::platform::qt::StartupAppKind::kGui);

    QCOMPARE(fm.preferred_style, QStringLiteral("totally-invalid-style"));
    QCOMPARE(gui.preferred_style, fm.preferred_style);
    QCOMPARE(gui.hidpi.scale_factor_rounding, fm.hidpi.scale_factor_rounding);

    clear_shared_startup_settings();
  }

  void portableSettingsDefaultRootUsesGlobalConfigPath() {
    const QString expected_root =
        QDir::home().filePath(QStringLiteral(".config/7zqt"));
    const QString expected_file =
        QDir(expected_root).filePath(QStringLiteral("settings.json"));

    const QString plain_dir = QStringLiteral("/tmp/z7-plain-root");
    QCOMPARE(
        z7::platform::qt::portable_settings_internal::
            default_portable_settings_root_for_application_dir(plain_dir),
        expected_root);
    QCOMPARE(
        z7::platform::qt::portable_settings_internal::
            default_portable_settings_root_for_application_dir(
                QStringLiteral("/tmp/SevenZip.app/Contents/MacOS")),
        expected_root);
    QCOMPARE(
        z7::platform::qt::portable_settings_internal::
            default_portable_settings_root_for_application_dir(
                QStringLiteral("/tmp/SevenZip.appex/Contents/MacOS")),
        expected_root);
    QCOMPARE(
        z7::platform::qt::portable_settings_internal::
            default_portable_settings_root_for_executable_hint(
                QStringLiteral("/tmp/SevenZip.app/Contents/MacOS/7zFM")),
        expected_root);
    QCOMPARE(
        z7::platform::qt::portable_settings_internal::
            default_portable_settings_root_for_executable_hint(
                QStringLiteral("/tmp/z7-plain-root/7zFM")),
        expected_root);

    const QString test_root = z7::platform::qt::portable_settings_root_dir();
    const ScopedPortableSettingsRoot restore_settings_root(test_root);
    z7::platform::qt::set_portable_settings_root(QString());
    QCOMPARE(z7::platform::qt::portable_settings_root_dir(), expected_root);
    QCOMPARE(z7::platform::qt::portable_settings_file_path(), expected_file);

    const ScopedEnvironmentVariable restore_env("Z7_PORTABLE_SETTINGS_ROOT");
    qputenv("Z7_PORTABLE_SETTINGS_ROOT", "/tmp/z7-env-root");
    QCOMPARE(z7::platform::qt::portable_settings_root_dir(), expected_root);
    QCOMPARE(z7::platform::qt::portable_settings_file_path(), expected_file);

    const ScopedEnvironmentVariable restore_test_env(
        z7::platform::qt::portable_settings_internal::kTestRootEnv);
    qputenv(z7::platform::qt::portable_settings_internal::kTestRootEnv,
            "/tmp/z7-test-env-root");
    QCOMPARE(z7::platform::qt::portable_settings_root_dir(),
             QStringLiteral("/tmp/z7-test-env-root"));
    QCOMPARE(z7::platform::qt::portable_settings_file_path(),
             QStringLiteral("/tmp/z7-test-env-root/settings.json"));

    z7::platform::qt::set_portable_settings_root(test_root);
    QString init_error;
    QVERIFY2(z7::platform::qt::initialize_portable_settings(&init_error),
             qPrintable(init_error));
  }

  void invalidPreferredStyleDoesNotApplyAnyFallbackStyle() {
    const QStringList styles = z7::platform::qt::available_qt_styles();
    QVERIFY(!styles.isEmpty());

    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    QVERIFY(app != nullptr);
    const QString original_style = app->style() != nullptr ? app->style()->objectName() : QString();

    z7::platform::qt::AppStartupConfig startup =
        z7::platform::qt::default_startup_config(
            z7::platform::qt::StartupAppKind::kFileManager);
    startup.preferred_style = QStringLiteral("__invalid_style_name__");
    z7::platform::qt::apply_post_app_startup(*app, startup);

    QVERIFY(app->style() != nullptr);
    QCOMPARE(app->style()->objectName().compare(original_style, Qt::CaseInsensitive), 0);

    z7::platform::qt::AppStartupConfig restore =
        z7::platform::qt::default_startup_config(
            z7::platform::qt::StartupAppKind::kFileManager);
    restore.preferred_style = original_style;
    z7::platform::qt::apply_post_app_startup(*app, restore);
  }

  void fileManagerUsesCentralizedHiDpiMetrics() {
    z7::ui::filemanager::MainWindow window;
    window.show();
    QApplication::processEvents();

    QVERIFY(window.path_combo_ != nullptr);
    const int expected_small_icon = z7::platform::qt::small_icon_extent(&window);
    QCOMPARE(window.path_combo_->iconSize(),
             QSize(expected_small_icon, expected_small_icon));

    const int expected_small_toolbar_icon =
        z7::platform::qt::toolbar_icon_extent(false, &window);
    QCOMPARE(window.archive_toolbar_->iconSize(),
             QSize(expected_small_toolbar_icon, expected_small_toolbar_icon));
    QCOMPARE(window.standard_toolbar_->iconSize(),
             QSize(expected_small_toolbar_icon, expected_small_toolbar_icon));

    window.large_buttons_action_->setChecked(true);
    window.apply_runtime_settings();
    const int expected_large_toolbar_icon =
        z7::platform::qt::toolbar_icon_extent(true, &window);
    QCOMPARE(window.archive_toolbar_->iconSize(),
             QSize(expected_large_toolbar_icon, expected_large_toolbar_icon));
    QCOMPARE(window.standard_toolbar_->iconSize(),
             QSize(expected_large_toolbar_icon, expected_large_toolbar_icon));

    window.apply_view_mode_to_panel(0, z7::ui::filemanager::MainWindow::PanelController::kViewModeLargeIcons);
    auto* icon_view = window.panels_[0].ui.icon_list_view;
    QVERIFY(icon_view != nullptr);
    const int expected_large_list_icon =
        z7::platform::qt::file_list_icon_extent(true, &window);
    QCOMPARE(icon_view->iconSize(),
             QSize(expected_large_list_icon, expected_large_list_icon));
    QCOMPARE(icon_view->gridSize(),
             z7::platform::qt::file_list_grid_size(true, &window));
    QVERIFY(icon_view->gridSize().height() > icon_view->iconSize().height());

    window.apply_view_mode_to_panel(0, z7::ui::filemanager::MainWindow::PanelController::kViewModeSmallIcons);
    const int expected_small_list_icon =
        z7::platform::qt::file_list_icon_extent(false, &window);
    QCOMPARE(icon_view->iconSize(),
             QSize(expected_small_list_icon, expected_small_list_icon));
    QCOMPARE(icon_view->gridSize(),
             z7::platform::qt::file_list_grid_size(false, &window));
  }

  void dialogButtonsFollowHiDpiBaseline() {
    z7::ui::filemanager::OptionsDialog options_dialog;
    auto* options_buttons = options_dialog.findChild<QDialogButtonBox*>(
        QStringLiteral("optionsDialogButtons"));
    verify_button_box_baseline(options_buttons, &options_dialog);

    z7::ui::gui::ExtractCommandOptions extract_options;
    z7::ui::gui::ExtractDialog extract_dialog(extract_options);
    verify_button_box_baseline(
        extract_dialog.findChild<QDialogButtonBox*>(), &extract_dialog);

    z7::ui::gui::CompressCommandOptions compress_options;
    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    verify_button_box_baseline(
        compress_dialog.findChild<QDialogButtonBox*>(), &compress_dialog);
  }

  void extractDialogStoresFinalSplitDestinationHistory() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString previous_root = z7::platform::qt::portable_settings_root_dir();
    const ScopedPortableSettingsRoot restore_settings_root(previous_root);
    z7::platform::qt::set_portable_settings_root(root.path());
    QString init_error;
    QVERIFY2(z7::platform::qt::initialize_portable_settings(&init_error),
             qPrintable(init_error));

    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
    z7::ui::gui::ExtractCommandOptions options;
    options.archive_name = to_native_path_string(
        QDir(root.path()).filePath(QStringLiteral("pack:bad.7z")));
    options.output_dir = to_native_path_string(output_dir);
    options.split_dest_enabled = true;
    options.split_dest_name = to_native_path_string(QStringLiteral("pack_bad"));

    z7::ui::gui::ExtractDialog dialog(options);
    auto* path_mode_combo =
        dialog.findChild<QComboBox*>(QStringLiteral("extractPathModeCombo"));
    auto* overwrite_combo =
        dialog.findChild<QComboBox*>(QStringLiteral("extractOverwriteCombo"));
    auto* eliminate_dup =
        dialog.findChild<QCheckBox*>(QStringLiteral("extractEliminateDupCheckBox"));
    QVERIFY(path_mode_combo != nullptr);
    QVERIFY(overwrite_combo != nullptr);
    QVERIFY(eliminate_dup != nullptr);
    const int no_paths_index = path_mode_combo->findData(QStringLiteral("no"));
    const int overwrite_all_index = overwrite_combo->findData(QStringLiteral("-aoa"));
    QVERIFY(no_paths_index >= 0);
    QVERIFY(overwrite_all_index >= 0);
    path_mode_combo->setCurrentIndex(no_paths_index);
    overwrite_combo->setCurrentIndex(overwrite_all_index);
    eliminate_dup->setChecked(false);

    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    QVERIFY(buttons != nullptr);
    auto* ok_button = buttons->button(QDialogButtonBox::Ok);
    QVERIFY(ok_button != nullptr);
    ok_button->click();

    z7::platform::qt::PortableSettings settings;
    const QStringList history =
        settings.value(QStringLiteral("Extraction/PathHistory")).toStringList();
    QVERIFY(!history.isEmpty());
    QCOMPARE(history.front(),
             QDir(output_dir).filePath(QStringLiteral("pack_bad")));
    QCOMPARE(settings.value(QStringLiteral("Extraction/ExtractMode")).toInt(), 2);
    QCOMPARE(settings.value(QStringLiteral("Extraction/OverwriteMode")).toInt(), 1);
    QCOMPARE(settings.value(QStringLiteral("Extraction/ElimDup")).toBool(), false);
    QVERIFY(settings.contains(QStringLiteral("Extraction/SplitDest")));
    QVERIFY(!settings.contains(QStringLiteral("Gui/Extract/PathHistory")));
    QVERIFY(!settings.contains(QStringLiteral("Gui/Extract/PathMode")));

    settings.clear();
    settings.setValue(QStringLiteral("Gui/Extract/PathHistory"),
                      QStringList({QDir(root.path()).filePath(QStringLiteral("legacy"))}));
    settings.setValue(QStringLiteral("Gui/Extract/PathMode"), QStringLiteral("no"));
    settings.setValue(QStringLiteral("Gui/Extract/OverwriteMode"), QStringLiteral("-aoa"));

    const QString second_output_dir =
        QDir(root.path()).filePath(QStringLiteral("second-out"));
    options.output_dir = to_native_path_string(second_output_dir);
    options.split_dest_enabled = false;
    options.split_dest_name.clear();
    z7::ui::gui::ExtractDialog legacy_ignored_dialog(options);
    auto* legacy_path_mode =
        legacy_ignored_dialog.findChild<QComboBox*>(QStringLiteral("extractPathModeCombo"));
    auto* legacy_overwrite =
        legacy_ignored_dialog.findChild<QComboBox*>(QStringLiteral("extractOverwriteCombo"));
    QVERIFY(legacy_path_mode != nullptr);
    QVERIFY(legacy_overwrite != nullptr);
    QCOMPARE(legacy_path_mode->currentData().toString(), QStringLiteral("full"));
    QCOMPARE(legacy_overwrite->currentData().toString(), QString());

    auto* legacy_buttons = legacy_ignored_dialog.findChild<QDialogButtonBox*>();
    QVERIFY(legacy_buttons != nullptr);
    auto* legacy_ok = legacy_buttons->button(QDialogButtonBox::Ok);
    QVERIFY(legacy_ok != nullptr);
    legacy_ok->click();

    const QStringList second_history =
        settings.value(QStringLiteral("Extraction/PathHistory")).toStringList();
    QVERIFY(!second_history.isEmpty());
    QCOMPARE(second_history.front(), second_output_dir);
    QVERIFY(!second_history.contains(
        QDir(root.path()).filePath(QStringLiteral("legacy"))));
  }

  void addDialogSingleFileFormatsMatchOriginal() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString file_path =
        QDir(root.path()).filePath(QStringLiteral("alpha.txt"));
    QVERIFY(write_file(file_path, QByteArrayLiteral("alpha-content")));

    z7::ui::gui::CompressCommandOptions compress_options;
    compress_options.archive_path = to_native_path_string(file_path);
    compress_options.keep_archive_name_extension = false;
    compress_options.single_file_input = true;
    compress_options.single_file_name = "alpha.txt";

    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    auto* format_combo =
        compress_dialog.findChild<QComboBox*>(QStringLiteral("formatCombo"));
    QVERIFY(format_combo != nullptr);
    QCOMPARE(combo_data_values(format_combo),
             QStringList({QStringLiteral("7z"),
                          QStringLiteral("bzip2"),
                          QStringLiteral("gzip"),
                          QStringLiteral("tar"),
                          QStringLiteral("wim"),
                          QStringLiteral("xz"),
                          QStringLiteral("zip")}));
  }

  void addDialogMultipleFileFormatsMatchOriginal() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());
    const QString alpha = root_dir.filePath(QStringLiteral("alpha.txt"));
    const QString beta = root_dir.filePath(QStringLiteral("beta.txt"));
    QVERIFY(write_file(alpha, QByteArrayLiteral("alpha")));
    QVERIFY(write_file(beta, QByteArrayLiteral("beta")));

    z7::ui::gui::CompressCommandOptions compress_options;
    compress_options.archive_path = to_native_path_string(
        root_dir.filePath(QStringLiteral("archive")));
    compress_options.keep_archive_name_extension = true;

    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    auto* format_combo =
        compress_dialog.findChild<QComboBox*>(QStringLiteral("formatCombo"));
    QVERIFY(format_combo != nullptr);
    QCOMPARE(combo_data_values(format_combo),
             QStringList({QStringLiteral("7z"),
                          QStringLiteral("tar"),
                          QStringLiteral("wim"),
                          QStringLiteral("zip")}));
  }

  void addDialogFolderFormatsMatchOriginal() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString folder_path =
        QDir(root.path()).filePath(QStringLiteral("payload"));
    QVERIFY(QDir().mkpath(folder_path));

    z7::ui::gui::CompressCommandOptions compress_options;
    compress_options.archive_path = to_native_path_string(folder_path);
    compress_options.keep_archive_name_extension = true;

    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    auto* format_combo =
        compress_dialog.findChild<QComboBox*>(QStringLiteral("formatCombo"));
    QVERIFY(format_combo != nullptr);
    QCOMPARE(combo_data_values(format_combo),
             QStringList({QStringLiteral("7z"),
                          QStringLiteral("tar"),
                          QStringLiteral("wim"),
                          QStringLiteral("zip")}));
  }

  void addDialogModeCombosMatchOriginal() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString file_path =
        QDir(root.path()).filePath(QStringLiteral("alpha.txt"));
    QVERIFY(write_file(file_path, QByteArrayLiteral("alpha-content")));

    z7::ui::gui::CompressCommandOptions compress_options;
    compress_options.archive_path = to_native_path_string(file_path);
    compress_options.keep_archive_name_extension = false;
    compress_options.single_file_input = true;
    compress_options.single_file_name = "alpha.txt";

    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    auto* update_mode_combo =
        compress_dialog.findChild<QComboBox*>(QStringLiteral("updateModeCombo"));
    auto* path_mode_combo =
        compress_dialog.findChild<QComboBox*>(QStringLiteral("pathModeCombo"));
    QVERIFY(update_mode_combo != nullptr);
    QVERIFY(path_mode_combo != nullptr);

    QCOMPARE(combo_data_values(update_mode_combo),
             QStringList({QStringLiteral("add"),
                          QStringLiteral("update"),
                          QStringLiteral("fresh"),
                          QStringLiteral("sync")}));
    QCOMPARE(update_mode_combo->currentData().toString(),
             QStringLiteral("add"));

    QCOMPARE(combo_data_values(path_mode_combo),
             QStringList({QStringLiteral("relative"),
                          QStringLiteral("full"),
                          QStringLiteral("absolute")}));
    QCOMPARE(path_mode_combo->currentData().toString(),
             QStringLiteral("relative"));

    set_combo_data(update_mode_combo, QStringLiteral("sync"));
    set_combo_data(path_mode_combo, QStringLiteral("absolute"));
    const z7::ui::gui::CompressCommandOptions accepted =
        compress_dialog.options();
    QCOMPARE(QString::fromStdString(accepted.update_mode),
             QStringLiteral("sync"));
    QCOMPARE(QString::fromStdString(accepted.path_mode),
             QStringLiteral("absolute"));
  }

  void compressDialogSingleFileNamingMatchesOriginal() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString file_path =
        QDir(root.path()).filePath(QStringLiteral("alpha.txt"));
    QVERIFY(write_file(file_path, QByteArrayLiteral("alpha-content")));

    z7::ui::gui::CompressCommandOptions compress_options;
    compress_options.archive_path = to_native_path_string(file_path);
    compress_options.keep_archive_name_extension = false;
    compress_options.single_file_input = true;
    compress_options.single_file_name = "alpha.txt";

    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    auto* format_combo =
        compress_dialog.findChild<QComboBox*>(QStringLiteral("formatCombo"));
    QVERIFY(format_combo != nullptr);

    set_combo_data(format_combo, QStringLiteral("gzip"));
    QString archive_path = z7::ui::archive_support::from_native_string(
        compress_dialog.options().archive_path);
    QCOMPARE(QFileInfo(archive_path).fileName(),
             QStringLiteral("alpha.txt.gz"));

    set_combo_data(format_combo, QStringLiteral("7z"));
    archive_path = z7::ui::archive_support::from_native_string(
        compress_dialog.options().archive_path);
    QCOMPARE(QFileInfo(archive_path).fileName(),
             QStringLiteral("alpha.7z"));
  }

  void compressDialogSfxUsesSfxExtensionAndOnly7zEnablesCheckbox() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString file_path =
        QDir(root.path()).filePath(QStringLiteral("alpha.txt"));
    QVERIFY(write_file(file_path, QByteArrayLiteral("alpha-content")));

    z7::ui::gui::CompressCommandOptions compress_options;
    compress_options.archive_path = to_native_path_string(file_path);
    compress_options.archive_type = "7z";
    compress_options.keep_archive_name_extension = false;
    compress_options.single_file_input = true;
    compress_options.single_file_name = "alpha.txt";

    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    auto* format_combo =
        compress_dialog.findChild<QComboBox*>(QStringLiteral("formatCombo"));
    auto* create_sfx =
        compress_dialog.findChild<QCheckBox*>(QStringLiteral("createSfxCheckBox"));
    QVERIFY(format_combo != nullptr);
    QVERIFY(create_sfx != nullptr);
    QCOMPARE(format_combo->currentData().toString(), QStringLiteral("7z"));
    QVERIFY(create_sfx->isEnabled());

    create_sfx->setChecked(true);
    QApplication::processEvents();
    QString archive_path = z7::ui::archive_support::from_native_string(
        compress_dialog.options().archive_path);
    QCOMPARE(QFileInfo(archive_path).fileName(),
             QStringLiteral("alpha.sfx"));

    set_combo_data(format_combo, QStringLiteral("zip"));
    QVERIFY(!create_sfx->isChecked());
    QVERIFY(!create_sfx->isEnabled());
    archive_path = z7::ui::archive_support::from_native_string(
        compress_dialog.options().archive_path);
    QCOMPARE(QFileInfo(archive_path).fileName(),
             QStringLiteral("alpha.zip"));
  }

  void compressDialogUnsupportedEncryptionKeepsDisabledShowPasswordState() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString previous_root = z7::platform::qt::portable_settings_root_dir();
    const ScopedPortableSettingsRoot restore_settings_root(previous_root);
    z7::platform::qt::set_portable_settings_root(root.path());
    QString init_error;
    QVERIFY2(z7::platform::qt::initialize_portable_settings(&init_error),
             qPrintable(init_error));
    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("Compression/ShowPassword"), true);
    }

    const QString file_path =
        QDir(root.path()).filePath(QStringLiteral("alpha.txt"));
    QVERIFY(write_file(file_path, QByteArrayLiteral("alpha-content")));

    z7::ui::gui::CompressCommandOptions compress_options;
    compress_options.archive_path = to_native_path_string(file_path);
    compress_options.archive_type = "gzip";
    compress_options.keep_archive_name_extension = false;
    compress_options.single_file_input = true;
    compress_options.single_file_name = "alpha.txt";

    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    auto* format_combo =
        compress_dialog.findChild<QComboBox*>(QStringLiteral("formatCombo"));
    auto* password_label =
        compress_dialog.findChild<QLabel*>(QStringLiteral("passwordLabel"));
    auto* password_edit =
        compress_dialog.findChild<QLineEdit*>(QStringLiteral("passwordEdit"));
    auto* reenter_password_label =
        compress_dialog.findChild<QLabel*>(QStringLiteral("reenterPasswordLabel"));
    auto* reenter_password_edit =
        compress_dialog.findChild<QLineEdit*>(QStringLiteral("reenterPasswordEdit"));
    auto* show_password =
        compress_dialog.findChild<QCheckBox*>(QStringLiteral("showPasswordCheckBox"));
    auto* encryption_method_label =
        compress_dialog.findChild<QLabel*>(QStringLiteral("encryptionMethodLabel"));
    auto* encryption_method =
        compress_dialog.findChild<QComboBox*>(QStringLiteral("encryptionMethodCombo"));
    QVERIFY(format_combo != nullptr);
    QVERIFY(password_label != nullptr);
    QVERIFY(password_edit != nullptr);
    QVERIFY(reenter_password_label != nullptr);
    QVERIFY(reenter_password_edit != nullptr);
    QVERIFY(show_password != nullptr);
    QVERIFY(encryption_method_label != nullptr);
    QVERIFY(encryption_method != nullptr);
    QCOMPARE(format_combo->currentData().toString(), QStringLiteral("gzip"));
    QVERIFY(show_password->isChecked());
    QVERIFY(!password_label->isEnabled());
    QVERIFY(!password_edit->isEnabled());
    QVERIFY(!reenter_password_label->isEnabled());
    QVERIFY(!reenter_password_edit->isEnabled());
    QVERIFY(!show_password->isEnabled());
    QVERIFY(!encryption_method_label->isEnabled());
    QVERIFY(!encryption_method->isEnabled());
    QCOMPARE(password_edit->echoMode(), QLineEdit::Normal);
    QVERIFY(reenter_password_edit->isHidden());
  }

  void compressDialogPreservesDottedDirectoryArchiveName() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString folder_name =
        QStringLiteral("ffmpeg-n8.1.1-static-nonfree-m4-macos-15-arm64");
    const QString folder_path = QDir(root.path()).filePath(folder_name);
    QVERIFY(QDir().mkpath(folder_path));

    z7::ui::gui::CompressCommandOptions compress_options;
    compress_options.archive_path = to_native_path_string(folder_path);
    compress_options.keep_archive_name_extension = true;

    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    const QString archive_path = z7::ui::archive_support::from_native_string(
        compress_dialog.options().archive_path);
    QCOMPARE(QFileInfo(archive_path).fileName(),
             folder_name + QStringLiteral(".7z"));
  }

  void compressDialogPreservesArchiveSuffixInDirectoryName() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString folder_name = QStringLiteral("release.zip");
    const QString folder_path = QDir(root.path()).filePath(folder_name);
    QVERIFY(QDir().mkpath(folder_path));

    z7::ui::gui::CompressCommandOptions compress_options;
    compress_options.archive_path = to_native_path_string(folder_path);
    compress_options.keep_archive_name_extension = true;

    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    const QString archive_path = z7::ui::archive_support::from_native_string(
        compress_dialog.options().archive_path);
    QCOMPARE(QFileInfo(archive_path).fileName(),
             folder_name + QStringLiteral(".7z"));
  }

  void compressDialogArchiveHistoryKeepsOriginalTwentyItems() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString previous_root = z7::platform::qt::portable_settings_root_dir();
    const ScopedPortableSettingsRoot restore_settings_root(previous_root);
    z7::platform::qt::set_portable_settings_root(root.path());
    QString init_error;
    QVERIFY2(z7::platform::qt::initialize_portable_settings(&init_error),
             qPrintable(init_error));

    QStringList history;
    for (int i = 0; i < 20; ++i) {
      history.push_back(
          QDir(root.path()).filePath(QStringLiteral("old-%1.7z").arg(i)));
    }
    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("Compression/ArcHistory"),
                        history);
    }

    const QString new_archive_input =
        QDir(root.path()).filePath(QStringLiteral("new"));
    const QString new_archive_saved =
        QDir(root.path()).filePath(QStringLiteral("new.7z"));
    z7::ui::gui::CompressCommandOptions compress_options;
    compress_options.archive_path = to_native_path_string(new_archive_input);
    compress_options.keep_archive_name_extension = true;

    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    auto* buttons = compress_dialog.findChild<QDialogButtonBox*>();
    QVERIFY(buttons != nullptr);
    auto* ok_button = buttons->button(QDialogButtonBox::Ok);
    QVERIFY(ok_button != nullptr);
    ok_button->click();

    z7::platform::qt::PortableSettings settings;
    const QStringList saved =
        settings.value(QStringLiteral("Compression/ArcHistory"))
            .toStringList();
    QCOMPARE(saved.size(), 20);
    QCOMPARE(saved.front(), new_archive_saved);
    QCOMPARE(saved.back(), QDir(root.path()).filePath(QStringLiteral("old-18.7z")));
    QVERIFY(!saved.contains(
        QDir(root.path()).filePath(QStringLiteral("old-19.7z"))));
    QVERIFY(!settings.contains(QStringLiteral("Gui/Compress/ArchiveHistory")));
  }

  void compressDialogUsesOriginalCompressionKeysAndIgnoresLegacyQtKeys() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString previous_root = z7::platform::qt::portable_settings_root_dir();
    const ScopedPortableSettingsRoot restore_settings_root(previous_root);
    z7::platform::qt::set_portable_settings_root(root.path());
    QString init_error;
    QVERIFY2(z7::platform::qt::initialize_portable_settings(&init_error),
             qPrintable(init_error));

    const QString legacy_archive =
        QDir(root.path()).filePath(QStringLiteral("legacy.7z"));
    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("Gui/Compress/ArchiveType"),
                        QStringLiteral("zip"));
      settings.setValue(QStringLiteral("Gui/Compress/ArchiveHistory"),
                        QStringList({legacy_archive}));
      settings.setValue(QStringLiteral("Gui/Compress/ShowPassword"), true);
      settings.setValue(QStringLiteral("Compression/SymLinks"), true);
      settings.setValue(QStringLiteral("Compression/PreserveATime"), true);
      settings.setValue(QStringLiteral("Compression/Options/7z/MTime"), false);
      settings.setValue(QStringLiteral("Compression/Options/7z/TimePrec"), 3);
      settings.setValue(sizeof(void*) == 4
                            ? QStringLiteral("Compression/Options/7z/MemUse32")
                            : QStringLiteral("Compression/Options/7z/MemUse64"),
                        QStringLiteral("75%"));
      settings.setValue(QStringLiteral("Compression/Options/7z/Options"),
                        QStringLiteral("-mqs=on"));
    }

    const QString new_archive_input =
        QDir(root.path()).filePath(QStringLiteral("new"));
    const QString new_archive_saved =
        QDir(root.path()).filePath(QStringLiteral("new.7z"));
    z7::ui::gui::CompressCommandOptions compress_options;
    compress_options.archive_path = to_native_path_string(new_archive_input);
    compress_options.keep_archive_name_extension = true;

    z7::ui::gui::CompressDialog compress_dialog(compress_options);
    auto* format_combo =
        compress_dialog.findChild<QComboBox*>(QStringLiteral("formatCombo"));
    QVERIFY(format_combo != nullptr);
    QCOMPARE(format_combo->currentData().toString(), QStringLiteral("7z"));
    auto* parameters_edit =
        compress_dialog.findChild<QLineEdit*>(QStringLiteral("compressParametersEdit"));
    QVERIFY(parameters_edit != nullptr);
    QVERIFY(parameters_edit->text().contains(QStringLiteral("-mtp=3")));
    QVERIFY(parameters_edit->text().contains(QStringLiteral("-mmemuse=75%")));
    QVERIFY(parameters_edit->text().contains(QStringLiteral("-ssp")));
    QVERIFY(parameters_edit->text().contains(QStringLiteral("-mqs=on")));

    auto* buttons = compress_dialog.findChild<QDialogButtonBox*>();
    QVERIFY(buttons != nullptr);
    auto* ok_button = buttons->button(QDialogButtonBox::Ok);
    QVERIFY(ok_button != nullptr);
    ok_button->click();

    z7::platform::qt::PortableSettings settings;
    QCOMPARE(settings.value(QStringLiteral("Compression/Archiver")).toString(),
             QStringLiteral("7z"));
    QCOMPARE(settings.value(QStringLiteral("Compression/ShowPassword")).toBool(),
             false);
    const QStringList saved_history =
        settings.value(QStringLiteral("Compression/ArcHistory")).toStringList();
    QVERIFY(!saved_history.isEmpty());
    QCOMPARE(saved_history.front(), new_archive_saved);
    QVERIFY(!saved_history.contains(legacy_archive));
    QVERIFY(!settings.value(QStringLiteral("Compression/Level")).toString().isEmpty());
    QVERIFY(!settings.value(QStringLiteral("Compression/Options/7z/Level"))
                 .toString()
                 .isEmpty());
    QVERIFY(settings.contains(QStringLiteral("Compression/Options/7z/Order")));
    QVERIFY(settings.contains(QStringLiteral("Compression/Options/7z/BlockSize")));
    QVERIFY(settings.contains(QStringLiteral("Compression/Options/7z/NumThreads")));
    QCOMPARE(settings.value(QStringLiteral("Compression/SymLinks")).toBool(), true);
    QCOMPARE(settings.value(QStringLiteral("Compression/PreserveATime")).toBool(), true);
    QCOMPARE(settings.value(QStringLiteral("Compression/Options/7z/MTime")).toBool(),
             false);
    QCOMPARE(settings.value(QStringLiteral("Compression/Options/7z/TimePrec")).toInt(),
             3);
    QCOMPARE(settings
                 .value(sizeof(void*) == 4
                            ? QStringLiteral("Compression/Options/7z/MemUse32")
                            : QStringLiteral("Compression/Options/7z/MemUse64"))
                 .toString(),
             QStringLiteral("75%"));
    QCOMPARE(settings.value(QStringLiteral("Compression/Options/7z/Options")).toString(),
             QStringLiteral("-mqs=on"));
  }

  void addDialogZipPasswordAesDoesNotSendEncryptedHeaderProperty() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());

    const QString input_file = root_dir.filePath(QStringLiteral("alpha.txt"));
    QVERIFY(write_file(input_file, QByteArrayLiteral("alpha-content")));
    const QString archive_path = root_dir.filePath(QStringLiteral("protected.zip"));

    z7::ui::gui::AddTaskSpec add_spec;
    add_spec.show_dialog = true;
    add_spec.archive_path = to_native_path_string(archive_path);
    add_spec.archive_type = "zip";
    add_spec.input_paths.push_back(to_native_path_string(input_file));

    bool dialog_seen = false;
    bool encryption_method_seen = false;
    QTimer dialog_driver;
    dialog_driver.setInterval(10);
    QObject::connect(&dialog_driver, &QTimer::timeout, [&]() {
      if (dialog_seen) {
        return;
      }
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<z7::ui::gui::CompressDialog*>(widget);
        if (dialog == nullptr || !dialog->isVisible()) {
          continue;
        }
        dialog_seen = true;

        if (auto* password =
                dialog->findChild<QLineEdit*>(QStringLiteral("passwordEdit"))) {
          password->setText(QStringLiteral("test-password"));
        }
        if (auto* reenter =
                dialog->findChild<QLineEdit*>(QStringLiteral("reenterPasswordEdit"))) {
          reenter->setText(QStringLiteral("test-password"));
        }

        const QList<QComboBox*> combos = dialog->findChildren<QComboBox*>();
        for (QComboBox* combo : combos) {
          const int aes_index = combo->findData(QStringLiteral("AES-256"));
          if (aes_index >= 0) {
            combo->setCurrentIndex(aes_index);
            encryption_method_seen = true;
            break;
          }
        }

        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (buttons != nullptr) {
          if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
              ok != nullptr) {
            ok->click();
          }
        }
        break;
      }
    });
    dialog_driver.start();

    z7::ui::gui::GuiTaskCompletion completion;
    bool finished = false;
    z7::ui::gui::GuiAppController controller;
    controller.run_task_spec_async(
        z7::ui::gui::GuiTaskSpec{add_spec},
        QString(),
        {},
        [&completion, &finished](
            const z7::ui::gui::GuiTaskCompletion& result) {
          completion = result;
          finished = true;
        });

    const bool completed =
        QTest::qWaitFor([&finished]() { return finished; }, 20000);
    dialog_driver.stop();
    if (!completed) {
      close_all_visible_dialogs();
      QTest::qWait(100);
    }

    QVERIFY(dialog_seen);
    QVERIFY(encryption_method_seen);
    QVERIFY2(completed, qPrintable(completion.summary));
    QCOMPARE(completion.exit_code, 0);
    QVERIFY2(QFileInfo::exists(archive_path), qPrintable(archive_path));
  }

  void addDialog7zPasswordEncryptedHeadersCreatesHeaderEncryptedArchive() {
    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
        QByteArrayLiteral("1"));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());

    const QString input_file = root_dir.filePath(QStringLiteral("report.txt"));
    QVERIFY(write_file(input_file, QByteArrayLiteral("header-secret-from-gui")));
    const QString archive_path = root_dir.filePath(QStringLiteral("report.7z"));

    z7::ui::gui::AddTaskSpec add_spec;
    add_spec.show_dialog = true;
    add_spec.archive_path = to_native_path_string(archive_path);
    add_spec.archive_type = "7z";
    add_spec.input_paths.push_back(to_native_path_string(input_file));

    bool dialog_seen = false;
    bool encrypt_headers_seen = false;
    bool encrypt_headers_enabled = false;
    bool dialog_options_encrypt_headers = false;
    bool dialog_options_after_accept_encrypt_headers = false;
    QString dialog_options_archive_type;
    std::vector<std::string> dialog_options_extra_parameters;
    bool encryption_method_seen = false;
    QTimer dialog_driver;
    dialog_driver.setInterval(10);
    QObject::connect(&dialog_driver, &QTimer::timeout, [&]() {
      if (dialog_seen) {
        return;
      }
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<z7::ui::gui::CompressDialog*>(widget);
        if (dialog == nullptr || !dialog->isVisible()) {
          continue;
        }
        dialog_seen = true;

        if (auto* password =
                dialog->findChild<QLineEdit*>(QStringLiteral("passwordEdit"))) {
          password->setText(QStringLiteral("1"));
        }
        if (auto* reenter =
                dialog->findChild<QLineEdit*>(QStringLiteral("reenterPasswordEdit"))) {
          reenter->setText(QStringLiteral("1"));
        }
        if (auto* show_password =
                dialog->findChild<QCheckBox*>(QStringLiteral("showPasswordCheckBox"))) {
          show_password->setChecked(true);
        }
        if (auto* encrypt_headers =
                dialog->findChild<QCheckBox*>(QStringLiteral("encryptHeadersCheckBox"))) {
          encrypt_headers_seen = encrypt_headers->isVisible();
          encrypt_headers_enabled = encrypt_headers->isEnabled();
          encrypt_headers->setChecked(true);
        }

        const QList<QComboBox*> combos = dialog->findChildren<QComboBox*>();
        for (QComboBox* combo : combos) {
          const int aes_index = combo->findData(QStringLiteral("AES-256"));
          if (aes_index >= 0) {
            combo->setCurrentIndex(aes_index);
            encryption_method_seen = true;
            break;
          }
        }

        const z7::ui::gui::CompressCommandOptions accepted_options =
            dialog->options();
        dialog_options_encrypt_headers = accepted_options.encrypt_headers;
        dialog_options_archive_type =
            QString::fromStdString(accepted_options.archive_type);
        dialog_options_extra_parameters = accepted_options.extra_parameters;

        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (buttons != nullptr) {
          if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
              ok != nullptr) {
            ok->click();
            const z7::ui::gui::CompressCommandOptions after_accept_options =
                dialog->options();
            dialog_options_after_accept_encrypt_headers =
                after_accept_options.encrypt_headers;
          }
        }
        break;
      }
    });
    dialog_driver.start();

    z7::ui::gui::GuiTaskCompletion completion;
    bool finished = false;
    z7::ui::gui::GuiAppController controller;
    controller.run_task_spec_async(
        z7::ui::gui::GuiTaskSpec{add_spec},
        QString(),
        {},
        [&completion, &finished](
            const z7::ui::gui::GuiTaskCompletion& result) {
          completion = result;
          finished = true;
        });

    const bool completed =
        QTest::qWaitFor([&finished]() { return finished; }, 20000);
    dialog_driver.stop();
    if (!completed) {
      close_all_visible_dialogs();
      QTest::qWait(100);
    }

    QVERIFY(dialog_seen);
    QVERIFY(encrypt_headers_seen);
    QVERIFY(encrypt_headers_enabled);
    QVERIFY(dialog_options_encrypt_headers);
    QVERIFY(dialog_options_after_accept_encrypt_headers);
    QCOMPARE(dialog_options_archive_type, QStringLiteral("7z"));
    QVERIFY(dialog_options_extra_parameters.empty());
    QVERIFY(encryption_method_seen);
    QVERIFY2(completed, qPrintable(completion.summary));
    QCOMPARE(completion.exit_code, 0);
    QVERIFY2(QFileInfo::exists(archive_path), qPrintable(archive_path));

    z7::app::OpenArchiveFromPathRequest open_request;
    open_request.archive_path = to_native_path_string(archive_path);
    open_request.archive_type_hint = "7z";
    z7::app::ArchiveRequest archive_request;
    archive_request.payload = open_request;

    int cancel_prompt_count = 0;
    const z7::app::OperationOutcome canceled_open =
        run_archive_request_and_await(
            archive_request,
            [&cancel_prompt_count](const z7::app::PasswordPrompt&) {
              ++cancel_prompt_count;
              z7::app::PasswordReply reply;
              reply.kind = z7::app::PasswordReplyKind::kCancel;
              return std::optional<z7::app::PasswordReply>(reply);
            });
    QVERIFY(!canceled_open.ok);
    QCOMPARE(canceled_open.error.domain, z7::app::ArchiveErrorDomain::kPassword);
    QCOMPARE(cancel_prompt_count, 1);

    int provide_prompt_count = 0;
    const z7::app::OperationOutcome opened_outcome =
        run_archive_request_and_await(
            archive_request,
            [&provide_prompt_count](const z7::app::PasswordPrompt&) {
              ++provide_prompt_count;
              z7::app::PasswordReply reply;
              reply.kind = z7::app::PasswordReplyKind::kProvide;
              reply.password = "1";
              return std::optional<z7::app::PasswordReply>(reply);
            });
    QVERIFY2(opened_outcome.ok, opened_outcome.summary.c_str());
    QCOMPARE(provide_prompt_count, 1);
    const auto* opened =
        std::get_if<z7::app::OpenArchiveSessionResult>(
            &opened_outcome.payload);
    QVERIFY(opened != nullptr);
    QVERIFY(opened->token.is_valid());

    z7::app::ListRequest list_request;
    list_request.session_token = opened->token;
    z7::app::ArchiveRequest list_archive_request;
    list_archive_request.payload = list_request;
    const z7::app::OperationOutcome list_outcome =
        run_archive_request_and_await(list_archive_request);
    QVERIFY2(list_outcome.ok, list_outcome.summary.c_str());
    const auto* list_result =
        std::get_if<z7::app::ListResult>(&list_outcome.payload);
    QVERIFY(list_result != nullptr);
    bool saw_report = false;
    for (const z7::app::ArchiveListEntry& entry : list_result->entries) {
      if (entry.path == "report.txt" && !entry.is_dir) {
        saw_report = true;
        break;
      }
    }
    QVERIFY(saw_report);

    z7::app::CloseArchiveSessionRequest close_request;
    close_request.token = opened->token;
    z7::app::ArchiveRequest close_archive_request;
    close_archive_request.payload = close_request;
    const z7::app::OperationOutcome close_outcome =
        run_archive_request_and_await(close_archive_request);
    QVERIFY2(close_outcome.ok, close_outcome.summary.c_str());
  }

  void sfxModuleExtractsAppendedPlainAndEncryptedArchives() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());
    const QString sfx_module = side_by_side_sfx_module();
    QVERIFY2(QFileInfo::exists(sfx_module), qPrintable(sfx_module));
    QVERIFY2(QFileInfo(sfx_module).isExecutable(), qPrintable(sfx_module));

    const DirectCliRunResult no_payload =
        run_sfx_module_for_test(
            sfx_module,
            root_dir.filePath(QStringLiteral("no-payload-out")));
    QVERIFY2(no_payload.started, qPrintable(no_payload.error_message));
    QVERIFY2(no_payload.finished, qPrintable(no_payload.error_message));
    QVERIFY(no_payload.exit_code != 0);
    QVERIFY(!no_payload.stderr_data.trimmed().isEmpty());

    QString plain_archive;
    QString error;
    QVERIFY2(create_archive_with_entries(
                 root,
                 QStringLiteral("plain.7z"),
                 {{QStringLiteral("alpha.txt"), QByteArrayLiteral("plain-sfx")}},
                 &plain_archive,
                 &error),
             qPrintable(error));
    const QString plain_sfx = root_dir.filePath(QStringLiteral("plain.sfx"));
    QVERIFY2(append_archive_payload_to_sfx(sfx_module,
                                           plain_archive,
                                           plain_sfx,
                                           &error),
             qPrintable(error));
    QVERIFY2(QFileInfo(plain_sfx).isExecutable(), qPrintable(plain_sfx));
    const QString plain_out = root_dir.filePath(QStringLiteral("plain-out"));
    const DirectCliRunResult plain_result =
        run_sfx_module_for_test(plain_sfx, plain_out);
    QVERIFY2(plain_result.started, qPrintable(plain_result.error_message));
    QVERIFY2(plain_result.finished, qPrintable(plain_result.error_message));
    QVERIFY2(plain_result.exit_code == 0,
             qPrintable(QString::fromLocal8Bit(plain_result.stderr_data)));
    QCOMPARE(read_file(QDir(plain_out).filePath(QStringLiteral("alpha.txt"))),
             QByteArrayLiteral("plain-sfx"));

    const QString encrypted_source =
        root_dir.filePath(QStringLiteral("encrypted-src/secret.txt"));
    QVERIFY(QDir().mkpath(QFileInfo(encrypted_source).absolutePath()));
    QVERIFY(write_file(encrypted_source, QByteArrayLiteral("encrypted-sfx")));
    const QString encrypted_archive =
        root_dir.filePath(QStringLiteral("encrypted.7z"));
    z7::app::AddRequest encrypted_add;
    encrypted_add.archive_path = to_native_path_string(encrypted_archive);
    encrypted_add.format = "7z";
    encrypted_add.password = "secret";
    encrypted_add.encrypt_headers_defined = true;
    encrypted_add.encrypt_headers = true;
    encrypted_add.input_items.push_back(
        z7::app::AddInputItem{to_native_path_string(encrypted_source),
                              "secret.txt"});
    z7::app::ArchiveRequest encrypted_request;
    encrypted_request.payload = encrypted_add;
    const z7::app::OperationOutcome encrypted_add_outcome =
        run_archive_request_and_await(encrypted_request);
    QVERIFY2(encrypted_add_outcome.ok, encrypted_add_outcome.summary.c_str());

    const QString encrypted_sfx =
        root_dir.filePath(QStringLiteral("encrypted.sfx"));
    QVERIFY2(append_archive_payload_to_sfx(sfx_module,
                                           encrypted_archive,
                                           encrypted_sfx,
                                           &error),
             qPrintable(error));
    const DirectCliRunResult wrong_password =
        run_sfx_module_for_test(
            encrypted_sfx,
            root_dir.filePath(QStringLiteral("encrypted-wrong-out")),
            QStringLiteral("wrong"));
    QVERIFY2(wrong_password.started, qPrintable(wrong_password.error_message));
    QVERIFY2(wrong_password.finished, qPrintable(wrong_password.error_message));
    QVERIFY(wrong_password.exit_code != 0);
    QVERIFY(!wrong_password.stderr_data.trimmed().isEmpty());

    const QString encrypted_out =
        root_dir.filePath(QStringLiteral("encrypted-out"));
    const DirectCliRunResult correct_password =
        run_sfx_module_for_test(encrypted_sfx,
                                encrypted_out,
                                QStringLiteral("secret"));
    QVERIFY2(correct_password.started, qPrintable(correct_password.error_message));
    QVERIFY2(correct_password.finished, qPrintable(correct_password.error_message));
    QVERIFY2(correct_password.exit_code == 0,
             qPrintable(QString::fromLocal8Bit(correct_password.stderr_data)));
    QCOMPARE(read_file(QDir(encrypted_out).filePath(QStringLiteral("secret.txt"))),
             QByteArrayLiteral("encrypted-sfx"));
  }

  void addDialogCreateSfxCreatesRunnableSfxArchive() {
    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
        QByteArrayLiteral("1"));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());

    const QString input_file = root_dir.filePath(QStringLiteral("report.txt"));
    QVERIFY(write_file(input_file, QByteArrayLiteral("gui-created-sfx")));
    const QString initial_archive_path = root_dir.filePath(QStringLiteral("report.7z"));
    const QString expected_sfx_path = root_dir.filePath(QStringLiteral("report.sfx"));

    z7::ui::gui::AddTaskSpec add_spec;
    add_spec.show_dialog = true;
    add_spec.archive_path = to_native_path_string(initial_archive_path);
    add_spec.archive_type = "7z";
    add_spec.input_paths.push_back(to_native_path_string(input_file));

    bool dialog_seen = false;
    bool sfx_checkbox_enabled = false;
    bool sfx_extension_seen = false;
    QTimer dialog_driver;
    dialog_driver.setInterval(10);
    QObject::connect(&dialog_driver, &QTimer::timeout, [&]() {
      if (dialog_seen) {
        return;
      }
      const QWidgetList top_levels = QApplication::topLevelWidgets();
      for (QWidget* widget : top_levels) {
        auto* dialog = qobject_cast<z7::ui::gui::CompressDialog*>(widget);
        if (dialog == nullptr || !dialog->isVisible()) {
          continue;
        }
        dialog_seen = true;
        auto* create_sfx =
            dialog->findChild<QCheckBox*>(QStringLiteral("createSfxCheckBox"));
        QVERIFY(create_sfx != nullptr);
        sfx_checkbox_enabled = create_sfx->isEnabled();
        create_sfx->setChecked(true);
        QApplication::processEvents();
        const z7::ui::gui::CompressCommandOptions options = dialog->options();
        const QString selected_archive =
            z7::ui::archive_support::from_native_string(options.archive_path);
        sfx_extension_seen =
            QFileInfo(selected_archive).fileName() == QStringLiteral("report.sfx");

        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (buttons != nullptr) {
          if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
              ok != nullptr) {
            ok->click();
          }
        }
        break;
      }
    });
    dialog_driver.start();

    z7::ui::gui::GuiTaskCompletion completion;
    bool finished = false;
    z7::ui::gui::GuiAppController controller;
    controller.run_task_spec_async(
        z7::ui::gui::GuiTaskSpec{add_spec},
        QString(),
        {},
        [&completion, &finished](
            const z7::ui::gui::GuiTaskCompletion& result) {
          completion = result;
          finished = true;
        });

    const bool completed =
        QTest::qWaitFor([&finished]() { return finished; }, 20000);
    dialog_driver.stop();
    if (!completed) {
      close_all_visible_dialogs();
      QTest::qWait(100);
    }

    QVERIFY(dialog_seen);
    QVERIFY(sfx_checkbox_enabled);
    QVERIFY(sfx_extension_seen);
    QVERIFY2(completed, qPrintable(completion.summary));
    QCOMPARE(completion.exit_code, 0);
    QVERIFY2(QFileInfo::exists(expected_sfx_path), qPrintable(expected_sfx_path));
    QVERIFY2(QFileInfo(expected_sfx_path).isExecutable(),
             qPrintable(expected_sfx_path));

    const QString output_dir = root_dir.filePath(QStringLiteral("gui-sfx-out"));
    const DirectCliRunResult run_result =
        run_sfx_module_for_test(expected_sfx_path, output_dir);
    QVERIFY2(run_result.started, qPrintable(run_result.error_message));
    QVERIFY2(run_result.finished, qPrintable(run_result.error_message));
    QVERIFY2(run_result.exit_code == 0,
             qPrintable(QString::fromLocal8Bit(run_result.stderr_data)));
    QCOMPARE(read_file(QDir(output_dir).filePath(QStringLiteral("report.txt"))),
             QByteArrayLiteral("gui-created-sfx"));
  }

  void real7zGTaskIpcAddSfxCreatesRunnableSfxArchive() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());

    const QFileInfo side_by_side_7zg(side_by_side_7zg_program());
    QVERIFY2(side_by_side_7zg.exists(), qPrintable(side_by_side_7zg.absoluteFilePath()));
    QVERIFY2(side_by_side_7zg.isExecutable(),
             qPrintable(side_by_side_7zg.absoluteFilePath()));
    const QString side_by_side_sfx =
        QDir(side_by_side_7zg.absolutePath()).filePath(QStringLiteral("7z.sfx"));
    QVERIFY2(QFileInfo::exists(side_by_side_sfx), qPrintable(side_by_side_sfx));
    QVERIFY2(QFileInfo(side_by_side_sfx).isExecutable(),
             qPrintable(side_by_side_sfx));

    const QString input_file = root_dir.filePath(QStringLiteral("report.txt"));
    QVERIFY(write_file(input_file, QByteArrayLiteral("real-7zg-created-sfx")));
    const QString sfx_path = root_dir.filePath(QStringLiteral("report.sfx"));

    z7::task_ipc_runtime::TaskIpcPayload payload =
        make_add_payload(sfx_path, QStringLiteral("7z"), {input_file});
    payload.add->create_sfx = true;

    const RealTaskIpcRunResult add_result =
        run_real_7zg_task_ipc_and_wait(root.path(), payload);
    QString task_error;
    QVERIFY2(task_ipc_succeeded(add_result, &task_error), qPrintable(task_error));
    QVERIFY2(QFileInfo::exists(sfx_path), qPrintable(sfx_path));
    QVERIFY2(QFileInfo(sfx_path).isExecutable(), qPrintable(sfx_path));

    const QString output_dir = root_dir.filePath(QStringLiteral("real-7zg-sfx-out"));
    const DirectCliRunResult run_result =
        run_sfx_module_for_test(sfx_path, output_dir);
    QVERIFY2(run_result.started, qPrintable(run_result.error_message));
    QVERIFY2(run_result.finished, qPrintable(run_result.error_message));
    QVERIFY2(run_result.exit_code == 0,
             qPrintable(QString::fromLocal8Bit(run_result.stderr_data)));
    QCOMPARE(read_file(QDir(output_dir).filePath(QStringLiteral("report.txt"))),
             QByteArrayLiteral("real-7zg-created-sfx"));
  }

  void pathHistoriesDedupeCaseInsensitivelyLikeOriginal() {
    const QString first = QStringLiteral("/tmp/MixedCase/Archive.7z");
    const QString second = QStringLiteral("/tmp/mixedcase/archive.7z");

    const QStringList history =
        z7::ui::common::normalized_path_history({first}, second, 16);
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.front(), QDir::cleanPath(second));
  }

  void real7zGTaskIpcExtractHonorsPathRemaps() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString archive_path;
    QString archive_error;
    QVERIFY2(create_path_remap_archive(root, &archive_path, &archive_error),
             qPrintable(archive_error));

    const QString worker_program = real_7zg_program();
    QVERIFY2(QFileInfo::exists(worker_program), qPrintable(worker_program));
    QVERIFY2(QFileInfo(worker_program).isExecutable(),
             qPrintable(worker_program));

    const QString output_dir =
        QDir(root.path()).filePath(QStringLiteral("rewritten"));
    const QString remap_destination =
        QDir(output_dir).filePath(QStringLiteral("docs-export"));
    QVERIFY(QDir().mkpath(output_dir));
    QVERIFY(QDir().mkpath(QFileInfo(remap_destination).absolutePath()));

    const z7::task_ipc_runtime::TaskIpcPayload payload =
        make_extract_remap_payload(archive_path, output_dir, remap_destination);

    const QString owner_instance_id =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    const RealTaskIpcRunResult run_result = run_real_7zg_task_ipc_and_wait(
        worker_program,
        QFileInfo(archive_path).absolutePath(),
        owner_instance_id,
        payload);

    QVERIFY2(run_result.dispatched, qPrintable(run_result.error_message));
    QVERIFY2(run_result.completed, qPrintable(run_result.error_message));
    QVERIFY2(run_result.error_message.trimmed().isEmpty(),
             qPrintable(run_result.error_message));
    QVERIFY2(run_result.completion.result_code == 0,
             qPrintable(QStringLiteral("result_code=%1 summary=%2")
                            .arg(run_result.completion.result_code)
                            .arg(run_result.completion.summary)));

    const QString rewritten_readme =
        QDir(remap_destination).filePath(QStringLiteral("readme.md"));
    QVERIFY2(QFileInfo::exists(rewritten_readme),
             qPrintable(rewritten_readme));
    QCOMPARE(read_file(rewritten_readme), QByteArrayLiteral("docs-body"));

    const QString ordinary_docs_readme =
        QDir(output_dir).filePath(QStringLiteral("docs/readme.md"));
    QVERIFY2(!QFileInfo::exists(ordinary_docs_readme),
             qPrintable(ordinary_docs_readme));

    const QString wrongly_remapped_report =
        QDir(remap_destination).filePath(QStringLiteral("report.txt"));
    QVERIFY2(!QFileInfo::exists(wrongly_remapped_report),
             qPrintable(wrongly_remapped_report));
  }

  void real7zGTaskIpcExtractHonorsOverwriteModes() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());

    QString archive_path;
    QString archive_error;
    QVERIFY2(create_single_file_archive(root,
                                        QStringLiteral("overwrite-modes.7z"),
                                        QStringLiteral("conflict.txt"),
                                        QByteArrayLiteral("archive-body"),
                                        &archive_path,
                                        &archive_error),
             qPrintable(archive_error));

    const auto run_extract = [&](const QString& output_dir,
                                 const QString& overwrite_switch) {
      QVERIFY(QDir().mkpath(output_dir));
      QVERIFY(write_file(QDir(output_dir).filePath(QStringLiteral("conflict.txt")),
                         QByteArrayLiteral("existing-body")));

      const RealTaskIpcRunResult run_result =
          run_real_7zg_task_ipc_and_wait(
              root.path(),
              make_extract_payload(archive_path,
                                   output_dir,
                                   QString(),
                                   overwrite_switch));
      QVERIFY2(run_result.dispatched, qPrintable(run_result.error_message));
      QVERIFY2(run_result.completed, qPrintable(run_result.error_message));
      QVERIFY2(run_result.error_message.trimmed().isEmpty(),
               qPrintable(run_result.error_message));
      QVERIFY2(run_result.completion.result_code == 0,
               qPrintable(QStringLiteral("%1 result_code=%2 summary=%3")
                              .arg(overwrite_switch)
                              .arg(run_result.completion.result_code)
                              .arg(run_result.completion.summary)));
    };

    const QString overwrite_dir =
        root_dir.filePath(QStringLiteral("overwrite-out"));
    run_extract(overwrite_dir, QStringLiteral("-aoa"));
    QCOMPARE(read_file(QDir(overwrite_dir).filePath(QStringLiteral("conflict.txt"))),
             QByteArrayLiteral("archive-body"));

    const QString skip_dir =
        root_dir.filePath(QStringLiteral("skip-out"));
    run_extract(skip_dir, QStringLiteral("-aos"));
    QCOMPARE(read_file(QDir(skip_dir).filePath(QStringLiteral("conflict.txt"))),
             QByteArrayLiteral("existing-body"));
    QVERIFY(direct_child_files_with_contents(skip_dir,
                                             QByteArrayLiteral("archive-body"))
                .isEmpty());

    const QString rename_extracted_dir =
        root_dir.filePath(QStringLiteral("rename-extracted-out"));
    run_extract(rename_extracted_dir, QStringLiteral("-aou"));
    QCOMPARE(read_file(QDir(rename_extracted_dir)
                           .filePath(QStringLiteral("conflict.txt"))),
             QByteArrayLiteral("existing-body"));
    const QStringList renamed_extracted =
        direct_child_files_with_contents(rename_extracted_dir,
                                         QByteArrayLiteral("archive-body"));
    QCOMPARE(renamed_extracted.size(), 1);
    QVERIFY2(renamed_extracted.front() != QStringLiteral("conflict.txt"),
             qPrintable(renamed_extracted.front()));

    const QString rename_existing_dir =
        root_dir.filePath(QStringLiteral("rename-existing-out"));
    run_extract(rename_existing_dir, QStringLiteral("-aot"));
    QCOMPARE(read_file(QDir(rename_existing_dir)
                           .filePath(QStringLiteral("conflict.txt"))),
             QByteArrayLiteral("archive-body"));
    QCOMPARE(read_file(QDir(rename_existing_dir)
                           .filePath(QStringLiteral("conflict_1.txt"))),
             QByteArrayLiteral("existing-body"));
  }

  void real7zGTaskIpcExtractsMultipleArchivesInOneRequest() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());

    QString archive_a;
    QString archive_b;
    QString archive_error;
    QVERIFY2(create_single_file_archive(root,
                                        QStringLiteral("first.7z"),
                                        QStringLiteral("alpha.txt"),
                                        QByteArrayLiteral("alpha-body"),
                                        &archive_a,
                                        &archive_error),
             qPrintable(archive_error));
    archive_error.clear();
    QVERIFY2(create_single_file_archive(root,
                                        QStringLiteral("second.7z"),
                                        QStringLiteral("beta.txt"),
                                        QByteArrayLiteral("beta-body"),
                                        &archive_b,
                                        &archive_error),
             qPrintable(archive_error));

    const QString output_dir =
        root_dir.filePath(QStringLiteral("multi-archive-out"));
    QVERIFY(QDir().mkpath(output_dir));

    z7::task_ipc_runtime::TaskIpcPayload payload;
    payload.command = z7::task_ipc_runtime::TaskIpcCommandKind::kExtract;
    payload.show_dialog = false;
    payload.refresh_after_finish = false;
    payload.extract = z7::task_ipc_runtime::TaskIpcExtractPayload{};
    payload.extract->archive_inputs = {archive_a, archive_b};
    payload.extract->output_dir = output_dir;
    payload.extract->overwrite_switch = QStringLiteral("-aoa");

    const RealTaskIpcRunResult run_result =
        run_real_7zg_task_ipc_and_wait(root.path(), payload);
    QVERIFY2(run_result.dispatched, qPrintable(run_result.error_message));
    QVERIFY2(run_result.completed, qPrintable(run_result.error_message));
    QVERIFY2(run_result.error_message.trimmed().isEmpty(),
             qPrintable(run_result.error_message));
    QVERIFY2(run_result.completion.result_code == 0,
             qPrintable(QStringLiteral("result_code=%1 summary=%2")
                            .arg(run_result.completion.result_code)
                            .arg(run_result.completion.summary)));
    QVERIFY(run_result.completion.payload.extract.has_value());
    QCOMPARE(run_result.completion.payload.extract->archive_inputs.size(), 2);

    QCOMPARE(read_file(QDir(output_dir).filePath(QStringLiteral("alpha.txt"))),
             QByteArrayLiteral("alpha-body"));
    QCOMPARE(read_file(QDir(output_dir).filePath(QStringLiteral("beta.txt"))),
             QByteArrayLiteral("beta-body"));
  }

  void real7zGTaskIpcAddUpdateModesMatchOriginalActionSets() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());

    const QString source_dir = root_dir.filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(source_dir));
    const QString archive_newer_name = QStringLiteral("archive-newer.txt");
    const QString disk_newer_name = QStringLiteral("disk-newer.txt");
    const QString disk_only_name = QStringLiteral("disk-only.txt");
    const QString archive_only_name = QStringLiteral("archive-only.txt");
    const QString archive_newer_path = QDir(source_dir).filePath(archive_newer_name);
    const QString disk_newer_path = QDir(source_dir).filePath(disk_newer_name);
    const QString disk_only_path = QDir(source_dir).filePath(disk_only_name);
    const QString archive_only_path = QDir(source_dir).filePath(archive_only_name);

    QVERIFY(write_file(archive_newer_path, QByteArrayLiteral("archive-newer")));
    QVERIFY(write_file(disk_newer_path, QByteArrayLiteral("archive-stale")));
    QVERIFY(write_file(archive_only_path, QByteArrayLiteral("archive-only")));

    const QDateTime base_time = QDateTime::currentDateTimeUtc().addDays(-10);
    QVERIFY(set_file_modified_time(archive_newer_path, base_time));
    QVERIFY(set_file_modified_time(disk_newer_path, base_time));
    QVERIFY(set_file_modified_time(archive_only_path, base_time));

    const QStringList modes = {
        QStringLiteral("add"),
        QStringLiteral("update"),
        QStringLiteral("fresh"),
        QStringLiteral("sync"),
    };
    QHash<QString, QString> archives;
    for (const QString& mode : modes) {
      const QString archive_path =
          root_dir.filePath(QStringLiteral("%1-mode.7z").arg(mode));
      const RealTaskIpcRunResult create_result =
          run_real_7zg_task_ipc_and_wait(
              root.path(),
              make_add_payload(archive_path,
                               QStringLiteral("7z"),
                               QStringList{source_dir}));
      QVERIFY2(create_result.dispatched, qPrintable(create_result.error_message));
      QVERIFY2(create_result.completed, qPrintable(create_result.error_message));
      QVERIFY2(create_result.error_message.trimmed().isEmpty(),
               qPrintable(create_result.error_message));
      QVERIFY2(create_result.completion.result_code == 0,
               qPrintable(QStringLiteral("%1 create result_code=%2 summary=%3")
                              .arg(mode)
                              .arg(create_result.completion.result_code)
                              .arg(create_result.completion.summary)));
      QVERIFY2(QFileInfo::exists(archive_path), qPrintable(archive_path));
      archives.insert(mode, archive_path);
    }

    QVERIFY(write_file(archive_newer_path, QByteArrayLiteral("disk-older")));
    QVERIFY(set_file_modified_time(archive_newer_path, base_time.addDays(-2)));
    QVERIFY(write_file(disk_newer_path, QByteArrayLiteral("disk-newer")));
    QVERIFY(set_file_modified_time(disk_newer_path, base_time.addDays(2)));
    QVERIFY(write_file(disk_only_path, QByteArrayLiteral("disk-only")));
    QVERIFY(set_file_modified_time(disk_only_path, base_time.addDays(2)));
    QVERIFY(QFile::remove(archive_only_path));

    for (const QString& mode : modes) {
      z7::task_ipc_runtime::TaskIpcPayload update_payload =
          make_add_payload(archives.value(mode),
                           QStringLiteral("7z"),
                           QStringList{source_dir});
      update_payload.add->update_mode = mode;
      const RealTaskIpcRunResult update_result =
          run_real_7zg_task_ipc_and_wait(root.path(), update_payload);
      QVERIFY2(update_result.dispatched, qPrintable(update_result.error_message));
      QVERIFY2(update_result.completed, qPrintable(update_result.error_message));
      QVERIFY2(update_result.error_message.trimmed().isEmpty(),
               qPrintable(update_result.error_message));
      QVERIFY2(update_result.completion.result_code == 0,
               qPrintable(QStringLiteral("%1 update result_code=%2 summary=%3")
                              .arg(mode)
                              .arg(update_result.completion.result_code)
                              .arg(update_result.completion.summary)));
    }

    struct ExpectedMode {
      QString mode;
      QByteArray archive_newer_text;
      bool disk_only_present;
      bool archive_only_present;
    };
    const ExpectedMode expectations[] = {
        {QStringLiteral("add"), QByteArrayLiteral("disk-older"), true, true},
        {QStringLiteral("update"), QByteArrayLiteral("archive-newer"), true, true},
        {QStringLiteral("fresh"), QByteArrayLiteral("archive-newer"), false, true},
        {QStringLiteral("sync"), QByteArrayLiteral("archive-newer"), true, false},
    };

    const QString archive_entry_prefix =
        QFileInfo(source_dir).fileName() + QStringLiteral("/");
    for (const ExpectedMode& expected : expectations) {
      const QString output_dir =
          root_dir.filePath(QStringLiteral("out-%1").arg(expected.mode));
      QVERIFY(QDir().mkpath(output_dir));
      const RealTaskIpcRunResult extract_result =
          run_real_7zg_task_ipc_and_wait(
              root.path(),
              make_extract_payload(archives.value(expected.mode), output_dir));
      QVERIFY2(extract_result.dispatched, qPrintable(extract_result.error_message));
      QVERIFY2(extract_result.completed, qPrintable(extract_result.error_message));
      QVERIFY2(extract_result.error_message.trimmed().isEmpty(),
               qPrintable(extract_result.error_message));
      QVERIFY2(extract_result.completion.result_code == 0,
               qPrintable(QStringLiteral("%1 extract result_code=%2 summary=%3")
                              .arg(expected.mode)
                              .arg(extract_result.completion.result_code)
                              .arg(extract_result.completion.summary)));

      QCOMPARE(read_file(QDir(output_dir).filePath(
                   archive_entry_prefix + archive_newer_name)),
               expected.archive_newer_text);
      QCOMPARE(read_file(QDir(output_dir).filePath(
                   archive_entry_prefix + disk_newer_name)),
               QByteArrayLiteral("disk-newer"));
      QCOMPARE(QFileInfo::exists(QDir(output_dir).filePath(
                   archive_entry_prefix + disk_only_name)),
               expected.disk_only_present);
      QCOMPARE(QFileInfo::exists(QDir(output_dir).filePath(
                   archive_entry_prefix + archive_only_name)),
               expected.archive_only_present);
    }
  }

  void real7zGTaskIpcFormatProductMatrix() {
    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
        QByteArrayLiteral("1"));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());
    QString task_error;

    for (const RealArchiveFormatSpec& format : single_file_format_matrix()) {
      const QString label =
          QStringLiteral("%1-single").arg(format.id);
      const QString source_path =
          root_dir.filePath(label + QStringLiteral("-payload.txt"));
      const QByteArray contents =
          QStringLiteral("single-body-%1").arg(format.id).toUtf8();
      QVERIFY(write_file(source_path, contents));

      const QString archive_path =
          format.supports_directory_input
              ? root_dir.filePath(label + QLatin1Char('.') + format.extension)
              : source_path + QLatin1Char('.') + format.extension;
      const RealTaskIpcRunResult add_result =
          run_real_7zg_task_ipc_and_wait(
              root.path(),
              make_add_payload(archive_path,
                               format.id,
                               QStringList{source_path}));
      QVERIFY2(task_ipc_succeeded(add_result, &task_error),
               qPrintable(QStringLiteral("%1 add: %2")
                              .arg(label, task_error)));
      QVERIFY2(QFileInfo::exists(archive_path), qPrintable(archive_path));

      const QString output_dir =
          root_dir.filePath(label + QStringLiteral("-extract-out"));
      const RealTaskIpcRunResult extract_result =
          run_real_7zg_task_ipc_and_wait(
              root.path(),
              make_extract_payload(archive_path, output_dir));
      QVERIFY2(task_ipc_succeeded(extract_result, &task_error),
               qPrintable(QStringLiteral("%1 extract: %2")
                              .arg(label, task_error)));

      const QStringList extracted_entries =
          direct_child_files_with_contents(output_dir, contents);
      QCOMPARE(extracted_entries.size(), 1);
      const QString entry_name = extracted_entries.front();
      if (format.supports_directory_input) {
        QCOMPARE(entry_name, QFileInfo(source_path).fileName());
      }

      const RealTaskIpcRunResult test_result =
          run_real_7zg_task_ipc_and_wait(
              QFileInfo(archive_path).absolutePath(),
              make_archive_test_payload(
                  archive_path,
                  QStringList{entry_name},
                  format.id));
      QVERIFY2(task_ipc_succeeded(test_result, &task_error),
               qPrintable(QStringLiteral("%1 test: %2")
                              .arg(label, task_error)));

      const QString expected_sha256 = sha256_hex(contents);
      const RealTaskIpcRunResult hash_result =
          run_real_7zg_task_ipc_and_wait(
              QFileInfo(archive_path).absolutePath(),
              make_archive_hash_payload(
                  archive_path,
                  QStringList{entry_name},
                  QStringLiteral("SHA256"),
                  format.id));
      QVERIFY2(task_ipc_succeeded(hash_result, &task_error),
               qPrintable(QStringLiteral("%1 hash: %2")
                              .arg(label, task_error)));
      QVERIFY2(hash_result.completion.summary.contains(
                   expected_sha256, Qt::CaseInsensitive),
               qPrintable(QStringLiteral("%1 hash summary: %2")
                              .arg(label, hash_result.completion.summary)));
    }

    for (const RealArchiveFormatSpec& format : directory_format_matrix()) {
      const QString label =
          QStringLiteral("%1-directory").arg(format.id);
      const QString source_dir =
          root_dir.filePath(label + QStringLiteral("-source"));
      const QString payload_path =
          QDir(source_dir).filePath(QStringLiteral("nested/payload.txt"));
      const QByteArray contents =
          QStringLiteral("directory-body-%1").arg(format.id).toUtf8();
      QVERIFY(QDir().mkpath(QFileInfo(payload_path).absolutePath()));
      QVERIFY(write_file(payload_path, contents));

      const QString archive_path =
          root_dir.filePath(label + QLatin1Char('.') + format.extension);
      const RealTaskIpcRunResult add_result =
          run_real_7zg_task_ipc_and_wait(
              root.path(),
              make_add_payload(archive_path,
                               format.id,
                               QStringList{source_dir}));
      QVERIFY2(task_ipc_succeeded(add_result, &task_error),
               qPrintable(QStringLiteral("%1 add: %2")
                              .arg(label, task_error)));

      const QString output_dir =
          root_dir.filePath(label + QStringLiteral("-extract-out"));
      const RealTaskIpcRunResult extract_result =
          run_real_7zg_task_ipc_and_wait(
              root.path(),
              make_extract_payload(archive_path, output_dir));
      QVERIFY2(task_ipc_succeeded(extract_result, &task_error),
               qPrintable(QStringLiteral("%1 extract: %2")
                              .arg(label, task_error)));

      const QString entry_path =
          QFileInfo(source_dir).fileName() + QStringLiteral("/nested/payload.txt");
      const QString extracted_file = QDir(output_dir).filePath(entry_path);
      QVERIFY2(QFileInfo::exists(extracted_file),
               qPrintable(QStringLiteral("%1 missing extracted file %2")
                              .arg(label, extracted_file)));
      QCOMPARE(read_file(extracted_file), contents);

      const RealTaskIpcRunResult test_result =
          run_real_7zg_task_ipc_and_wait(
              QFileInfo(archive_path).absolutePath(),
              make_archive_test_payload(
                  archive_path,
                  QStringList{entry_path},
                  format.id));
      QVERIFY2(task_ipc_succeeded(test_result, &task_error),
               qPrintable(QStringLiteral("%1 test: %2")
                              .arg(label, task_error)));

      const QString expected_sha256 = sha256_hex(contents);
      const RealTaskIpcRunResult hash_result =
          run_real_7zg_task_ipc_and_wait(
              QFileInfo(archive_path).absolutePath(),
              make_archive_hash_payload(
                  archive_path,
                  QStringList{entry_path},
                  QStringLiteral("SHA256"),
                  format.id));
      QVERIFY2(task_ipc_succeeded(hash_result, &task_error),
               qPrintable(QStringLiteral("%1 hash: %2")
                              .arg(label, task_error)));
      QVERIFY2(hash_result.completion.summary.contains(
                   expected_sha256, Qt::CaseInsensitive),
               qPrintable(QStringLiteral("%1 hash summary: %2")
                              .arg(label, hash_result.completion.summary)));
    }

    for (const RealArchivePasswordVariant& variant : password_format_matrix()) {
      const QString source_path =
          root_dir.filePath(variant.label + QStringLiteral("-payload.txt"));
      const QByteArray contents =
          QStringLiteral("password-body-%1").arg(variant.label).toUtf8();
      QVERIFY(write_file(source_path, contents));

      const QString archive_path =
          root_dir.filePath(variant.label + QLatin1Char('.') + variant.extension);
      z7::task_ipc_runtime::TaskIpcPayload add_payload =
          make_add_payload(archive_path,
                           variant.format_id,
                           QStringList{source_path});
      apply_password_variant_to_add_payload(add_payload, variant);

      const RealTaskIpcRunResult add_result =
          run_real_7zg_task_ipc_and_wait(root.path(), add_payload);
      QVERIFY2(task_ipc_succeeded(add_result, &task_error),
               qPrintable(QStringLiteral("%1 add: %2")
                              .arg(variant.label, task_error)));

      const QString output_dir =
          root_dir.filePath(variant.label + QStringLiteral("-extract-out"));
      const RealTaskIpcRunResult extract_result =
          run_real_7zg_task_ipc_and_wait(
              root.path(),
              make_extract_payload(archive_path, output_dir, variant.password));
      QVERIFY2(task_ipc_succeeded(extract_result, &task_error),
               qPrintable(QStringLiteral("%1 extract: %2")
                              .arg(variant.label, task_error)));

      const QString extracted_file =
          QDir(output_dir).filePath(QFileInfo(source_path).fileName());
      QVERIFY2(QFileInfo::exists(extracted_file),
               qPrintable(QStringLiteral("%1 missing extracted file %2")
                              .arg(variant.label, extracted_file)));
      QCOMPARE(read_file(extracted_file), contents);
    }
  }

  void real7zGTaskIpcArchiveExportProductMatrix() {
    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
        QByteArrayLiteral("1"));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());
    QString task_error;

    const QStringList path_modes = {
        QStringLiteral("full"),
        QStringLiteral("relative"),
        QStringLiteral("absolute"),
    };
    const QStringList overwrite_modes = {
        QStringLiteral("-aoa"),
        QStringLiteral("-aos"),
        QStringLiteral("-aou"),
        QStringLiteral("-aot"),
    };

    for (const RealArchiveFormatSpec& format : directory_format_matrix()) {
      const QString child_name =
          QStringLiteral("nested-child-%1.%2")
              .arg(format.id, format.extension);
      QString child_archive_path;
      QString archive_error;
      QVERIFY2(create_archive_with_entries(
                   root,
                   child_name,
                   format.id,
                   {{QStringLiteral("docs/export.txt"),
                     QByteArrayLiteral("nested-docs-body")},
                    {QStringLiteral("conflict.txt"),
                     QByteArrayLiteral("nested-conflict-body")}},
                   &child_archive_path,
                   &archive_error),
               qPrintable(QStringLiteral("%1 child archive: %2")
                              .arg(format.id, archive_error)));

      QString root_archive_path;
      archive_error.clear();
      QVERIFY2(create_archive_with_entries(
                   root,
                   QStringLiteral("export-root-%1.7z").arg(format.id),
                   {{QStringLiteral("nested/") + child_name,
                     read_file(child_archive_path)}},
                   &root_archive_path,
                   &archive_error),
               qPrintable(QStringLiteral("%1 root archive: %2")
                              .arg(format.id, archive_error)));

      const QString nested_entry = QStringLiteral("nested/") + child_name;
      for (const QString& path_mode : path_modes) {
        for (const QString& overwrite_mode : overwrite_modes) {
          const QString label =
              QStringLiteral("%1-%2-%3")
                  .arg(format.id, path_mode, overwrite_mode.mid(1));
          const QString output_dir =
              root_dir.filePath(QStringLiteral("export-%1").arg(label));
          const QString remap_destination =
              QDir(output_dir).filePath(QStringLiteral("docs-export"));
          QVERIFY(QDir().mkpath(output_dir));
          QVERIFY(write_file(QDir(output_dir).filePath(
                                 QStringLiteral("conflict.txt")),
                             QByteArrayLiteral("existing-conflict-body")));

          z7::task_ipc_runtime::TaskIpcPayload payload;
          payload.command =
              z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport;
          payload.show_dialog = false;
          payload.refresh_after_finish = false;
          payload.archive_export =
              z7::task_ipc_runtime::TaskIpcArchiveExportPayload{};
          payload.archive_export->root_archive_path = root_archive_path;
          payload.archive_export->root_archive_type = QStringLiteral("7z");
      payload.archive_export->nested_archive_entries =
          QStringList{nested_entry};
      payload.archive_export->archive_entry_paths =
          QStringList{QStringLiteral("docs/export.txt"),
                      QStringLiteral("conflict.txt")};
          payload.archive_export->output_dir = output_dir;
          payload.archive_export->overwrite_mode = overwrite_mode;
          payload.archive_export->path_mode = path_mode;

          z7::task_ipc_runtime::TaskIpcExtractPathRemap remap;
          remap.match_kind =
              z7::task_ipc_runtime::TaskIpcExtractPathRemapMatchKind::kArchivePrefix;
          remap.source_path = QStringLiteral("docs");
          remap.destination_path = remap_destination;
          payload.archive_export->path_remaps.push_back(remap);

          const RealTaskIpcRunResult export_result =
              run_real_7zg_task_ipc_and_wait(
                  QFileInfo(root_archive_path).absolutePath(),
                  payload);
          QVERIFY2(task_ipc_succeeded(export_result, &task_error),
                   qPrintable(QStringLiteral("%1 export: %2")
                                  .arg(label, task_error)));

          const QString remapped_file =
              QDir(remap_destination).filePath(QStringLiteral("export.txt"));
          QVERIFY2(QFileInfo::exists(remapped_file),
                   qPrintable(QStringLiteral("%1 missing remapped file %2")
                                  .arg(label, remapped_file)));
          QCOMPARE(read_file(remapped_file),
                   QByteArrayLiteral("nested-docs-body"));

          const QString conflict_file =
              QDir(output_dir).filePath(QStringLiteral("conflict.txt"));
          if (overwrite_mode == QStringLiteral("-aoa")) {
            QCOMPARE(read_file(conflict_file),
                     QByteArrayLiteral("nested-conflict-body"));
          } else if (overwrite_mode == QStringLiteral("-aos")) {
            QCOMPARE(read_file(conflict_file),
                     QByteArrayLiteral("existing-conflict-body"));
            QVERIFY(direct_child_files_with_contents(
                        output_dir,
                        QByteArrayLiteral("nested-conflict-body"))
                        .isEmpty());
          } else if (overwrite_mode == QStringLiteral("-aou")) {
            QCOMPARE(read_file(conflict_file),
                     QByteArrayLiteral("existing-conflict-body"));
            const QStringList renamed =
                direct_child_files_with_contents(
                    output_dir,
                    QByteArrayLiteral("nested-conflict-body"));
            QCOMPARE(renamed.size(), 1);
            QVERIFY(renamed.front() != QStringLiteral("conflict.txt"));
          } else if (overwrite_mode == QStringLiteral("-aot")) {
            QCOMPARE(read_file(conflict_file),
                     QByteArrayLiteral("nested-conflict-body"));
            const QStringList renamed =
                direct_child_files_with_contents(
                    output_dir,
                    QByteArrayLiteral("existing-conflict-body"));
            QCOMPARE(renamed.size(), 1);
            QVERIFY(renamed.front() != QStringLiteral("conflict.txt"));
          }
        }
      }
    }
  }

  void real7zGTaskIpcFailureProductMatrix() {
    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
        QByteArrayLiteral("1"));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());
    QString task_error;

    for (const RealArchiveFormatSpec& format : single_file_format_matrix()) {
      const QString archive_path =
          root_dir.filePath(QStringLiteral("missing-input-%1.%2")
                                .arg(format.id, format.extension));
      const QString missing_path =
          root_dir.filePath(QStringLiteral("missing-source-%1.txt")
                                .arg(format.id));
      const RealTaskIpcRunResult add_result =
          run_real_7zg_task_ipc_and_wait(
              root.path(),
              make_add_payload(archive_path,
                               format.id,
                               QStringList{missing_path}));
      QVERIFY2(task_ipc_failed_with_summary(add_result, &task_error),
               qPrintable(QStringLiteral("%1 missing input add: %2")
                              .arg(format.id, task_error)));
      QVERIFY2(!QFileInfo::exists(archive_path), qPrintable(archive_path));
    }

    const QString corrupt_archive =
        root_dir.filePath(QStringLiteral("corrupt.7z"));
    QVERIFY(write_file(corrupt_archive, QByteArrayLiteral("not an archive")));

    const QString corrupt_extract_out =
        root_dir.filePath(QStringLiteral("corrupt-extract-out"));
    const RealTaskIpcRunResult corrupt_extract =
        run_real_7zg_task_ipc_and_wait(
            root.path(),
            make_extract_payload(corrupt_archive, corrupt_extract_out));
    QVERIFY2(task_ipc_failed_with_summary(corrupt_extract, &task_error),
             qPrintable(QStringLiteral("corrupt extract: %1").arg(task_error)));

    const RealTaskIpcRunResult corrupt_test =
        run_real_7zg_task_ipc_and_wait(
            root.path(),
            make_test_payload(QStringList{corrupt_archive}));
    QVERIFY2(task_ipc_failed_with_summary(corrupt_test, &task_error),
             qPrintable(QStringLiteral("corrupt test: %1").arg(task_error)));

    const RealTaskIpcRunResult corrupt_hash =
        run_real_7zg_task_ipc_and_wait(
            root.path(),
            make_archive_hash_payload(
                corrupt_archive,
                QStringList{QStringLiteral("payload.txt")},
                QStringLiteral("SHA256")));
    QVERIFY2(task_ipc_failed_with_summary(corrupt_hash, &task_error),
             qPrintable(QStringLiteral("corrupt hash: %1").arg(task_error)));

    z7::task_ipc_runtime::TaskIpcPayload corrupt_export_payload;
    corrupt_export_payload.command =
        z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport;
    corrupt_export_payload.show_dialog = false;
    corrupt_export_payload.refresh_after_finish = false;
    corrupt_export_payload.archive_export =
        z7::task_ipc_runtime::TaskIpcArchiveExportPayload{};
    corrupt_export_payload.archive_export->root_archive_path = corrupt_archive;
    corrupt_export_payload.archive_export->root_archive_type =
        QStringLiteral("7z");
    corrupt_export_payload.archive_export->archive_entry_paths =
        QStringList{QStringLiteral("payload.txt")};
    corrupt_export_payload.archive_export->output_dir =
        root_dir.filePath(QStringLiteral("corrupt-export-out"));
    corrupt_export_payload.archive_export->overwrite_mode =
        QStringLiteral("-aoa");
    corrupt_export_payload.archive_export->path_mode = QStringLiteral("full");
    const RealTaskIpcRunResult corrupt_export =
        run_real_7zg_task_ipc_and_wait(root.path(), corrupt_export_payload);
    QVERIFY2(task_ipc_failed_with_summary(corrupt_export, &task_error),
             qPrintable(QStringLiteral("corrupt export: %1").arg(task_error)));

    for (const RealArchivePasswordVariant& variant : password_format_matrix()) {
      const QString source_path =
          root_dir.filePath(variant.label + QStringLiteral("-wrong-source.txt"));
      const QByteArray contents =
          QStringLiteral("wrong-password-body-%1").arg(variant.label).toUtf8();
      QVERIFY(write_file(source_path, contents));
      const QString archive_path =
          root_dir.filePath(variant.label + QStringLiteral("-wrong.") +
                            variant.extension);

      z7::task_ipc_runtime::TaskIpcPayload add_payload =
          make_add_payload(archive_path,
                           variant.format_id,
                           QStringList{source_path});
      apply_password_variant_to_add_payload(add_payload, variant);
      const RealTaskIpcRunResult add_result =
          run_real_7zg_task_ipc_and_wait(root.path(), add_payload);
      QVERIFY2(task_ipc_succeeded(add_result, &task_error),
               qPrintable(QStringLiteral("%1 add: %2")
                              .arg(variant.label, task_error)));

      const QString wrong_out =
          root_dir.filePath(variant.label + QStringLiteral("-wrong-out"));
      const RealTaskIpcRunResult wrong_extract =
          run_real_7zg_task_ipc_and_wait(
              root.path(),
              make_extract_payload(archive_path,
                                   wrong_out,
                                   QStringLiteral("wrong-password")));
      QVERIFY2(task_ipc_failed_with_summary(wrong_extract, &task_error),
               qPrintable(QStringLiteral("%1 wrong password: %2")
                              .arg(variant.label, task_error)));
      const QString wrong_extracted =
          QDir(wrong_out).filePath(QFileInfo(source_path).fileName());
      if (QFileInfo::exists(wrong_extracted)) {
        QVERIFY(read_file(wrong_extracted) != contents);
      }

      const QString correct_out =
          root_dir.filePath(variant.label + QStringLiteral("-correct-out"));
      const RealTaskIpcRunResult correct_extract =
          run_real_7zg_task_ipc_and_wait(
              root.path(),
              make_extract_payload(archive_path,
                                   correct_out,
                                   variant.password));
      QVERIFY2(task_ipc_succeeded(correct_extract, &task_error),
               qPrintable(QStringLiteral("%1 correct password: %2")
                              .arg(variant.label, task_error)));
      QCOMPARE(read_file(QDir(correct_out).filePath(
                   QFileInfo(source_path).fileName())),
               contents);
    }

    {
      const QString source_path =
          root_dir.filePath(QStringLiteral("zip-invalid-he.txt"));
      QVERIFY(write_file(source_path, QByteArrayLiteral("zip-he-body")));
      const QString archive_path =
          root_dir.filePath(QStringLiteral("zip-invalid-he.zip"));
      z7::task_ipc_runtime::TaskIpcPayload add_payload =
          make_add_payload(archive_path,
                           QStringLiteral("zip"),
                           QStringList{source_path});
      add_payload.add->password = QStringLiteral("test-password");
      add_payload.add->encryption_method = QStringLiteral("AES-256");
      add_payload.add->encrypt_headers_defined = true;
      add_payload.add->encrypt_headers = true;
      const RealTaskIpcRunResult invalid_he =
          run_real_7zg_task_ipc_and_wait(root.path(), add_payload);
      QVERIFY2(task_ipc_failed_with_summary(invalid_he, &task_error),
               qPrintable(QStringLiteral("zip invalid he: %1").arg(task_error)));
      QVERIFY2(!QFileInfo::exists(archive_path), qPrintable(archive_path));
    }

    QString selected_archive;
    QString archive_error;
    QVERIFY2(create_archive_with_entries(
                 root,
                 QStringLiteral("missing-entry.7z"),
                 {{QStringLiteral("docs/readme.md"),
                   QByteArrayLiteral("readme-body")}},
                 &selected_archive,
                 &archive_error),
             qPrintable(archive_error));

    const RealTaskIpcRunResult missing_test =
        run_real_7zg_task_ipc_and_wait(
            root.path(),
            make_archive_test_payload(
                selected_archive,
                QStringList{QStringLiteral("docs/missing.md")}));
    QVERIFY2(task_ipc_succeeded(missing_test, &task_error),
             qPrintable(QStringLiteral("missing test entry: %1").arg(task_error)));

    const RealTaskIpcRunResult missing_hash =
        run_real_7zg_task_ipc_and_wait(
            root.path(),
            make_archive_hash_payload(
                selected_archive,
                QStringList{QStringLiteral("docs/missing.md")},
                QStringLiteral("SHA256")));
    QVERIFY2(task_ipc_failed_with_summary(missing_hash, &task_error),
             qPrintable(QStringLiteral("missing hash entry: %1").arg(task_error)));

    z7::task_ipc_runtime::TaskIpcPayload missing_export_payload;
    missing_export_payload.command =
        z7::task_ipc_runtime::TaskIpcCommandKind::kArchiveExport;
    missing_export_payload.show_dialog = false;
    missing_export_payload.refresh_after_finish = false;
    missing_export_payload.archive_export =
        z7::task_ipc_runtime::TaskIpcArchiveExportPayload{};
    missing_export_payload.archive_export->root_archive_path = selected_archive;
    missing_export_payload.archive_export->root_archive_type =
        QStringLiteral("7z");
    missing_export_payload.archive_export->archive_entry_paths =
        QStringList{QStringLiteral("docs/missing.md")};
    missing_export_payload.archive_export->output_dir =
        root_dir.filePath(QStringLiteral("missing-export-out"));
    missing_export_payload.archive_export->overwrite_mode =
        QStringLiteral("-aoa");
    missing_export_payload.archive_export->path_mode = QStringLiteral("full");
    const RealTaskIpcRunResult missing_export =
        run_real_7zg_task_ipc_and_wait(root.path(), missing_export_payload);
    QVERIFY2(task_ipc_succeeded(missing_export, &task_error),
             qPrintable(QStringLiteral("missing export entry: %1")
                            .arg(task_error)));
    QVERIFY(!QFileInfo::exists(QDir(missing_export_payload.archive_export->output_dir)
                                   .filePath(QStringLiteral("docs/missing.md"))));
  }

  void real7zGTaskIpcCliPayloadEditsAndExtractsRealArchive() {
    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
        QByteArrayLiteral("1"));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());
    QString task_error;

    const QString alpha_path =
        root_dir.filePath(QStringLiteral("alpha.txt"));
    const QString beta_path =
        root_dir.filePath(QStringLiteral("beta.txt"));
    QVERIFY(write_file(alpha_path, QByteArrayLiteral("alpha-from-cli\n")));
    QVERIFY(write_file(beta_path, QByteArrayLiteral("beta-from-cli\n")));

    const QString archive_name = QStringLiteral("ipc-cli.7z");
    const auto run_cli = [&](const QStringList& argv) {
      return run_real_7zg_task_ipc_and_wait(
          root.path(),
          make_cli_payload(root.path(), argv));
    };

    RealTaskIpcRunResult result =
        run_cli({QStringLiteral("a"),
                 archive_name,
                 QStringLiteral("alpha.txt")});
    QVERIFY2(task_ipc_succeeded(result, &task_error),
             qPrintable(QStringLiteral("cli add alpha: %1").arg(task_error)));
    QVERIFY2(QFileInfo::exists(root_dir.filePath(archive_name)),
             qPrintable(root_dir.filePath(archive_name)));

    result = run_cli({QStringLiteral("u"),
                      archive_name,
                      QStringLiteral("beta.txt")});
    QVERIFY2(task_ipc_succeeded(result, &task_error),
             qPrintable(QStringLiteral("cli update beta: %1").arg(task_error)));

    result = run_cli({QStringLiteral("rn"),
                      archive_name,
                      QStringLiteral("alpha.txt"),
                      QStringLiteral("renamed.txt")});
    QVERIFY2(task_ipc_succeeded(result, &task_error),
             qPrintable(QStringLiteral("cli rename: %1").arg(task_error)));

    result = run_cli({QStringLiteral("d"),
                      archive_name,
                      QStringLiteral("beta.txt")});
    QVERIFY2(task_ipc_succeeded(result, &task_error),
             qPrintable(QStringLiteral("cli delete beta: %1").arg(task_error)));

    result = run_cli({QStringLiteral("t"), archive_name});
    QVERIFY2(task_ipc_succeeded(result, &task_error),
             qPrintable(QStringLiteral("cli test: %1").arg(task_error)));

    const QString output_dir =
        root_dir.filePath(QStringLiteral("ipc-cli-out"));
    result = run_cli({QStringLiteral("x"),
                      QStringLiteral("-y"),
                      QStringLiteral("-o") + output_dir,
                      archive_name});
    QVERIFY2(task_ipc_succeeded(result, &task_error),
             qPrintable(QStringLiteral("cli extract: %1").arg(task_error)));
    QCOMPARE(read_file(QDir(output_dir).filePath(QStringLiteral("renamed.txt"))),
             QByteArrayLiteral("alpha-from-cli\n"));
    QVERIFY(!QFileInfo::exists(
        QDir(output_dir).filePath(QStringLiteral("alpha.txt"))));
    QVERIFY(!QFileInfo::exists(
        QDir(output_dir).filePath(QStringLiteral("beta.txt"))));

    const RealTaskIpcRunResult missing_archive =
        run_cli({QStringLiteral("t"), QStringLiteral("missing.7z")});
    QVERIFY2(task_ipc_failed_with_summary(missing_archive, &task_error),
             qPrintable(QStringLiteral("cli missing archive: %1")
                            .arg(task_error)));
  }

  void real7zGTaskIpcAddZipPasswordAesRoundTripsWithExtract() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());

    const QString input_file = root_dir.filePath(QStringLiteral("alpha.txt"));
    QVERIFY(write_file(input_file, QByteArrayLiteral("alpha-secret-body")));
    const QString archive_path =
        root_dir.filePath(QStringLiteral("protected.zip"));

    z7::task_ipc_runtime::TaskIpcPayload add_payload =
        make_add_payload(archive_path,
                         QStringLiteral("zip"),
                         QStringList{input_file});
    add_payload.add->password = QStringLiteral("test-password");
    add_payload.add->encryption_method = QStringLiteral("AES-256");
    add_payload.add->encrypt_headers_defined = false;
    add_payload.add->encrypt_headers = false;

    const RealTaskIpcRunResult add_result =
        run_real_7zg_task_ipc_and_wait(root.path(), add_payload);
    QVERIFY2(add_result.dispatched, qPrintable(add_result.error_message));
    QVERIFY2(add_result.completed, qPrintable(add_result.error_message));
    QVERIFY2(add_result.error_message.trimmed().isEmpty(),
             qPrintable(add_result.error_message));
    QVERIFY2(add_result.completion.result_code == 0,
             qPrintable(QStringLiteral("add result_code=%1 summary=%2")
                            .arg(add_result.completion.result_code)
                            .arg(add_result.completion.summary)));
    QVERIFY2(QFileInfo::exists(archive_path), qPrintable(archive_path));

    const QString wrong_output_dir =
        root_dir.filePath(QStringLiteral("wrong-password-out"));
    QVERIFY(QDir().mkpath(wrong_output_dir));
    const DirectCliRunResult wrong_extract =
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("x"),
                        QStringLiteral("-pwrong-password"),
                        QStringLiteral("-y"),
                        QStringLiteral("-o") + wrong_output_dir,
                        archive_path},
            60000,
            true);
    QVERIFY2(wrong_extract.started, qPrintable(wrong_extract.error_message));
    QVERIFY2(wrong_extract.finished, qPrintable(wrong_extract.error_message));
    QVERIFY2(wrong_extract.exit_code != 0,
             "wrong password extract should fail");
    const QString wrong_extracted_file =
        QDir(wrong_output_dir).filePath(QStringLiteral("alpha.txt"));
    if (QFileInfo::exists(wrong_extracted_file)) {
      QVERIFY(read_file(wrong_extracted_file) !=
              QByteArrayLiteral("alpha-secret-body"));
    }

    const QString output_dir =
        root_dir.filePath(QStringLiteral("correct-password-out"));
    QVERIFY(QDir().mkpath(output_dir));
    const RealTaskIpcRunResult extract_result =
        run_real_7zg_task_ipc_and_wait(
            root.path(),
            make_extract_payload(archive_path,
                                 output_dir,
                                 QStringLiteral("test-password")));
    QVERIFY2(extract_result.dispatched, qPrintable(extract_result.error_message));
    QVERIFY2(extract_result.completed, qPrintable(extract_result.error_message));
    QVERIFY2(extract_result.error_message.trimmed().isEmpty(),
             qPrintable(extract_result.error_message));
    QVERIFY2(extract_result.completion.result_code == 0,
             qPrintable(QStringLiteral("extract result_code=%1 summary=%2")
                            .arg(extract_result.completion.result_code)
                            .arg(extract_result.completion.summary)));

    const QString extracted_file =
        QDir(output_dir).filePath(QStringLiteral("alpha.txt"));
    QVERIFY2(QFileInfo::exists(extracted_file), qPrintable(extracted_file));
    QCOMPARE(read_file(extracted_file), QByteArrayLiteral("alpha-secret-body"));
  }

  void real7zGTaskIpcAdd7zEncryptedHeadersRequirePassword() {
    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
        QByteArrayLiteral("1"));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());

    const QString input_file =
        root_dir.filePath(QStringLiteral("secret-name.txt"));
    QVERIFY(write_file(input_file, QByteArrayLiteral("seven-header-secret")));
    const QString archive_path =
        root_dir.filePath(QStringLiteral("headers-encrypted.7z"));

    z7::task_ipc_runtime::TaskIpcPayload add_payload =
        make_add_payload(archive_path,
                         QStringLiteral("7z"),
                         QStringList{input_file});
    add_payload.add->password = QStringLiteral("test-password");
    add_payload.add->encrypt_headers_defined = true;
    add_payload.add->encrypt_headers = true;

    const RealTaskIpcRunResult add_result =
        run_real_7zg_task_ipc_and_wait(root.path(), add_payload);
    QVERIFY2(add_result.dispatched, qPrintable(add_result.error_message));
    QVERIFY2(add_result.completed, qPrintable(add_result.error_message));
    QVERIFY2(add_result.error_message.trimmed().isEmpty(),
             qPrintable(add_result.error_message));
    QVERIFY2(add_result.completion.result_code == 0,
             qPrintable(QStringLiteral("add result_code=%1 summary=%2")
                            .arg(add_result.completion.result_code)
                            .arg(add_result.completion.summary)));
    QVERIFY2(QFileInfo::exists(archive_path), qPrintable(archive_path));

    z7::app::OpenArchiveFromPathRequest open_request;
    open_request.archive_path = to_native_path_string(archive_path);
    open_request.archive_type_hint = "7z";
    z7::app::ArchiveRequest archive_request;
    archive_request.payload = open_request;

    int cancel_prompt_count = 0;
    const z7::app::OperationOutcome canceled_open =
        run_archive_request_and_await(
            archive_request,
            [&cancel_prompt_count](const z7::app::PasswordPrompt&) {
              ++cancel_prompt_count;
              z7::app::PasswordReply reply;
              reply.kind = z7::app::PasswordReplyKind::kCancel;
              return std::optional<z7::app::PasswordReply>(reply);
            });
    QVERIFY(!canceled_open.ok);
    QCOMPARE(canceled_open.error.domain, z7::app::ArchiveErrorDomain::kPassword);
    QCOMPARE(cancel_prompt_count, 1);

    int provide_prompt_count = 0;
    const z7::app::OperationOutcome opened_outcome =
        run_archive_request_and_await(
            archive_request,
            [&provide_prompt_count](const z7::app::PasswordPrompt&) {
              ++provide_prompt_count;
              z7::app::PasswordReply reply;
              reply.kind = z7::app::PasswordReplyKind::kProvide;
              reply.password = "test-password";
              return std::optional<z7::app::PasswordReply>(reply);
            });
    QVERIFY2(opened_outcome.ok, opened_outcome.summary.c_str());
    QCOMPARE(provide_prompt_count, 1);
    const auto* opened =
        std::get_if<z7::app::OpenArchiveSessionResult>(
            &opened_outcome.payload);
    QVERIFY(opened != nullptr);
    QVERIFY(opened->token.is_valid());

    z7::app::ListRequest list_request;
    list_request.session_token = opened->token;
    z7::app::ArchiveRequest list_archive_request;
    list_archive_request.payload = list_request;
    const z7::app::OperationOutcome list_outcome =
        run_archive_request_and_await(list_archive_request);
    QVERIFY2(list_outcome.ok, list_outcome.summary.c_str());
    const auto* list_result =
        std::get_if<z7::app::ListResult>(&list_outcome.payload);
    QVERIFY(list_result != nullptr);
    bool saw_secret_name = false;
    for (const z7::app::ArchiveListEntry& entry : list_result->entries) {
      if (entry.path == "secret-name.txt" && !entry.is_dir) {
        saw_secret_name = true;
        break;
      }
    }
    QVERIFY(saw_secret_name);

    const QString session_output_dir =
        root_dir.filePath(QStringLiteral("headers-session-out"));
    QVERIFY(QDir().mkpath(session_output_dir));
    z7::app::ExtractRequest session_extract;
    session_extract.session_token = opened->token;
    session_extract.output_dir = to_native_path_string(session_output_dir);
    session_extract.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
    z7::app::ArchiveRequest session_extract_archive_request;
    session_extract_archive_request.payload = session_extract;
    const z7::app::OperationOutcome session_extract_outcome =
        run_archive_request_and_await(session_extract_archive_request);
    QVERIFY2(session_extract_outcome.ok,
             session_extract_outcome.summary.c_str());
    QCOMPARE(read_file(QDir(session_output_dir)
                           .filePath(QStringLiteral("secret-name.txt"))),
             QByteArrayLiteral("seven-header-secret"));

    z7::app::CloseArchiveSessionRequest close_request;
    close_request.token = opened->token;
    z7::app::ArchiveRequest close_archive_request;
    close_archive_request.payload = close_request;
    const z7::app::OperationOutcome close_outcome =
        run_archive_request_and_await(close_archive_request);
    QVERIFY2(close_outcome.ok, close_outcome.summary.c_str());

    const QString backend_output_dir =
        root_dir.filePath(QStringLiteral("headers-backend-out"));
    QVERIFY(QDir().mkpath(backend_output_dir));
    z7::app::ExtractRequest backend_extract;
    backend_extract.archive_path = to_native_path_string(archive_path);
    backend_extract.output_dir = to_native_path_string(backend_output_dir);
    backend_extract.password = "test-password";
    backend_extract.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
    z7::app::ArchiveRequest backend_extract_archive_request;
    backend_extract_archive_request.payload = backend_extract;
    int backend_extract_prompt_count = 0;
    const z7::app::OperationOutcome backend_extract_outcome =
        run_archive_request_and_await(
            backend_extract_archive_request,
            [&backend_extract_prompt_count](const z7::app::PasswordPrompt&) {
              ++backend_extract_prompt_count;
              z7::app::PasswordReply reply;
              reply.kind = z7::app::PasswordReplyKind::kProvide;
              reply.password = "test-password";
              return std::optional<z7::app::PasswordReply>(reply);
            });
    QVERIFY2(backend_extract_outcome.ok,
             qPrintable(QStringLiteral("%1 prompts=%2")
                            .arg(QString::fromStdString(
                                backend_extract_outcome.summary))
                            .arg(backend_extract_prompt_count)));
    QVERIFY(backend_extract_prompt_count <= 1);
    QCOMPARE(read_file(QDir(backend_output_dir)
                           .filePath(QStringLiteral("secret-name.txt"))),
             QByteArrayLiteral("seven-header-secret"));

    const QString output_dir =
        root_dir.filePath(QStringLiteral("headers-password-out"));
    QVERIFY(QDir().mkpath(output_dir));
    const RealTaskIpcRunResult extract_result =
        run_real_7zg_task_ipc_and_wait(
            root.path(),
            make_extract_payload(archive_path,
                                 output_dir,
                                 QStringLiteral("test-password")));
    QVERIFY2(extract_result.dispatched,
             qPrintable(extract_result.error_message));
    QVERIFY2(extract_result.completed,
             qPrintable(extract_result.error_message));
    QVERIFY2(extract_result.error_message.trimmed().isEmpty(),
             qPrintable(extract_result.error_message));
    QVERIFY2(extract_result.completion.result_code == 0,
             qPrintable(QStringLiteral("extract result_code=%1 summary=%2")
                            .arg(extract_result.completion.result_code)
                            .arg(extract_result.completion.summary)));

    const QString extracted_file =
        QDir(output_dir).filePath(QStringLiteral("secret-name.txt"));
    QVERIFY2(QFileInfo::exists(extracted_file), qPrintable(extracted_file));
    QCOMPARE(read_file(extracted_file),
             QByteArrayLiteral("seven-header-secret"));
  }

  void fileManagerLaunchesReal7zGForExtractTaskIpc() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString archive_path;
    QVERIFY2(create_sample_archive(root, &archive_path),
             "failed to prepare sample archive");

    const QString output_dir =
        QDir(root.path()).filePath(QStringLiteral("fm-launch-extract-out"));
    QVERIFY(QDir().mkpath(output_dir));

    const FileManagerRealExtractResult run_result =
        launch_file_manager_real_7zg_extract_and_wait(
            root.path(),
            QStringList{archive_path},
            output_dir,
            QDir(root.path()).filePath(
                QStringLiteral(".z7-fm-real-7zg-settings")));
    QVERIFY2(run_result.launched, qPrintable(run_result.error_message));
    QVERIFY2(run_result.completed, qPrintable(run_result.error_message));
    QVERIFY2(run_result.error_message.trimmed().isEmpty(),
             qPrintable(run_result.error_message));
    QVERIFY2(run_result.completion.result_code == 0,
             qPrintable(QStringLiteral("result_code=%1 summary=%2")
                            .arg(run_result.completion.result_code)
                            .arg(run_result.completion.summary)));

    const QString extracted_file =
        QDir(output_dir).filePath(QStringLiteral("sample.txt"));
    QVERIFY2(QFileInfo::exists(extracted_file), qPrintable(extracted_file));
    QCOMPARE(read_file(extracted_file),
             QByteArrayLiteral("7zg test result lifecycle smoke\n"));
  }

  void fileManagerLaunchesReal7zGForMultiSelectExtractTaskIpc() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString first_archive;
    QString second_archive;
    QString archive_error;
    QVERIFY2(create_single_file_archive(root,
                                        QStringLiteral("multi-one.7z"),
                                        QStringLiteral("one.txt"),
                                        QByteArrayLiteral("one-body\n"),
                                        &first_archive,
                                        &archive_error),
             qPrintable(archive_error));
    QVERIFY2(create_single_file_archive(root,
                                        QStringLiteral("multi-two.7z"),
                                        QStringLiteral("two.txt"),
                                        QByteArrayLiteral("two-body\n"),
                                        &second_archive,
                                        &archive_error),
             qPrintable(archive_error));

    const QString output_dir =
        QDir(root.path()).filePath(QStringLiteral("fm-launch-multi-out"));
    QVERIFY(QDir().mkpath(output_dir));

    const FileManagerRealExtractResult run_result =
        launch_file_manager_real_7zg_extract_and_wait(
            root.path(),
            QStringList{first_archive, second_archive},
            output_dir,
            QDir(root.path()).filePath(
                QStringLiteral(".z7-fm-real-7zg-multi-settings")));
    QVERIFY2(run_result.launched, qPrintable(run_result.error_message));
    QVERIFY2(run_result.completed, qPrintable(run_result.error_message));
    QVERIFY2(run_result.error_message.trimmed().isEmpty(),
             qPrintable(run_result.error_message));
    QVERIFY2(run_result.completion.result_code == 0,
             qPrintable(QStringLiteral("result_code=%1 summary=%2")
                            .arg(run_result.completion.result_code)
                            .arg(run_result.completion.summary)));
    QVERIFY(run_result.completion.payload.extract.has_value());
    QCOMPARE(run_result.completion.payload.extract->archive_inputs.size(), 2);

    const QString first_output = QDir(output_dir).filePath(QStringLiteral("one.txt"));
    const QString second_output = QDir(output_dir).filePath(QStringLiteral("two.txt"));
    QVERIFY2(QFileInfo::exists(first_output), qPrintable(first_output));
    QVERIFY2(QFileInfo::exists(second_output), qPrintable(second_output));
    QCOMPARE(read_file(first_output), QByteArrayLiteral("one-body\n"));
    QCOMPARE(read_file(second_output), QByteArrayLiteral("two-body\n"));
  }

  void fileManagerLaunchesReal7zGForMixedSelectExtractTaskIpc() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString archive_path;
    QString archive_error;
    QVERIFY2(create_single_file_archive(root,
                                        QStringLiteral("mixed-valid.7z"),
                                        QStringLiteral("valid.txt"),
                                        QByteArrayLiteral("valid-body\n"),
                                        &archive_path,
                                        &archive_error),
             qPrintable(archive_error));
    const QString plain_path =
        QDir(root.path()).filePath(QStringLiteral("plain-not-archive.txt"));
    QVERIFY(write_file(plain_path, QByteArrayLiteral("not an archive\n")));

    const QString output_dir =
        QDir(root.path()).filePath(QStringLiteral("fm-launch-mixed-out"));
    QVERIFY(QDir().mkpath(output_dir));

    const FileManagerRealExtractResult run_result =
        launch_file_manager_real_7zg_extract_and_wait(
            root.path(),
            QStringList{archive_path, plain_path},
            output_dir,
            QDir(root.path()).filePath(
                QStringLiteral(".z7-fm-real-7zg-mixed-settings")));
    QVERIFY2(run_result.launched, qPrintable(run_result.error_message));
    QVERIFY2(run_result.completed, qPrintable(run_result.error_message));
    QVERIFY2(run_result.error_message.trimmed().isEmpty(),
             qPrintable(run_result.error_message));
    QVERIFY2(run_result.completion.result_code != 0,
             "mixed archive/non-archive extract should report worker failure");
    QVERIFY(!run_result.completion.summary.trimmed().isEmpty());
    QVERIFY(run_result.completion.payload.extract.has_value());
    QCOMPARE(run_result.completion.payload.extract->archive_inputs.size(), 2);

    const QString valid_output =
        QDir(output_dir).filePath(QStringLiteral("valid.txt"));
    QVERIFY2(QFileInfo::exists(valid_output), qPrintable(valid_output));
    QCOMPARE(read_file(valid_output), QByteArrayLiteral("valid-body\n"));
    QVERIFY(!QFileInfo::exists(
        QDir(output_dir).filePath(QStringLiteral("plain-not-archive.txt"))));
  }

  void real7zGTaskIpcArchiveExportHonorsPathRemaps() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString archive_path;
    QString archive_error;
    QVERIFY2(create_path_remap_archive(root, &archive_path, &archive_error),
             qPrintable(archive_error));

    const QString output_dir =
        QDir(root.path()).filePath(QStringLiteral("export-out"));
    const QString remap_destination =
        QDir(output_dir).filePath(QStringLiteral("docs-export"));
    QVERIFY(QDir().mkpath(output_dir));

    const z7::task_ipc_runtime::TaskIpcPayload payload =
        make_archive_export_remap_payload(archive_path,
                                          output_dir,
                                          remap_destination);
    const RealTaskIpcRunResult run_result = run_real_7zg_task_ipc_and_wait(
        QFileInfo(archive_path).absolutePath(),
        payload);

    QVERIFY2(run_result.dispatched, qPrintable(run_result.error_message));
    QVERIFY2(run_result.completed, qPrintable(run_result.error_message));
    QVERIFY2(run_result.error_message.trimmed().isEmpty(),
             qPrintable(run_result.error_message));
    QVERIFY2(run_result.completion.result_code == 0,
             qPrintable(QStringLiteral("result_code=%1 summary=%2")
                            .arg(run_result.completion.result_code)
                            .arg(run_result.completion.summary)));

    const QString rewritten_readme =
        QDir(remap_destination).filePath(QStringLiteral("readme.md"));
    QVERIFY2(QFileInfo::exists(rewritten_readme),
             qPrintable(rewritten_readme));
    QCOMPARE(read_file(rewritten_readme), QByteArrayLiteral("docs-body"));
    QVERIFY(!QFileInfo::exists(
        QDir(output_dir).filePath(QStringLiteral("docs/readme.md"))));
    QVERIFY(!QFileInfo::exists(
        QDir(remap_destination).filePath(QStringLiteral("report.txt"))));
  }

  void real7zGTaskIpcAddEmailPayloadIsRejectedWithoutCreatingArchive() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString input_file =
        QDir(root.path()).filePath(QStringLiteral("mail-source.txt"));
    const QString archive_path =
        QDir(root.path()).filePath(QStringLiteral("mail-output.7z"));
    QVERIFY(write_file(input_file, QByteArrayLiteral("mail-body")));

    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS", "1");
    const RealTaskIpcRunResult run_result =
        run_real_7zg_task_ipc_and_wait(
            root.path(),
            make_email_add_payload(input_file, archive_path));

    QVERIFY2(run_result.dispatched, qPrintable(run_result.error_message));
    QVERIFY2(run_result.completed, qPrintable(run_result.error_message));
    QVERIFY2(run_result.error_message.trimmed().isEmpty(),
             qPrintable(run_result.error_message));
    QVERIFY(run_result.completion.result_code != 0);
    QVERIFY2(run_result.completion.summary.contains(QStringLiteral("Email")),
             qPrintable(run_result.completion.summary));
    QVERIFY2(run_result.completion.summary.contains(QStringLiteral("not supported")),
             qPrintable(run_result.completion.summary));
    QVERIFY(!QFileInfo::exists(archive_path));
  }

  void real7zGDirectCliErrorsUseGuiDialogs() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const DirectCliRunResult no_command =
        run_real_7zg_direct_cli(root.path(), {}, 60000, true);
    QVERIFY2(no_command.started, qPrintable(no_command.error_message));
    QVERIFY2(no_command.finished, qPrintable(no_command.error_message));
    QCOMPARE(no_command.exit_code, 0);
    QCOMPARE(no_command.stdout_data, QByteArray());
    QString stderr_text = QString::fromLocal8Bit(no_command.stderr_data);
    QVERIFY2(stderr_text.contains(QStringLiteral("GUI error dialog suppressed")),
             qPrintable(stderr_text));
    QVERIFY2(stderr_text.contains(QStringLiteral("title=7-Zip")),
             qPrintable(stderr_text));
    QVERIFY2(stderr_text.contains(QStringLiteral("Specify command")),
             qPrintable(stderr_text));

    QVERIFY(write_file(QDir(root.path()).filePath(QStringLiteral("mail.txt")),
                       QByteArrayLiteral("mail-body")));
    const DirectCliRunResult unsupported =
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("a"),
                        QStringLiteral("-semltest@example.invalid"),
                        QStringLiteral("mail.7z"),
                        QStringLiteral("mail.txt")},
            60000,
            true);
    QVERIFY2(unsupported.started, qPrintable(unsupported.error_message));
    QVERIFY2(unsupported.finished, qPrintable(unsupported.error_message));
    QVERIFY(unsupported.exit_code != 0);
    QCOMPARE(unsupported.stdout_data, QByteArray());
    stderr_text = QString::fromLocal8Bit(unsupported.stderr_data);
    QVERIFY2(stderr_text.contains(QStringLiteral("GUI error dialog suppressed")),
             qPrintable(stderr_text));
    QVERIFY2(stderr_text.contains(QStringLiteral("title=7-Zip")),
             qPrintable(stderr_text));
    QVERIFY2(stderr_text.contains(QStringLiteral("Unsupported command-line mode")),
             qPrintable(stderr_text));
    QVERIFY2(stderr_text.contains(QStringLiteral("email")),
             qPrintable(stderr_text));

    const DirectCliRunResult invalid_command =
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("not-a-command")},
                                60000,
                                true);
    QVERIFY2(invalid_command.started, qPrintable(invalid_command.error_message));
    QVERIFY2(invalid_command.finished, qPrintable(invalid_command.error_message));
    QVERIFY(invalid_command.exit_code != 0);
    QCOMPARE(invalid_command.stdout_data, QByteArray());
    stderr_text = QString::fromLocal8Bit(invalid_command.stderr_data);
    QVERIFY2(stderr_text.contains(QStringLiteral("GUI error dialog suppressed")),
             qPrintable(stderr_text));
    QVERIFY2(stderr_text.contains(QStringLiteral("title=7-Zip")),
             qPrintable(stderr_text));
    QVERIFY2(stderr_text.contains(QStringLiteral("Unsupported command")),
             qPrintable(stderr_text));
    QVERIFY2(!stderr_text.contains(QStringLiteral("Command Line Error:")),
             qPrintable(stderr_text));

    const DirectCliRunResult missing_archive_name =
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("x")},
                                60000,
                                true);
    QVERIFY2(missing_archive_name.started,
             qPrintable(missing_archive_name.error_message));
    QVERIFY2(missing_archive_name.finished,
             qPrintable(missing_archive_name.error_message));
    QVERIFY(missing_archive_name.exit_code != 0);
    QCOMPARE(missing_archive_name.stdout_data, QByteArray());
    stderr_text = QString::fromLocal8Bit(missing_archive_name.stderr_data);
    QVERIFY2(stderr_text.contains(QStringLiteral("GUI error dialog suppressed")),
             qPrintable(stderr_text));
    QVERIFY2(stderr_text.contains(QStringLiteral("title=7-Zip")),
             qPrintable(stderr_text));
    QVERIFY2(stderr_text.contains(QStringLiteral("Cannot find archive name")),
             qPrintable(stderr_text));
    QVERIFY2(!stderr_text.contains(QStringLiteral("Command Line Error:")),
             qPrintable(stderr_text));

    const DirectCliRunResult missing_archive =
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("x"),
                                            QStringLiteral("missing.7z")},
                                60000,
                                true);
    QVERIFY2(missing_archive.started, qPrintable(missing_archive.error_message));
    QVERIFY2(missing_archive.finished, qPrintable(missing_archive.error_message));
    QVERIFY(missing_archive.exit_code != 0);
    QCOMPARE(missing_archive.stdout_data, QByteArray());
    stderr_text = QString::fromLocal8Bit(missing_archive.stderr_data);
    QVERIFY2(stderr_text.contains(QStringLiteral("GUI error dialog suppressed")),
             qPrintable(stderr_text));
    QVERIFY2(stderr_text.contains(QStringLiteral("title=7-Zip")),
             qPrintable(stderr_text));
  }

  void real7zGDirectCliListAndInfoMatchOriginalUnsupportedGui() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString archive_path;
    QString archive_error;
    QVERIFY2(create_path_remap_archive(root, &archive_path, &archive_error),
             qPrintable(archive_error));

    const DirectCliRunResult info =
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("i")},
                                60000,
                                true);
    QVERIFY2(info.started, qPrintable(info.error_message));
    QVERIFY2(info.finished, qPrintable(info.error_message));
    QCOMPARE(info.exit_code, 2);
    const QString info_stderr = QString::fromLocal8Bit(info.stderr_data);
    QVERIFY2(info_stderr.contains(QStringLiteral("GUI error dialog suppressed")),
             qPrintable(info_stderr));
    QVERIFY2(info_stderr.contains(QStringLiteral("title=7-Zip")),
             qPrintable(info_stderr));
    QVERIFY2(info_stderr.contains(QStringLiteral("Unsupported command")),
             qPrintable(info_stderr));
    QVERIFY2(!info_stderr.contains(QStringLiteral("Command Line Error:")),
             qPrintable(info_stderr));
    QCOMPARE(info.stdout_data, QByteArray());

    const DirectCliRunResult list =
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("l"), archive_path},
                                60000,
                                true);
    QVERIFY2(list.started, qPrintable(list.error_message));
    QVERIFY2(list.finished, qPrintable(list.error_message));
    QCOMPARE(list.exit_code, 2);
    const QString list_stderr = QString::fromLocal8Bit(list.stderr_data);
    QVERIFY2(list_stderr.contains(QStringLiteral("GUI error dialog suppressed")),
             qPrintable(list_stderr));
    QVERIFY2(list_stderr.contains(QStringLiteral("title=7-Zip")),
             qPrintable(list_stderr));
    QVERIFY2(list_stderr.contains(QStringLiteral("Unsupported command")),
             qPrintable(list_stderr));
    QVERIFY2(!list_stderr.contains(QStringLiteral("Command Line Error:")),
             qPrintable(list_stderr));
    QCOMPARE(list.stdout_data, QByteArray());
  }

  void real7zGDirectCliStdInStdOutStreamsLikeOriginal() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());

    const QByteArray stdin_file_data("stdin-file-body\n");
    const QByteArray stdin_hash_data("hash-stream-body\n");
    const QString expected_sha256 =
        QString::fromLatin1(
            QCryptographicHash::hash(stdin_hash_data,
                                     QCryptographicHash::Sha256)
                .toHex())
            .toLower();

    const DirectCliRunResult hash =
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("h"),
                                            QStringLiteral("-scrcSHA256"),
                                            QStringLiteral("-sihash-input.bin")},
                                60000,
                                false,
                                stdin_hash_data);
    verify_direct_cli_success(hash, QStringLiteral("7zG h -si"));
    const QString hash_stdout = QString::fromLocal8Bit(hash.stdout_data);
    QVERIFY(hash_stdout.contains(QStringLiteral("SHA256")));
    QVERIFY(hash_stdout.contains(expected_sha256));

    verify_direct_cli_success(
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("a"),
                                            QStringLiteral("-y"),
                                            QStringLiteral("stdin-add.7z"),
                                            QStringLiteral("-sistdin.txt")},
                                60000,
                                false,
                                stdin_file_data),
        QStringLiteral("7zG a -si"));
    const QString stdin_add_out =
        root_dir.filePath(QStringLiteral("stdin-add-out"));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("x"),
                                            QStringLiteral("-y"),
                                            QStringLiteral("-o") + stdin_add_out,
                                            QStringLiteral("stdin-add.7z")}),
        QStringLiteral("7zG x stdin-add.7z"));
    QCOMPARE(read_file(QDir(stdin_add_out).filePath(QStringLiteral("stdin.txt"))),
             stdin_file_data);

    QString source_archive;
    QString archive_error;
    QVERIFY2(create_single_file_archive(root,
                                        QStringLiteral("stdout-source.7z"),
                                        QStringLiteral("payload.txt"),
                                        QByteArrayLiteral("payload-through-stdout"),
                                        &source_archive,
                                        &archive_error),
             qPrintable(archive_error));
    const DirectCliRunResult extract_stdout =
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("x"),
                                            QStringLiteral("-so"),
                                            source_archive,
                                            QStringLiteral("payload.txt")});
    verify_direct_cli_success(extract_stdout, QStringLiteral("7zG x -so"));
    QCOMPARE(extract_stdout.stdout_data,
             QByteArrayLiteral("payload-through-stdout"));

    QVERIFY(write_file(root_dir.filePath(QStringLiteral("stdin-gzip-source.txt")),
                       QByteArrayLiteral("gzip-stdin-payload")));
    const DirectCliRunResult gzip_stdout =
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("a"),
                                            QStringLiteral("-tgzip"),
                                            QStringLiteral("-so"),
                                            QStringLiteral("-an"),
                                            QStringLiteral("stdin-gzip-source.txt")});
    verify_direct_cli_success(gzip_stdout,
                              QStringLiteral("7zG a -tgzip -so -an"));
    QVERIFY(!gzip_stdout.stdout_data.isEmpty());
    const QByteArray archive_stream_bytes = gzip_stdout.stdout_data;

    verify_direct_cli_success(
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("t"),
                                            QStringLiteral("-sistreamed.gz")},
                                60000,
                                false,
                                archive_stream_bytes),
        QStringLiteral("7zG t -si"));
    const QString stdin_extract_out =
        root_dir.filePath(QStringLiteral("stdin-extract-out"));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("x"),
                                            QStringLiteral("-y"),
                                            QStringLiteral("-o") + stdin_extract_out,
                                            QStringLiteral("-sistreamed.gz")},
                                60000,
                                false,
                                archive_stream_bytes),
        QStringLiteral("7zG x -si"));
    QCOMPARE(read_file(QDir(stdin_extract_out)
                           .filePath(QStringLiteral("stdin-gzip-source.txt"))),
             QByteArrayLiteral("gzip-stdin-payload"));

    QVERIFY(write_file(root_dir.filePath(QStringLiteral("stdout-add.txt")),
                       QByteArrayLiteral("archive-written-to-stdout")));
    const DirectCliRunResult add_stdout =
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("a"),
                                            QStringLiteral("-tgzip"),
                                            QStringLiteral("-so"),
                                            QStringLiteral("-an"),
                                            QStringLiteral("stdout-add.txt")});
    verify_direct_cli_success(add_stdout, QStringLiteral("7zG a -so"));
    QVERIFY(!add_stdout.stdout_data.isEmpty());
    const QString captured_archive =
        root_dir.filePath(QStringLiteral("captured-stdout.gz"));
    QVERIFY(write_file(captured_archive, add_stdout.stdout_data));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("t"),
                                            captured_archive}),
        QStringLiteral("7zG t captured stdout archive"));
  }

  void real7zGDirectCliAddUpdateDeleteRenameTestHashAndBenchmark() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir root_dir(root.path());

    QVERIFY(write_file(root_dir.filePath(QStringLiteral("alpha.txt")),
                       QByteArrayLiteral("alpha-one\n")));
    QVERIFY(write_file(root_dir.filePath(QStringLiteral("beta.txt")),
                       QByteArrayLiteral("beta-two\n")));

    const QString archive_name = QStringLiteral("cli-edit.7z");
    verify_direct_cli_success(
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("a"),
                        archive_name,
                        QStringLiteral("alpha.txt")}),
        QStringLiteral("7zG a"));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("u"),
                        archive_name,
                        QStringLiteral("beta.txt")}),
        QStringLiteral("7zG u"));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("rn"),
                        archive_name,
                        QStringLiteral("alpha.txt"),
                        QStringLiteral("renamed.txt")}),
        QStringLiteral("7zG rn"));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("d"),
                        archive_name,
                        QStringLiteral("beta.txt")}),
        QStringLiteral("7zG d"));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("t"), archive_name}),
        QStringLiteral("7zG t"));

    const QString verify_out =
        root_dir.filePath(QStringLiteral("edit-verify-out"));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("x"),
                        QStringLiteral("-y"),
                        QStringLiteral("-o") + verify_out,
                        archive_name}),
        QStringLiteral("7zG x after edit"));
    QVERIFY(QFileInfo::exists(
        QDir(verify_out).filePath(QStringLiteral("renamed.txt"))));
    QVERIFY(!QFileInfo::exists(
        QDir(verify_out).filePath(QStringLiteral("alpha.txt"))));
    QVERIFY(!QFileInfo::exists(
        QDir(verify_out).filePath(QStringLiteral("beta.txt"))));

    const DirectCliRunResult hash =
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("h"),
                                            QStringLiteral("-scrcSHA256"),
                                            QStringLiteral("alpha.txt")});
    verify_direct_cli_success(hash, QStringLiteral("7zG h"));
    QVERIFY(QString::fromLocal8Bit(hash.stdout_data)
                .contains(QStringLiteral("SHA256")));

    const DirectCliRunResult benchmark =
        run_real_7zg_direct_cli(root.path(),
                                QStringList{QStringLiteral("b"),
                                            QStringLiteral("-mmt=1"),
                                            QStringLiteral("-md=1m"),
                                            QStringLiteral("1")},
                                90000);
    verify_direct_cli_success(benchmark, QStringLiteral("7zG b"));
    QVERIFY(QString::fromLocal8Bit(benchmark.stdout_data)
                .contains(QStringLiteral("Compressing")));
  }

  void real7zGDirectCliExtractModesSpeSpoAndSelectors() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString archive_path;
    QString archive_error;
    QVERIFY2(create_archive_with_entries(
                 root,
                 QStringLiteral("selectors.7z"),
                 {{QStringLiteral("docs/readme.md"),
                   QByteArrayLiteral("readme-body")},
                  {QStringLiteral("docs/skip.tmp"),
                   QByteArrayLiteral("skip-body")},
                  {QStringLiteral("report.txt"),
                   QByteArrayLiteral("report-body")}},
                 &archive_path,
                 &archive_error),
             qPrintable(archive_error));

    const QString full_out = QDir(root.path()).filePath(QStringLiteral("full-out"));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("x"),
                        QStringLiteral("-y"),
                        QStringLiteral("-o") + full_out,
                        archive_path}),
        QStringLiteral("7zG x"));
    QVERIFY(QFileInfo::exists(
        QDir(full_out).filePath(QStringLiteral("docs/readme.md"))));

    const QString flat_out = QDir(root.path()).filePath(QStringLiteral("flat-out"));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("e"),
                        QStringLiteral("-y"),
                        QStringLiteral("-o") + flat_out,
                        archive_path,
                        QStringLiteral("docs/readme.md")}),
        QStringLiteral("7zG e"));
    QVERIFY(QFileInfo::exists(
        QDir(flat_out).filePath(QStringLiteral("readme.md"))));
    QVERIFY(!QFileInfo::exists(
        QDir(flat_out).filePath(QStringLiteral("docs/readme.md"))));

    const QString wildcard_out =
        QDir(root.path()).filePath(QStringLiteral("wildcard-out"));
    const DirectCliRunResult wildcard_extract =
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("x"),
                        QStringLiteral("-y"),
                        QStringLiteral("-o") + wildcard_out,
                        archive_path,
                        QStringLiteral("docs/*"),
                        QStringLiteral("-xr!*.tmp")});
    verify_direct_cli_success(wildcard_extract,
                              QStringLiteral("7zG x wildcard"));
    QVERIFY(QFileInfo::exists(
        QDir(wildcard_out).filePath(QStringLiteral("docs/readme.md"))));
    QVERIFY(!QFileInfo::exists(
        QDir(wildcard_out).filePath(QStringLiteral("docs/skip.tmp"))));
    QVERIFY(!QFileInfo::exists(
        QDir(wildcard_out).filePath(QStringLiteral("report.txt"))));

    const QString listfile_path =
        QDir(root.path()).filePath(QStringLiteral("entry-list.txt"));
    QVERIFY(write_file(listfile_path, QByteArrayLiteral("docs/readme.md\n")));
    const QString listfile_out =
        QDir(root.path()).filePath(QStringLiteral("listfile-out"));
    const DirectCliRunResult listfile_extract =
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("x"),
                        QStringLiteral("-y"),
                        QStringLiteral("-o") + listfile_out,
                        archive_path,
                        QStringLiteral("@entry-list.txt")});
    verify_direct_cli_success(listfile_extract,
                              QStringLiteral("7zG x @listfile"));
    QVERIFY(QFileInfo::exists(
        QDir(listfile_out).filePath(QStringLiteral("docs/readme.md"))));
    QVERIFY(!QFileInfo::exists(
        QDir(listfile_out).filePath(QStringLiteral("report.txt"))));

    const QString rooted_archive = QDir(root.path()).filePath(QStringLiteral("root.7z"));
    QVERIFY2(create_archive_with_entries(
                 root,
                 QStringLiteral("root.7z"),
                 {{QStringLiteral("root/readme.txt"),
                   QByteArrayLiteral("root-body")}},
                 &archive_path,
                 &archive_error),
             qPrintable(archive_error));
    QCOMPARE(archive_path, rooted_archive);
    const QString spe_out =
        QDir(root.path()).filePath(QStringLiteral("spe-out/root"));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("x"),
                        QStringLiteral("-y"),
                        QStringLiteral("-spe"),
                        QStringLiteral("-o") + spe_out,
                        rooted_archive}),
        QStringLiteral("7zG x -spe"));
    QVERIFY(QFileInfo::exists(
        QDir(root.path()).filePath(QStringLiteral("spe-out/root/readme.txt"))));
    QVERIFY(!QFileInfo::exists(
        QDir(root.path()).filePath(QStringLiteral("spe-out/root/root/readme.txt"))));

    QString first_archive;
    QString second_archive;
    QVERIFY2(create_single_file_archive(root,
                                        QStringLiteral("one.7z"),
                                        QStringLiteral("same.txt"),
                                        QByteArrayLiteral("one-body"),
                                        &first_archive,
                                        &archive_error),
             qPrintable(archive_error));
    QVERIFY2(create_single_file_archive(root,
                                        QStringLiteral("two.7z"),
                                        QStringLiteral("same.txt"),
                                        QByteArrayLiteral("two-body"),
                                        &second_archive,
                                        &archive_error),
             qPrintable(archive_error));

    const QString spo_out = QDir(root.path()).filePath(QStringLiteral("spo-out"));
    verify_direct_cli_success(
        run_real_7zg_direct_cli(
            root.path(),
            QStringList{QStringLiteral("x"),
                        QStringLiteral("-y"),
                        QStringLiteral("-spoc"),
                        QStringLiteral("-o") + spo_out,
                        QStringLiteral("*.7z")}),
        QStringLiteral("7zG x -spoc"));
    QCOMPARE(read_file(QDir(spo_out).filePath(QStringLiteral("one/same.txt"))),
             QByteArrayLiteral("one-body"));
    QCOMPARE(read_file(QDir(spo_out).filePath(QStringLiteral("two/same.txt"))),
             QByteArrayLiteral("two-body"));
  }

  void real7zGTaskIpcTestPublishesSuccessAndFailureCompletions() {
    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
        QByteArrayLiteral("1"));

    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString archive_path;
    QVERIFY2(create_sample_archive(root, &archive_path),
             "failed to prepare sample archive");

    const RealTaskIpcRunResult success_result =
        run_real_7zg_task_ipc_and_wait(
            QFileInfo(archive_path).absolutePath(),
            make_test_payload(QStringList{archive_path}));
    QVERIFY2(success_result.dispatched,
             qPrintable(success_result.error_message));
    QVERIFY2(success_result.completed,
             qPrintable(success_result.error_message));
    QVERIFY2(success_result.error_message.trimmed().isEmpty(),
             qPrintable(success_result.error_message));
    QCOMPARE(success_result.completion.result_code, 0);
    QCOMPARE(success_result.completion.payload.command,
             z7::task_ipc_runtime::TaskIpcCommandKind::kTest);

    const QString missing_archive =
        QDir(root.path()).filePath(QStringLiteral("missing.7z"));
    const RealTaskIpcRunResult failure_result =
        run_real_7zg_task_ipc_and_wait(
            root.path(),
            make_test_payload(QStringList{missing_archive}));
    QVERIFY2(failure_result.dispatched,
             qPrintable(failure_result.error_message));
    QVERIFY2(failure_result.completed,
             qPrintable(failure_result.error_message));
    QVERIFY2(failure_result.error_message.trimmed().isEmpty(),
             qPrintable(failure_result.error_message));
    QVERIFY2(failure_result.completion.result_code != 0,
             qPrintable(failure_result.completion.summary));
    QVERIFY(!failure_result.completion.summary.trimmed().isEmpty());
    QCOMPARE(failure_result.completion.payload.command,
             z7::task_ipc_runtime::TaskIpcCommandKind::kTest);
  }

  void real7zGTaskIpcArchiveTestHonorsEntrySelection() {
    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
        QByteArrayLiteral("1"));

    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString archive_path;
    QString archive_error;
    QVERIFY2(create_archive_with_entries(
                 root,
                 QStringLiteral("selected-test.7z"),
                 {{QStringLiteral("docs/readme.md"),
                   QByteArrayLiteral("readme-body")},
                  {QStringLiteral("report.txt"),
                   QByteArrayLiteral("report-body")}},
                 &archive_path,
                 &archive_error),
             qPrintable(archive_error));

    const RealTaskIpcRunResult selected_success =
        run_real_7zg_task_ipc_and_wait(
            QFileInfo(archive_path).absolutePath(),
            make_archive_test_payload(
                archive_path,
                QStringList{QStringLiteral("docs/readme.md")}));
    QVERIFY2(selected_success.dispatched,
             qPrintable(selected_success.error_message));
    QVERIFY2(selected_success.completed,
             qPrintable(selected_success.error_message));
    QVERIFY2(selected_success.error_message.trimmed().isEmpty(),
             qPrintable(selected_success.error_message));
    QCOMPARE(selected_success.completion.result_code, 0);
    QCOMPARE(selected_success.completion.payload.command,
             z7::task_ipc_runtime::TaskIpcCommandKind::kTest);
    QVERIFY(selected_success.completion.payload.open.has_value());
    QCOMPARE(selected_success.completion.payload.open->archive_path,
             archive_path);
    QCOMPARE(selected_success.completion.payload.test->archive_inputs,
             QStringList{QStringLiteral("docs/readme.md")});

    const RealTaskIpcRunResult missing_entry =
        run_real_7zg_task_ipc_and_wait(
            QFileInfo(archive_path).absolutePath(),
            make_archive_test_payload(
                archive_path,
                QStringList{QStringLiteral("docs/missing.md")}));
    QVERIFY2(missing_entry.dispatched,
             qPrintable(missing_entry.error_message));
    QVERIFY2(missing_entry.completed,
             qPrintable(missing_entry.error_message));
    QVERIFY2(missing_entry.error_message.trimmed().isEmpty(),
             qPrintable(missing_entry.error_message));
    QCOMPARE(missing_entry.completion.result_code, 0);
    QCOMPARE(missing_entry.completion.payload.command,
             z7::task_ipc_runtime::TaskIpcCommandKind::kTest);
    QVERIFY(missing_entry.completion.payload.open.has_value());
    QCOMPARE(missing_entry.completion.payload.open->archive_path,
             archive_path);
    QCOMPARE(missing_entry.completion.payload.test->archive_inputs,
             QStringList{QStringLiteral("docs/missing.md")});
  }

  void real7zGTaskIpcHashPublishesCompletion() {
    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
        QByteArrayLiteral("1"));

    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QByteArray input_contents = QByteArrayLiteral("hash-body");
    const QString input_file =
        QDir(root.path()).filePath(QStringLiteral("hash-input.txt"));
    QVERIFY(write_file(input_file, input_contents));

    const QString hash_method = QStringLiteral("SHA256");
    const QString expected_file_sha256 = sha256_hex(input_contents);
    const RealTaskIpcRunResult run_result =
        run_real_7zg_task_ipc_and_wait(
            root.path(),
            make_hash_payload(QStringList{input_file}, hash_method));

    QVERIFY2(run_result.dispatched, qPrintable(run_result.error_message));
    QVERIFY2(run_result.completed, qPrintable(run_result.error_message));
    QVERIFY2(run_result.error_message.trimmed().isEmpty(),
             qPrintable(run_result.error_message));
    QVERIFY2(run_result.completion.result_code == 0,
             qPrintable(QStringLiteral("result_code=%1 summary=%2")
                            .arg(run_result.completion.result_code)
                            .arg(run_result.completion.summary)));
    QCOMPARE(run_result.completion.payload.command,
             z7::task_ipc_runtime::TaskIpcCommandKind::kHash);
    QVERIFY(run_result.completion.payload.hash.has_value());
    QCOMPARE(run_result.completion.payload.hash->hash_method, hash_method);
    QCOMPARE(run_result.completion.payload.hash->input_paths,
             QStringList{input_file});
    QVERIFY2(run_result.completion.summary.contains(
                 expected_file_sha256, Qt::CaseInsensitive),
             qPrintable(run_result.completion.summary));
    QVERIFY2(run_result.completion.summary.contains(
                 QStringLiteral("hash-input.txt")),
             qPrintable(run_result.completion.summary));

    const QByteArray archive_entry_contents =
        QByteArrayLiteral("archive-hash-body");
    QString archive_path;
    QString archive_error;
    QVERIFY2(create_archive_with_entries(
                 root,
                 QStringLiteral("hash-archive.7z"),
                 {{QStringLiteral("docs/hash-entry.txt"),
                   archive_entry_contents}},
                 &archive_path,
                 &archive_error),
             qPrintable(archive_error));

    const QString expected_archive_sha256 = sha256_hex(archive_entry_contents);
    const RealTaskIpcRunResult archive_run_result =
        run_real_7zg_task_ipc_and_wait(
            QFileInfo(archive_path).absolutePath(),
            make_archive_hash_payload(
                archive_path,
                QStringList{QStringLiteral("docs/hash-entry.txt")},
                hash_method));
    QVERIFY2(archive_run_result.dispatched,
             qPrintable(archive_run_result.error_message));
    QVERIFY2(archive_run_result.completed,
             qPrintable(archive_run_result.error_message));
    QVERIFY2(archive_run_result.error_message.trimmed().isEmpty(),
             qPrintable(archive_run_result.error_message));
    QVERIFY2(archive_run_result.completion.result_code == 0,
             qPrintable(QStringLiteral("result_code=%1 summary=%2")
                            .arg(archive_run_result.completion.result_code)
                            .arg(archive_run_result.completion.summary)));
    QCOMPARE(archive_run_result.completion.payload.command,
             z7::task_ipc_runtime::TaskIpcCommandKind::kHash);
    QVERIFY(archive_run_result.completion.payload.open.has_value());
    QCOMPARE(archive_run_result.completion.payload.open->archive_path,
             archive_path);
    QVERIFY(archive_run_result.completion.payload.hash.has_value());
    QCOMPARE(archive_run_result.completion.payload.hash->hash_method,
             hash_method);
    QCOMPARE(archive_run_result.completion.payload.hash->input_paths,
             QStringList{QStringLiteral("docs/hash-entry.txt")});
    QVERIFY2(archive_run_result.completion.summary.contains(
                 expected_archive_sha256, Qt::CaseInsensitive),
             qPrintable(archive_run_result.completion.summary));
    QVERIFY2(archive_run_result.completion.summary.contains(
                 QStringLiteral("docs/hash-entry.txt")),
             qPrintable(archive_run_result.completion.summary));
  }

  void hashResultDialogSuppressionForTestsAllowsAsyncCompletion() {
    const ScopedEnvironmentVariable suppress_dialogs(
        "Z7_SUPPRESS_GUI_RESULT_DIALOGS_FOR_TESTS",
        QByteArrayLiteral("1"));

    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString input_file =
        QDir(root.path()).filePath(QStringLiteral("hash-suppressed.txt"));
    QVERIFY(write_file(input_file, QByteArrayLiteral("hash-suppressed-body")));

    z7::ui::gui::HashTaskSpec hash_spec;
    hash_spec.hash_method = std::string("SHA256");
    hash_spec.input_paths.push_back(to_native_path_string(input_file));

    const DialogLifecycleRunResult result =
        run_test_result_lifecycle_case(z7::ui::gui::GuiTaskSpec{hash_spec});
    QVERIFY2(result.finished,
             qPrintable(result.dialog_history.join(QStringLiteral(" | "))));
    QCOMPARE(result.completion.exit_code, 0);
    QTRY_VERIFY_WITH_TIMEOUT(!has_visible_task_progress_dialog(), 5000);
    const QString visible_dialogs = visible_dialog_snapshot();
    QVERIFY2(visible_dialogs.isEmpty(), qPrintable(visible_dialogs));
  }

  void taskIpcEventsAreOwnerScopedAndAcked() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString archive_path;
    QString archive_error;
    QVERIFY2(create_path_remap_archive(root, &archive_path, &archive_error),
             qPrintable(archive_error));

    const QString owner_instance_id =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString other_owner_instance_id =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString output_dir =
        QDir(root.path()).filePath(QStringLiteral("lifecycle-out"));
    const QString remap_destination =
        QDir(output_dir).filePath(QStringLiteral("docs-export"));
    QVERIFY(QDir().mkpath(output_dir));

    z7::task_ipc_runtime::TaskIpcDispatchResult dispatch;
    QString dispatch_error;
    const bool dispatched = z7::task_ipc_runtime::dispatch_task_ipc_task(
        real_7zg_program(),
        QFileInfo(archive_path).absolutePath(),
        owner_instance_id,
        make_extract_remap_payload(archive_path, output_dir, remap_destination),
        &dispatch,
        &dispatch_error);
    QVERIFY2(dispatched, qPrintable(dispatch_error));

    QVector<z7::task_ipc_runtime::TaskIpcEvent> wrong_owner_events;
    QString collect_error;
    QVERIFY2(collect_task_ipc_events_for_test(other_owner_instance_id,
                                              &wrong_owner_events,
                                              &collect_error),
             qPrintable(collect_error));
    QVERIFY(wrong_owner_events.isEmpty());

    QVector<z7::task_ipc_runtime::TaskIpcEvent> first_events;
    QVERIFY2(collect_task_ipc_events_for_test(owner_instance_id,
                                              &first_events,
                                              &collect_error),
             qPrintable(collect_error));
    QVERIFY(contains_task_ipc_event_kind(
        first_events,
        z7::task_ipc_runtime::TaskIpcEventKind::kDispatched));

    z7::task_ipc_runtime::TaskIpcEvent completion;
    const bool saw_completion = QTest::qWaitFor(
        [&]() {
          QVector<z7::task_ipc_runtime::TaskIpcEvent> events;
          collect_error.clear();
          if (!collect_task_ipc_events_for_test(owner_instance_id,
                                                &events,
                                                &collect_error)) {
            return false;
          }
          for (const z7::task_ipc_runtime::TaskIpcEvent& event : events) {
            if (event.session_id == dispatch.session_id &&
                event.generation == dispatch.generation &&
                event.event_kind ==
                    z7::task_ipc_runtime::TaskIpcEventKind::kCompleted) {
              completion = event;
              return true;
            }
          }
          return false;
        },
        30000);
    QVERIFY2(saw_completion, qPrintable(collect_error));
    QCOMPARE(completion.result_code, 0);

    QString ack_error;
    QVERIFY2(z7::task_ipc_runtime::acknowledge_task_ipc_event(completion,
                                                              &ack_error),
             qPrintable(ack_error));
    QTRY_VERIFY_WITH_TIMEOUT(
        task_ipc_events_empty_for_owner(owner_instance_id, &collect_error),
        5000);
    QVERIFY2(collect_error.trimmed().isEmpty(), qPrintable(collect_error));
    QVERIFY(QFileInfo::exists(
        QDir(remap_destination).filePath(QStringLiteral("readme.md"))));
  }

  void testResultDialogKeepsProgressDialogVisibleUntilOk() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString archive_path;
    QVERIFY2(create_sample_archive(root, &archive_path),
             "failed to prepare sample archive");

    z7::app::TestRequest backend_test_request;
    backend_test_request.archive_paths.push_back(to_native_path_string(archive_path));
    z7::app::ArchiveRequest backend_archive_request;
    backend_archive_request.payload = backend_test_request;
    const z7::app::OperationOutcome backend_test_outcome =
        run_archive_request_and_await(backend_archive_request);
    QVERIFY2(backend_test_outcome.ok,
             backend_test_outcome.summary.c_str());

    z7::ui::gui::TestTaskSpec test_spec;
    test_spec.archive_inputs.push_back(to_native_path_string(archive_path));
    const DialogLifecycleRunResult direct_result =
        run_test_result_lifecycle_case(z7::ui::gui::GuiTaskSpec{test_spec});
    QVERIFY2(direct_result.finished,
             qPrintable(direct_result.dialog_history.join(QStringLiteral(" | "))));
    QCOMPARE(direct_result.completion.exit_code, 0);
    if (direct_result.saw_progress_dialog) {
      QVERIFY(direct_result.saw_progress_behind_summary);
    }
    QVERIFY(direct_result.summary_text.contains(
        localized_label(3907) + QStringLiteral(":")));
    QVERIFY(direct_result.summary_text.contains(
        localized_label(1007) + QStringLiteral(":")));
    QVERIFY(direct_result.summary_text.contains(localized_label(3001),
                                                Qt::CaseInsensitive));
    QTRY_VERIFY_WITH_TIMEOUT(!has_visible_task_progress_dialog(), 5000);

    z7::ui::gui::ArchiveTestTaskSpec archive_test_spec;
    archive_test_spec.archive_path = to_native_path_string(archive_path);
    archive_test_spec.archive_entry_paths.push_back(std::string("sample.txt"));
    const DialogLifecycleRunResult archive_result =
        run_test_result_lifecycle_case(
            z7::ui::gui::GuiTaskSpec{archive_test_spec});
    QVERIFY2(archive_result.finished,
             qPrintable(archive_result.dialog_history.join(QStringLiteral(" | "))));
    QCOMPARE(archive_result.completion.exit_code, 0);
    if (archive_result.saw_progress_dialog) {
      QVERIFY(archive_result.saw_progress_behind_summary);
    }
    QVERIFY(archive_result.summary_text.contains(
        localized_label(1007) + QStringLiteral(":")));
    QVERIFY(archive_result.summary_text.contains(localized_label(3001),
                                                 Qt::CaseInsensitive));
    QTRY_VERIFY_WITH_TIMEOUT(!has_visible_task_progress_dialog(), 5000);
  }

  void testFailureKeepsProgressDialogWithErrorsUntilClose() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QString archive_path;
    QVERIFY2(create_sample_archive(root, &archive_path),
             "failed to prepare sample archive");
    const QString plain_path =
        QDir(root.path()).filePath(QStringLiteral("plain.txt"));
    QVERIFY(write_file(plain_path, QByteArrayLiteral("not an archive\n")));

    z7::ui::gui::TestTaskSpec test_spec;
    test_spec.archive_inputs.push_back(to_native_path_string(plain_path));
    test_spec.archive_inputs.push_back(to_native_path_string(archive_path));

    const FailureProgressRunResult result =
        run_failure_progress_lifecycle_case(
            z7::ui::gui::GuiTaskSpec{test_spec},
            QStringLiteral("Testing"));
    QVERIFY(result.opened);
    QVERIFY(!result.finished_before_close);
    QVERIFY(!result.saw_test_result_dialog);
    QVERIFY(!result.saw_message_box);
    QVERIFY(result.title.contains(QStringLiteral("100%")));
    QVERIFY(result.title.contains(localized_label(3302), Qt::CaseInsensitive));
    QVERIFY2(result.log_text.contains(QStringLiteral("plain.txt")),
             qPrintable(result.log_text));
    QVERIFY2(result.log_text.trimmed().split(QLatin1Char('\n')).size() >= 2,
             qPrintable(result.log_text));
    QVERIFY(result.finished_after_close);
    QVERIFY(result.completion.exit_code != 0);
    QTRY_VERIFY_WITH_TIMEOUT(!has_visible_task_progress_dialog(), 5000);
    QVERIFY(!has_visible_message_box());
  }

  void testFailureMessagesUseCurrentLanguage() {
    ScopedCatalogLanguage language(QStringLiteral("zh-cn"));
    QVERIFY(language.changed());

    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString plain_path =
        QDir(root.path()).filePath(QStringLiteral("plain.txt"));
    QVERIFY(write_file(plain_path, QByteArrayLiteral("not an archive\n")));

    z7::ui::gui::TestTaskSpec test_spec;
    test_spec.archive_inputs.push_back(to_native_path_string(plain_path));

    const FailureProgressRunResult result =
        run_failure_progress_lifecycle_case(
            z7::ui::gui::GuiTaskSpec{test_spec},
            QStringLiteral("Testing"));
    QVERIFY(result.opened);
    QVERIFY2(result.log_text.contains(QStringLiteral("plain.txt")),
             qPrintable(result.log_text));
    QVERIFY2(result.log_text.contains(
                 z7::ui::runtime_support::LF(3005, {QString()})),
             qPrintable(result.log_text));
    QVERIFY(result.finished_after_close);
    QVERIFY(result.completion.exit_code != 0);
    QTRY_VERIFY_WITH_TIMEOUT(!has_visible_task_progress_dialog(), 5000);
    QVERIFY(!has_visible_message_box());
  }

  void directoryTestFailureUsesOriginalStyleMessageList() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString input_dir = QDir(root.path()).filePath(QStringLiteral("input"));
    QVERIFY(QDir().mkpath(QDir(input_dir).filePath(QStringLiteral("docs/guides"))));
    QVERIFY(write_file(QDir(input_dir).filePath(QStringLiteral(".DS_Store")),
                       QByteArrayLiteral("finder metadata")));
    QVERIFY(write_file(QDir(input_dir).filePath(QStringLiteral("media.m4s")),
                       QByteArrayLiteral("media")));
    QVERIFY(write_file(QDir(input_dir).filePath(QStringLiteral("docs/readme.md")),
                       QByteArrayLiteral("readme")));
    QVERIFY(write_file(QDir(input_dir).filePath(QStringLiteral("docs/guides/install.txt")),
                       QByteArrayLiteral("install")));

    const QString payload_path =
        QDir(root.path()).filePath(QStringLiteral("payload.txt"));
    QVERIFY(write_file(payload_path, QByteArrayLiteral("payload")));

    const QString archive_path = QDir(input_dir).filePath(QStringLiteral("valid.7z"));
    z7::app::AddRequest add_request;
    add_request.archive_path = to_native_path_string(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_native_path_string(payload_path)};
    z7::app::ArchiveRequest archive_request;
    archive_request.payload = add_request;
    const z7::app::OperationOutcome add_outcome =
        run_archive_request_and_await(archive_request);
    QVERIFY2(add_outcome.ok, add_outcome.summary.c_str());

    z7::ui::gui::TestTaskSpec test_spec;
    test_spec.archive_inputs.push_back(to_native_path_string(input_dir));

    const FailureProgressRunResult result =
        run_failure_progress_lifecycle_case(
            z7::ui::gui::GuiTaskSpec{test_spec},
            QStringLiteral("Testing"));
    QVERIFY(result.opened);
    QVERIFY(!result.finished_before_close);
    QVERIFY(!result.saw_test_result_dialog);
    QVERIFY(!result.saw_message_box);
    QCOMPARE(result.progress_minimum, 0);
    QCOMPARE(result.progress_maximum, 100);
    QCOMPARE(result.progress_value, 100);
    QVERIFY(result.title.contains(QStringLiteral("100%")));
    QVERIFY(result.title.contains(localized_label(3302), Qt::CaseInsensitive));

    QVERIFY2(result.log_text.contains(QStringLiteral(".DS_Store")),
             qPrintable(result.log_text));
    QVERIFY2(result.log_text.contains(QStringLiteral("media.m4s")),
             qPrintable(result.log_text));
    QVERIFY2(result.log_text.contains(QStringLiteral("docs/readme.md")),
             qPrintable(result.log_text));
    QVERIFY2(result.log_text.contains(QStringLiteral("docs/guides/install.txt")),
             qPrintable(result.log_text));
    QVERIFY(!result.log_text.contains(QStringLiteral("Archives:")));
    QVERIFY(!result.log_text.contains(QStringLiteral("Physical Size")));

    QStringList numbered_rows;
    int continuation_rows = 0;
    for (const QString& number : result.message_numbers) {
      if (number.trimmed().isEmpty()) {
        ++continuation_rows;
      } else {
        numbered_rows << number;
      }
    }
    QCOMPARE(numbered_rows, QStringList({QStringLiteral("1"),
                                         QStringLiteral("2"),
                                         QStringLiteral("3"),
                                         QStringLiteral("4")}));
    QVERIFY(continuation_rows >= 4);
    QVERIFY(result.finished_after_close);
    QVERIFY(result.completion.exit_code != 0);
    QTRY_VERIFY_WITH_TIMEOUT(!has_visible_task_progress_dialog(), 5000);
    QVERIFY(!has_visible_message_box());
  }

  void addFailureKeepsProgressDialogWithErrorsUntilClose() {
    QTemporaryDir root;
    QVERIFY(root.isValid());

    z7::ui::gui::AddTaskSpec add_spec;
    add_spec.archive_path = to_native_path_string(
        QDir(root.path()).filePath(QStringLiteral("out.7z")));
    add_spec.archive_type = "7z";
    add_spec.input_paths.push_back(to_native_path_string(
        QDir(root.path()).filePath(QStringLiteral("missing-input.txt"))));

    const FailureProgressRunResult result =
        run_failure_progress_lifecycle_case(
            z7::ui::gui::GuiTaskSpec{add_spec},
            QStringLiteral("Adding"));
    QVERIFY(result.opened);
    QVERIFY(!result.finished_before_close);
    QVERIFY(!result.saw_test_result_dialog);
    QVERIFY(!result.saw_message_box);
    QVERIFY(result.title.contains(QStringLiteral("100%")));
    QVERIFY2(!result.log_text.trimmed().isEmpty(),
             qPrintable(result.title));
    QVERIFY(result.finished_after_close);
    QVERIFY(result.completion.exit_code != 0);
    QTRY_VERIFY_WITH_TIMEOUT(!has_visible_task_progress_dialog(), 5000);
    QVERIFY(!has_visible_message_box());
  }
};

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QTemporaryDir settings_dir;
  if (settings_dir.isValid()) {
    z7::platform::qt::set_portable_settings_root(settings_dir.path());
  }
  Q_INIT_RESOURCE(generated_filemanager_resources);

  const z7::platform::qt::AppStartupConfig startup =
      z7::platform::qt::default_startup_config(
          z7::platform::qt::StartupAppKind::kFileManager);
  z7::platform::qt::apply_pre_app_startup(startup);
  QApplication app(argc, argv);
  z7::platform::qt::apply_post_app_startup(app, startup);
  clear_all_smoke_settings();

  HiDpiSmokeTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "smoke.moc"
