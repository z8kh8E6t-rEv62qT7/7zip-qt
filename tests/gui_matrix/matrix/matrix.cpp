// tests/gui_matrix/matrix/matrix.cpp
// Role: Validate GUI behavior matrix schema and coverage guarantees.

#include "internal.h"

#include <QFile>

#include <algorithm>

namespace {

struct GuiMatrixData {
  QVector<GuiMatrixItem> items;
  QHash<QString, QString> alias_to_canonical;
};

QString matrix_file_path() {
  return QStringLiteral(Z7_GUI_BEHAVIOR_MATRIX_PATH);
}

QString source_root_path() {
  return QStringLiteral(Z7_SOURCE_ROOT_PATH);
}

QString to_lines(const QSet<QString>& values) {
  QStringList list = values.values();
  std::sort(list.begin(), list.end());
  return list.join(QStringLiteral(", "));
}

QString read_text_file(const QString& path, QString* error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    *error = QStringLiteral("cannot open file: %1").arg(path);
    return QString();
  }
  return QString::fromUtf8(file.readAll());
}

QStringList parse_string_or_array(const QJsonObject& object,
                                  const QString& key,
                                  QString* error) {
  if (!object.contains(key)) {
    *error = QStringLiteral("missing key: %1").arg(key);
    return {};
  }
  const QJsonValue value = object.value(key);
  if (value.isString()) {
    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
      *error = QStringLiteral("key %1 is empty").arg(key);
      return {};
    }
    return {text};
  }
  if (value.isArray()) {
    QStringList result;
    const QJsonArray array = value.toArray();
    for (int i = 0; i < array.size(); ++i) {
      const QJsonValue element = array.at(i);
      if (!element.isString()) {
        *error = QStringLiteral("key %1 has non-string item at %2")
                     .arg(key)
                     .arg(i);
        return {};
      }
      const QString text = element.toString().trimmed();
      if (text.isEmpty()) {
        *error = QStringLiteral("key %1 has empty string item at %2")
                     .arg(key)
                     .arg(i);
        return {};
      }
      result.push_back(text);
    }
    if (result.isEmpty()) {
      *error = QStringLiteral("key %1 has empty array").arg(key);
      return {};
    }
    return result;
  }

  *error = QStringLiteral("key %1 must be string or array").arg(key);
  return {};
}

QString normalize_case_name(const QString& name,
                            const QHash<QString, QString>& alias_to_canonical) {
  const auto it = alias_to_canonical.constFind(name);
  if (it == alias_to_canonical.constEnd()) {
    return name;
  }
  return *it;
}

QHash<QString, QString> parse_case_aliases(const QJsonObject& root,
                                           QString* error) {
  QHash<QString, QString> alias_to_canonical;
  const QJsonValue aliases_value = root.value(QStringLiteral("case_aliases"));
  if (aliases_value.isUndefined() || aliases_value.isNull()) {
    return alias_to_canonical;
  }
  if (!aliases_value.isObject()) {
    *error = QStringLiteral("root.case_aliases must be object");
    return {};
  }

  const QJsonObject aliases_object = aliases_value.toObject();
  for (auto it = aliases_object.constBegin(); it != aliases_object.constEnd(); ++it) {
    const QString canonical = it.key().trimmed();
    if (canonical.isEmpty()) {
      *error = QStringLiteral("case_aliases has empty canonical key");
      return {};
    }
    if (alias_to_canonical.contains(canonical)) {
      *error = QStringLiteral("case_aliases canonical cannot also be alias: %1")
                   .arg(canonical);
      return {};
    }

    const QStringList aliases =
        parse_string_or_array(aliases_object, canonical, error);
    if (!error->isEmpty()) {
      return {};
    }
    for (const QString& alias : aliases) {
      const QString normalized_alias = alias.trimmed();
      if (normalized_alias == canonical) {
        *error = QStringLiteral("case_aliases contains self alias: %1")
                     .arg(canonical);
        return {};
      }
      if (alias_to_canonical.contains(normalized_alias) &&
          alias_to_canonical.value(normalized_alias) != canonical) {
        *error = QStringLiteral("case_aliases alias reused by multiple canonicals: %1")
                     .arg(normalized_alias);
        return {};
      }
      alias_to_canonical.insert(normalized_alias, canonical);
    }
  }

  return alias_to_canonical;
}

