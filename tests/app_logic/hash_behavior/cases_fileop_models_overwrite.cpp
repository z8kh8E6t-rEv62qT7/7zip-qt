// tests/app_logic/hash_behavior/cases_fileop_models_overwrite.cpp
// Role: Extract ask-overwrite behavior matrix coverage.

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

}  // namespace

void AppLogicHashBehaviorTest::extractAskOverwriteCallbackDecisionsAreApplied() {
    auto run_case =
        [](z7::app::OverwriteDecision decision,
           bool expect_replace,
           bool expect_extra_file,
           bool expect_cancel) {
          QTemporaryDir root;
          QVERIFY2(root.isValid(), "failed to create temp dir");

          const QString source_path =
              QDir(root.path()).filePath(QStringLiteral("item.txt"));
          const QByteArray archive_payload("from-archive");
          write_text_file(source_path, archive_payload);

          const QString archive_path =
              QDir(root.path()).filePath(QStringLiteral("sample.7z"));
          z7::app::AddRequest add_request;
          add_request.archive_path = to_std_path(archive_path);
          add_request.format = "7z";
          add_request.input_paths = {to_std_path(source_path)};
          QVERIFY(run_request_sync(add_request).ok);

          const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
          QVERIFY(QDir().mkpath(output_dir));
          const QString conflict_path = QDir(output_dir).filePath(QStringLiteral("item.txt"));
          const QByteArray existing_payload("existing");
          write_text_file(conflict_path, existing_payload);

          int ask_count = 0;
          DelegateInteraction interaction;
          interaction.request_overwrite = [decision, &ask_count](
                                              const z7::app::OverwritePrompt&) {
            ++ask_count;
            return decision;
          };

          z7::app::ExtractRequest request;
          request.archive_path = to_std_path(archive_path);
          request.output_dir = to_std_path(output_dir);
          request.overwrite_mode = z7::app::OverwriteMode::kAsk;
          const z7::app::ExtractResult result = run_request_sync(request, interaction);
          QCOMPARE(ask_count, 1);

          if (expect_cancel) {
            QVERIFY(!result.ok);
            QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kCanceled);
            QCOMPARE(read_text_file(conflict_path), existing_payload);
            return;
          }

          QVERIFY(result.ok);
          if (expect_replace) {
            QCOMPARE(read_text_file(conflict_path), archive_payload);
          } else {
            QCOMPARE(read_text_file(conflict_path), existing_payload);
          }

          if (expect_extra_file) {
            const QStringList files = QDir(output_dir).entryList(
                QStringList() << QStringLiteral("item*"),
                QDir::Files | QDir::NoDotAndDotDot);
            QVERIFY(files.size() >= 2);
          } else {
            const QStringList files =
                QDir(output_dir).entryList(QDir::Files | QDir::NoDotAndDotDot);
            QCOMPARE(files.size(), 1);
          }
        };

    run_case(z7::app::OverwriteDecision::kYes, true, false, false);
    run_case(z7::app::OverwriteDecision::kNo, false, false, false);
    run_case(z7::app::OverwriteDecision::kAutoRename, false, true, false);
    run_case(z7::app::OverwriteDecision::kCancel, false, false, true);
}

void AppLogicHashBehaviorTest::extractAskOverwriteStickyNoToAllSkipsRemainingConflicts() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_a = QDir(root.path()).filePath(QStringLiteral("a.txt"));
    const QString source_b = QDir(root.path()).filePath(QStringLiteral("b.txt"));
    write_text_file(source_a, QByteArray("new-a"));
    write_text_file(source_b, QByteArray("new-b"));

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("sample.7z"));
    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(source_a), to_std_path(source_b)};
    QVERIFY(run_request_sync(add_request).ok);

    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(output_dir));
    const QString conflict_a = QDir(output_dir).filePath(QStringLiteral("a.txt"));
    const QString conflict_b = QDir(output_dir).filePath(QStringLiteral("b.txt"));
    write_text_file(conflict_a, QByteArray("old-a"));
    write_text_file(conflict_b, QByteArray("old-b"));

    int ask_count = 0;
    DelegateInteraction interaction;
    interaction.request_overwrite = [&ask_count](const z7::app::OverwritePrompt&) {
      ++ask_count;
      return z7::app::OverwriteDecision::kNoToAll;
    };

    z7::app::ExtractRequest request;
    request.archive_path = to_std_path(archive_path);
    request.output_dir = to_std_path(output_dir);
    request.overwrite_mode = z7::app::OverwriteMode::kAsk;
    const z7::app::ExtractResult result = run_request_sync(request, interaction);

    QVERIFY(result.ok);
    QCOMPARE(ask_count, 1);
    QCOMPARE(read_text_file(conflict_a), QByteArray("old-a"));
    QCOMPARE(read_text_file(conflict_b), QByteArray("old-b"));
}

