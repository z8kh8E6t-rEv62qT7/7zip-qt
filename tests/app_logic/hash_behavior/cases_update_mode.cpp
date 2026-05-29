// tests/app_logic/hash_behavior/cases_update_mode.cpp
// Role: Update action-set semantics and raw -u switch behavior cases.

#include "internal.h"

using namespace hash_behavior_internal;

namespace {

void write_text_file(const QString& path, const QByteArray& contents) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(file.write(contents), contents.size());
}

QByteArray read_text_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  return file.readAll();
}

void set_modified_time(const QString& path, const QDateTime& modified_at) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadWrite));
  QVERIFY(file.setFileTime(modified_at, QFileDevice::FileModificationTime));
}

bool extract_archive_to_dir(
    const QString& archive_path,
    const QString& output_dir,
    z7::app::ExtractPathMode path_mode = z7::app::ExtractPathMode::kFullPaths) {
  if (!QDir().mkpath(output_dir)) {
    return false;
  }
  z7::app::ExtractRequest extract_request;
  extract_request.archive_path = to_std_path(archive_path);
  extract_request.output_dir = to_std_path(output_dir);
  extract_request.overwrite_mode = z7::app::OverwriteMode::kOverwrite;
  extract_request.path_mode = path_mode;
  return run_request_sync(extract_request).ok;
}

QByteArray extract_file_text(const QString& root,
                             const QString& archive_path,
                             const QString& out_dir_name,
                             const QString& file_name) {
  const QString output_dir = QDir(root).filePath(out_dir_name);
  if (!extract_archive_to_dir(archive_path, output_dir)) {
    return QByteArray();
  }
  return read_text_file(QDir(output_dir).filePath(file_name));
}

QStringList recursive_archive_file_paths(const QString& archive_path, bool* ok) {
  if (ok != nullptr) {
    *ok = false;
  }

  z7::app::ListRequest list_request;
  list_request.archive_path = to_std_path(archive_path);
  list_request.recursive_dirs = true;
  const z7::app::ListResult list_result = run_request_sync(list_request);
  if (!list_result.ok) {
    return {};
  }

  QStringList paths;
  for (const z7::app::ArchiveListEntry& entry : list_result.entries) {
    if (!entry.is_dir) {
      paths.push_back(QString::fromStdString(entry.path));
    }
  }
  paths.sort();
  if (ok != nullptr) {
    *ok = true;
  }
  return paths;
}

QString normalized_absolute_archive_path(const QString& path) {
  QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
  while (normalized.startsWith(QLatin1Char('/'))) {
    normalized.remove(0, 1);
  }
  while (normalized.endsWith(QLatin1Char('/'))) {
    normalized.chop(1);
  }
  return normalized;
}

}  // namespace

