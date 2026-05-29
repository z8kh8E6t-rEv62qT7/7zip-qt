#include <QObject>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <cstdint>

#include "shell_integration_menu.h"

namespace {
namespace shell = z7::shell_integration;

bool create_file_with_text(const QString& path,
                           const QByteArray& content = QByteArray("data")) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(content) == content.size();
}

bool has_action(const shell::ShellIntegrationMenuPlan& plan,
                const QString& action_id) {
  for (const auto& action : plan.actions) {
    if (action.action_id == action_id) {
      return true;
    }
  }
  return false;
}

QString action_title(const shell::ShellIntegrationMenuPlan& plan,
                     const QString& action_id) {
  for (const auto& action : plan.actions) {
    if (action.action_id == action_id) {
      return action.title;
    }
  }
  return {};
}

QStringList crc_sha_child_action_ids() {
  return {
      QString::fromLatin1(shell::kActionCrc32),
      QString::fromLatin1(shell::kActionCrc64),
      QString::fromLatin1(shell::kActionXxh64),
      QString::fromLatin1(shell::kActionMd5),
      QString::fromLatin1(shell::kActionSha1),
      QString::fromLatin1(shell::kActionSha256),
      QString::fromLatin1(shell::kActionSha384),
      QString::fromLatin1(shell::kActionSha512),
      QString::fromLatin1(shell::kActionSha3_256),
      QString::fromLatin1(shell::kActionBlake2sp),
      QString::fromLatin1(shell::kActionCrcAll),
      QString::fromLatin1(shell::kActionGenerateSha256),
      QString::fromLatin1(shell::kActionChecksumTest),
  };
}

shell::ShellIntegrationConfig enabled_config() {
  shell::ShellIntegrationConfig config;
  config.enabled = true;
  return config;
}

}  // namespace

class ShellIntegrationBehaviorTest final : public QObject {
  Q_OBJECT

 private slots:
  void menuPlanForArchiveContainsOpenExtractAndCrc();
  void menuPlanForTextFileHidesOpenAndExtract();
  void menuPlanForDirectoryShowsAddAndCrcOnly();
  void menuPlanForMultipleArchivesUsesWildcardExtractTarget();
  void menuPlanLocalizedTitlesComeFromJson();
  void emptyLocaleHintUsesConfiguredLanguageAndFallsBackToEnglish();
  void contextMenuActionIdsNormalizeInputInFixedOrder();
  void contextMenuActionIdsRoundTripOriginalBitmask();
  void configuredFalseUsesDefaultVisibleActions();
  void configuredEmptyVisibleActionsHidesMenu();
  void visibleActionsFilterMenuPlan();
  void validationRejectsUnavailableAction();
};

void ShellIntegrationBehaviorTest::menuPlanForArchiveContainsOpenExtractAndCrc() {
  QTemporaryDir fs_root;
  QVERIFY(fs_root.isValid());
  const QString archive_path =
      QDir(fs_root.path()).filePath(QStringLiteral("payload.7z"));
  QVERIFY(create_file_with_text(archive_path));

  shell::ShellIntegrationSelection selection;
  selection.selected_paths = {archive_path};
  selection.working_directory = fs_root.path();

  const shell::ShellIntegrationMenuPlan plan =
      shell::build_shell_integration_menu_plan(
          selection, enabled_config(), QStringLiteral("en"));
  QVERIFY(plan.menu_visible);
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionOpen)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionOpenAsMenu)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionExtractFiles)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionExtractHere)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionExtractTo)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionTestArchive)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionAddToArchive)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionCrcShaMenu)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionGenerateSha256)));
  for (const QString& action_id : crc_sha_child_action_ids()) {
    QVERIFY2(has_action(plan, action_id), qPrintable(action_id));
  }
  QVERIFY(plan.extract_subdir.endsWith(QDir::separator()));
}

void ShellIntegrationBehaviorTest::menuPlanForTextFileHidesOpenAndExtract() {
  QTemporaryDir fs_root;
  QVERIFY(fs_root.isValid());
  const QString text_path =
      QDir(fs_root.path()).filePath(QStringLiteral("notes.txt"));
  QVERIFY(create_file_with_text(text_path, "hello"));

  shell::ShellIntegrationSelection selection;
  selection.selected_paths = {text_path};
  selection.working_directory = fs_root.path();

  const shell::ShellIntegrationMenuPlan plan =
      shell::build_shell_integration_menu_plan(
          selection, enabled_config(), QStringLiteral("en"));
  QVERIFY(plan.menu_visible);
  QVERIFY(!has_action(plan, QString::fromLatin1(shell::kActionOpen)));
  QVERIFY(!has_action(plan, QString::fromLatin1(shell::kActionExtractFiles)));
  QVERIFY(!has_action(plan, QString::fromLatin1(shell::kActionTestArchive)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionAddToArchive)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionCrcShaMenu)));
}

