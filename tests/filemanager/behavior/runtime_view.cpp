// tests/filemanager/behavior/runtime_view.cpp
// Role: View/layout/timestamp behavior cases.

#include "internal.h"

#include <QSplitter>
#include <QStatusBar>

#include "main_window/model/model.h"

using namespace filemanager_behavior_internal;

namespace {

QString original_status_size(quint64 value) {
  const QString digits = QString::number(value);
  if (digits.size() <= 3) {
    return digits;
  }

  QString out;
  out.reserve(digits.size() + digits.size() / 3);
  const int first_group = (digits.size() % 3 == 0) ? 3 : (digits.size() % 3);
  out += digits.left(first_group);
  for (int i = first_group; i < digits.size(); i += 3) {
    out += QLatin1Char(' ');
    out += digits.mid(i, 3);
  }
  return out;
}

int operable_model_row_count(const z7::ui::filemanager::DirectoryListModel& model) {
  int count = 0;
  const int rows = model.rowCount();
  for (int row = 0; row < rows; ++row) {
    if (!model.is_parent_link_for_row(row)) {
      ++count;
    }
  }
  return count;
}

quint32 dword_at(const QByteArray& payload, int offset) {
  return decode_dword_le(payload.mid(offset, 4));
}

QString selected_status_text(int selected_count, int total_count) {
  return z7::ui::runtime_support::LF(
      3002,
      {QStringLiteral("%1 / %2")
           .arg(QString::number(selected_count),
                QString::number(total_count))});
}

int row_by_name_for_panel(const z7::ui::filemanager::MainWindow& window,
                          int panel_index,
                          const QString& name) {
  const auto& panel = window.panels_[panel_index];
  const QAbstractItemModel* model =
      panel.ui.details_view != nullptr ? panel.ui.details_view->model()
                                       : nullptr;
  if (model == nullptr) {
    return -1;
  }
  for (int row = 0; row < model->rowCount(); ++row) {
    const QModelIndex index = model->index(row, 0);
    if (model->data(index, Qt::DisplayRole).toString() == name) {
      return row;
    }
  }
  return -1;
}

bool set_current_row_by_name_for_panel(
    z7::ui::filemanager::MainWindow* window,
    int panel_index,
    const QString& name) {
  if (window == nullptr) {
    return false;
  }
  auto& panel = window->panels_[panel_index];
  if (panel.ui.details_view == nullptr ||
      panel.ui.details_view->selectionModel() == nullptr ||
      panel.ui.details_view->model() == nullptr) {
    return false;
  }
  const int row = row_by_name_for_panel(*window, panel_index, name);
  if (row < 0) {
    return false;
  }
  const QModelIndex index = panel.ui.details_view->model()->index(row, 0);
  if (!index.isValid()) {
    return false;
  }
  panel.ui.details_view->selectionModel()->setCurrentIndex(
      index,
      QItemSelectionModel::NoUpdate);
  window->refresh_action_states();
  return true;
}

}  // namespace

void FileManagerBehaviorTest::viewMenuContainsOriginalActionsAndSubmenus() {
    z7::ui::filemanager::MainWindow window;

    QVERIFY(window.view_menu_ != nullptr);
    QVERIFY(window.time_submenu_ != nullptr);
    QVERIFY(window.toolbars_submenu_ != nullptr);

    QCOMPARE(window.large_icons_action_->shortcut(), QKeySequence(QStringLiteral("Ctrl+1")));
    QCOMPARE(window.small_icons_action_->shortcut(), QKeySequence(QStringLiteral("Ctrl+2")));
    QCOMPARE(window.list_mode_action_->shortcut(), QKeySequence(QStringLiteral("Ctrl+3")));
    QCOMPARE(window.details_mode_action_->shortcut(), QKeySequence(QStringLiteral("Ctrl+4")));
    QCOMPARE(window.sort_name_action_->shortcut(), QKeySequence(QStringLiteral("Ctrl+F3")));
    QCOMPARE(window.sort_type_action_->shortcut(), QKeySequence(QStringLiteral("Ctrl+F4")));
    QCOMPARE(window.sort_date_action_->shortcut(), QKeySequence(QStringLiteral("Ctrl+F5")));
    QCOMPARE(window.sort_size_action_->shortcut(), QKeySequence(QStringLiteral("Ctrl+F6")));
    QCOMPARE(window.unsorted_action_->shortcut(), QKeySequence(QStringLiteral("Ctrl+F7")));
    QCOMPARE(window.two_panels_action_->shortcut(), QKeySequence(Qt::Key_F9));
    QCOMPARE(window.folders_history_action_->shortcut(), QKeySequence(QStringLiteral("Alt+F12")));
    QCOMPARE(window.refresh_action_->shortcut(), QKeySequence(QStringLiteral("Ctrl+R")));

    const QList<QAction*> view_actions = window.view_menu_->actions();
    QVERIFY(view_actions.size() >= 20);
    QCOMPARE(view_actions.at(0), window.large_icons_action_);
    QCOMPARE(view_actions.at(1), window.small_icons_action_);
    QCOMPARE(view_actions.at(2), window.list_mode_action_);
    QCOMPARE(view_actions.at(3), window.details_mode_action_);
    QVERIFY(view_actions.at(4)->isSeparator());
    QCOMPARE(view_actions.at(5), window.sort_name_action_);
    QCOMPARE(view_actions.at(6), window.sort_type_action_);
    QCOMPARE(view_actions.at(7), window.sort_date_action_);
    QCOMPARE(view_actions.at(8), window.sort_size_action_);
    QCOMPARE(view_actions.at(9), window.unsorted_action_);
    QVERIFY(view_actions.at(10)->isSeparator());
    QCOMPARE(view_actions.at(11), window.flat_view_action_);
    QCOMPARE(view_actions.at(12), window.two_panels_action_);
    QCOMPARE(view_actions.at(13), window.time_submenu_->menuAction());
    QCOMPARE(view_actions.at(14), window.toolbars_submenu_->menuAction());
    QCOMPARE(view_actions.at(15), window.open_root_action_);
    QCOMPARE(view_actions.at(16), window.open_parent_action_);
    QCOMPARE(view_actions.at(17), window.folders_history_action_);
    QCOMPARE(view_actions.at(18), window.refresh_action_);
    QCOMPARE(view_actions.at(19), window.auto_refresh_action_);

    const QList<QAction*> time_actions = window.time_submenu_->actions();
    QCOMPARE(time_actions.size(), 7);
    QCOMPARE(time_actions.at(0), window.time_day_action_);
    QCOMPARE(time_actions.at(1), window.time_min_action_);
    QCOMPARE(time_actions.at(2), window.time_sec_action_);
    QCOMPARE(time_actions.at(3), window.time_ntfs_action_);
    QCOMPARE(time_actions.at(4), window.time_ns_action_);
    QVERIFY(time_actions.at(5)->isSeparator());
    QCOMPARE(time_actions.at(6), window.time_utc_action_);

    const QList<QAction*> toolbar_actions = window.toolbars_submenu_->actions();
    QCOMPARE(toolbar_actions.size(), 5);
    QCOMPARE(toolbar_actions.at(0), window.archive_toolbar_action_);
    QCOMPARE(toolbar_actions.at(1), window.standard_toolbar_action_);
    QVERIFY(toolbar_actions.at(2)->isSeparator());
    QCOMPARE(toolbar_actions.at(3), window.large_buttons_action_);
    QCOMPARE(toolbar_actions.at(4), window.show_buttons_text_action_);

    const QRegularExpression day_regex(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}Z?$"));
    QVERIFY(day_regex.match(window.time_submenu_->title()).hasMatch());
  }