void AppLogicHashBehaviorTest::addRequestInputItemsTargetExplicitArchiveEntriesAndMergeOverlaps() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString source_dir = QDir(root.path()).filePath(QStringLiteral("source-tree"));
  const QString source_nested_dir = QDir(source_dir).filePath(QStringLiteral("nested"));
  QVERIFY(QDir().mkpath(source_nested_dir));

  const QString source_child = QDir(source_dir).filePath(QStringLiteral("child.txt"));
  const QString source_leaf = QDir(source_nested_dir).filePath(QStringLiteral("leaf.txt"));
  const QString override_path = QDir(root.path()).filePath(QStringLiteral("override.txt"));
  const QString top_level_path = QDir(root.path()).filePath(QStringLiteral("top-level.txt"));
  write_text_file(source_child, QByteArray("from-directory"));
  write_text_file(source_leaf, QByteArray("nested-from-directory"));
  write_text_file(override_path, QByteArray("from-explicit-item"));
  write_text_file(top_level_path, QByteArray("top-level"));

  const QString archive_path = QDir(root.path()).filePath(QStringLiteral("input-items.7z"));
  z7::app::AddRequest add_request;
  add_request.archive_path = to_std_path(archive_path);
  add_request.format = "7z";
  add_request.path_mode = "absolute";
  add_request.input_items = {
      {to_std_path(source_dir), "pack"},
      {to_std_path(override_path), "pack/child.txt"},
      {to_std_path(top_level_path), "top.txt"},
  };

  const z7::app::AddResult add_result = run_request_sync(add_request);
  QVERIFY(add_result.ok);

  z7::app::ListRequest root_list;
  root_list.archive_path = to_std_path(archive_path);
  const z7::app::ListResult root_result = run_request_sync(root_list);
  QVERIFY(root_result.ok);

  bool has_pack_dir = false;
  bool has_top_file = false;
  bool has_source_tree_name = false;
  for (const z7::app::ArchiveListEntry& entry : root_result.entries) {
    if (entry.path == "pack" && entry.is_dir) {
      has_pack_dir = true;
    }
    if (entry.path == "top.txt" && !entry.is_dir) {
      has_top_file = true;
    }
    if (entry.path == "source-tree") {
      has_source_tree_name = true;
    }
  }
  QVERIFY(has_pack_dir);
  QVERIFY(has_top_file);
  QVERIFY(!has_source_tree_name);

  const QByteArray overridden_child = extract_file_text(
      root.path(),
      archive_path,
      QStringLiteral("out-input-items"),
      QStringLiteral("pack/child.txt"));
  QCOMPARE(overridden_child, QByteArray("from-explicit-item"));

  const QByteArray nested_leaf = extract_file_text(
      root.path(),
      archive_path,
      QStringLiteral("out-input-items"),
      QStringLiteral("pack/nested/leaf.txt"));
  QCOMPARE(nested_leaf, QByteArray("nested-from-directory"));

  const QByteArray top_level = extract_file_text(
      root.path(),
      archive_path,
      QStringLiteral("out-input-items"),
      QStringLiteral("top.txt"));
  QCOMPARE(top_level, QByteArray("top-level"));
}

