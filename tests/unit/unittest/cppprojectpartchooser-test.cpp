/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "googletest.h"

#include <cpptools/cppprojectpartchooser.h>
#include <cpptools/cpptools_utils.h>
#include <cpptools/projectpart.h>

using CppTools::Internal::ProjectPartChooser;
using CppTools::ProjectPart;
using CppTools::ProjectPartInfo;
using CppTools::Language;

using testing::Eq;

namespace {

class ProjectPartChooser : public ::testing::Test
{
protected:
    void SetUp() override;
    const ProjectPartInfo choose() const;

    static QList<ProjectPart::Ptr> createProjectPartsWithDifferentProjects();
    static QList<ProjectPart::Ptr> createCAndCxxProjectParts();

protected:
    QString filePath;
    ProjectPart::Ptr currentProjectPart{new ProjectPart};
    ProjectPart::Ptr manuallySetProjectPart;
    bool stickToPreviousProjectPart = false;
    const ProjectExplorer::Project *activeProject = nullptr;
    bool projectHasChanged = false;
    Language languagePreference = Language::Cxx;
    ::ProjectPartChooser chooser;

    QList<ProjectPart::Ptr> projectPartsForFile;
    QList<ProjectPart::Ptr> projectPartsFromDependenciesForFile;
    ProjectPart::Ptr fallbackProjectPart;
};

TEST_F(ProjectPartChooser, ChooseManuallySet)
{
    manuallySetProjectPart.reset(new ProjectPart);

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(manuallySetProjectPart));
}

TEST_F(ProjectPartChooser, ForMultipleChoosePrevious)
{
    const ProjectPart::Ptr otherProjectPart;
    projectPartsForFile += otherProjectPart;
    projectPartsForFile += currentProjectPart;

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(currentProjectPart));
}

TEST_F(ProjectPartChooser, ForMultipleChooseFromActiveProject)
{
    const QList<ProjectPart::Ptr> projectParts = createProjectPartsWithDifferentProjects();
    const ProjectPart::Ptr secondProjectPart = projectParts.at(1);
    projectPartsForFile += projectParts;
    activeProject = secondProjectPart->project;

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(secondProjectPart));
}

TEST_F(ProjectPartChooser, ForMultiplePreferSelectedForBuilding)
{
    const ProjectPart::Ptr firstProjectPart{new ProjectPart};
    const ProjectPart::Ptr secondProjectPart{new ProjectPart};
    firstProjectPart->selectedForBuilding = false;
    secondProjectPart->selectedForBuilding = true;
    projectPartsForFile += firstProjectPart;
    projectPartsForFile += secondProjectPart;

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(secondProjectPart));
}

TEST_F(ProjectPartChooser, ForMultipleFromDependenciesChooseFromActiveProject)
{
    const QList<ProjectPart::Ptr> projectParts = createProjectPartsWithDifferentProjects();
    const ProjectPart::Ptr secondProjectPart = projectParts.at(1);
    projectPartsFromDependenciesForFile += projectParts;
    activeProject = secondProjectPart->project;

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(secondProjectPart));
}

TEST_F(ProjectPartChooser, ForMultipleCheckIfActiveProjectChanged)
{
    const QList<ProjectPart::Ptr> projectParts = createProjectPartsWithDifferentProjects();
    const ProjectPart::Ptr firstProjectPart = projectParts.at(0);
    const ProjectPart::Ptr secondProjectPart = projectParts.at(1);
    projectPartsForFile += projectParts;
    currentProjectPart = firstProjectPart;
    activeProject = secondProjectPart->project;
    projectHasChanged = true;

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(secondProjectPart));
}

TEST_F(ProjectPartChooser, ForMultipleAndAmbigiousHeaderPreferCProjectPart)
{
    languagePreference = Language::C;
    projectPartsForFile = createCAndCxxProjectParts();
    const ProjectPart::Ptr cProjectPart = projectPartsForFile.at(0);

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(cProjectPart));
}