void FileManagerBehaviorTest::viewModeActionsSwitchActivePanelView() {
    z7::ui::filemanager::MainWindow window;
    auto& panel = window.panels_[0];
    QVERIFY(panel.ui.view_stack != nullptr);
    QVERIFY(panel.ui.details_view != nullptr);
    QVERIFY(panel.ui.icon_list_view != nullptr);

    window.large_icons_action_->trigger();
    QCOMPARE(panel.view_mode,
             z7::ui::filemanager::MainWindow::PanelController::kViewModeLargeIcons);
    QCOMPARE(panel.ui.view_stack->currentWidget(), static_cast<QWidget*>(panel.ui.icon_list_view));
    QCOMPARE(panel.ui.icon_list_view->viewMode(), QListView::IconMode);
    QCOMPARE(panel.ui.icon_list_view->iconSize(), QSize(48, 48));

    window.small_icons_action_->trigger();
    QCOMPARE(panel.view_mode,
             z7::ui::filemanager::MainWindow::PanelController::kViewModeSmallIcons);
    QCOMPARE(panel.ui.view_stack->currentWidget(), static_cast<QWidget*>(panel.ui.icon_list_view));
    QCOMPARE(panel.ui.icon_list_view->viewMode(), QListView::IconMode);
    QCOMPARE(panel.ui.icon_list_view->iconSize(), QSize(16, 16));

    window.list_mode_action_->trigger();
    QCOMPARE(panel.view_mode,
             z7::ui::filemanager::MainWindow::PanelController::kViewModeList);
    QCOMPARE(panel.ui.view_stack->currentWidget(), static_cast<QWidget*>(panel.ui.icon_list_view));
    QCOMPARE(panel.ui.icon_list_view->viewMode(), QListView::ListMode);
    QCOMPARE(panel.ui.icon_list_view->flow(), QListView::TopToBottom);

    window.details_mode_action_->trigger();
    QCOMPARE(panel.view_mode,
             z7::ui::filemanager::MainWindow::PanelController::kViewModeDetails);
    QCOMPARE(panel.ui.view_stack->currentWidget(), static_cast<QWidget*>(panel.ui.details_view));
    QVERIFY(panel.ui.details_view->model() != nullptr);
    QVERIFY(panel.ui.details_view->model()->columnCount() >= 18);
    QVERIFY(!panel.ui.details_view->isColumnHidden(0));  // Name
    QVERIFY(!panel.ui.details_view->isColumnHidden(1));  // Size
    QVERIFY(!panel.ui.details_view->isColumnHidden(3));  // Modified
    QVERIFY(!panel.ui.details_view->isColumnHidden(4));  // Created
    QVERIFY(panel.ui.details_view->isColumnHidden(2));   // Packed size (archive-only)

    QTest::keyClick(panel.ui.details_view, Qt::Key_1, Qt::ControlModifier);
    QCOMPARE(panel.view_mode,
             z7::ui::filemanager::MainWindow::PanelController::kViewModeLargeIcons);
    QCOMPARE(panel.ui.view_stack->currentWidget(), static_cast<QWidget*>(panel.ui.icon_list_view));

    QTest::keyClick(panel.ui.icon_list_view, Qt::Key_2, Qt::ControlModifier);
    QCOMPARE(panel.view_mode,
             z7::ui::filemanager::MainWindow::PanelController::kViewModeSmallIcons);
    QCOMPARE(panel.ui.view_stack->currentWidget(), static_cast<QWidget*>(panel.ui.icon_list_view));

    QTest::keyClick(panel.ui.icon_list_view, Qt::Key_3, Qt::ControlModifier);
    QCOMPARE(panel.view_mode,
             z7::ui::filemanager::MainWindow::PanelController::kViewModeList);
    QCOMPARE(panel.ui.view_stack->currentWidget(), static_cast<QWidget*>(panel.ui.icon_list_view));

    QTest::keyClick(panel.ui.icon_list_view, Qt::Key_4, Qt::ControlModifier);
    QCOMPARE(panel.view_mode,
             z7::ui::filemanager::MainWindow::PanelController::kViewModeDetails);
    QCOMPARE(panel.ui.view_stack->currentWidget(), static_cast<QWidget*>(panel.ui.details_view));
  }

void FileManagerBehaviorTest::closeShortcutClosesMainWindowFromPanelFocus() {
    z7::ui::filemanager::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.isVisible());

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_W,
                    Qt::ControlModifier);
    QTRY_VERIFY(!window.isVisible());
  }