void AppLogicHashBehaviorTest::addRequestUpdateModeMatrixMatchesOriginalActionSets() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QStringList modes = {
      QStringLiteral("add"),
      QStringLiteral("update"),
      QStringLiteral("fresh"),
      QStringLiteral("sync"),
  };
  const QStringList path_modes = {
      QStringLiteral("relative"),
      QStringLiteral("full"),
      QStringLiteral("absolute"),
  };

  struct ExpectedMode {
    QString mode;
    QByteArray archive_newer_text;
    bool disk_only_present;
    bool archive_only_present;
  };

  const ExpectedMode expectations[] = {
      {QStringLiteral("add"), QByteArray("disk-older"), true, true},
      {QStringLiteral("update"), QByteArray("archive-newer"), true, true},
      {QStringLiteral("fresh"), QByteArray("archive-newer"), false, true},
      {QStringLiteral("sync"), QByteArray("archive-newer"), true, false},
  };

  const QString archive_newer_name = QStringLiteral("archive-newer.txt");
  const QString disk_newer_name = QStringLiteral("disk-newer.txt");
  const QString disk_only_name = QStringLiteral("disk-only.txt");
  const QString archive_only_name = QStringLiteral("archive-only.txt");

  for (const QString& path_mode : path_modes) {
    const QString source_dir =
        QDir(root.path()).filePath(QStringLiteral("source-%1").arg(path_mode));
    QVERIFY(QDir().mkpath(source_dir));

    const QString archive_newer_path =
        QDir(source_dir).filePath(archive_newer_name);
    const QString disk_newer_path = QDir(source_dir).filePath(disk_newer_name);
    const QString disk_only_path = QDir(source_dir).filePath(disk_only_name);
    const QString archive_only_path =
        QDir(source_dir).filePath(archive_only_name);

    write_text_file(archive_newer_path, QByteArray("archive-newer"));
    write_text_file(disk_newer_path, QByteArray("archive-stale"));
    write_text_file(archive_only_path, QByteArray("archive-only"));

    const QDateTime base_time = QDateTime::currentDateTimeUtc().addDays(-10);
    set_modified_time(archive_newer_path, base_time);
    set_modified_time(disk_newer_path, base_time);
    set_modified_time(archive_only_path, base_time);

    QHash<QString, QString> archives;
    for (const QString& mode : modes) {
      const QString archive_path =
          QDir(root.path()).filePath(
              QStringLiteral("%1-%2.7z").arg(path_mode, mode));
      z7::app::AddRequest create_request;
      create_request.archive_path = to_std_path(archive_path);
      create_request.format = "7z";
      create_request.path_mode = path_mode.toStdString();
      create_request.input_paths = {to_std_path(source_dir)};
      const z7::app::AddResult create_result = run_request_sync(create_request);
      QVERIFY2(create_result.ok,
               qPrintable(QStringLiteral("base add failed for path mode %1, "
                                         "update mode %2")
                              .arg(path_mode, mode)));
      archives.insert(mode, archive_path);
    }

    write_text_file(archive_newer_path, QByteArray("disk-older"));
    set_modified_time(archive_newer_path, base_time.addDays(-2));
    write_text_file(disk_newer_path, QByteArray("disk-newer"));
    set_modified_time(disk_newer_path, base_time.addDays(2));
    write_text_file(disk_only_path, QByteArray("disk-only"));
    set_modified_time(disk_only_path, base_time.addDays(2));
    QVERIFY(QFile::remove(archive_only_path));

    for (const QString& mode : modes) {
      z7::app::AddRequest update_request;
      update_request.archive_path = to_std_path(archives.value(mode));
      update_request.format = "7z";
      update_request.update_mode = mode.toStdString();
      update_request.path_mode = path_mode.toStdString();
      update_request.input_paths = {to_std_path(source_dir)};
      const z7::app::AddResult update_result = run_request_sync(update_request);
      QVERIFY2(update_result.ok,
               qPrintable(QStringLiteral("update failed for path mode %1, "
                                         "update mode %2")
                              .arg(path_mode, mode)));
    }

    for (const ExpectedMode& expected : expectations) {
      const QString out_dir = QDir(root.path()).filePath(
          QStringLiteral("out-%1-%2").arg(path_mode, expected.mode));
      QVERIFY2(extract_archive_to_dir(archives.value(expected.mode),
                                      out_dir,
                                      z7::app::ExtractPathMode::kNoPaths),
               qPrintable(QStringLiteral("extract failed for path mode %1, "
                                         "update mode %2")
                              .arg(path_mode, expected.mode)));

      QCOMPARE(read_text_file(QDir(out_dir).filePath(archive_newer_name)),
               expected.archive_newer_text);
      QCOMPARE(read_text_file(QDir(out_dir).filePath(disk_newer_name)),
               QByteArray("disk-newer"));
      QCOMPARE(QFileInfo::exists(QDir(out_dir).filePath(disk_only_name)),
               expected.disk_only_present);
      QCOMPARE(QFileInfo::exists(QDir(out_dir).filePath(archive_only_name)),
               expected.archive_only_present);
    }
  }
}

