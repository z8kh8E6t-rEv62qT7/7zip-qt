// tests/filemanager/behavior/runtime_options_language.cpp
// Role: Language and retranslation behavior cases for options dialog.

#include "internal.h"

#if defined(Q_OS_MAC)
#include "macos_integration_config.h"
#endif

using namespace filemanager_behavior_internal;

namespace {

class ScopedCatalogLanguageState final {
 public:
  explicit ScopedCatalogLanguageState(z7::ui::runtime_support::OfficialLangCatalog* catalog)
      : catalog_(catalog),
        active_(catalog->active_),
        current_language_id_(catalog->current_language_id_) {}

  ~ScopedCatalogLanguageState() {
    catalog_->active_ = active_;
    catalog_->current_language_id_ = current_language_id_;
  }

 private:
  z7::ui::runtime_support::OfficialLangCatalog* catalog_;
  QHash<uint32_t, QString> active_;
  QString current_language_id_;
};

class ScopedApplicationName final {
 public:
  explicit ScopedApplicationName(const QString& app_name)
      : previous_(QCoreApplication::applicationName()) {
    QCoreApplication::setApplicationName(app_name);
  }

  ~ScopedApplicationName() {
    QCoreApplication::setApplicationName(previous_);
  }

 private:
  QString previous_;
};

}  // namespace

void FileManagerBehaviorTest::optionsDialogLanguagePageShowsSummaryAndComments() {
    auto& catalog = z7::ui::runtime_support::OfficialLangCatalog::instance();
    QVERIFY(catalog.set_language_and_persist(QStringLiteral("zh-cn")));

    z7::ui::filemanager::OptionsDialog dialog;
    auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("optionsTabs"));
    auto* language_combo = dialog.findChild<QComboBox*>(QStringLiteral("languageCombo"));
    auto* language_info = dialog.findChild<QPlainTextEdit*>(QStringLiteral("languageInfoText"));
    QVERIFY(tabs != nullptr);
    QVERIFY(language_combo != nullptr);
    QVERIFY(language_info != nullptr);
    QCOMPARE(tabs->tabText(6), z7::ui::runtime_support::strip_mnemonic(z7::ui::runtime_support::L(2101)));

    const int zh_cn_index = language_combo->findData(QStringLiteral("zh-cn"), Qt::UserRole);
    QVERIFY(zh_cn_index >= 0);
    language_combo->setCurrentIndex(zh_cn_index);

    const QString text = language_info->toPlainText();
    const QRegularExpression summary_regex(
        QStringLiteral("^zh-cn\\s*:\\s*\\d+\\s*/\\s*\\d+\\s*=\\s*\\d+%"),
        QRegularExpression::MultilineOption);
    QVERIFY(summary_regex.match(text).hasMatch());
    QVERIFY(text.contains(QStringLiteral("24.05 : 2024-05-16 : MagicGenius")));
  }

void FileManagerBehaviorTest::optionsDialogLanguageSelectionPersistsAndFlagsChange() {
    auto& catalog = z7::ui::runtime_support::OfficialLangCatalog::instance();
    QVERIFY(catalog.set_language_and_persist(QStringLiteral("-")));

    z7::ui::filemanager::OptionsDialog dialog;
    auto* language_combo = dialog.findChild<QComboBox*>(QStringLiteral("languageCombo"));
    QVERIFY(language_combo != nullptr);
    const int zh_cn_index = language_combo->findData(QStringLiteral("zh-cn"), Qt::UserRole);
    QVERIFY(zh_cn_index >= 0);
    language_combo->setCurrentIndex(zh_cn_index);
    QVERIFY(!dialog.language_changed());

    QVERIFY(QMetaObject::invokeMethod(&dialog, "on_accept", Qt::DirectConnection));
    QCOMPARE(dialog.result(), QDialog::Accepted);
    QVERIFY(dialog.language_changed());

    z7::platform::qt::PortableSettings settings;
    QCOMPARE(settings.value(QStringLiteral("Lang")).toString(), QStringLiteral("zh-cn"));
    z7::platform::qt::PortableSettings shared(
        QCoreApplication::organizationName(),
        QStringLiteral("7z-shared"));
    QVERIFY(!shared.contains(QStringLiteral("Shared/Lang")));
    const QJsonObject root = read_settings_json_root();
    const QString app_name = QCoreApplication::applicationName().trimmed().isEmpty()
                                 ? QStringLiteral("7zFM")
                                 : QCoreApplication::applicationName().trimmed();
    const QJsonObject app_json =
        root.value(QStringLiteral("apps")).toObject().value(app_name).toObject();
    QCOMPARE(app_json.value(QStringLiteral("Lang")).toString(),
             QStringLiteral("zh-cn"));

    catalog.set_language(QStringLiteral("-"));
    catalog.reload_from_settings();
    QCOMPARE(catalog.current_language(), QStringLiteral("zh-cn"));

    z7::ui::filemanager::MainWindow window;
    QVERIFY(window.tools_menu_ != nullptr);
    const QString tools_menu_text = window.tools_menu_->title();
    QCOMPARE(tools_menu_text, z7::ui::runtime_support::L(504));
  }