void FileManagerBehaviorTest::detailsHeadersAreInteractiveAndPerPanelWidthsPersistWithCorruptionFallback() {
    clear_runtime_settings();

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/View/DetailsColumns/Panel0"),
                        QStringList{QStringLiteral("v2"),
                                    QStringLiteral("19"),
                                    QStringLiteral("1"),
                                    QStringLiteral("1")});
      settings.sync();
    }

    {
      z7::ui::filemanager::MainWindow window;
      window.show();
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      QVERIFY(window.panels_[0].ui.details_view != nullptr);
      QVERIFY(window.panels_[1].ui.details_view != nullptr);
      auto* header0 = window.panels_[0].ui.details_view->horizontalHeader();
      auto* header1 = window.panels_[1].ui.details_view->horizontalHeader();
      QVERIFY(header0 != nullptr);
      QVERIFY(header1 != nullptr);
      QVERIFY(header0->count() >= 18);
      QVERIFY(header1->count() >= 18);
      QVERIFY(header0->sectionsMovable());
      QVERIFY(header1->sectionsMovable());

      for (int i = 0; i < header0->count(); ++i) {
        QCOMPARE(header0->sectionResizeMode(i), QHeaderView::Interactive);
      }
      for (int i = 0; i < header1->count(); ++i) {
        QCOMPARE(header1->sectionResizeMode(i), QHeaderView::Interactive);
      }

      window.sort_size_action_->trigger();
      QVERIFY(window.sort_size_action_->isChecked());
      QCOMPARE(header0->sortIndicatorSection(),
               z7::ui::filemanager::DirectoryListModel::kSizeColumn);
      QCOMPARE(header0->sortIndicatorOrder(), Qt::DescendingOrder);

      header0->resizeSection(1, 177);
      header0->resizeSection(3, 188);
      header0->setSectionHidden(4, true);
      header0->moveSection(header0->visualIndex(3), 1);
      header1->resizeSection(0, 455);
      header1->resizeSection(1, 222);
      window.save_details_column_state();
    }

    {
      z7::platform::qt::PortableSettings settings;
      const QByteArray panel0 =
          settings.value(QStringLiteral("FM/Columns/Panel0"), QByteArray())
              .toByteArray();
      const QByteArray panel1 =
          settings.value(QStringLiteral("FM/Columns/Panel1"), QByteArray())
              .toByteArray();
      QCOMPARE(panel0.size(),
               12 + 12 * z7::ui::filemanager::DirectoryListModel::kColumnCount);
      QCOMPARE(panel1.size(), panel0.size());
      QCOMPARE(dword_at(panel0, 0), 1u);
      QCOMPARE(dword_at(panel0, 4),
               static_cast<quint32>(
                   z7::ui::filemanager::DirectoryListModel::kSizeColumn));
      QCOMPARE(dword_at(panel0, 8), 0u);
      QCOMPARE(dword_at(panel0, 12), 0u);
      QCOMPARE(dword_at(panel0, 24),
               static_cast<quint32>(
                   z7::ui::filemanager::DirectoryListModel::kModifiedColumn));
      QCOMPARE(dword_at(panel0, 32), 188u);
      QCOMPARE(dword_at(panel0, 36),
               static_cast<quint32>(
                   z7::ui::filemanager::DirectoryListModel::kSizeColumn));
      QCOMPARE(dword_at(panel0, 44), 177u);
      QVERIFY(!settings.contains(QStringLiteral("FM/View/DetailsColumns/Panel1")));
    }

    {
      z7::ui::filemanager::MainWindow restored;
      restored.show();
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      auto* header0 = restored.panels_[0].ui.details_view->horizontalHeader();
      auto* header1 = restored.panels_[1].ui.details_view->horizontalHeader();
      QCOMPARE(header0->sectionSize(1), 177);
      QCOMPARE(header0->sectionSize(3), 188);
      QVERIFY(header0->isSectionHidden(4));
      QVERIFY(!header0->isSectionHidden(0));
      QVERIFY(header0->isSectionHidden(2));
      QCOMPARE(header0->visualIndex(3), 1);
      QCOMPARE(header0->sortIndicatorSection(),
               z7::ui::filemanager::DirectoryListModel::kSizeColumn);
      QCOMPARE(header0->sortIndicatorOrder(), Qt::DescendingOrder);
      QVERIFY(restored.sort_size_action_->isChecked());
      QCOMPARE(header1->sectionSize(0), 455);
      QCOMPARE(header1->sectionSize(1), 222);
    }

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/Columns/Panel0"),
                        QByteArrayLiteral("bad"));
      settings.sync();
    }

    {
      z7::ui::filemanager::MainWindow default_window;
      default_window.show();
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
      auto* header0 = default_window.panels_[0].ui.details_view->horizontalHeader();
      auto* header1 = default_window.panels_[1].ui.details_view->horizontalHeader();
      QCOMPARE(header0->sectionSize(1), 100);
      QCOMPARE(header0->sortIndicatorSection(),
               z7::ui::filemanager::DirectoryListModel::kNameColumn);
      QVERIFY(!header0->isSectionHidden(4));
      QCOMPARE(header1->sectionSize(0), 455);
    }
  }

void FileManagerBehaviorTest::panelUiStatePersistsListModeFlatViewActivePanelAndSplitter() {
    clear_runtime_settings();
    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/View/ListMode"), 0);
      settings.setValue(QStringLiteral("FM/View/FlatViewArc0"), false);
      settings.setValue(QStringLiteral("FM/View/FlatViewArc1"), true);
      settings.setValue(QStringLiteral("FM/View/ActivePanel"), 0);
      settings.setValue(QStringLiteral("FM/View/SplitterSizes"),
                        QStringList{QStringLiteral("1"), QStringLiteral("1")});
      settings.sync();
    }

    {
      z7::ui::filemanager::MainWindow window;
      window.resize(1600, 900);
      window.show();
      QVERIFY(QTest::qWaitForWindowExposed(&window));

      window.two_panels_action_->trigger();
      QVERIFY(window.two_panels_visible_);
      QVERIFY(window.panels_[1].ui.container->isVisible());

      window.apply_view_mode_to_panel(
          0,
          z7::ui::filemanager::MainWindow::PanelController::kViewModeList);
      window.apply_view_mode_to_panel(
          1,
          z7::ui::filemanager::MainWindow::PanelController::kViewModeLargeIcons);

      window.set_active_panel(0);
      QVERIFY(!window.panels_[0].model->flat_view());
      window.flat_view_action_->trigger();
      QVERIFY(window.panels_[0].model->flat_view());
      QVERIFY(!window.panels_[1].model->flat_view());

      window.set_active_panel(1);
      QVERIFY(!window.flat_view_action_->isChecked());
      window.panels_splitter_->setSizes({520, 1040});
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

      QVERIFY(window.close());
    }

    {
      z7::platform::qt::PortableSettings settings;
      const int expected_list_mode =
          static_cast<int>(
              z7::ui::filemanager::MainWindow::PanelController::kViewModeList) |
          (static_cast<int>(
               z7::ui::filemanager::MainWindow::PanelController::kViewModeLargeIcons)
           << 8);
      QCOMPARE(settings.value(QStringLiteral("FM/ListMode")).toInt(),
               expected_list_mode);
      QCOMPARE(settings.value(QStringLiteral("FM/FlatViewArc0")).toBool(),
               true);
      QCOMPARE(settings.value(QStringLiteral("FM/FlatViewArc1")).toBool(),
               false);
      QCOMPARE(settings.value(QStringLiteral("FM/View/ListMode")).toInt(), 0);
      QCOMPARE(settings.value(QStringLiteral("FM/View/FlatViewArc0")).toBool(),
               false);
      QCOMPARE(settings.value(QStringLiteral("FM/View/FlatViewArc1")).toBool(),
               true);
      QCOMPARE(settings.value(QStringLiteral("FM/View/ActivePanel")).toInt(), 0);
      QCOMPARE(settings.value(QStringLiteral("FM/View/SplitterSizes")).toStringList(),
               (QStringList{QStringLiteral("1"), QStringLiteral("1")}));
      const QByteArray panels =
          settings.value(QStringLiteral("FM/Panels"), QByteArray()).toByteArray();
      QCOMPARE(panels.size(), 12);
      QCOMPARE(dword_at(panels, 0), 2u);
      QCOMPARE(dword_at(panels, 4), 1u);
      QVERIFY(dword_at(panels, 8) > 0u);
    }

    {
      z7::ui::filemanager::MainWindow restored;
      restored.resize(1600, 900);
      restored.show();
      QVERIFY(QTest::qWaitForWindowExposed(&restored));
      QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

      QVERIFY(restored.two_panels_visible_);
      QVERIFY(restored.panels_[1].ui.container->isVisible());
      QCOMPARE(restored.active_panel_index_, 1);
      QCOMPARE(restored.panels_[0].view_mode,
               z7::ui::filemanager::MainWindow::PanelController::kViewModeList);
      QCOMPARE(restored.panels_[1].view_mode,
               z7::ui::filemanager::MainWindow::PanelController::kViewModeLargeIcons);
      QVERIFY(restored.panels_[0].model->flat_view());
      QVERIFY(!restored.panels_[1].model->flat_view());
      QVERIFY(!restored.flat_view_action_->isChecked());

      const QList<int> restored_sizes = restored.panels_splitter_->sizes();
      QCOMPARE(restored_sizes.size(), 2);
      QVERIFY(restored_sizes.at(0) > 0);
      QVERIFY(restored_sizes.at(1) > restored_sizes.at(0));
    }
  }