void ShellIntegrationBehaviorTest::menuPlanForDirectoryShowsAddAndCrcOnly() {
  QTemporaryDir fs_root;
  QVERIFY(fs_root.isValid());
  const QString dir_path = QDir(fs_root.path()).filePath(QStringLiteral("folder"));
  QVERIFY(QDir().mkpath(dir_path));

  shell::ShellIntegrationSelection selection;
  selection.selected_paths = {dir_path};
  selection.working_directory = fs_root.path();

  const shell::ShellIntegrationMenuPlan plan =
      shell::build_shell_integration_menu_plan(
          selection, enabled_config(), QStringLiteral("en"));
  QVERIFY(plan.menu_visible);
  QVERIFY(!has_action(plan, QString::fromLatin1(shell::kActionOpen)));
  QVERIFY(!has_action(plan, QString::fromLatin1(shell::kActionExtractFiles)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionAddToArchive)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionCrcShaMenu)));
}

void ShellIntegrationBehaviorTest::menuPlanForMultipleArchivesUsesWildcardExtractTarget() {
  QTemporaryDir fs_root;
  QVERIFY(fs_root.isValid());
  const QString first = QDir(fs_root.path()).filePath(QStringLiteral("a.7z"));
  const QString second = QDir(fs_root.path()).filePath(QStringLiteral("b.zip"));
  QVERIFY(create_file_with_text(first));
  QVERIFY(create_file_with_text(second));

  shell::ShellIntegrationSelection selection;
  selection.selected_paths = {first, second};
  selection.working_directory = fs_root.path();

  const shell::ShellIntegrationMenuPlan plan =
      shell::build_shell_integration_menu_plan(
          selection, enabled_config(), QStringLiteral("en"));
  QVERIFY(plan.menu_visible);
  QVERIFY(!has_action(plan, QString::fromLatin1(shell::kActionOpen)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionExtractFiles)));
  QCOMPARE(plan.extract_subdir, QStringLiteral("*") + QDir::separator());
}

void ShellIntegrationBehaviorTest::menuPlanLocalizedTitlesComeFromJson() {
  QTemporaryDir fs_root;
  QVERIFY(fs_root.isValid());
  const QString archive_path =
      QDir(fs_root.path()).filePath(QStringLiteral("payload.7z"));
  QVERIFY(create_file_with_text(archive_path));

  shell::ShellIntegrationSelection selection;
  selection.selected_paths = {archive_path};
  selection.working_directory = fs_root.path();

  const shell::ShellIntegrationMenuPlan zh_plan =
      shell::build_shell_integration_menu_plan(
          selection, enabled_config(), QStringLiteral("zh-Hant-TW"));
  QVERIFY(zh_plan.menu_visible);
  QCOMPARE(action_title(zh_plan, QString::fromLatin1(shell::kActionOpen)),
           QStringLiteral("打开"));
  QCOMPARE(action_title(zh_plan, QString::fromLatin1(shell::kActionExtractFiles)),
           QStringLiteral("解压文件..."));
  QCOMPARE(action_title(zh_plan, QString::fromLatin1(shell::kActionAddToArchive)),
           QStringLiteral("添加到压缩包..."));
  QCOMPARE(action_title(zh_plan, QString::fromLatin1(shell::kActionOpenAsMenu)),
           QStringLiteral("打开方式"));

  const shell::ShellIntegrationMenuPlan en_plan =
      shell::build_shell_integration_menu_plan(
          selection, enabled_config(), QStringLiteral("en"));
  QVERIFY(en_plan.menu_visible);
  QCOMPARE(action_title(en_plan, QString::fromLatin1(shell::kActionOpen)),
           QStringLiteral("Open"));
  QCOMPARE(action_title(en_plan, QString::fromLatin1(shell::kActionOpenAsMenu)),
           QStringLiteral("Open As"));
  QCOMPARE(action_title(en_plan, QString::fromLatin1(shell::kActionExtractTo)),
           QStringLiteral("Extract To \"payload/\""));
}

