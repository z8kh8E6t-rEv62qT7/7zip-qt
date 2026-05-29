// tests/filemanager/behavior/setup_helpers_archive.cpp
// Role: Archive/test fixture helper functions.

#include "internal.h"

namespace filemanager_behavior_internal {

std::string to_native_path_string(const QString& path) {
  return QFile::encodeName(path).toStdString();
}

class ScopedCurrentDirectory {
 public:
  explicit ScopedCurrentDirectory(const QString& path)
      : previous_(QDir::currentPath()),
        changed_(QDir::setCurrent(path)) {}

  ~ScopedCurrentDirectory() {
    if (changed_) {
      QDir::setCurrent(previous_);
    }
  }

  bool changed() const { return changed_; }

 private:
  QString previous_;
  bool changed_ = false;
};

namespace {

bool create_archive_via_backend_with_options(const QString& working_dir,
                                             const QString& archive_path,
                                             const QStringList& inputs,
                                             const QString& format,
                                             const QString& compression_level,
                                             QString* error) {
  if (error != nullptr) {
    error->clear();
  }

  ScopedCurrentDirectory cwd_scope(working_dir);
  if (!cwd_scope.changed()) {
    if (error != nullptr) {
      *error = QStringLiteral("failed to switch cwd to working dir");
    }
    return false;
  }

  z7::app::AddRequest request;
  request.archive_path = to_native_path_string(archive_path);
  request.format = format.trimmed().isEmpty() ? std::string("7z")
                                              : to_native_path_string(format.trimmed());
  if (!compression_level.trimmed().isEmpty()) {
    request.compression_level = to_native_path_string(compression_level.trimmed());
  }
  for (const QString& input : inputs) {
    request.input_paths.push_back(to_native_path_string(input));
  }

  z7::app::ArchiveRequest archive_request;
  archive_request.payload = request;
  const z7::app::OperationOutcome outcome =
      run_archive_request_and_await(archive_request);
  z7::app::AddResult result;
  if (const std::optional<z7::app::AddResult> typed =
          z7::app::outcome_payload_as<z7::app::AddResult>(outcome);
      typed.has_value()) {
    result = *typed;
  } else {
    result.ok = outcome.ok;
    result.error = outcome.error;
    result.native_exit_code = outcome.native_code;
    result.native_execution = outcome.native_execution;
    result.summary = outcome.summary;
  }
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

}  // namespace

bool create_archive_via_backend(const QString& working_dir,
                                const QString& archive_path,
                                const QStringList& inputs,
                                QString* error) {
  return create_archive_via_backend_with_options(
      working_dir,
      archive_path,
      inputs,
      QStringLiteral("7z"),
      QString(),
      error);
}

QString create_sample_archive(const QTemporaryDir& root) {
  const QString sample_file = QDir(root.path()).filePath(QStringLiteral("sample.txt"));
  QFile file(sample_file);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return QString();
  }
  QTextStream out(&file);
  out << "7zfm behavior test\n";
  file.close();

  const QString archive = QDir(root.path()).filePath(QStringLiteral("sample.7z"));
  QString error;
  if (!create_archive_via_backend(root.path(),
                                  archive,
                                  QStringList{QStringLiteral("sample.txt")},
                                  &error)) {
    qWarning() << "create_sample_archive failed:" << error;
    return QString();
  }
  return archive;
}

QString create_nested_archive(const QTemporaryDir& root) {
  const QString top_dir = QDir(root.path()).filePath(QStringLiteral("top"));
  const QString inner_dir = QDir(top_dir).filePath(QStringLiteral("inner"));
  if (!QDir().mkpath(inner_dir)) {
    return QString();
  }

  const QString nested_file = QDir(inner_dir).filePath(QStringLiteral("leaf.txt"));
  QFile file(nested_file);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return QString();
  }
  QTextStream out(&file);
  out << "nested archive entry\n";
  file.close();

  const QString archive = QDir(root.path()).filePath(QStringLiteral("nested.7z"));
  QString error;
  if (!create_archive_via_backend(root.path(),
                                  archive,
                                  QStringList{QStringLiteral("top")},
                                  &error)) {
    qWarning() << "create_nested_archive failed:" << error;
    return QString();
  }
  return archive;
}