GuiMatrixData load_matrix_data(QString* error) {
  QFile matrix_file(matrix_file_path());
  if (!matrix_file.open(QIODevice::ReadOnly)) {
    *error = QStringLiteral("cannot open matrix file: %1")
                 .arg(matrix_file.fileName());
    return GuiMatrixData{};
  }

  QJsonParseError parse_error;
  const QJsonDocument document =
      QJsonDocument::fromJson(matrix_file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    *error = QStringLiteral("invalid matrix json: %1")
                 .arg(parse_error.errorString());
    return GuiMatrixData{};
  }

  GuiMatrixData data;
  const QJsonObject root = document.object();
  data.alias_to_canonical = parse_case_aliases(root, error);
  if (!error->isEmpty()) {
    return GuiMatrixData{};
  }

  const QJsonValue items_value = root.value(QStringLiteral("items"));
  if (!items_value.isArray()) {
    *error = QStringLiteral("root.items must be array");
    return GuiMatrixData{};
  }

  const QJsonArray items_array = items_value.toArray();
  data.items.reserve(items_array.size());

  for (int i = 0; i < items_array.size(); ++i) {
    const QJsonValue value = items_array.at(i);
    if (!value.isObject()) {
      *error = QStringLiteral("items[%1] must be object").arg(i);
      return GuiMatrixData{};
    }

    const QJsonObject object = value.toObject();
    GuiMatrixItem item;
    item.behavior_id = object.value(QStringLiteral("behavior_id"))
                           .toString()
                           .trimmed();
    item.behavior = object.value(QStringLiteral("behavior")).toString().trimmed();
    item.suite = object.value(QStringLiteral("suite")).toString().trimmed();
    item.status = object.value(QStringLiteral("status")).toString().trimmed();
    item.gate = object.value(QStringLiteral("gate")).toString().trimmed();
    item.original_anchor =
        parse_string_or_array(object, QStringLiteral("original_anchor"), error);
    if (!error->isEmpty()) {
      *error = QStringLiteral("items[%1]: %2").arg(i).arg(*error);
      return GuiMatrixData{};
    }
    item.qt_anchor =
        parse_string_or_array(object, QStringLiteral("qt_anchor"), error);
    if (!error->isEmpty()) {
      *error = QStringLiteral("items[%1]: %2").arg(i).arg(*error);
      return GuiMatrixData{};
    }
    item.cases = parse_string_or_array(object, QStringLiteral("cases"), error);
    if (!error->isEmpty()) {
      *error = QStringLiteral("items[%1]: %2").arg(i).arg(*error);
      return GuiMatrixData{};
    }

    if (item.behavior_id.isEmpty() || item.behavior.isEmpty() || item.suite.isEmpty() ||
        item.status.isEmpty() || item.gate.isEmpty()) {
      *error = QStringLiteral("items[%1] has empty required scalar field").arg(i);
      return GuiMatrixData{};
    }

    data.items.push_back(item);
  }

  return data;
}

QSet<QString> extract_slots_from_class(const QString& header_path,
                                       const QString& class_name,
                                       QString* error) {
  QFile header_file(header_path);
  if (!header_file.open(QIODevice::ReadOnly)) {
    *error = QStringLiteral("cannot open header: %1").arg(header_path);
    return {};
  }

  const QString text = QString::fromUtf8(header_file.readAll());
  const QString class_token = QStringLiteral("class %1").arg(class_name);
  const int class_start = text.indexOf(class_token);
  if (class_start < 0) {
    *error = QStringLiteral("class token not found: %1 in %2")
                 .arg(class_name, header_path);
    return {};
  }

  const int body_start = text.indexOf('{', class_start);
  const int body_end = text.indexOf(QStringLiteral("};"), body_start);
  if (body_start < 0 || body_end < 0 || body_end <= body_start) {
    *error = QStringLiteral("cannot locate class body: %1 in %2")
                 .arg(class_name, header_path);
    return {};
  }

  const QString class_body = text.mid(body_start + 1, body_end - body_start - 1);
  const QRegularExpression slot_decl(
      QStringLiteral(R"(\bvoid\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*\)\s*;)"));
  QSet<QString> cases;
  QRegularExpressionMatchIterator it = slot_decl.globalMatch(class_body);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    const QString name = match.captured(1);
    if (name == QStringLiteral("init")) {
      continue;
    }
    cases.insert(name);
  }

  if (cases.isEmpty()) {
    *error = QStringLiteral("no slots discovered for class: %1").arg(class_name);
    return {};
  }

  return cases;
}