void FileManagerBehaviorTest::mainWindowGeometryPersistsOriginalPositionPayload() {
    clear_runtime_settings();
    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/Window/X"), 900);
      settings.setValue(QStringLiteral("FM/Window/Y"), 901);
      settings.setValue(QStringLiteral("FM/Window/Width"), 902);
      settings.setValue(QStringLiteral("FM/Window/Height"), 903);
      settings.setValue(QStringLiteral("FM/Window/Maximized"), true);
      settings.sync();
    }

    {
      z7::ui::filemanager::MainWindow window;
      window.setGeometry(48, 64, 720, 520);
      window.save_main_window_geometry();
    }

    {
      z7::platform::qt::PortableSettings settings;
      const QByteArray position =
          settings.value(QStringLiteral("FM/Position"), QByteArray()).toByteArray();
      QCOMPARE(position.size(), 20);
      QCOMPARE(dword_at(position, 0), 48u);
      QCOMPARE(dword_at(position, 4), 64u);
      QCOMPARE(dword_at(position, 8), 768u);
      QCOMPARE(dword_at(position, 12), 584u);
      QCOMPARE(dword_at(position, 16), 0u);
      QCOMPARE(settings.value(QStringLiteral("FM/Window/X")).toInt(), 900);
      QCOMPARE(settings.value(QStringLiteral("FM/Window/Y")).toInt(), 901);
      QCOMPARE(settings.value(QStringLiteral("FM/Window/Width")).toInt(), 902);
      QCOMPARE(settings.value(QStringLiteral("FM/Window/Height")).toInt(), 903);
      QCOMPARE(settings.value(QStringLiteral("FM/Window/Maximized")).toBool(), true);
    }

    {
      z7::ui::filemanager::MainWindow restored;
      QRect const geometry = restored.geometry();
      QCOMPARE(geometry.x(), 48);
      QCOMPARE(geometry.y(), 64);
      QCOMPARE(geometry.width(), 720);
      QCOMPARE(geometry.height(), 520);
    }
  }

void FileManagerBehaviorTest::optionsDialogAssociationsTableColumnWidthsPersist() {
    clear_runtime_settings();

    {
      z7::ui::filemanager::OptionsDialog dialog;
      auto* table =
          dialog.findChild<QTableWidget*>(QStringLiteral("systemAssociationsTable"));
      QVERIFY(table != nullptr);
      auto* header = table->horizontalHeader();
      QVERIFY(header != nullptr);
      QVERIFY(!header->isHidden());
      QCOMPARE(header->sectionResizeMode(0), QHeaderView::Interactive);
      QCOMPARE(header->sectionResizeMode(1), QHeaderView::Interactive);
      QCOMPARE(header->sectionResizeMode(2), QHeaderView::Interactive);

      header->resizeSection(0, 140);
      header->resizeSection(1, 260);
      header->resizeSection(2, 320);
      dialog.close();
    }

    {
      z7::ui::filemanager::OptionsDialog restored;
      auto* table =
          restored.findChild<QTableWidget*>(QStringLiteral("systemAssociationsTable"));
      QVERIFY(table != nullptr);
      auto* header = table->horizontalHeader();
      QVERIFY(header != nullptr);
      QCOMPARE(header->sectionSize(0), 140);
      QCOMPARE(header->sectionSize(1), 260);
      QCOMPARE(header->sectionSize(2), 320);
    }

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/View/OptionsAssociationsColumns"),
                        QStringLiteral("v2|3|1,2,3"));
      settings.sync();
    }

    {
      z7::ui::filemanager::OptionsDialog default_dialog;
      auto* table =
          default_dialog.findChild<QTableWidget*>(QStringLiteral("systemAssociationsTable"));
      QVERIFY(table != nullptr);
      auto* header = table->horizontalHeader();
      QVERIFY(header != nullptr);
      QCOMPARE(header->sectionSize(0), 120);
      QCOMPARE(header->sectionSize(1), 220);
      QCOMPARE(header->sectionSize(2), 220);
    }
  }

void FileManagerBehaviorTest::checksumDialogHeaderVisibleAndColumnWidthsPersist() {
    clear_runtime_settings();

    const QVector<QPair<QString, QString>> initial_rows = {
        {QStringLiteral("VeryLongNameFieldForChecksum"), QStringLiteral("value-1")},
        {QStringLiteral("CRC32"), QStringLiteral("A1B2C3D4")}};

    {
      z7::ui::filemanager::ChecksumResultDialog dialog;
      auto* table = dialog.findChild<QTableWidget*>();
      QVERIFY(table != nullptr);
      auto* header = table->horizontalHeader();
      QVERIFY(header != nullptr);
      QVERIFY(!header->isHidden());
      QCOMPARE(header->sectionResizeMode(0), QHeaderView::Interactive);
      QCOMPARE(header->sectionResizeMode(1), QHeaderView::Interactive);

      dialog.set_rows(initial_rows);
      header->resizeSection(0, 420);
      header->resizeSection(1, 310);
      dialog.close();
    }

    {
      z7::ui::filemanager::ChecksumResultDialog restored;
      auto* table = restored.findChild<QTableWidget*>();
      QVERIFY(table != nullptr);
      auto* header = table->horizontalHeader();
      QVERIFY(header != nullptr);
      QCOMPARE(header->sectionSize(0), 420);
      QCOMPARE(header->sectionSize(1), 310);

      restored.set_rows({{QStringLiteral("N"), QStringLiteral("x")}});
      QCOMPARE(header->sectionSize(0), 420);
      QCOMPARE(header->sectionSize(1), 310);
    }

    {
      z7::platform::qt::PortableSettings settings;
      settings.setValue(QStringLiteral("FM/View/ChecksumResultColumns"),
                        QStringLiteral("v1|2|abc,1"));
      settings.sync();
    }

    {
      z7::ui::filemanager::ChecksumResultDialog default_dialog;
      auto* table = default_dialog.findChild<QTableWidget*>();
      QVERIFY(table != nullptr);
      auto* header = table->horizontalHeader();
      QVERIFY(header != nullptr);
      QCOMPARE(header->sectionSize(0), 280);
      QCOMPARE(header->sectionSize(1), 620);
    }
  }