QString create_archive_with_embedded_archive(const QTemporaryDir& root) {
  const QString child_txt = QDir(root.path()).filePath(QStringLiteral("child.txt"));
  {
    QFile file(child_txt);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return QString();
    }
    QTextStream out(&file);
    out << "embedded archive payload\n";
    file.close();
  }

  const QString child_archive = QDir(root.path()).filePath(QStringLiteral("child.7z"));
  {
    QString error;
    if (!create_archive_via_backend(root.path(),
                                    child_archive,
                                    QStringList{QStringLiteral("child.txt")},
                                    &error)) {
      qWarning() << "create_archive_with_embedded_archive child failed:" << error;
      return QString();
    }
  }

  const QString top_note = QDir(root.path()).filePath(QStringLiteral("note.txt"));
  {
    QFile file(top_note);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return QString();
    }
    QTextStream out(&file);
    out << "outer archive note\n";
    file.close();
  }

  const QString outer_archive = QDir(root.path()).filePath(QStringLiteral("outer.7z"));
  QString error;
  if (!create_archive_via_backend(root.path(),
                                  outer_archive,
                                  QStringList{
                                      QStringLiteral("child.7z"),
                                      QStringLiteral("note.txt")},
                                  &error)) {
    qWarning() << "create_archive_with_embedded_archive outer failed:" << error;
    return QString();
  }

  return outer_archive;
}

QString create_archive_with_embedded_archive_in_folder(const QTemporaryDir& root) {
  const QString child_txt = QDir(root.path()).filePath(QStringLiteral("child2.txt"));
  {
    QFile file(child_txt);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return QString();
    }
    QTextStream out(&file);
    out << "embedded in folder\n";
    file.close();
  }

  const QString child_archive = QDir(root.path()).filePath(QStringLiteral("child2.7z"));
  {
    QString error;
    if (!create_archive_via_backend(root.path(),
                                    child_archive,
                                    QStringList{QStringLiteral("child2.txt")},
                                    &error)) {
      qWarning() << "create_archive_with_embedded_archive_in_folder child failed:" << error;
      return QString();
    }
  }

  const QString pack_dir = QDir(root.path()).filePath(QStringLiteral("pack"));
  if (!QDir().mkpath(pack_dir)) {
    return QString();
  }
  const QString packed_child = QDir(pack_dir).filePath(QStringLiteral("child2.7z"));
  if (!QFile::copy(child_archive, packed_child)) {
    return QString();
  }

  const QString outer_archive = QDir(root.path()).filePath(QStringLiteral("outer2.7z"));
  QString error;
  if (!create_archive_via_backend(root.path(),
                                  outer_archive,
                                  QStringList{QStringLiteral("pack")},
                                  &error)) {
    qWarning() << "create_archive_with_embedded_archive_in_folder outer failed:" << error;
    return QString();
  }
  return outer_archive;
}

QString create_archive_with_same_name_embedded_archive(const QTemporaryDir& root) {
  const QString shared_base = QStringLiteral("apps.apple.com-main");
  const QString leaf_name = QStringLiteral("same-name-leaf.txt");
  const QString leaf_path = QDir(root.path()).filePath(leaf_name);
  {
    QFile file(leaf_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return QString();
    }
    QTextStream out(&file);
    out << "same-name embedded archive payload\n";
    file.close();
  }

  const QString child_source_archive =
      QDir(root.path()).filePath(QStringLiteral("child-source.7z"));
  {
    QString error;
    if (!create_archive_via_backend(root.path(),
                                    child_source_archive,
                                    QStringList{leaf_name},
                                    &error)) {
      qWarning() << "create_archive_with_same_name_embedded_archive child failed:" << error;
      return QString();
    }
  }

  const QString container_dir = QDir(root.path()).filePath(shared_base);
  if (!QDir().mkpath(container_dir)) {
    return QString();
  }
  const QString packed_child =
      QDir(container_dir).filePath(QStringLiteral("%1.7z").arg(shared_base));
  if (!QFile::copy(child_source_archive, packed_child)) {
    return QString();
  }

  const QString outer_archive =
      QDir(root.path()).filePath(QStringLiteral("%1.7z").arg(shared_base));
  QString error;
  if (!create_archive_via_backend(root.path(),
                                  outer_archive,
                                  QStringList{shared_base},
                                  &error)) {
    qWarning() << "create_archive_with_same_name_embedded_archive outer failed:" << error;
    return QString();
  }
  return outer_archive;
}