void FileManagerBehaviorTest::optionsDialogLanguageApplySyncsMacOSIntegrationSnapshot() {
#if defined(Q_OS_MAC)
    ScopedApplicationName scoped_app_name(QStringLiteral("7zFM"));
    auto& catalog = z7::ui::runtime_support::OfficialLangCatalog::instance();
    QVERIFY(catalog.set_language_and_persist(QStringLiteral("-")));

    z7::ui::filemanager::OptionsDialog dialog;
    auto* language_combo = dialog.findChild<QComboBox*>(QStringLiteral("languageCombo"));
    QVERIFY(language_combo != nullptr);
    const int zh_cn_index = language_combo->findData(QStringLiteral("zh-cn"), Qt::UserRole);
    QVERIFY(zh_cn_index >= 0);
    language_combo->setCurrentIndex(zh_cn_index);

    QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));

    QFile snapshot_file(z7::macos_integration::macos_integration_snapshot_path());
    QVERIFY2(snapshot_file.open(QIODevice::ReadOnly), qPrintable(snapshot_file.errorString()));
    const QJsonDocument doc = QJsonDocument::fromJson(snapshot_file.readAll());
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("locale_preferred")).toString(),
             QStringLiteral("zh-cn"));

    const int english_index = language_combo->findData(QStringLiteral("-"), Qt::UserRole);
    QVERIFY(english_index >= 0);
    language_combo->setCurrentIndex(english_index);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));

    QFile english_snapshot_file(z7::macos_integration::macos_integration_snapshot_path());
    QVERIFY2(english_snapshot_file.open(QIODevice::ReadOnly),
             qPrintable(english_snapshot_file.errorString()));
    const QJsonDocument english_doc =
        QJsonDocument::fromJson(english_snapshot_file.readAll());
    QVERIFY(english_doc.isObject());
    QCOMPARE(english_doc.object().value(QStringLiteral("locale_preferred")).toString(),
             QStringLiteral("en"));
#else
    QSKIP("macOS Finder extension snapshot is only available on macOS.");
#endif
  }

void FileManagerBehaviorTest::optionsDialogApplyImmediatelyPersistsLanguageAndEmitsApplied() {
    auto& catalog = z7::ui::runtime_support::OfficialLangCatalog::instance();
    QVERIFY(catalog.set_language_and_persist(QStringLiteral("-")));

    z7::ui::filemanager::OptionsDialog dialog;
    auto* language_combo = dialog.findChild<QComboBox*>(QStringLiteral("languageCombo"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("optionsDialogButtons"));
    QVERIFY(language_combo != nullptr);
    QVERIFY(buttons != nullptr);

    QPushButton* apply_button = buttons->button(QDialogButtonBox::Apply);
    QVERIFY(apply_button != nullptr);
    QVERIFY(!apply_button->isEnabled());

    const int zh_cn_index = language_combo->findData(QStringLiteral("zh-cn"), Qt::UserRole);
    QVERIFY(zh_cn_index >= 0);
    language_combo->setCurrentIndex(zh_cn_index);
    QVERIFY(apply_button->isEnabled());

    QSignalSpy applied_spy(&dialog, SIGNAL(settings_applied()));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));
    QCOMPARE(applied_spy.count(), 1);
    QVERIFY(dialog.language_changed());
    QVERIFY(!apply_button->isEnabled());

    z7::platform::qt::PortableSettings settings;
    QCOMPARE(settings.value(QStringLiteral("Lang")).toString(), QStringLiteral("zh-cn"));
    z7::platform::qt::PortableSettings shared(
        QCoreApplication::organizationName(),
        QStringLiteral("7z-shared"));
    QVERIFY(!shared.contains(QStringLiteral("Shared/Lang")));
    QCOMPARE(catalog.current_language(), QStringLiteral("zh-cn"));
  }