void AppLogicHashBehaviorTest::addRequestPathModeMatrixMatchesOriginalCensorModes() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString source_dir =
      QDir(root.path()).filePath(QStringLiteral("path-mode-source"));
  const QString nested_dir = QDir(source_dir).filePath(QStringLiteral("nested"));
  QVERIFY(QDir().mkpath(nested_dir));

  const QString file_name = QStringLiteral("payload.txt");
  const QString file_path = QDir(nested_dir).filePath(file_name);
  write_text_file(file_path, QByteArray("payload"));

  struct ExpectedPathMode {
    QString mode;
    bool leaf_only;
    bool listable_from_root;
  };
  const ExpectedPathMode expectations[] = {
      {QStringLiteral("relative"), true, true},
      {QStringLiteral("full"), false, true},
      {QStringLiteral("absolute"), false, false},
  };

  const QString preserved_tail =
      QStringLiteral("path-mode-source/nested/payload.txt");
  const QString absolute_selection = normalized_absolute_archive_path(file_path);
  for (const ExpectedPathMode& expected : expectations) {
    const QString archive_path =
        QDir(root.path()).filePath(QStringLiteral("%1-path.7z").arg(expected.mode));
    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.path_mode = expected.mode.toStdString();
    add_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult add_result = run_request_sync(add_request);
    QVERIFY2(add_result.ok,
             qPrintable(QStringLiteral("add failed for path mode %1")
                            .arg(expected.mode)));

    QString selected_entry = expected.leaf_only ? file_name : absolute_selection;
    if (expected.listable_from_root) {
      bool list_ok = false;
      const QStringList paths = recursive_archive_file_paths(archive_path, &list_ok);
      QVERIFY2(list_ok,
               qPrintable(QStringLiteral("list failed for path mode %1")
                              .arg(expected.mode)));
      QVERIFY2(paths.size() == 1,
               qPrintable(QStringLiteral("path mode %1 listed %2 file entries: %3")
                              .arg(expected.mode)
                              .arg(paths.size())
                              .arg(paths.join(QStringLiteral(", ")))));

      selected_entry = paths.first();
      if (expected.leaf_only) {
        QCOMPARE(selected_entry, file_name);
      } else {
        QVERIFY2(selected_entry.endsWith(preserved_tail),
                 qPrintable(QStringLiteral("path mode %1 stored unexpected path: %2")
                                .arg(expected.mode, selected_entry)));
        QVERIFY(selected_entry != file_name);
      }
    }

    z7::app::TestRequest test_request;
    test_request.archive_path = to_std_path(archive_path);
    test_request.entries = {selected_entry.toStdString()};
    const z7::app::TestResult test_result = run_request_sync(test_request);
    QVERIFY2(test_result.ok,
             qPrintable(QStringLiteral("test failed for path mode %1 entry %2")
                            .arg(expected.mode, selected_entry)));
    QVERIFY(test_result.hash_summary.has_value());
    QCOMPARE(test_result.hash_summary->num_files, uint64_t{1});
    QVERIFY2(QString::fromStdString(test_result.hash_summary->first_file_name)
                 .endsWith(expected.leaf_only ? file_name : preserved_tail),
             qPrintable(QStringLiteral("path mode %1 selected unexpected entry: %2")
                            .arg(expected.mode,
                                 QString::fromStdString(
                                     test_result.hash_summary->first_file_name))));
  }
}

void AppLogicHashBehaviorTest::addRequestRawUpdateSwitchOverridesModeAndSkipsOnlyDiskItems() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString a_name = QStringLiteral("a.txt");
  const QString b_name = QStringLiteral("b.txt");
  const QString a_path = QDir(root.path()).filePath(a_name);
  const QString b_path = QDir(root.path()).filePath(b_name);
  write_text_file(a_path, QByteArray("base-a"));
  write_text_file(b_path, QByteArray("disk-b"));

  const QString archive_update = QDir(root.path()).filePath(QStringLiteral("update.7z"));
  const QString archive_raw = QDir(root.path()).filePath(QStringLiteral("raw.7z"));

  auto create_base_archive = [&](const QString& archive_path) {
    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(a_path)};
    QVERIFY(run_request_sync(add_request).ok);
  };
  create_base_archive(archive_update);
  create_base_archive(archive_raw);

  {
    z7::app::AddRequest update_request;
    update_request.archive_path = to_std_path(archive_update);
    update_request.format = "7z";
    update_request.update_mode = "update";
    update_request.input_paths = {to_std_path(a_path), to_std_path(b_path)};
    const z7::app::AddResult update_result = run_request_sync(update_request);
    QVERIFY(update_result.ok);
  }

  {
    z7::app::AddRequest raw_request;
    raw_request.archive_path = to_std_path(archive_raw);
    raw_request.format = "7z";
    raw_request.update_mode = "update";
    raw_request.raw_update_switch = "r0p1q1x1y2z1w2";
    raw_request.input_paths = {to_std_path(a_path), to_std_path(b_path)};
    const z7::app::AddResult raw_result = run_request_sync(raw_request);
    QVERIFY(raw_result.ok);
  }

  const QString out_update = QDir(root.path()).filePath(QStringLiteral("out-update"));
  const QString out_raw = QDir(root.path()).filePath(QStringLiteral("out-raw"));

  const QByteArray update_a = extract_file_text(
      root.path(), archive_update, QStringLiteral("out-update"), a_name);
  const QByteArray update_b = extract_file_text(
      root.path(), archive_update, QStringLiteral("out-update"), b_name);
  QVERIFY(!update_a.isEmpty());
  QCOMPARE(update_b, QByteArray("disk-b"));

  const QByteArray raw_a = extract_file_text(
      root.path(), archive_raw, QStringLiteral("out-raw"), a_name);
  const QByteArray raw_b = extract_file_text(
      root.path(), archive_raw, QStringLiteral("out-raw"), b_name);
  QVERIFY(!raw_a.isEmpty());
  QVERIFY(raw_b.isEmpty());
  QVERIFY(QFileInfo::exists(QDir(out_raw).filePath(a_name)));
  QVERIFY(!QFileInfo::exists(QDir(out_raw).filePath(b_name)));
}