TEST_F(ProjectPartChooser, ForMultipleAndAmbigiousHeaderPreferCxxProjectPart)
{
    languagePreference = Language::Cxx;
    projectPartsForFile = createCAndCxxProjectParts();
    const ProjectPart::Ptr cxxProjectPart = projectPartsForFile.at(1);

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(cxxProjectPart));
}

TEST_F(ProjectPartChooser, IndicateMultiple)
{
    const ProjectPart::Ptr p1{new ProjectPart};
    const ProjectPart::Ptr p2{new ProjectPart};
    projectPartsForFile += { p1, p2 };

    const ProjectPartInfo::Hint hint = choose().hint;

    ASSERT_THAT(hint, Eq(ProjectPartInfo::Hint::IsAmbiguousMatch));
}

TEST_F(ProjectPartChooser, IfProjectIsGoneStickToPrevious) // Built-in Code Model
{
    stickToPreviousProjectPart = true;

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(currentProjectPart));
}

TEST_F(ProjectPartChooser, IfProjectIsGoneDoNotStickToPrevious) // Clang Code Model
{
    currentProjectPart.clear();
    stickToPreviousProjectPart = true;

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(ProjectPart::Ptr()));
}

TEST_F(ProjectPartChooser, ForMultipleChooseNewIfPreviousIsGone)
{
    const ProjectPart::Ptr newProjectPart{new ProjectPart};
    projectPartsForFile += newProjectPart;

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(newProjectPart));
}

TEST_F(ProjectPartChooser, FallbackToProjectPartFromDependencies)
{
    const ProjectPart::Ptr fromDependencies{new ProjectPart};
    projectPartsFromDependenciesForFile += fromDependencies;

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(fromDependencies));
}

TEST_F(ProjectPartChooser, FallbackToProjectPartFromModelManager)
{
    fallbackProjectPart.reset(new ProjectPart);

    const ProjectPart::Ptr chosen = choose().projectPart;

    ASSERT_THAT(chosen, Eq(fallbackProjectPart));
}

TEST_F(ProjectPartChooser, IndicateFallbacktoProjectPartFromModelManager)
{
    fallbackProjectPart.reset(new ProjectPart);

    const ProjectPartInfo::Hint hint = choose().hint;

    ASSERT_THAT(hint, Eq(ProjectPartInfo::Hint::IsFallbackMatch));
}

void ProjectPartChooser::SetUp()
{
    chooser.setFallbackProjectPart([&](){
        return fallbackProjectPart;
    });
    chooser.setProjectPartsForFile([&](const QString &) {
        return projectPartsForFile;
    });
    chooser.setProjectPartsFromDependenciesForFile([&](const QString &) {
        return projectPartsFromDependenciesForFile;
    });
}

const ProjectPartInfo ProjectPartChooser::choose() const
{
    return chooser.choose(filePath,
                          currentProjectPart,
                          manuallySetProjectPart,
                          stickToPreviousProjectPart,
                          activeProject,
                          languagePreference,
                          projectHasChanged);
}

QList<ProjectPart::Ptr> ProjectPartChooser::createProjectPartsWithDifferentProjects()
{
    QList<ProjectPart::Ptr> projectParts;

    const ProjectPart::Ptr p1{new ProjectPart};
    p1->project = reinterpret_cast<ProjectExplorer::Project *>(1 << 0);
    projectParts.append(p1);
    const ProjectPart::Ptr p2{new ProjectPart};
    p2->project = reinterpret_cast<ProjectExplorer::Project *>(1 << 1);
    projectParts.append(p2);

    return projectParts;
}

QList<ProjectPart::Ptr> ProjectPartChooser::createCAndCxxProjectParts()
{
    QList<ProjectPart::Ptr> projectParts;

    // Create project part for C
    const ProjectPart::Ptr cprojectpart{new ProjectPart};
    cprojectpart->languageVersion = ProjectPart::C11;
    projectParts.append(cprojectpart);

    // Create project part for CXX
    const ProjectPart::Ptr cxxprojectpart{new ProjectPart};
    cxxprojectpart->languageVersion = ProjectPart::CXX98;
    projectParts.append(cxxprojectpart);

    return projectParts;
}

} // anonymous namespace
