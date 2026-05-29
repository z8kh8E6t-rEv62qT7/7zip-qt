// tests/filemanager/behavior/runner_extract_command.cpp
// Role: ArchiveProcessRunner extract command behavior case.

#include "internal.h"

using namespace filemanager_behavior_internal;

void FileManagerBehaviorTest::extractActionUsesExtractCommand() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString archive_path = create_sample_archive(root);
    QVERIFY2(!archive_path.isEmpty(), "failed to prepare sample archive");

    QTemporaryDir output_root;
    QVERIFY2(output_root.isValid(), "failed to create output temp dir");

    z7::ui::filemanager::ArchiveProcessRunner runner;
    QSignalSpy started_spy(
        &runner,
        SIGNAL(started(QString,QString,QStringList)));
    QSignalSpy finished_spy(
        &runner,
        SIGNAL(finished(bool,int,int,QString)));

    QVERIFY(runner.start_extract(
        archive_path,
        output_root.path(),
        z7::ui::filemanager::OverwriteMode::kOverwrite));

    QTRY_VERIFY_WITH_TIMEOUT(started_spy.count() > 0, 5000);
    const QList<QVariant> started_args = started_spy.takeFirst();
    QCOMPARE(started_args.at(1).toString(), QStringLiteral("Extract"));
    const QStringList targets = started_args.at(2).toStringList();
    QVERIFY(targets.contains(archive_path));
    QVERIFY(targets.contains(output_root.path()));

    QTRY_VERIFY_WITH_TIMEOUT(finished_spy.count() > 0, 20000);
}
