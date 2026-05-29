// tests/app_logic/hash_behavior/matrix_coverage.cpp
// Role: Validate operation/outcome coverage matrix schema and case mapping.

#include "internal.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>

#include <algorithm>

namespace {

QString matrix_file_path() {
  return QStringLiteral(Z7_APPLOGIC_OUTCOME_MATRIX_PATH);
}

QString to_lines(const QSet<QString>& values) {
  QStringList list = values.values();
  std::sort(list.begin(), list.end());
  return list.join(QStringLiteral(", "));
}

QSet<QString> required_operations() {
  return QSet<QString>{
      QStringLiteral("add"),
      QStringLiteral("extract"),
      QStringLiteral("test"),
      QStringLiteral("benchmark"),
      QStringLiteral("split"),
      QStringLiteral("combine"),
      QStringLiteral("hash"),
      QStringLiteral("delete"),
      QStringLiteral("open_archive"),
      QStringLiteral("list"),
      QStringLiteral("archive_properties"),
      QStringLiteral("navigate"),
      QStringLiteral("copy"),
      QStringLiteral("move"),
      QStringLiteral("rename"),
      QStringLiteral("create"),
      QStringLiteral("comment"),
      QStringLiteral("get_entry_info"),
      QStringLiteral("list_streaming"),
  };
}

QSet<QString> required_statuses() {
  return QSet<QString>{
      QStringLiteral("success"),
      QStringLiteral("failure"),
      QStringLiteral("cancel"),
      QStringLiteral("partial"),
  };
}

QSet<QString> discover_test_slots() {
  QSet<QString> slot_names;
  const QMetaObject& meta = AppLogicHashBehaviorTest::staticMetaObject;
  for (int i = meta.methodOffset(); i < meta.methodCount(); ++i) {
    const QMetaMethod method = meta.method(i);
    if (method.methodType() != QMetaMethod::Slot) {
      continue;
    }
    slot_names.insert(QString::fromLatin1(method.name()));
  }
  return slot_names;
}

QStringList parse_cases_array(const QJsonObject& object,
                              const QString& key,
                              QString* error) {
  const QJsonValue value = object.value(key);
  if (!value.isArray()) {
    *error = QStringLiteral("key %1 must be array").arg(key);
    return {};
  }
  const QJsonArray array = value.toArray();
  QStringList cases;
  for (int i = 0; i < array.size(); ++i) {
    const QJsonValue cell = array.at(i);
    if (!cell.isString()) {
      *error = QStringLiteral("key %1 has non-string case at index %2")
                   .arg(key)
                   .arg(i);
      return {};
    }
    const QString name = cell.toString().trimmed();
    if (name.isEmpty()) {
      *error = QStringLiteral("key %1 has empty case at index %2")
                   .arg(key)
                   .arg(i);
      return {};
    }
    cases.push_back(name);
  }
  return cases;
}

}  // namespace

void AppLogicHashBehaviorTest::operationOutcomeMatrixIsCompleteAndMapped() {
  QFile file(matrix_file_path());
  QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

  QJsonParseError parse_error;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
  QVERIFY2(parse_error.error == QJsonParseError::NoError, qPrintable(parse_error.errorString()));
  QVERIFY(doc.isObject());

  const QJsonObject root = doc.object();
  const QJsonValue operations_value = root.value(QStringLiteral("operations"));
  QVERIFY2(operations_value.isArray(), "root.operations must be an array");
  const QJsonArray operations = operations_value.toArray();

  const QSet<QString> required_ops = required_operations();
  const QSet<QString> required_stat = required_statuses();
  const QSet<QString> known_slots = discover_test_slots();
  QVERIFY(!known_slots.isEmpty());

  QSet<QString> seen_ops;
  QSet<QString> referenced_slots;

  for (int i = 0; i < operations.size(); ++i) {
    QVERIFY2(operations.at(i).isObject(), "operation entry must be object");
    const QJsonObject op_obj = operations.at(i).toObject();
    const QString op = op_obj.value(QStringLiteral("operation")).toString().trimmed();
    QVERIFY2(!op.isEmpty(), "operation name is empty");
    QVERIFY2(required_ops.contains(op), qPrintable(QStringLiteral("unknown operation: %1").arg(op)));
    QVERIFY2(!seen_ops.contains(op), qPrintable(QStringLiteral("duplicate operation: %1").arg(op)));
    seen_ops.insert(op);

    const QJsonValue coverage_value = op_obj.value(QStringLiteral("status_coverage"));
    QVERIFY2(coverage_value.isObject(), "status_coverage must be object");
    const QJsonObject coverage = coverage_value.toObject();

    QSet<QString> status_keys;
    for (auto it = coverage.constBegin(); it != coverage.constEnd(); ++it) {
      status_keys.insert(it.key());
    }
    QCOMPARE(status_keys, required_stat);

    for (const QString& status : required_stat) {
      QVERIFY2(coverage.value(status).isObject(),
               qPrintable(QStringLiteral("%1/%2 must be object").arg(op, status)));
      const QJsonObject status_obj = coverage.value(status).toObject();

      QString parse_cases_error;
      QStringList cases;
      if (status_obj.contains(QStringLiteral("cases"))) {
        cases = parse_cases_array(status_obj, QStringLiteral("cases"), &parse_cases_error);
        QVERIFY2(parse_cases_error.isEmpty(), qPrintable(parse_cases_error));
      }
      const QString waived_reason =
          status_obj.value(QStringLiteral("waived_reason")).toString().trimmed();

      QVERIFY2(!cases.isEmpty() || !waived_reason.isEmpty(),
               qPrintable(QStringLiteral("%1/%2 must declare cases or waived_reason")
                              .arg(op, status)));
      if (!cases.isEmpty()) {
        for (const QString& name : cases) {
          QVERIFY2(known_slots.contains(name),
                   qPrintable(QStringLiteral("matrix references unknown case: %1").arg(name)));
          referenced_slots.insert(name);
        }
      }
    }
  }

  QCOMPARE(seen_ops, required_ops);

  // Keep setup entrypoints and matrix checker out of reference requirements.
  QSet<QString> expected_referenced = known_slots;
  expected_referenced.remove(QStringLiteral("init"));
  expected_referenced.remove(QStringLiteral("operationOutcomeMatrixIsCompleteAndMapped"));
  const QSet<QString> missing = expected_referenced - referenced_slots;
  QVERIFY2(missing.isEmpty(),
           qPrintable(QStringLiteral("operation matrix missing case references: %1")
                          .arg(to_lines(missing))));
}