void AppLogicHashBehaviorTest::addRequestRawUSwitchCreatesAdditionalArchiveTarget() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString file_name = QStringLiteral("item.txt");
  const QString file_path = QDir(root.path()).filePath(file_name);
  write_text_file(file_path, QByteArray("payload"));

  const QString main_archive = QDir(root.path()).filePath(QStringLiteral("main.7z"));
  {
    z7::app::AddRequest create_request;
    create_request.archive_path = to_std_path(main_archive);
    create_request.format = "7z";
    create_request.input_paths = {to_std_path(file_path)};
    QVERIFY(run_request_sync(create_request).ok);
  }

  {
    const QString shadow_archive = QDir(root.path()).filePath(QStringLiteral("shadow.7z"));
    z7::app::AddRequest update_request;
    update_request.archive_path = to_std_path(main_archive);
    update_request.format = "7z";
    update_request.update_mode = "update";
    update_request.raw_update_switch = "!" + to_std_path(shadow_archive);
    update_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult update_result = run_request_sync(update_request);
    QVERIFY(update_result.ok);
  }

  const QString shadow_archive = QDir(root.path()).filePath(QStringLiteral("shadow.7z"));
  QVERIFY(QFileInfo::exists(shadow_archive));

  const QByteArray shadow_text = extract_file_text(root.path(),
                                                   shadow_archive,
                                                   QStringLiteral("out-shadow"),
                                                   file_name);
  QCOMPARE(shadow_text, QByteArray("payload"));
}

void AppLogicHashBehaviorTest::addRequestRawUSwitchCreatesMultipleAdditionalArchiveTargets() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString file_name = QStringLiteral("item.txt");
  const QString file_path = QDir(root.path()).filePath(file_name);
  write_text_file(file_path, QByteArray("payload"));

  const QString main_archive = QDir(root.path()).filePath(QStringLiteral("main.7z"));
  {
    z7::app::AddRequest create_request;
    create_request.archive_path = to_std_path(main_archive);
    create_request.format = "7z";
    create_request.input_paths = {to_std_path(file_path)};
    QVERIFY(run_request_sync(create_request).ok);
  }

  const QString shadow_1 = QDir(root.path()).filePath(QStringLiteral("shadow-1.7z"));
  const QString shadow_2 = QDir(root.path()).filePath(QStringLiteral("shadow-2.7z"));
  {
    z7::app::AddRequest update_request;
    update_request.archive_path = to_std_path(main_archive);
    update_request.format = "7z";
    update_request.update_mode = "update";
    update_request.raw_update_switches = {
        "!" + to_std_path(shadow_1),
        "!" + to_std_path(shadow_2),
    };
    update_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult update_result = run_request_sync(update_request);
    QVERIFY(update_result.ok);
  }

  for (const QString& shadow_archive : {shadow_1, shadow_2}) {
    QVERIFY(QFileInfo::exists(shadow_archive));
    const QByteArray shadow_text = extract_file_text(
        root.path(),
        shadow_archive,
        QStringLiteral("out-%1").arg(QFileInfo(shadow_archive).baseName()),
        file_name);
    QCOMPARE(shadow_text, QByteArray("payload"));
  }
}