void ShellIntegrationBehaviorTest::emptyLocaleHintUsesConfiguredLanguageAndFallsBackToEnglish() {
  QTemporaryDir fs_root;
  QVERIFY(fs_root.isValid());
  const QString archive_path =
      QDir(fs_root.path()).filePath(QStringLiteral("payload.7z"));
  QVERIFY(create_file_with_text(archive_path));

  shell::ShellIntegrationSelection selection;
  selection.selected_paths = {archive_path};
  selection.working_directory = fs_root.path();

  shell::ShellIntegrationConfig zh_config = enabled_config();
  zh_config.locale_preferred = QStringLiteral("zh-cn");
  const shell::ShellIntegrationMenuPlan zh_plan =
      shell::build_shell_integration_menu_plan(selection, zh_config, {});
  QVERIFY(zh_plan.menu_visible);
  QCOMPARE(action_title(zh_plan, QString::fromLatin1(shell::kActionOpen)),
           QStringLiteral("打开"));
  QCOMPARE(action_title(zh_plan, QString::fromLatin1(shell::kActionExtractFiles)),
           QStringLiteral("解压文件..."));

  shell::ShellIntegrationConfig empty_config = enabled_config();
  const shell::ShellIntegrationMenuPlan en_plan =
      shell::build_shell_integration_menu_plan(selection, empty_config, {});
  QVERIFY(en_plan.menu_visible);
  QCOMPARE(action_title(en_plan, QString::fromLatin1(shell::kActionOpen)),
           QStringLiteral("Open"));
  QCOMPARE(action_title(en_plan, QString::fromLatin1(shell::kActionExtractFiles)),
           QStringLiteral("Extract Files..."));
}

void ShellIntegrationBehaviorTest::contextMenuActionIdsNormalizeInputInFixedOrder() {
  const QStringList action_ids = shell::shell_integration_context_menu_action_ids();
  QCOMPARE(action_ids, shell::default_shell_integration_visible_actions());
  QCOMPARE(action_ids.size(), 10);
  QCOMPARE(action_ids.front(), QString::fromLatin1(shell::kActionOpen));
  QCOMPARE(action_ids.back(), QString::fromLatin1(shell::kActionCrcShaMenu));
  for (const QString& action_id : action_ids) {
    QVERIFY2(!action_id.contains(QStringLiteral("email"), Qt::CaseInsensitive),
             qPrintable(action_id));
  }

  const QStringList normalized =
      shell::normalize_shell_integration_visible_actions({
          QStringLiteral("unknown"),
          QStringLiteral("compress_email"),
          QStringLiteral("compress_to_7z_email"),
          QString::fromLatin1(shell::kActionCrcShaMenu),
          QString::fromLatin1(shell::kActionAddToArchive),
          QString::fromLatin1(shell::kActionCrcShaMenu),
          QString::fromLatin1(shell::kActionOpenAsMenu),
      });
  QCOMPARE(normalized,
           QStringList({QString::fromLatin1(shell::kActionOpenAsMenu),
                        QString::fromLatin1(shell::kActionAddToArchive),
                        QString::fromLatin1(shell::kActionCrcShaMenu)}));
}

void ShellIntegrationBehaviorTest::contextMenuActionIdsRoundTripOriginalBitmask() {
  const QStringList actions = {
      QString::fromLatin1(shell::kActionOpen),
      QString::fromLatin1(shell::kActionExtractFiles),
      QString::fromLatin1(shell::kActionAddToZip),
      QString::fromLatin1(shell::kActionCrcShaMenu),
  };
  const std::uint32_t flags =
      shell::shell_integration_context_menu_flags_from_visible_actions(actions);
  QCOMPARE(flags & (std::uint32_t{1} << 5), std::uint32_t{1} << 5);
  QCOMPARE(flags & (std::uint32_t{1} << 0), std::uint32_t{1} << 0);
  QCOMPARE(flags & (std::uint32_t{1} << 12), std::uint32_t{1} << 12);
  QCOMPARE(flags & (std::uint32_t{1} << 30), std::uint32_t{1} << 30);
  QCOMPARE(flags & (std::uint32_t{1} << 31), std::uint32_t{1} << 31);
  const std::uint32_t email_flags =
      (std::uint32_t{1} << 10) | (std::uint32_t{1} << 11) |
      (std::uint32_t{1} << 13);
  QCOMPARE(flags & email_flags, std::uint32_t{0});

  QCOMPARE(shell::shell_integration_visible_actions_from_context_menu_flags(flags),
           shell::normalize_shell_integration_visible_actions(actions));
  QCOMPARE(shell::shell_integration_visible_actions_from_context_menu_flags(
               flags | email_flags),
           shell::normalize_shell_integration_visible_actions(actions));
  QCOMPARE(shell::shell_integration_visible_actions_from_context_menu_flags(0),
           QStringList());
}