void FileManagerBehaviorTest::sortActionsApplyAndUnsortedRestoresLoadOrder() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString f1 = QDir(root.path()).filePath(QStringLiteral("b.log"));
    const QString f2 = QDir(root.path()).filePath(QStringLiteral("a.txt"));
    const QString f3 = QDir(root.path()).filePath(QStringLiteral("c.bin"));
    {
      QFile file(f1);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("111111");
      file.close();
    }
    {
      QFile file(f2);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("22");
      file.close();
    }
    {
      QFile file(f3);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("333333333333");
      file.close();
    }

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());

    window.unsorted_action_->trigger();
    const QStringList unsorted_before = first_column_items(window);
    QVERIFY(!unsorted_before.isEmpty());

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F3,
                    Qt::ControlModifier);
    QVERIFY(window.sort_name_action_->isChecked());
    const QStringList name_sorted_by_shortcut = first_column_items(window);
    QVERIFY(!name_sorted_by_shortcut.isEmpty());

    window.sort_type_action_->trigger();
    QVERIFY(window.sort_type_action_->isChecked());
    const QStringList type_sorted = first_column_items(window);
    QVERIFY(!type_sorted.isEmpty());

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F4,
                    Qt::ControlModifier);
    QVERIFY(window.sort_type_action_->isChecked());

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F6,
                    Qt::ControlModifier);
    QVERIFY(window.sort_size_action_->isChecked());
    const QStringList size_sorted = first_column_items(window);
    QVERIFY(!size_sorted.isEmpty());

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F5,
                    Qt::ControlModifier);
    QVERIFY(window.sort_date_action_->isChecked());

    QTest::keyClick(window.active_panel_controller().ui.details_view,
                    Qt::Key_F7,
                    Qt::ControlModifier);
    QVERIFY(window.unsorted_action_->isChecked());
    const QStringList unsorted_after = first_column_items(window);
    QCOMPARE(unsorted_after, unsorted_before);

    const int selected_a_row = row_by_name(window, QStringLiteral("a.txt"));
    const int selected_b_row = row_by_name(window, QStringLiteral("b.log"));
    const int focused_row = row_by_name(window, QStringLiteral("c.bin"));
    QVERIFY(selected_a_row >= 0);
    QVERIFY(selected_b_row >= 0);
    QVERIFY(focused_row >= 0);
    select_rows_in_active_panel(&window, {selected_a_row, selected_b_row});
    QItemSelectionModel* selection =
        window.active_panel_controller().ui.details_view->selectionModel();
    QVERIFY(selection != nullptr);
    const QModelIndex focused_index =
        window.active_panel_controller().ui.details_view->model()->index(
            focused_row, 0);
    QVERIFY(focused_index.isValid());
    selection->setCurrentIndex(focused_index, QItemSelectionModel::NoUpdate);
    const QString focused_path =
        QFileInfo(QDir(root.path()).filePath(QStringLiteral("c.bin")))
            .absoluteFilePath();
    const QSet<QString> expected_selected = {
        QFileInfo(QDir(root.path()).filePath(QStringLiteral("a.txt")))
            .absoluteFilePath(),
        QFileInfo(QDir(root.path()).filePath(QStringLiteral("b.log")))
            .absoluteFilePath()};

    window.sort_size_action_->trigger();
    QCOMPARE(window.focused_path_for_panel(window.active_panel_index_),
             focused_path);
    const QStringList size_sort_selected_paths = window.selected_filesystem_paths_including_parent_link();
    QCOMPARE(QSet<QString>(size_sort_selected_paths.cbegin(),
                           size_sort_selected_paths.cend()),
             expected_selected);

    window.sort_name_action_->trigger();
    QCOMPARE(window.focused_path_for_panel(window.active_panel_index_),
             focused_path);
    const QStringList name_sort_selected_paths = window.selected_filesystem_paths_including_parent_link();
    QCOMPARE(QSet<QString>(name_sort_selected_paths.cbegin(),
                           name_sort_selected_paths.cend()),
             expected_selected);
  }