void AppLogicHashBehaviorTest::addRequestRawUSwitchesIgnoreMainAndCreateAdditionalArchive() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString file_name = QStringLiteral("item.txt");
  const QString file_path = QDir(root.path()).filePath(file_name);
  write_text_file(file_path, QByteArray("archive-new"));

  const QString main_archive = QDir(root.path()).filePath(QStringLiteral("main.7z"));
  {
    z7::app::AddRequest create_request;
    create_request.archive_path = to_std_path(main_archive);
    create_request.format = "7z";
    create_request.input_paths = {to_std_path(file_path)};
    QVERIFY(run_request_sync(create_request).ok);
  }

  write_text_file(file_path, QByteArray("disk-changed"));

  const QString shadow_archive = QDir(root.path()).filePath(QStringLiteral("shadow.7z"));
  {
    z7::app::AddRequest update_request;
    update_request.archive_path = to_std_path(main_archive);
    update_request.format = "7z";
    update_request.update_mode = "update";
    update_request.raw_update_switches = {
        "-",
        "!" + to_std_path(shadow_archive),
    };
    update_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult update_result = run_request_sync(update_request);
    QVERIFY(update_result.ok);
  }

  const QByteArray main_text = extract_file_text(root.path(),
                                                 main_archive,
                                                 QStringLiteral("out-combo-main"),
                                                 file_name);
  QCOMPARE(main_text, QByteArray("archive-new"));

  const QByteArray shadow_text = extract_file_text(root.path(),
                                                   shadow_archive,
                                                   QStringLiteral("out-combo-shadow"),
                                                   file_name);
  QCOMPARE(shadow_text, QByteArray("disk-changed"));
}

void AppLogicHashBehaviorTest::addRequestRawUSwitchIgnoreMainArchiveLeavesContentUnchanged() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString file_name = QStringLiteral("item.txt");
  const QString file_path = QDir(root.path()).filePath(file_name);
  write_text_file(file_path, QByteArray("archive-new"));

  const QString archive_path = QDir(root.path()).filePath(QStringLiteral("sample.7z"));
  {
    z7::app::AddRequest create_request;
    create_request.archive_path = to_std_path(archive_path);
    create_request.format = "7z";
    create_request.input_paths = {to_std_path(file_path)};
    QVERIFY(run_request_sync(create_request).ok);
  }

  write_text_file(file_path, QByteArray("disk-changed"));

  {
    z7::app::AddRequest update_request;
    update_request.archive_path = to_std_path(archive_path);
    update_request.format = "7z";
    update_request.update_mode = "update";
    update_request.raw_update_switch = "-";
    update_request.input_paths = {to_std_path(file_path)};
    const z7::app::AddResult update_result = run_request_sync(update_request);
    QVERIFY(update_result.ok);
  }

  const QByteArray extracted = extract_file_text(root.path(),
                                                 archive_path,
                                                 QStringLiteral("out-u-ignore"),
                                                 file_name);
  QCOMPARE(extracted, QByteArray("archive-new"));
}

void AppLogicHashBehaviorTest::addRequestInvalidRawUpdateSwitchReturnsInvalidArguments() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString file_name = QStringLiteral("item.txt");
  const QString file_path = QDir(root.path()).filePath(file_name);
  write_text_file(file_path, QByteArray("payload"));

  const QString archive_path = QDir(root.path()).filePath(QStringLiteral("sample.7z"));
  {
    z7::app::AddRequest create_request;
    create_request.archive_path = to_std_path(archive_path);
    create_request.format = "7z";
    create_request.input_paths = {to_std_path(file_path)};
    QVERIFY(run_request_sync(create_request).ok);
  }

  z7::app::AddRequest invalid_request;
  invalid_request.archive_path = to_std_path(archive_path);
  invalid_request.format = "7z";
  invalid_request.update_mode = "update";
  invalid_request.raw_update_switch = "p2";
  invalid_request.input_paths = {to_std_path(file_path)};
  const z7::app::AddResult invalid_result = run_request_sync(invalid_request);
  QVERIFY(!invalid_result.ok);
  QCOMPARE(invalid_result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);
}