void FileManagerBehaviorTest::missingSavedLanguageFallsBackToEnglishAndPersists() {
    clear_runtime_settings();

    auto& catalog = z7::ui::runtime_support::OfficialLangCatalog::instance();
    QVERIFY(catalog.set_language_and_persist(QStringLiteral("-")));

    z7::platform::qt::PortableSettings settings;
    settings.setValue(QStringLiteral("Lang"), QStringLiteral("zz-does-not-exist"));
    z7::platform::qt::PortableSettings shared(
        QCoreApplication::organizationName(),
        QStringLiteral("7z-shared"));
    shared.setValue(QStringLiteral("Shared/Lang"), QStringLiteral("zh-cn"));

    catalog.reload_from_settings();
    QCOMPARE(catalog.current_language(), QStringLiteral("-"));
    QCOMPARE(settings.value(QStringLiteral("Lang")).toString(), QStringLiteral("-"));
    QCOMPARE(shared.value(QStringLiteral("Shared/Lang")).toString(), QStringLiteral("zh-cn"));
  }

void FileManagerBehaviorTest::languageKeyMissingShowsIdPlaceholderWithoutEnglishFallback() {
    auto& catalog = z7::ui::runtime_support::OfficialLangCatalog::instance();
    QVERIFY(catalog.set_language_and_persist(QStringLiteral("-")));

    ScopedCatalogLanguageState scoped_state(&catalog);
    catalog.current_language_id_ = QStringLiteral("zz-missing-key-test");
    catalog.active_.clear();
    catalog.active_.insert(1, QStringLiteral("NoFallbackEnglish"));
    catalog.active_.insert(2, QStringLiteral("NoFallbackNative"));
    QCOMPARE(catalog.text(504), QStringLiteral("#504"));
  }

void FileManagerBehaviorTest::languageApplyImmediatelyRetranslatesMainWindow() {
    auto& catalog = z7::ui::runtime_support::OfficialLangCatalog::instance();
    QVERIFY(catalog.set_language_and_persist(QStringLiteral("-")));

    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");
    {
      QFile file(QDir(root.path()).filePath(QStringLiteral("status.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("status");
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const QString before_tools = window.tools_menu_ != nullptr
                                     ? window.tools_menu_->title()
                                     : QString();
    const QString before_status =
        window.panels_[0].ui.status_selected_count != nullptr
            ? window.panels_[0].ui.status_selected_count->text()
            : QString();

    z7::ui::filemanager::OptionsDialog dialog(&window);
    QObject::connect(&dialog,
                     &z7::ui::filemanager::OptionsDialog::settings_applied,
                     &window,
                     [&window, &dialog]() {
                       window.load_runtime_settings();
                       window.apply_runtime_settings();
                       if (dialog.language_changed()) {
                         z7::ui::runtime_support::OfficialLangCatalog::instance().reload_from_settings();
                         window.retranslate_ui();
                         window.refresh_action_states();
                       }
                     });

    auto* language_combo = dialog.findChild<QComboBox*>(QStringLiteral("languageCombo"));
    QVERIFY(language_combo != nullptr);
    const int de_index = language_combo->findData(QStringLiteral("de"), Qt::UserRole);
    QVERIFY(de_index >= 0);
    language_combo->setCurrentIndex(de_index);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "on_apply", Qt::DirectConnection));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    const QString after_tools = window.tools_menu_ != nullptr
                                    ? window.tools_menu_->title()
                                    : QString();
    QVERIFY(!before_tools.isEmpty());
    QVERIFY(!after_tools.isEmpty());
    QVERIFY(before_tools != after_tools);

    const QString after_status =
        window.panels_[0].ui.status_selected_count != nullptr
            ? window.panels_[0].ui.status_selected_count->text()
            : QString();
    QVERIFY(!before_status.isEmpty());
    QVERIFY(!after_status.isEmpty());
    QVERIFY2(before_status.contains(QStringLiteral("object(s) selected")),
             qPrintable(before_status));
    QVERIFY2(after_status.contains(QStringLiteral("Objekt(e) markiert")),
             qPrintable(after_status));
    QVERIFY2(after_status.contains(QStringLiteral("0 / 1")),
             qPrintable(after_status));
  }