void FileManagerBehaviorTest::twoPanelsToggleAndPanelContextAreIndependent() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString left = QDir(root.path()).filePath(QStringLiteral("left"));
    const QString right = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(left));
    QVERIFY(QDir().mkpath(right));

    {
      QFile file(QDir(left).filePath(QStringLiteral("only_left.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("left");
      file.close();
    }
    {
      QFile file(QDir(right).filePath(QStringLiteral("only_right.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("right");
      file.close();
    }

    z7::ui::filemanager::MainWindow window;
    QVERIFY(!window.two_panels_visible_);
    QVERIFY(window.panels_[1].ui.container->isHidden());

    QTest::keyClick(window.panels_[0].ui.details_view, Qt::Key_F9);
    QVERIFY(window.two_panels_visible_);
    QVERIFY(!window.panels_[1].ui.container->isHidden());

    window.set_active_panel(0);
    window.set_current_directory(left);
    window.set_active_panel(1);
    window.set_current_directory(right);

    QCOMPARE(QDir(window.current_directory_for_panel(0)).absolutePath(), QDir(left).absolutePath());
    QCOMPARE(QDir(window.current_directory_for_panel(1)).absolutePath(), QDir(right).absolutePath());

    window.set_active_panel(0);
    const int left_row = row_by_name(window, QStringLiteral("only_left.txt"));
    QVERIFY(left_row >= 0);
    select_rows_in_active_panel(&window, {left_row});

    window.set_active_panel(1);
    const int right_row = row_by_name(window, QStringLiteral("only_right.txt"));
    QVERIFY(right_row >= 0);
    select_rows_in_active_panel(&window, {right_row});

    QCOMPARE(QFileInfo(window.selected_filesystem_paths_including_parent_link().value(0)).fileName(), QStringLiteral("only_right.txt"));
    QTest::keyClick(window.panels_[1].ui.details_view, Qt::Key_Backspace);
    QCOMPARE(QDir(window.current_directory()).absolutePath(), QDir(root.path()).absolutePath());
    QCOMPARE(QDir(window.current_directory_for_panel(0)).absolutePath(), QDir(left).absolutePath());

    window.set_active_panel(0);
    QCOMPARE(QFileInfo(window.selected_filesystem_paths_including_parent_link().value(0)).fileName(), QStringLiteral("only_left.txt"));

    QTest::keyClick(window.panels_[0].ui.details_view, Qt::Key_F9);
    QVERIFY(!window.two_panels_visible_);
    QVERIFY(window.panels_[1].ui.container->isHidden());
  }

void FileManagerBehaviorTest::crossPanelAltUpBindsOppositePanelToSameFilesystemFolder() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString left = QDir(root.path()).filePath(QStringLiteral("left"));
    const QString right = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(left));
    QVERIFY(QDir().mkpath(right));

    z7::ui::filemanager::MainWindow window;
    if (!window.two_panels_visible_) {
      window.two_panels_action_->trigger();
    }
    QVERIFY(window.two_panels_visible_);
    window.set_current_directory_for_panel(0, left);
    window.set_current_directory_for_panel(1, right);
    window.set_active_panel(0);

    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Up,
                    Qt::AltModifier);

    QCOMPARE(QDir(window.current_directory_for_panel(0)).absolutePath(),
             QDir(left).absolutePath());
    QCOMPARE(QDir(window.current_directory_for_panel(1)).absolutePath(),
             QDir(left).absolutePath());
    QCOMPARE(window.active_panel_index_, 0);
  }

void FileManagerBehaviorTest::crossPanelAltLeftRightBindOppositePanelToFocusedFilesystemFolder() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString left = QDir(root.path()).filePath(QStringLiteral("left"));
    const QString child = QDir(left).filePath(QStringLiteral("child"));
    const QString sibling = QDir(left).filePath(QStringLiteral("sibling"));
    const QString right = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(child));
    QVERIFY(QDir().mkpath(sibling));
    QVERIFY(QDir().mkpath(right));

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();
    if (!window.two_panels_visible_) {
      window.two_panels_action_->trigger();
    }
    QVERIFY(window.two_panels_visible_);
    window.set_current_directory_for_panel(0, left);
    window.set_current_directory_for_panel(1, right);
    window.set_active_panel(0);

    QVERIFY(set_current_row_by_name_for_panel(&window, 0, QStringLiteral("child")));
    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Right,
                    Qt::AltModifier);
    QCOMPARE(QDir(window.current_directory_for_panel(1)).absolutePath(),
             QDir(child).absolutePath());
    QCOMPARE(window.active_panel_index_, 0);

    window.set_current_directory_for_panel(1, right);
    QVERIFY(set_current_row_by_name_for_panel(&window, 0, QStringLiteral("sibling")));
    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Left,
                    Qt::AltModifier);
    QCOMPARE(QDir(window.current_directory_for_panel(1)).absolutePath(),
             QDir(sibling).absolutePath());
    QCOMPARE(window.active_panel_index_, 0);

    window.set_current_directory_for_panel(1, right);
    QVERIFY(set_current_row_by_name_for_panel(&window, 0, QStringLiteral("..")));
    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Right,
                    Qt::AltModifier);
    QCOMPARE(QDir(window.current_directory_for_panel(1)).absolutePath(),
             QDir(root.path()).absolutePath());
    QCOMPARE(window.active_panel_index_, 0);
  }