void AppLogicHashBehaviorTest::extractAskOverwriteWithoutCallbackReportsMissingInteraction() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString source_path = QDir(root.path()).filePath(QStringLiteral("item.txt"));
    write_text_file(source_path, QByteArray("archive-value"));

    const QString archive_path = QDir(root.path()).filePath(QStringLiteral("sample.7z"));
    z7::app::AddRequest add_request;
    add_request.archive_path = to_std_path(archive_path);
    add_request.format = "7z";
    add_request.input_paths = {to_std_path(source_path)};
    QVERIFY(run_request_sync(add_request).ok);

    const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(output_dir));
    const QString conflict_path = QDir(output_dir).filePath(QStringLiteral("item.txt"));
    write_text_file(conflict_path, QByteArray("existing-value"));

    z7::app::ExtractRequest request;
    request.archive_path = to_std_path(archive_path);
    request.output_dir = to_std_path(output_dir);
    request.overwrite_mode = z7::app::OverwriteMode::kAsk;
    const z7::app::ExtractResult result = run_request_sync(request);

    QVERIFY(!result.ok);
    QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kInvalidArguments);
    QVERIFY(QString::fromStdString(result.error.message)
                .contains(QStringLiteral("request_overwrite")));
    QCOMPARE(read_text_file(conflict_path), QByteArray("existing-value"));
}

void AppLogicHashBehaviorTest::copyMoveAskOverwriteCallbackDecisionsAreApplied() {
    auto run_copy_case =
        [](z7::app::OverwriteDecision decision,
           bool expect_replace,
           bool expect_extra_file,
           bool expect_cancel) {
          QTemporaryDir root;
          QVERIFY2(root.isValid(), "failed to create temp dir");

          const QString source_path =
              QDir(root.path()).filePath(QStringLiteral("source.txt"));
          const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
          QVERIFY(QDir().mkpath(output_dir));
          const QString conflict_path =
              QDir(output_dir).filePath(QStringLiteral("source.txt"));
          write_text_file(source_path, QByteArray("new-copy"));
          write_text_file(conflict_path, QByteArray("old-copy"));

          int ask_count = 0;
          DelegateInteraction interaction;
          interaction.request_overwrite = [decision, &ask_count](
                                              const z7::app::OverwritePrompt&) {
            ++ask_count;
            return decision;
          };

          z7::app::CopyRequest request;
          request.source_paths = {to_std_path(source_path)};
          request.destination_dir = to_std_path(output_dir);
          request.overwrite_mode = z7::app::OverwriteMode::kAsk;
          const z7::app::CopyResult result = run_request_sync(request, interaction);
          QCOMPARE(ask_count, 1);

          if (expect_cancel) {
            QVERIFY(!result.ok);
            QCOMPARE(result.error.domain, z7::app::ArchiveErrorDomain::kCanceled);
            QCOMPARE(read_text_file(conflict_path), QByteArray("old-copy"));
            QVERIFY(QFileInfo::exists(source_path));
            return;
          }

          QVERIFY(result.ok);
          QCOMPARE(result.copied_count,
                   expect_replace || expect_extra_file ? static_cast<size_t>(1)
                                                       : static_cast<size_t>(0));
          QCOMPARE(read_text_file(conflict_path),
                   expect_replace ? QByteArray("new-copy") : QByteArray("old-copy"));
          QVERIFY(QFileInfo::exists(source_path));
          const QStringList files =
              QDir(output_dir).entryList(QStringList() << QStringLiteral("source*"),
                                         QDir::Files | QDir::NoDotAndDotDot);
          QCOMPARE(files.size(), expect_extra_file ? 2 : 1);
        };

    run_copy_case(z7::app::OverwriteDecision::kYes, true, false, false);
    run_copy_case(z7::app::OverwriteDecision::kNo, false, false, false);
    run_copy_case(z7::app::OverwriteDecision::kAutoRename, false, true, false);
    run_copy_case(z7::app::OverwriteDecision::kCancel, false, false, true);

    auto run_move_case =
        [](z7::app::OverwriteDecision decision,
           bool expect_replace) {
          QTemporaryDir root;
          QVERIFY2(root.isValid(), "failed to create temp dir");

          const QString source_path =
              QDir(root.path()).filePath(QStringLiteral("move.txt"));
          const QString output_dir = QDir(root.path()).filePath(QStringLiteral("out"));
          QVERIFY(QDir().mkpath(output_dir));
          const QString conflict_path =
              QDir(output_dir).filePath(QStringLiteral("move.txt"));
          write_text_file(source_path, QByteArray("new-move"));
          write_text_file(conflict_path, QByteArray("old-move"));

          int ask_count = 0;
          DelegateInteraction interaction;
          interaction.request_overwrite = [decision, &ask_count](
                                              const z7::app::OverwritePrompt&) {
            ++ask_count;
            return decision;
          };

          z7::app::MoveRequest request;
          request.source_paths = {to_std_path(source_path)};
          request.destination_dir = to_std_path(output_dir);
          request.overwrite_mode = z7::app::OverwriteMode::kAsk;
          const z7::app::MoveResult result = run_request_sync(request, interaction);
          QVERIFY(result.ok);
          QCOMPARE(ask_count, 1);
          QCOMPARE(result.moved_count,
                   expect_replace ? static_cast<size_t>(1) : static_cast<size_t>(0));
          QCOMPARE(QFileInfo::exists(source_path), !expect_replace);
          QCOMPARE(read_text_file(conflict_path),
                   expect_replace ? QByteArray("new-move") : QByteArray("old-move"));
        };

    run_move_case(z7::app::OverwriteDecision::kNo, false);
    run_move_case(z7::app::OverwriteDecision::kYes, true);
}