void ShellIntegrationBehaviorTest::configuredFalseUsesDefaultVisibleActions() {
  QTemporaryDir fs_root;
  QVERIFY(fs_root.isValid());
  const QString archive_path =
      QDir(fs_root.path()).filePath(QStringLiteral("payload.7z"));
  QVERIFY(create_file_with_text(archive_path));

  shell::ShellIntegrationSelection selection;
  selection.selected_paths = {archive_path};
  selection.working_directory = fs_root.path();

  shell::ShellIntegrationConfig config = enabled_config();
  config.visible_actions_configured = false;
  config.visible_actions.clear();

  const shell::ShellIntegrationMenuPlan plan =
      shell::build_shell_integration_menu_plan(
          selection, config, QStringLiteral("en"));
  QVERIFY(plan.menu_visible);
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionOpen)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionExtractFiles)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionAddToArchive)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionCrcShaMenu)));
}

void ShellIntegrationBehaviorTest::configuredEmptyVisibleActionsHidesMenu() {
  QTemporaryDir fs_root;
  QVERIFY(fs_root.isValid());
  const QString archive_path =
      QDir(fs_root.path()).filePath(QStringLiteral("payload.7z"));
  QVERIFY(create_file_with_text(archive_path));

  shell::ShellIntegrationSelection selection;
  selection.selected_paths = {archive_path};
  selection.working_directory = fs_root.path();

  shell::ShellIntegrationConfig config = enabled_config();
  config.visible_actions_configured = true;
  config.visible_actions.clear();

  const shell::ShellIntegrationMenuPlan plan =
      shell::build_shell_integration_menu_plan(
          selection, config, QStringLiteral("en"));
  QVERIFY(!plan.menu_visible);
  QVERIFY(!plan.show_open);
  QVERIFY(!plan.show_extract_group);
  QVERIFY(!plan.show_compress_group);
  QVERIFY(!plan.show_crc_group);
  QVERIFY(plan.actions.isEmpty());
}

void ShellIntegrationBehaviorTest::visibleActionsFilterMenuPlan() {
  QTemporaryDir fs_root;
  QVERIFY(fs_root.isValid());
  const QString archive_path =
      QDir(fs_root.path()).filePath(QStringLiteral("payload.7z"));
  QVERIFY(create_file_with_text(archive_path));

  shell::ShellIntegrationSelection selection;
  selection.selected_paths = {archive_path};
  selection.working_directory = fs_root.path();

  shell::ShellIntegrationConfig config = enabled_config();
  config.visible_actions_configured = true;
  config.visible_actions = {
      QString::fromLatin1(shell::kActionAddToArchive),
      QString::fromLatin1(shell::kActionCrcShaMenu),
  };

  const shell::ShellIntegrationMenuPlan plan =
      shell::build_shell_integration_menu_plan(
          selection, config, QStringLiteral("en"));
  QVERIFY(plan.menu_visible);
  QVERIFY(!has_action(plan, QString::fromLatin1(shell::kActionOpen)));
  QVERIFY(!has_action(plan, QString::fromLatin1(shell::kActionExtractFiles)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionAddToArchive)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionCrcShaMenu)));
  QVERIFY(has_action(plan, QString::fromLatin1(shell::kActionSha256)));
  for (const QString& action_id : crc_sha_child_action_ids()) {
    QVERIFY2(has_action(plan, action_id), qPrintable(action_id));
  }
  QVERIFY(!plan.show_open);
  QVERIFY(!plan.show_open_as);
  QVERIFY(!plan.show_extract_group);
  QVERIFY(plan.show_compress_group);
  QVERIFY(plan.show_crc_group);
}

void ShellIntegrationBehaviorTest::validationRejectsUnavailableAction() {
  QTemporaryDir fs_root;
  QVERIFY(fs_root.isValid());
  const QString text_path =
      QDir(fs_root.path()).filePath(QStringLiteral("notes.txt"));
  QVERIFY(create_file_with_text(text_path, "hello"));

  shell::ShellIntegrationSelection selection;
  selection.selected_paths = {text_path};
  selection.working_directory = fs_root.path();

  const shell::ShellIntegrationValidationResult result =
      shell::validate_shell_integration_action(
          QString::fromLatin1(shell::kActionExtractFiles),
          selection,
          enabled_config(),
          QStringLiteral("en"));
  QVERIFY(!result.ok);
  QVERIFY(result.error.contains(QStringLiteral("Action is not available")));
}

QTEST_MAIN(ShellIntegrationBehaviorTest)

#include "main.moc"