void FileManagerBehaviorTest::crossPanelAltNavigationNoOpsForOnePanelAndFocusedFile() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString left = QDir(root.path()).filePath(QStringLiteral("left"));
    const QString child = QDir(left).filePath(QStringLiteral("child"));
    const QString right = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(child));
    QVERIFY(QDir().mkpath(right));
    QFile file(QDir(left).filePath(QStringLiteral("plain.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("plain");
    file.close();

    z7::ui::filemanager::MainWindow window;
    if (!window.two_panels_visible_) {
      window.two_panels_action_->trigger();
    }
    QVERIFY(window.two_panels_visible_);
    window.set_current_directory_for_panel(0, left);
    window.set_current_directory_for_panel(1, right);
    window.set_active_panel(0);

    QVERIFY(set_current_row_by_name_for_panel(&window, 0, QStringLiteral("plain.txt")));
    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Right,
                    Qt::AltModifier);
    QCOMPARE(QDir(window.current_directory_for_panel(1)).absolutePath(),
             QDir(right).absolutePath());
    QCOMPARE(window.active_panel_index_, 0);

    window.two_panels_action_->trigger();
    QVERIFY(!window.two_panels_visible_);
    QVERIFY(set_current_row_by_name_for_panel(&window, 0, QStringLiteral("child")));
    QTest::keyClick(window.panels_[0].ui.details_view,
                    Qt::Key_Right,
                    Qt::AltModifier);
    QCOMPARE(QDir(window.current_directory_for_panel(0)).absolutePath(),
             QDir(left).absolutePath());
    QCOMPARE(window.active_panel_index_, 0);
  }

void FileManagerBehaviorTest::flatViewToggleFollowsActivePanelOnly() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString left = QDir(root.path()).filePath(QStringLiteral("left"));
    const QString right = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(left));
    QVERIFY(QDir().mkpath(right));

    {
      QFile file(QDir(left).filePath(QStringLiteral("only_left.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("left");
    }
    {
      QFile file(QDir(right).filePath(QStringLiteral("only_right.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("right");
    }

    z7::ui::filemanager::MainWindow window;
    window.two_panels_action_->trigger();
    QVERIFY(window.two_panels_visible_);
    window.set_current_directory_for_panel(0, left);
    window.set_current_directory_for_panel(1, right);

    QVERIFY(window.flat_view_action_ != nullptr);
    QVERIFY(!window.panels_[0].model->flat_view());
    QVERIFY(!window.panels_[1].model->flat_view());

    window.set_active_panel(0);
    QVERIFY(!window.flat_view_action_->isChecked());
    window.flat_view_action_->trigger();
    QVERIFY(window.panels_[0].model->flat_view());
    QVERIFY(!window.panels_[1].model->flat_view());
    QVERIFY(window.flat_view_action_->isChecked());

    window.set_active_panel(1);
    QVERIFY(!window.flat_view_action_->isChecked());
    window.flat_view_action_->trigger();
    QVERIFY(window.panels_[0].model->flat_view());
    QVERIFY(window.panels_[1].model->flat_view());
    QVERIFY(window.flat_view_action_->isChecked());

    window.set_active_panel(0);
    QVERIFY(window.flat_view_action_->isChecked());
    window.flat_view_action_->trigger();
    QVERIFY(!window.panels_[0].model->flat_view());
    QVERIFY(window.panels_[1].model->flat_view());
    QVERIFY(!window.flat_view_action_->isChecked());
}

void FileManagerBehaviorTest::panelStatusBarsTrackSelectionFocusAndTransientMessages() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString left = QDir(root.path()).filePath(QStringLiteral("left"));
    const QString right = QDir(root.path()).filePath(QStringLiteral("right"));
    QVERIFY(QDir().mkpath(left));
    QVERIFY(QDir().mkpath(right));

    {
      QFile file(QDir(left).filePath(QStringLiteral("a.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("12345");
    }
    {
      QFile file(QDir(left).filePath(QStringLiteral("b.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("1234567");
    }
    QVERIFY(QDir().mkpath(QDir(left).filePath(QStringLiteral("folder"))));
    {
      QFile file(QDir(left).filePath(QStringLiteral("large.bin")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.resize(1234567);
    }
    {
      QFile file(QDir(right).filePath(QStringLiteral("right.txt")));
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write("right");
    }

    z7::ui::filemanager::MainWindow window;
    window.display_settings_.show_dots = true;
    window.apply_runtime_settings();
    QVERIFY(window.panels_[0].ui.status_bar != nullptr);
    QVERIFY(window.panels_[0].ui.status_selected_count != nullptr);
    QVERIFY(window.panels_[0].ui.status_selected_size != nullptr);
    QVERIFY(window.panels_[0].ui.status_focused_size != nullptr);
    QVERIFY(window.panels_[0].ui.status_focused_modified != nullptr);
    QVERIFY(window.panels_[0].ui.status_transient_message != nullptr);
    QVERIFY(window.panels_[1].ui.status_transient_message != nullptr);
    QCOMPARE(window.panels_[0].ui.status_transient_message->text(), QString());
    QCOMPARE(window.panels_[1].ui.status_transient_message->text(), QString());
    QCOMPARE(window.findChildren<QStatusBar*>().size(), 2);
    QVERIFY(window.findChildren<QStatusBar*>(
                QString(), Qt::FindDirectChildrenOnly).isEmpty());

    window.set_active_panel(0);
    window.set_current_directory(left);
    auto& left_panel = window.panels_[0];
    QVERIFY(left_panel.model != nullptr);
    const int left_operable_rows = operable_model_row_count(*left_panel.model);
    QVERIFY(left_operable_rows >= 4);
    QCOMPARE(left_panel.model->rowCount(), left_operable_rows + 1);
    QVERIFY(row_by_name(window, QStringLiteral("..")) >= 0);
    QCOMPARE(left_panel.ui.status_selected_count->text(),
             selected_status_text(0, left_operable_rows));
    QCOMPARE(left_panel.ui.status_transient_message->text(), QString());
    QVERIFY(window.findChildren<QStatusBar*>(
                QString(), Qt::FindDirectChildrenOnly).isEmpty());

    const int a_row = row_by_name(window, QStringLiteral("a.txt"));
    const int b_row = row_by_name(window, QStringLiteral("b.txt"));
    const int folder_row = row_by_name(window, QStringLiteral("folder"));
    const int large_row = row_by_name(window, QStringLiteral("large.bin"));
    const int parent_row = row_by_name(window, QStringLiteral(".."));
    QVERIFY(a_row >= 0);
    QVERIFY(b_row >= 0);
    QVERIFY(folder_row >= 0);
    QVERIFY(large_row >= 0);
    QVERIFY(parent_row >= 0);

    {
      QItemSelectionModel* selection =
          left_panel.ui.details_view->selectionModel();
      QVERIFY(selection != nullptr);
      selection->clearSelection();
      QModelIndex last_selected;
      for (const int row : {a_row, b_row}) {
        const QModelIndex index =
            left_panel.ui.details_view->model()->index(row, 0);
        QVERIFY(index.isValid());
        selection->select(index,
                          QItemSelectionModel::Select |
                              QItemSelectionModel::Rows);
        last_selected = index;
      }
      selection->setCurrentIndex(last_selected, QItemSelectionModel::NoUpdate);
    }
    QCOMPARE(left_panel.ui.status_selected_count->text(),
             selected_status_text(2, left_operable_rows));
    QCOMPARE(left_panel.ui.status_selected_size->text(), QStringLiteral("12"));

    QItemSelectionModel* left_selection =
        left_panel.ui.details_view->selectionModel();
    QVERIFY(left_selection != nullptr);
    left_selection->clearSelection();
    const QModelIndex folder_index = left_panel.ui.details_view->model()->index(folder_row, 0);
    QVERIFY(folder_index.isValid());
    left_selection->setCurrentIndex(folder_index, QItemSelectionModel::NoUpdate);
    QCOMPARE(left_panel.ui.status_selected_count->text(),
             selected_status_text(0, left_operable_rows));
    QCOMPARE(left_panel.ui.status_selected_size->text(), QString());
    QCOMPARE(left_panel.ui.status_focused_size->text(), QString());
    QCOMPARE(left_panel.ui.status_focused_modified->text(), QString());

    const QModelIndex a_index = left_panel.ui.details_view->model()->index(a_row, 0);
    QVERIFY(a_index.isValid());
    left_selection->setCurrentIndex(a_index, QItemSelectionModel::NoUpdate);
    QCOMPARE(left_panel.ui.status_focused_size->text(), QString());
    QCOMPARE(left_panel.ui.status_focused_modified->text(), QString());

    const QModelIndex parent_index = left_panel.ui.details_view->model()->index(parent_row, 0);
    QVERIFY(parent_index.isValid());
    left_selection->select(parent_index,
                           QItemSelectionModel::ClearAndSelect |
                               QItemSelectionModel::Rows);
    left_selection->setCurrentIndex(parent_index, QItemSelectionModel::NoUpdate);
    QCOMPARE(left_panel.ui.status_selected_count->text(),
             selected_status_text(0, left_operable_rows));
    QCOMPARE(left_panel.ui.status_selected_size->text(), QString());
    QCOMPARE(left_panel.ui.status_focused_size->text(), QString());
    QCOMPARE(left_panel.ui.status_focused_modified->text(), QString());
    left_selection->setCurrentIndex(a_index, QItemSelectionModel::NoUpdate);
    QCOMPARE(left_panel.ui.status_selected_count->text(),
             selected_status_text(0, left_operable_rows));
    QCOMPARE(left_panel.ui.status_selected_size->text(), QString());
    QCOMPARE(left_panel.ui.status_focused_size->text(), QStringLiteral("5"));
    QVERIFY(!left_panel.ui.status_focused_modified->text().isEmpty());

    left_selection->select(folder_index,
                           QItemSelectionModel::ClearAndSelect |
                               QItemSelectionModel::Rows);
    left_selection->setCurrentIndex(folder_index, QItemSelectionModel::NoUpdate);
    QCOMPARE(left_panel.ui.status_selected_count->text(),
             selected_status_text(1, left_operable_rows));
    QCOMPARE(left_panel.ui.status_selected_size->text(), QStringLiteral("0"));
    QCOMPARE(left_panel.ui.status_focused_size->text(), QStringLiteral("0"));
    QVERIFY(!left_panel.ui.status_focused_modified->text().isEmpty());

    const QModelIndex large_index = left_panel.ui.details_view->model()->index(large_row, 0);
    QVERIFY(large_index.isValid());
    left_selection->select(large_index,
                           QItemSelectionModel::ClearAndSelect |
                               QItemSelectionModel::Rows);
    left_selection->setCurrentIndex(large_index, QItemSelectionModel::NoUpdate);
    QCOMPARE(left_panel.ui.status_selected_count->text(),
             selected_status_text(1, left_operable_rows));
    QCOMPARE(left_panel.ui.status_selected_size->text(), original_status_size(1234567));
    QCOMPARE(left_panel.ui.status_focused_size->text(), original_status_size(1234567));
    QVERIFY(!left_panel.ui.status_focused_modified->text().isEmpty());

    left_selection->clearSelection();
    left_selection->setCurrentIndex(a_index, QItemSelectionModel::NoUpdate);
    QCOMPARE(left_panel.ui.status_selected_count->text(),
             selected_status_text(0, left_operable_rows));
    QCOMPARE(left_panel.ui.status_selected_size->text(), QString());
    QCOMPARE(left_panel.ui.status_focused_size->text(), QString());
    QCOMPARE(left_panel.ui.status_focused_modified->text(), QString());

    window.show_transient_status_message(QStringLiteral("temporary task message"),
                                         5000);
    QCOMPARE(left_panel.ui.status_transient_message->text(),
             QStringLiteral("temporary task message"));
    QCOMPARE(left_panel.ui.status_selected_count->text(),
             selected_status_text(0, left_operable_rows));
    QCOMPARE(left_panel.ui.status_focused_size->text(), QString());
    QCOMPARE(window.findChildren<QStatusBar*>().size(), 2);
    QVERIFY(window.findChildren<QStatusBar*>(
                QString(), Qt::FindDirectChildrenOnly).isEmpty());
    window.show_transient_status_message(QStringLiteral("short task message"),
                                         1);
    QTRY_COMPARE(left_panel.ui.status_transient_message->text(), QString());
    QCOMPARE(left_panel.ui.status_selected_count->text(),
             selected_status_text(0, left_operable_rows));
    QCOMPARE(left_panel.ui.status_focused_size->text(), QString());

    QTest::keyClick(left_panel.ui.details_view, Qt::Key_F9);
    QVERIFY(window.two_panels_visible_);
    window.set_active_panel(1);
    window.set_current_directory(right);
    auto& right_panel = window.panels_[1];
    QVERIFY(right_panel.model != nullptr);
    const int right_operable_rows = operable_model_row_count(*right_panel.model);
    QVERIFY(right_operable_rows >= 1);
    QCOMPARE(right_panel.model->rowCount(), right_operable_rows + 1);
    QCOMPARE(right_panel.ui.status_selected_count->text(),
             selected_status_text(0, right_operable_rows));

    const int right_row = row_by_name(window, QStringLiteral("right.txt"));
    QVERIFY(right_row >= 0);
    select_rows_in_active_panel(&window, {right_row});
    QCOMPARE(right_panel.ui.status_selected_count->text(),
             selected_status_text(1, right_operable_rows));
    QCOMPARE(right_panel.ui.status_selected_size->text(), QStringLiteral("5"));
    window.show_transient_status_message(QStringLiteral("right task message"),
                                         5000);
    QCOMPARE(left_panel.ui.status_transient_message->text(), QString());
    QCOMPARE(right_panel.ui.status_transient_message->text(),
             QStringLiteral("right task message"));

    window.set_active_panel(0);
    QCOMPARE(left_panel.ui.status_selected_count->text(),
             selected_status_text(0, left_operable_rows));
    QCOMPARE(left_panel.ui.status_focused_size->text(), QString());
    window.set_active_panel(1);
    QCOMPARE(right_panel.ui.status_selected_count->text(),
             selected_status_text(1, right_operable_rows));
  }

void FileManagerBehaviorTest::timeSubmenuUpdatesPrecisionAndUtcDisplay() {
    QTemporaryDir root;
    QVERIFY2(root.isValid(), "failed to create temp dir");

    const QString file_path = QDir(root.path()).filePath(QStringLiteral("clock.txt"));
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("clock");
    file.close();

    z7::ui::filemanager::MainWindow window;
    window.set_current_directory(root.path());
    const int row = row_by_name(window, QStringLiteral("clock.txt"));
    QVERIFY(row >= 0);
    QVERIFY(window.active_panel_controller().ui.details_view != nullptr);
    QVERIFY(window.active_panel_controller().ui.details_view->model() != nullptr);
    const QAbstractItemModel* model = window.active_panel_controller().ui.details_view->model();

    const QModelIndex modified_idx = model->index(row, 3);
    const QModelIndex created_idx = model->index(row, 4);
    QVERIFY(modified_idx.isValid());
    QVERIFY(created_idx.isValid());
    select_rows_in_active_panel(&window, {row});
    auto& panel = window.panels_[0];
    QVERIFY(panel.ui.status_focused_modified != nullptr);

    window.time_day_action_->trigger();
    const QString day_modified = model->data(modified_idx, Qt::DisplayRole).toString();
    const QRegularExpression day_regex(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}Z?$"));
    QVERIFY(day_regex.match(day_modified).hasMatch());
    QCOMPARE(panel.ui.status_focused_modified->text(), day_modified);

    window.time_sec_action_->trigger();
    const QString sec_modified = model->data(modified_idx, Qt::DisplayRole).toString();
    const QRegularExpression sec_regex(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}Z?$"));
    QVERIFY(sec_regex.match(sec_modified).hasMatch());
    QCOMPARE(panel.ui.status_focused_modified->text(), sec_modified);

    window.time_utc_action_->trigger();
    const QString utc_modified = model->data(modified_idx, Qt::DisplayRole).toString();
    const QString utc_created = model->data(created_idx, Qt::DisplayRole).toString();
    QVERIFY(utc_modified.endsWith(QStringLiteral("Z")));
    QVERIFY(utc_created.endsWith(QStringLiteral("Z")));
    QCOMPARE(panel.ui.status_focused_modified->text(), utc_modified);

    window.time_utc_action_->trigger();
    const QString local_modified = model->data(modified_idx, Qt::DisplayRole).toString();
    QVERIFY(!local_modified.endsWith(QStringLiteral("Z")));
    QCOMPARE(panel.ui.status_focused_modified->text(), local_modified);

    QVERIFY(window.time_utc_action_ != nullptr);
    QVERIFY(!window.time_utc_action_->isChecked());
    QVERIFY(window.time_sec_action_->isChecked());
    QVERIFY(!window.time_day_action_->isChecked());
    QVERIFY(!window.time_min_action_->isChecked());
    QVERIFY(!window.time_ntfs_action_->isChecked());
    QVERIFY(!window.time_ns_action_->isChecked());

    window.time_ns_action_->trigger();
    QVERIFY(window.time_ns_action_->isChecked());
    QVERIFY(!window.time_sec_action_->isChecked());

    window.time_utc_action_->trigger();
    QVERIFY(window.time_utc_action_->isChecked());
    QVERIFY(window.time_ns_action_->isChecked());
  }

// End of runtime_view.cpp