void AppLogicHashBehaviorTest::addRequestInputItemsRejectInvalidContractsAndMissingSources() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString source_path = QDir(root.path()).filePath(QStringLiteral("source.txt"));
  const QString other_path = QDir(root.path()).filePath(QStringLiteral("other.txt"));
  const QString missing_path = QDir(root.path()).filePath(QStringLiteral("missing.txt"));
  write_text_file(source_path, QByteArray("payload"));
  write_text_file(other_path, QByteArray("other"));

  auto expect_invalid = [&](const z7::app::AddRequest& request,
                            const QString& expected_fragment) {
    const z7::app::AddResult result = run_request_sync(request);
    QVERIFY(!result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);
    const QString combined =
        QString::fromStdString(result.summary + "\n" + result.error.message);
    QVERIFY2(combined.contains(expected_fragment),
             qPrintable(QStringLiteral("missing diagnostic fragment: %1 in %2")
                            .arg(expected_fragment, combined)));
  };

  {
    z7::app::AddRequest request;
    request.archive_path =
        to_std_path(QDir(root.path()).filePath(QStringLiteral("mixing.7z")));
    request.format = "7z";
    request.input_paths = {to_std_path(source_path)};
    request.input_items = {{to_std_path(other_path), "mapped.txt"}};
    expect_invalid(request, QStringLiteral("cannot mix input_paths and input_items"));
  }

  {
    z7::app::AddRequest request;
    request.archive_path =
        to_std_path(QDir(root.path()).filePath(QStringLiteral("directory.7z")));
    request.format = "7z";
    request.directory = "folder";
    request.input_items = {{to_std_path(source_path), "mapped.txt"}};
    expect_invalid(request, QStringLiteral("cannot also set directory"));
  }

  {
    z7::app::AddRequest request;
    request.archive_path =
        to_std_path(QDir(root.path()).filePath(QStringLiteral("delete-after.7z")));
    request.format = "7z";
    request.delete_after_compressing = true;
    request.input_items = {{to_std_path(source_path), "mapped.txt"}};
    expect_invalid(request,
                   QStringLiteral("does not support delete_after_compressing"));
  }

  {
    z7::app::AddRequest request;
    request.archive_path =
        to_std_path(QDir(root.path()).filePath(QStringLiteral("duplicate.7z")));
    request.format = "7z";
    request.input_items = {
        {to_std_path(source_path), "dup.txt"},
        {to_std_path(other_path), "dup.txt"},
    };
    expect_invalid(request, QStringLiteral("duplicate archive_entry"));
  }

  {
    z7::app::AddRequest request;
    request.archive_path =
        to_std_path(QDir(root.path()).filePath(QStringLiteral("missing.7z")));
    request.format = "7z";
    request.input_items = {{to_std_path(missing_path), "missing.txt"}};
    expect_invalid(request, QStringLiteral("source does not exist"));
  }
}

void AppLogicHashBehaviorTest::addRequestReturnsPartialSuccessWhenInputMissing() {
  QTemporaryDir root;
  QVERIFY2(root.isValid(), "failed to create temp dir");

  const QString existing_name = QStringLiteral("exists.txt");
  const QString missing_name = QStringLiteral("missing.txt");
  const QString existing_path = QDir(root.path()).filePath(existing_name);
  const QString missing_path = QDir(root.path()).filePath(missing_name);
  write_text_file(existing_path, QByteArray("payload"));

  const QString archive_path = QDir(root.path()).filePath(QStringLiteral("partial.7z"));
  z7::app::AddRequest add_request;
  add_request.archive_path = to_std_path(archive_path);
  add_request.format = "7z";
  add_request.input_paths = {to_std_path(existing_path), to_std_path(missing_path)};

  const z7::app::AddResult add_result = run_request_sync(add_request);
  QVERIFY(!add_result.ok);
  QCOMPARE(add_result.error.domain, z7::app::ArchiveErrorDomain::kPartialSuccess);
  QVERIFY(!QString::fromStdString(add_result.summary).trimmed().isEmpty());
}