void AppLogicHashBehaviorTest::copyMoveAskOverwriteStickyAllDecisionsApplyToRemainingConflicts() {
    {
      QTemporaryDir root;
      QVERIFY2(root.isValid(), "failed to create temp dir");

      const QString output_dir = QDir(root.path()).filePath(QStringLiteral("copy-out"));
      QVERIFY(QDir().mkpath(output_dir));
      QStringList sources;
      for (const QString& name : {QStringLiteral("a.txt"), QStringLiteral("b.txt")}) {
        const QString source = QDir(root.path()).filePath(name);
        sources << source;
        write_text_file(source, QByteArray("new-") + name.toUtf8());
        write_text_file(QDir(output_dir).filePath(name), QByteArray("old-") + name.toUtf8());
      }

      int ask_count = 0;
      DelegateInteraction interaction;
      interaction.request_overwrite = [&ask_count](const z7::app::OverwritePrompt&) {
        ++ask_count;
        return z7::app::OverwriteDecision::kYesToAll;
      };

      z7::app::CopyRequest request;
      request.destination_dir = to_std_path(output_dir);
      request.overwrite_mode = z7::app::OverwriteMode::kAsk;
      for (const QString& source : sources) {
        request.source_paths.push_back(to_std_path(source));
      }

      const z7::app::CopyResult result = run_request_sync(request, interaction);
      QVERIFY(result.ok);
      QCOMPARE(ask_count, 1);
      QCOMPARE(result.copied_count, static_cast<size_t>(2));
      QCOMPARE(read_text_file(QDir(output_dir).filePath(QStringLiteral("a.txt"))),
               QByteArray("new-a.txt"));
      QCOMPARE(read_text_file(QDir(output_dir).filePath(QStringLiteral("b.txt"))),
               QByteArray("new-b.txt"));
    }

    {
      QTemporaryDir root;
      QVERIFY2(root.isValid(), "failed to create temp dir");

      const QString output_dir = QDir(root.path()).filePath(QStringLiteral("copy-out"));
      QVERIFY(QDir().mkpath(output_dir));
      QStringList sources;
      for (const QString& name : {QStringLiteral("a.txt"), QStringLiteral("b.txt")}) {
        const QString source = QDir(root.path()).filePath(name);
        sources << source;
        write_text_file(source, QByteArray("new-") + name.toUtf8());
        write_text_file(QDir(output_dir).filePath(name), QByteArray("old-") + name.toUtf8());
      }

      int ask_count = 0;
      DelegateInteraction interaction;
      interaction.request_overwrite = [&ask_count](const z7::app::OverwritePrompt&) {
        ++ask_count;
        return z7::app::OverwriteDecision::kNoToAll;
      };

      z7::app::CopyRequest request;
      request.destination_dir = to_std_path(output_dir);
      request.overwrite_mode = z7::app::OverwriteMode::kAsk;
      for (const QString& source : sources) {
        request.source_paths.push_back(to_std_path(source));
      }

      const z7::app::CopyResult result = run_request_sync(request, interaction);
      QVERIFY(result.ok);
      QCOMPARE(ask_count, 1);
      QCOMPARE(result.copied_count, static_cast<size_t>(0));
      for (const QString& source : sources) {
        QVERIFY(QFileInfo::exists(source));
        const QString name = QFileInfo(source).fileName();
        QCOMPARE(read_text_file(QDir(output_dir).filePath(name)),
                 QByteArray("old-") + name.toUtf8());
      }
    }

    {
      QTemporaryDir root;
      QVERIFY2(root.isValid(), "failed to create temp dir");

      const QString output_dir = QDir(root.path()).filePath(QStringLiteral("move-out"));
      QVERIFY(QDir().mkpath(output_dir));
      QStringList sources;
      for (const QString& name : {QStringLiteral("a.txt"), QStringLiteral("b.txt")}) {
        const QString source = QDir(root.path()).filePath(name);
        sources << source;
        write_text_file(source, QByteArray("new-") + name.toUtf8());
        write_text_file(QDir(output_dir).filePath(name), QByteArray("old-") + name.toUtf8());
      }

      int ask_count = 0;
      DelegateInteraction interaction;
      interaction.request_overwrite = [&ask_count](const z7::app::OverwritePrompt&) {
        ++ask_count;
        return z7::app::OverwriteDecision::kYesToAll;
      };

      z7::app::MoveRequest request;
      request.destination_dir = to_std_path(output_dir);
      request.overwrite_mode = z7::app::OverwriteMode::kAsk;
      for (const QString& source : sources) {
        request.source_paths.push_back(to_std_path(source));
      }

      const z7::app::MoveResult result = run_request_sync(request, interaction);
      QVERIFY(result.ok);
      QCOMPARE(ask_count, 1);
      QCOMPARE(result.moved_count, static_cast<size_t>(2));
      for (const QString& source : sources) {
        QVERIFY(!QFileInfo::exists(source));
        const QString name = QFileInfo(source).fileName();
        QCOMPARE(read_text_file(QDir(output_dir).filePath(name)),
                 QByteArray("new-") + name.toUtf8());
      }
    }

    {
      QTemporaryDir root;
      QVERIFY2(root.isValid(), "failed to create temp dir");

      const QString output_dir = QDir(root.path()).filePath(QStringLiteral("move-out"));
      QVERIFY(QDir().mkpath(output_dir));
      QStringList sources;
      for (const QString& name : {QStringLiteral("a.txt"), QStringLiteral("b.txt")}) {
        const QString source = QDir(root.path()).filePath(name);
        sources << source;
        write_text_file(source, QByteArray("new-") + name.toUtf8());
        write_text_file(QDir(output_dir).filePath(name), QByteArray("old-") + name.toUtf8());
      }

      int ask_count = 0;
      DelegateInteraction interaction;
      interaction.request_overwrite = [&ask_count](const z7::app::OverwritePrompt&) {
        ++ask_count;
        return z7::app::OverwriteDecision::kNoToAll;
      };

      z7::app::MoveRequest request;
      request.destination_dir = to_std_path(output_dir);
      request.overwrite_mode = z7::app::OverwriteMode::kAsk;
      for (const QString& source : sources) {
        request.source_paths.push_back(to_std_path(source));
      }

      const z7::app::MoveResult result = run_request_sync(request, interaction);
      QVERIFY(result.ok);
      QCOMPARE(ask_count, 1);
      QCOMPARE(result.moved_count, static_cast<size_t>(0));
      for (const QString& source : sources) {
        QVERIFY(QFileInfo::exists(source));
        const QString name = QFileInfo(source).fileName();
        QCOMPARE(read_text_file(QDir(output_dir).filePath(name)),
                 QByteArray("old-") + name.toUtf8());
      }
    }
}