QString create_archive_with_embedded_zip_archive(const QTemporaryDir& root,
                                                bool store_outer_archive) {
  const QString child_txt = QDir(root.path()).filePath(QStringLiteral("zip-child.txt"));
  {
    QFile file(child_txt);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return QString();
    }
    QTextStream out(&file);
    out << "embedded zip payload\n";
    file.close();
  }

  const QString child_archive = QDir(root.path()).filePath(QStringLiteral("child.zip"));
  {
    QString error;
    if (!create_archive_via_backend_with_options(root.path(),
                                                 child_archive,
                                                 QStringList{QStringLiteral("zip-child.txt")},
                                                 QStringLiteral("zip"),
                                                 QString(),
                                                 &error)) {
      qWarning() << "create_archive_with_embedded_zip_archive child failed:" << error;
      return QString();
    }
  }

  const QString outer_note = QDir(root.path()).filePath(QStringLiteral("outer-zip-note.txt"));
  {
    QFile file(outer_note);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return QString();
    }
    QTextStream out(&file);
    out << "outer zip archive note\n";
    file.close();
  }

  const QString outer_archive = QDir(root.path()).filePath(QStringLiteral("outer.zip"));
  QString error;
  if (!create_archive_via_backend_with_options(root.path(),
                                               outer_archive,
                                               QStringList{QStringLiteral("child.zip"),
                                                           QStringLiteral("outer-zip-note.txt")},
                                               QStringLiteral("zip"),
                                               store_outer_archive ? QStringLiteral("0") : QString(),
                                               &error)) {
    qWarning() << "create_archive_with_embedded_zip_archive outer failed:" << error;
    return QString();
  }
  return outer_archive;
}

int row_by_name(const z7::ui::filemanager::MainWindow& window,
                const QString& name) {
  const QAbstractItemModel* model = window.active_panel_controller().ui.details_view != nullptr
                                        ? window.active_panel_controller().ui.details_view->model()
                                        : nullptr;
  if (model == nullptr) {
    return -1;
  }

  for (int row = 0; row < model->rowCount(); ++row) {
    const QModelIndex idx = model->index(row, 0);
    if (model->data(idx, Qt::DisplayRole).toString() == name) {
      return row;
    }
  }
  return -1;
}

QStringList first_column_items(const z7::ui::filemanager::MainWindow& window) {
  QStringList names;
  const QAbstractItemModel* model = window.active_panel_controller().ui.details_view != nullptr
                                        ? window.active_panel_controller().ui.details_view->model()
                                        : nullptr;
  if (model == nullptr) {
    return names;
  }
  for (int row = 0; row < model->rowCount(); ++row) {
    names << model->data(model->index(row, 0), Qt::DisplayRole).toString();
  }
  return names;
}

QIcon decoration_icon_for_name(const z7::ui::filemanager::MainWindow& window,
                               const QString& name) {
  const QAbstractItemModel* model = window.active_panel_controller().ui.details_view != nullptr
                                        ? window.active_panel_controller().ui.details_view->model()
                                        : nullptr;
  if (model == nullptr) {
    return QIcon();
  }

  const int row = row_by_name(window, name);
  if (row < 0) {
    return QIcon();
  }
  return qvariant_cast<QIcon>(
      model->data(model->index(row, 0), Qt::DecorationRole));
}

bool icon_has_pixels(const QIcon& icon, int extent) {
  if (icon.isNull()) {
    return false;
  }
  return !icon.pixmap(extent, extent).isNull();
}

bool icon_matches_resource(const QIcon& actual,
                           const QString& expected_resource_path,
                           int extent) {
  if (!icon_has_pixels(actual, extent)) {
    return false;
  }
  const QIcon expected(expected_resource_path);
  if (!icon_has_pixels(expected, extent)) {
    return false;
  }
  const QImage actual_image = actual.pixmap(extent, extent).toImage().convertToFormat(
      QImage::Format_ARGB32_Premultiplied);
  const QImage expected_image = expected.pixmap(extent, extent).toImage().convertToFormat(
      QImage::Format_ARGB32_Premultiplied);
  return actual_image == expected_image;
}

void select_rows_in_active_panel(z7::ui::filemanager::MainWindow* window,
                                 const QList<int>& rows) {
  QVERIFY(window != nullptr);
  QVERIFY(window->active_panel_controller().ui.details_view != nullptr);
  QVERIFY(window->active_panel_controller().ui.details_view->selectionModel() != nullptr);
  QAbstractItemModel* model =
      window->active_panel_controller().ui.details_view->model();
  QVERIFY(model != nullptr);
  QItemSelectionModel* selection = window->active_panel_controller().ui.details_view->selectionModel();
  selection->clearSelection();
  if (rows.isEmpty()) {
    selection->clearCurrentIndex();
    return;
  }
  QModelIndex last_index;
  for (const int row : rows) {
    const QModelIndex idx = model->index(row, 0);
    QVERIFY(idx.isValid());
    selection->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    last_index = idx;
  }
  if (last_index.isValid()) {
    selection->setCurrentIndex(last_index, QItemSelectionModel::NoUpdate);
  }
}


}  // namespace filemanager_behavior_internal

// End of setup_helpers_archive.cpp
