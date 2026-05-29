// tests/app_logic/hash_behavior/cases_extract_output_helpers.cpp
// Role: Extract output directory helper behavior.

#include "internal.h"

#include "operations/extract_output.h"

using namespace hash_behavior_internal;

namespace {

QString resolve_template(const QString& output_template,
                         const QString& archive_path) {
  return QString::fromStdString(z7::app::resolve_multi_archive_output_dir(
      to_std_path(output_template), to_std_path(archive_path)));
}

}  // namespace

void AppLogicHashBehaviorTest::extractOutputTemplateNamesFollowArchiveSuffixRules() {
  QCOMPARE(resolve_template(QStringLiteral("/out/*"),
                            QStringLiteral("/archives/photos.zip")),
           QStringLiteral("/out/photos"));
  QCOMPARE(resolve_template(QStringLiteral("/out/*"),
                            QStringLiteral("/archives/photos.7z.001")),
           QStringLiteral("/out/photos"));
  QCOMPARE(resolve_template(QStringLiteral("/out/*"),
                            QStringLiteral("/archives/movie.part001.rar")),
           QStringLiteral("/out/movie"));
  QCOMPARE(resolve_template(QStringLiteral("/out/*"),
                            QStringLiteral("/archives/source.tar.gz")),
           QStringLiteral("/out/source.tar"));
  QCOMPARE(resolve_template(QStringLiteral("/out/*"),
                            QStringLiteral("/archives/source.tgz")),
           QStringLiteral("/out/source"));
  QCOMPARE(resolve_template(QStringLiteral("/out/*"),
                            QStringLiteral("/archives/  .zip")),
           QStringLiteral("/out/  .zip"));
  QCOMPARE(resolve_template(QStringLiteral("/out/*/*"),
                            QStringLiteral("/archives/data")),
           QStringLiteral("/out/data~/data~"));
  QCOMPARE(resolve_template(QStringLiteral("/out/fixed"),
                            QStringLiteral("/archives/data.zip")),
           QStringLiteral("/out/fixed"));
}

void AppLogicHashBehaviorTest::
    extractOutputTailNameNormalizesSeparatorsAndTrailingSlashes() {
  QCOMPARE(QString::fromStdString(z7::app::output_tail_name("C:\\tmp\\out\\")),
           QStringLiteral("out"));
  QCOMPARE(QString::fromStdString(z7::app::output_tail_name("/tmp/out///")),
           QStringLiteral("out"));
  QCOMPARE(QString::fromStdString(z7::app::output_tail_name("relative")),
           QStringLiteral("relative"));
  QCOMPARE(QString::fromStdString(z7::app::output_tail_name("/")),
           QString());
  QCOMPARE(QString::fromStdString(z7::app::output_tail_name("")),
           QString());
}