QHash<QString, QSet<QString>> discover_suite_cases(QString* error) {
  const QString root = source_root_path();
  QHash<QString, QSet<QString>> suites;

  suites.insert(QStringLiteral("z7.filemanager.behavior"),
                extract_slots_from_class(
                    root + QStringLiteral("/tests/filemanager/behavior/internal.h"),
                    QStringLiteral("FileManagerBehaviorTest"),
                    error));
  if (!error->isEmpty()) {
    return {};
  }

  suites.insert(QStringLiteral("z7.app_logic.hash.behavior"),
                extract_slots_from_class(
                    root + QStringLiteral("/tests/app_logic/hash_behavior/internal.h"),
                    QStringLiteral("AppLogicHashBehaviorTest"),
                    error));
  if (!error->isEmpty()) {
    return {};
  }

  suites.insert(QStringLiteral("z7.smoke"),
                QSet<QString>{
                    QStringLiteral("z7.smoke"),
                    QStringLiteral("z7.smoke.hidpi.150"),
                    QStringLiteral("z7.smoke.hidpi.200"),
                });

  return suites;
}

bool is_allowed_status(const QString& value) {
  return value == QStringLiteral("DONE") || value == QStringLiteral("TODO") ||
         value == QStringLiteral("BLOCKED");
}

bool is_allowed_gate(const QString& value) {
  return value == QStringLiteral("YES") || value == QStringLiteral("NO");
}

}  // namespace

