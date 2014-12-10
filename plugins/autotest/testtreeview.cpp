/****************************************************************************
**
** Copyright (C) 2014 Digia Plc
** All rights reserved.
** For any questions to Digia, please use contact form at http://qt.digia.com
**
** This file is part of the Qt Creator Enterprise Auto Test Add-on.
**
** Licensees holding valid Qt Enterprise licenses may use this file in
** accordance with the Qt Enterprise License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.
**
** If you have questions regarding the use of this file, please use
** contact form at http://qt.digia.com
**
****************************************************************************/

#include "autotestconstants.h"
#include "testcodeparser.h"
#include "testrunner.h"
#include "testtreeitem.h"
#include "testtreeitemdelegate.h"
#include "testtreemodel.h"
#include "testtreeview.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>

#include <coreplugin/find/itemviewfind.h>

#include <cpptools/cppmodelmanager.h>

#include <projectexplorer/project.h>
#include <projectexplorer/session.h>

#include <qmljstools/qmljsmodelmanager.h>

#include <texteditor/texteditor.h>

#include <QToolButton>
#include <QVBoxLayout>

namespace Autotest {
namespace Internal {

TestTreeViewWidget::TestTreeViewWidget(QWidget *parent) :
    QWidget(parent)
{
    setWindowTitle(tr("Tests"));
    m_model = TestTreeModel::instance();
    m_sortFilterModel = new TestTreeSortFilterModel(m_model, m_model);
    m_sortFilterModel->setDynamicSortFilter(true);
    m_view = new TestTreeView(this);
    m_view->setModel(m_sortFilterModel);
    m_view->setSortingEnabled(true);
    m_view->setItemDelegate(new TestTreeItemDelegate(this));

    QVBoxLayout *layout = new QVBoxLayout;
    layout->setMargin(0);
    layout->setSpacing(0);
    layout->addWidget(Core::ItemViewFind::createSearchableWrapper(m_view));
    setLayout(layout);

    TestCodeParser *parser = m_model->parser();
    ProjectExplorer::SessionManager *sm = ProjectExplorer::SessionManager::instance();
    connect(sm, &ProjectExplorer::SessionManager::startupProjectChanged,
            parser, &TestCodeParser::emitUpdateTestTree);

    CppTools::CppModelManager *cppMM = CppTools::CppModelManager::instance();
    connect(cppMM, &CppTools::CppModelManager::documentUpdated,
            parser, &TestCodeParser::onCppDocumentUpdated, Qt::QueuedConnection);
    connect(cppMM, &CppTools::CppModelManager::aboutToRemoveFiles,
            parser, &TestCodeParser::removeFiles, Qt::QueuedConnection);

    QmlJS::ModelManagerInterface *qmlJsMM = QmlJSTools::Internal::ModelManager::instance();
    connect(qmlJsMM, &QmlJS::ModelManagerInterface::documentUpdated,
            parser, &TestCodeParser::onQmlDocumentUpdated, Qt::QueuedConnection);
    connect(qmlJsMM, &QmlJS::ModelManagerInterface::aboutToRemoveFiles,
            parser, &TestCodeParser::removeFiles, Qt::QueuedConnection);

    connect(m_view, &TestTreeView::activated, this, &TestTreeViewWidget::onItemActivated);
}

void TestTreeViewWidget::contextMenuEvent(QContextMenuEvent *event)
{
    const bool enabled = !TestRunner::instance()->isTestRunning();
    const bool hasTests = m_model->hasTests();
    QMenu menu;
    QAction *runAll = new QAction(tr("Run All Tests"), &menu);
    QAction *runSelected = new QAction(tr("Run Selected Tests"), &menu);
    QAction *selectAll = new QAction(tr("Select All"), &menu);
    QAction *deselectAll = new QAction(tr("Deselect All"), &menu);
    // TODO remove?
    QAction *rescan = new QAction(tr("Rescan"), &menu);

    connect(runAll, &QAction::triggered, this, &TestTreeViewWidget::onRunAllTriggered);
    connect(runSelected, &QAction::triggered, this, &TestTreeViewWidget::onRunSelectedTriggered);
    connect(selectAll, &QAction::triggered, m_view, &TestTreeView::selectAll);
    connect(deselectAll, &QAction::triggered, m_view, &TestTreeView::deselectAll);
    connect(rescan, &QAction::triggered,
            TestTreeModel::instance()->parser(), &TestCodeParser::updateTestTree);

    runAll->setEnabled(enabled && hasTests);
    runSelected->setEnabled(enabled && hasTests);
    selectAll->setEnabled(enabled && hasTests);
    deselectAll->setEnabled(enabled && hasTests);
    rescan->setEnabled(enabled);

    menu.addAction(runAll);
    menu.addAction(runSelected);
    menu.addSeparator();
    menu.addAction(selectAll);
    menu.addAction(deselectAll);
    menu.addSeparator();
    menu.addAction(rescan);

    menu.exec(mapToGlobal(event->pos()));
}

QList<QToolButton *> TestTreeViewWidget::createToolButtons()
{
    QList<QToolButton *> list;

    m_filterButton = new QToolButton(m_view);
    m_filterButton->setIcon(QIcon(QLatin1String(Core::Constants::ICON_FILTER)));
    m_filterButton->setToolTip(tr("Filter Test Tree"));
    m_filterButton->setProperty("noArrow", true);
    m_filterButton->setAutoRaise(true);
    m_filterButton->setPopupMode(QToolButton::InstantPopup);
    m_filterMenu = new QMenu(m_filterButton);
    initializeFilterMenu();
    connect(m_filterMenu, &QMenu::triggered, this, &TestTreeViewWidget::onFilterMenuTriggered);
    m_filterButton->setMenu(m_filterMenu);

    m_sortAlphabetically = true;
    m_sort = new QToolButton(this);
    m_sort->setIcon((QIcon(QLatin1String(":/images/leafsort.png"))));
    m_sort->setToolTip(tr("Sort Naturally"));

    QToolButton *expand = new QToolButton(this);
    expand->setIcon(QIcon(QLatin1String(":/images/expand.png")));
    expand->setToolTip(tr("Expand All"));

    QToolButton *collapse = new QToolButton(this);
    collapse->setIcon(QIcon(QLatin1String(":/images/collapse.png")));
    collapse->setToolTip(tr("Collapse All"));

    connect(expand, &QToolButton::clicked, m_view, &TestTreeView::expandAll);
    connect(collapse, &QToolButton::clicked, m_view, &TestTreeView::collapseAll);
    connect(m_sort, &QToolButton::clicked, this, &TestTreeViewWidget::onSortClicked);

    list << m_filterButton << m_sort << expand << collapse;
    return list;
}

void TestTreeViewWidget::onItemActivated(const QModelIndex &index)
{
    const TextEditor::TextEditorWidget::Link link
            = index.data(LinkRole).value<TextEditor::TextEditorWidget::Link>();
    if (link.hasValidTarget()) {
        Core::EditorManager::openEditorAt(link.targetFileName,
                                          link.targetLine,
                                          link.targetColumn);
    }
}

void TestTreeViewWidget::onRunAllTriggered()
{
    TestRunner *runner = TestRunner::instance();
    runner->setSelectedTests(m_model->getAllTestCases());
    runner->runTests();
}

void TestTreeViewWidget::onRunSelectedTriggered()
{
    TestRunner *runner = TestRunner::instance();
    runner->setSelectedTests(m_model->getSelectedTests());
    runner->runTests();
}

void TestTreeViewWidget::onSortClicked()
{
    if (m_sortAlphabetically) {
        m_sort->setIcon((QIcon(QLatin1String(":/images/sort.png"))));
        m_sort->setToolTip(tr("Sort Alphabetically"));
        m_sortFilterModel->setSortMode(TestTreeSortFilterModel::Naturally);
    } else {
        m_sort->setIcon((QIcon(QLatin1String(":/images/leafsort.png"))));
        m_sort->setToolTip(tr("Sort Naturally"));
        m_sortFilterModel->setSortMode(TestTreeSortFilterModel::Alphabetically);
    }
    m_sortAlphabetically = !m_sortAlphabetically;
}

void TestTreeViewWidget::onFilterMenuTriggered(QAction *action)
{
    m_sortFilterModel->toggleFilter(
                TestTreeSortFilterModel::toFilterMode(action->data().value<int>()));
}

void TestTreeViewWidget::initializeFilterMenu()
{
    QAction *action = new QAction(m_filterMenu);
    action->setText(tr("Show init and cleanup functions"));
    action->setCheckable(true);
    action->setChecked(false);
    action->setData(TestTreeSortFilterModel::ShowInitAndCleanup);
    m_filterMenu->addAction(action);
    action = new QAction(m_filterMenu);
    action->setText(tr("Show data functions"));
    action->setCheckable(true);
    action->setChecked(false);
    action->setData(TestTreeSortFilterModel::ShowTestData);
    m_filterMenu->addAction(action);
}

TestViewFactory::TestViewFactory()
{
    setDisplayName(tr("Tests"));
    setId(Autotest::Constants::AUTOTEST_ID);
    setPriority(666);
}

Core::NavigationView TestViewFactory::createWidget()
{
    TestTreeViewWidget *treeViewWidget = new TestTreeViewWidget;
    Core::NavigationView view;
    view.widget = treeViewWidget;
    view.dockToolBarWidgets = treeViewWidget->createToolButtons();
    return view;
}

TestTreeView::TestTreeView(QWidget *parent)
    : NavigationTreeView(parent),
      m_context(new Core::IContext(this))
{
    setExpandsOnDoubleClick(false);
    m_context->setWidget(this);
    m_context->setContext(Core::Context(Constants::AUTOTEST_CONTEXT));
    Core::ICore::addContextObject(m_context);
}

void TestTreeView::selectAll()
{
    changeCheckStateAll(Qt::Checked);
}

void TestTreeView::deselectAll()
{
    changeCheckStateAll(Qt::Unchecked);
}

// this avoids the re-evaluation of parent nodes when modifying the child nodes (setData())
void TestTreeView::changeCheckStateAll(const Qt::CheckState checkState)
{
    const TestTreeModel *model = TestTreeModel::instance();

    // 2 == Auto Tests and Quick Tests - must be raised if there will be others
    for (int rootRow = 0; rootRow < 2; ++rootRow) {
        QModelIndex currentRootIndex = model->index(rootRow, 0, rootIndex());
        if (!currentRootIndex.isValid())
            return;
        int count = model->rowCount(currentRootIndex);
        QModelIndex last;
        for (int classesRow = 0; classesRow < count; ++classesRow) {
            const QModelIndex classesIndex = model->index(classesRow, 0, currentRootIndex);
            int funcCount = model->rowCount(classesIndex);
            TestTreeItem *item = static_cast<TestTreeItem *>(classesIndex.internalPointer());
            if (item) {
                item->setChecked(checkState);
                if (!item->childCount())
                    last = classesIndex;
            }
            for (int functionRow = 0; functionRow < funcCount; ++functionRow) {
                last = model->index(functionRow, 0, classesIndex);
                TestTreeItem *item = static_cast<TestTreeItem *>(last.internalPointer());
                if (item)
                    item->setChecked(checkState);
            }
        }
        emit dataChanged(currentRootIndex, last);
    }
}

} // namespace Internal
} // namespace Autotest