void GuiBehaviorMatrixTest::matrixSchemaIsValid() {
  QString error;
  const GuiMatrixData data = load_matrix_data(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  const QVector<GuiMatrixItem>& items = data.items;
  QVERIFY2(!items.isEmpty(), "matrix must contain at least one item");

  QSet<QString> seen_ids;
  for (const GuiMatrixItem& item : items) {
    QVERIFY2(!seen_ids.contains(item.behavior_id),
             qPrintable(QStringLiteral("duplicate behavior_id: %1")
                            .arg(item.behavior_id)));
    seen_ids.insert(item.behavior_id);

    QVERIFY2(is_allowed_status(item.status),
             qPrintable(QStringLiteral("invalid status for %1: %2")
                            .arg(item.behavior_id, item.status)));
    QVERIFY2(is_allowed_gate(item.gate),
             qPrintable(QStringLiteral("invalid gate for %1: %2")
                            .arg(item.behavior_id, item.gate)));

    if (item.gate == QStringLiteral("YES")) {
      QVERIFY2(item.status == QStringLiteral("DONE"),
               qPrintable(QStringLiteral(
                              "gate YES requires DONE status for %1")
                              .arg(item.behavior_id)));
    }

    for (const QString& path : item.original_anchor) {
      QVERIFY2(path.startsWith(
                   QStringLiteral("src/third_party/original_7zip/")),
               qPrintable(QStringLiteral(
                              "original_anchor must point to original_7zip: %1")
                              .arg(path)));
    }

    for (const QString& path : item.qt_anchor) {
      const bool valid = path.startsWith(QStringLiteral("src/")) ||
                         path.startsWith(QStringLiteral("tests/"));
      QVERIFY2(valid,
               qPrintable(QStringLiteral("qt_anchor must point to src/ or tests/: %1")
                              .arg(path)));
    }

    QSet<QString> normalized_case_names;
    for (const QString& case_name : item.cases) {
      QVERIFY2(
          !data.alias_to_canonical.contains(case_name),
          qPrintable(QStringLiteral(
                         "matrix case must use canonical name: %1 -> %2")
                         .arg(case_name,
                              data.alias_to_canonical.value(case_name))));
      const QString normalized =
          normalize_case_name(case_name, data.alias_to_canonical);
      QVERIFY2(
          !normalized_case_names.contains(normalized),
          qPrintable(QStringLiteral(
                         "duplicate case after alias normalization for %1: %2")
                         .arg(item.behavior_id, normalized)));
      normalized_case_names.insert(normalized);
    }
  }
}

void GuiBehaviorMatrixTest::matrixReferencesKnownTestsOnly() {
  QString error;
  const GuiMatrixData data = load_matrix_data(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  const QVector<GuiMatrixItem>& items = data.items;

  const QHash<QString, QSet<QString>> known_cases = discover_suite_cases(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));

  for (const GuiMatrixItem& item : items) {
    QVERIFY2(known_cases.contains(item.suite),
             qPrintable(QStringLiteral("unknown suite for %1: %2")
                            .arg(item.behavior_id, item.suite)));
    const QSet<QString>& suite_cases_raw = known_cases[item.suite];
    QSet<QString> suite_cases_normalized;
    for (const QString& discovered_case : suite_cases_raw) {
      suite_cases_normalized.insert(
          normalize_case_name(discovered_case, data.alias_to_canonical));
    }
    for (const QString& case_name : item.cases) {
      const QString normalized =
          normalize_case_name(case_name, data.alias_to_canonical);
      QVERIFY2(
          suite_cases_normalized.contains(normalized),
          qPrintable(QStringLiteral(
                         "unknown test case for %1: %2::%3")
                         .arg(item.behavior_id, item.suite, case_name)));
    }
  }
}

void GuiBehaviorMatrixTest::matrixCoversAllBehaviorTests() {
  QString error;
  const GuiMatrixData data = load_matrix_data(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  const QVector<GuiMatrixItem>& items = data.items;

  const QHash<QString, QSet<QString>> known_cases = discover_suite_cases(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));

  QHash<QString, QSet<QString>> mapped_cases;
  for (const GuiMatrixItem& item : items) {
    if (item.status != QStringLiteral("DONE")) {
      continue;
    }
    for (const QString& case_name : item.cases) {
      mapped_cases[item.suite].insert(
          normalize_case_name(case_name, data.alias_to_canonical));
    }
  }

  QStringList suites = known_cases.keys();
  std::sort(suites.begin(), suites.end());

  QStringList missing_reports;
  for (const QString& suite : suites) {
    QSet<QString> missing;
    for (const QString& known_case : known_cases.value(suite)) {
      missing.insert(normalize_case_name(known_case, data.alias_to_canonical));
    }
    missing.subtract(mapped_cases.value(suite));
    if (!missing.isEmpty()) {
      missing_reports.push_back(
          QStringLiteral("%1 -> %2").arg(suite, to_lines(missing)));
    }
  }

  QVERIFY2(missing_reports.isEmpty(),
           qPrintable(QStringLiteral("matrix missing coverage:\n%1")
                          .arg(missing_reports.join(QLatin1Char('\n')))));
}

void GuiBehaviorMatrixTest::macOSInfoPlistDeclaresArchiveDocumentTypes() {
  QString error;
  const QString plist_path =
      source_root_path() + QStringLiteral("/packaging/macos/Info.plist.in");
  const QString plist_text = read_text_file(plist_path, &error);
  QVERIFY2(error.isEmpty(), qPrintable(error));

  const QStringList required_snippets = {
      QStringLiteral("CFBundleDocumentTypes"),
      QStringLiteral("LSItemContentTypes"),
      QStringLiteral("UTImportedTypeDeclarations"),
      QStringLiteral("LSHandlerRank"),
      QStringLiteral("<string>Alternate</string>"),
      QStringLiteral("org.7-zip.7-zip-archive"),
      QStringLiteral("public.zip-archive"),
      QStringLiteral("com.rarlab.rar-archive"),
      QStringLiteral("public.tar-archive"),
      QStringLiteral("org.gnu.gnu-zip-archive"),
      QStringLiteral("public.bzip2-archive"),
      QStringLiteral("org.tukaani.xz-archive"),
      QStringLiteral("public.zstandard-archive"),
      QStringLiteral("com.microsoft.cab"),
      QStringLiteral("public.iso-image"),
      QStringLiteral("com.microsoft.wim-archive"),
      QStringLiteral("app.sevenzip.archive.arj"),
      QStringLiteral("public.cpio-archive"),
      QStringLiteral("public.lzma-archive"),
      QStringLiteral("public.lzip-archive"),
      QStringLiteral("public.filename-extension"),
      QStringLiteral("<string>arj</string>"),
  };

  for (const QString& snippet : required_snippets) {
    QVERIFY2(plist_text.contains(snippet),
             qPrintable(QStringLiteral("Info.plist.in missing snippet: %1")
                            .arg(snippet)));
  }
}
