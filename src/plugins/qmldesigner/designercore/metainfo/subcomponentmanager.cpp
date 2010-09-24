/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "subcomponentmanager.h"
#include "metainfo.h"

#include <QDir>
#include <QMetaType>
#include <QUrl>
#include <QDeclarativeEngine>
#include <private/qdeclarativemetatype_p.h>
#include <QFileSystemWatcher>
#include <private/qdeclarativedom_p.h>

enum { debug = false };

QT_BEGIN_NAMESPACE

// Allow usage of QFileInfo in qSort

static bool operator<(const QFileInfo &file1, const QFileInfo &file2)
{
    return file1.filePath() < file2.filePath();
}


QT_END_NAMESPACE

static inline QStringList importPaths() {
    QStringList paths;

    // env import paths
    QByteArray envImportPath = qgetenv("QML_IMPORT_PATH");
    if (!envImportPath.isEmpty()) {
#if defined(Q_OS_WIN) || defined(Q_OS_SYMBIAN)
        QLatin1Char pathSep(';');
#else
        QLatin1Char pathSep(':');
#endif
        paths = QString::fromLatin1(envImportPath).split(pathSep, QString::SkipEmptyParts);
    }

    return paths;
}

namespace QmlDesigner {

namespace Internal {

static const QString QMLFILEPATTERN = QString(QLatin1String("*.qml"));


class SubComponentManagerPrivate : QObject {
    Q_OBJECT
public:
    SubComponentManagerPrivate(MetaInfo metaInfo, SubComponentManager *q);

    void addImport(int pos, const QDeclarativeDomImport &import);
    void removeImport(int pos);
    void parseDirectories();

public slots:
    void parseDirectory(const QString &canonicalDirPath,  bool addToLibrary = true, const QString& qualification = QString());
    void parseFile(const QString &canonicalFilePath,  bool addToLibrary, const QString&);
    void parseFile(const QString &canonicalFilePath);

public:
    QList<QFileInfo> watchedFiles(const QString &canonicalDirPath);
    void unregisterQmlFile(const QFileInfo &fileInfo, const QString &qualifier);
    void registerQmlFile(const QFileInfo &fileInfo, const QString &qualifier, const QDeclarativeDomDocument &document,  bool addToLibrary);

    SubComponentManager *m_q;

    MetaInfo m_metaInfo;
    QDeclarativeEngine m_engine;

    QFileSystemWatcher m_watcher;

    // key: canonical directory path
    QMultiHash<QString,QString> m_dirToQualifier;

    QUrl m_filePath;

    QList<QDeclarativeDomImport> m_imports;
};

SubComponentManagerPrivate::SubComponentManagerPrivate(MetaInfo metaInfo, SubComponentManager *q) :
        m_q(q),
        m_metaInfo(metaInfo)
{
    connect(&m_watcher, SIGNAL(directoryChanged(QString)), this, SLOT(parseDirectory(QString)));
    connect(&m_watcher, SIGNAL(fileChanged(QString)), this, SLOT(parseFile(QString)));
}

void SubComponentManagerPrivate::addImport(int pos, const QDeclarativeDomImport &import)
{
    if (debug)
        qDebug() << Q_FUNC_INFO << pos << import.uri();

    if (import.type() == QDeclarativeDomImport::File) {
        QFileInfo dirInfo = QFileInfo(m_filePath.resolved(import.uri()).toLocalFile());
        if (dirInfo.exists() && dirInfo.isDir()) {
            const QString canonicalDirPath = dirInfo.canonicalFilePath();
            m_watcher.addPath(canonicalDirPath);
            m_dirToQualifier.insertMulti(canonicalDirPath, import.qualifier());
        }
    } else {
        QString url = import.uri();
        
        url.replace(QLatin1Char('.'), QLatin1Char('/'));

        foreach(const QString path, importPaths()) {
            url  = path + QLatin1String("/") + url;
            QFileInfo dirInfo = QFileInfo(url);
            if (dirInfo.exists() && dirInfo.isDir()) {
                const QString canonicalDirPath = dirInfo.canonicalFilePath();
                m_watcher.addPath(canonicalDirPath);
                m_dirToQualifier.insertMulti(canonicalDirPath, import.qualifier());
            }
        }
        // TODO: QDeclarativeDomImport::Library
    }

    m_imports.insert(pos, import);
}

void SubComponentManagerPrivate::removeImport(int pos)
{
    const QDeclarativeDomImport import = m_imports.takeAt(pos);

    if (import.type() == QDeclarativeDomImport::File) {
        const QFileInfo dirInfo = QFileInfo(m_filePath.resolved(import.uri()).toLocalFile());
        const QString canonicalDirPath = dirInfo.canonicalFilePath();

        m_dirToQualifier.remove(canonicalDirPath, import.qualifier());

        if (!m_dirToQualifier.contains(canonicalDirPath))
            m_watcher.removePath(canonicalDirPath);

        foreach (const QFileInfo &monitoredFile, watchedFiles(canonicalDirPath)) {
            if (!m_dirToQualifier.contains(canonicalDirPath))
                m_watcher.removePath(monitoredFile.filePath());
            unregisterQmlFile(monitoredFile, import.qualifier());
        }
    } else {
            // TODO: QDeclarativeDomImport::Library
    }
}

void SubComponentManagerPrivate::parseDirectories()
{
    if (!m_filePath.isEmpty()) {
        const QString file = m_filePath.toLocalFile();
        QFileInfo dirInfo = QFileInfo(QFileInfo(file).path());
        if (dirInfo.exists() && dirInfo.isDir())
            parseDirectory(dirInfo.canonicalFilePath());
    }

    foreach (const QDeclarativeDomImport &import, m_imports) {
        if (import.type() == QDeclarativeDomImport::File) {
            QFileInfo dirInfo = QFileInfo(m_filePath.resolved(import.uri()).toLocalFile());
            if (dirInfo.exists() && dirInfo.isDir()) {
                parseDirectory(dirInfo.canonicalFilePath());
            }
        } else {
            QString url = import.uri();
            foreach(const QString path, importPaths()) {
                url.replace(QLatin1Char('.'), QLatin1Char('/'));
                url  = path + QLatin1String("/") + url;
                QFileInfo dirInfo = QFileInfo(url);
                if (dirInfo.exists() && dirInfo.isDir()) {
                    //### todo full qualified names QString nameSpace = import.uri();
                    parseDirectory(dirInfo.canonicalFilePath(), false);
                }
            }
        }
    }
}

void SubComponentManagerPrivate::parseDirectory(const QString &canonicalDirPath, bool addToLibrary, const QString& qualification)
{
    if (debug)
        qDebug() << Q_FUNC_INFO << canonicalDirPath;

    QDir dir(canonicalDirPath);

    dir.setNameFilters(QStringList(QMLFILEPATTERN));
    dir.setFilter(QDir::Files | QDir::Readable | QDir::CaseSensitive);

    QList<QFileInfo> monitoredList = watchedFiles(canonicalDirPath);
    QList<QFileInfo> newList;
    foreach (const QFileInfo &qmlFile, dir.entryInfoList()) {
        if (QFileInfo(m_filePath.toLocalFile()) == qmlFile) {
            // do not parse main file
            continue;
        }
        if (!qmlFile.fileName().at(0).isUpper()) {
            // QML sub components must be upper case
            continue;
        }
        newList << qmlFile;
    }

    qSort(monitoredList);
    qSort(newList);

    if (debug)
        qDebug() << "monitored list " << monitoredList.size() << "new list " << newList.size();
    QList<QFileInfo>::const_iterator oldIter = monitoredList.constBegin();
    QList<QFileInfo>::const_iterator newIter = newList.constBegin();

    while (oldIter != monitoredList.constEnd() && newIter != newList.constEnd()) {
        const QFileInfo oldFileInfo = *oldIter;
        const QFileInfo newFileInfo = *newIter;
        if (oldFileInfo == newFileInfo) {
            ++oldIter;
            ++newIter;
            continue;
        }
        if (oldFileInfo < newFileInfo) {
            foreach (const QString &qualifier, m_dirToQualifier.value(canonicalDirPath))
                unregisterQmlFile(oldFileInfo, qualifier);
            m_watcher.removePath(oldFileInfo.filePath());
            ++oldIter;
            continue;
        }
        // oldFileInfo > newFileInfo
        parseFile(newFileInfo.filePath(), addToLibrary, qualification);
        m_watcher.addPath(oldFileInfo.filePath());
        ++newIter;
    }

    while (oldIter != monitoredList.constEnd()) {
        foreach (const QString &qualifier, m_dirToQualifier.value(canonicalDirPath))
            unregisterQmlFile(*oldIter, qualifier);
        m_watcher.removePath(oldIter->filePath());
        ++oldIter;
    }

    while (newIter != newList.constEnd()) {
        parseFile(newIter->filePath(), addToLibrary, qualification);
        if (debug)
            qDebug() << "m_watcher.addPath(" << newIter->filePath() << ')';
        m_watcher.addPath(newIter->filePath());
        ++newIter;
    }
}

void SubComponentManagerPrivate::parseFile(const QString &canonicalFilePath, bool addToLibrary, const QString& /* qualification */)
{
    if (debug)
        qDebug() << Q_FUNC_INFO << canonicalFilePath;

    QFile file(canonicalFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QDeclarativeDomDocument document;
    if (!document.load(&m_engine, file.readAll(), QUrl::fromLocalFile(canonicalFilePath))) {
        // TODO: Put the errors somewhere?
        qWarning() << "Could not load qml file " << canonicalFilePath;
        return;
    }

    QString dir = QFileInfo(canonicalFilePath).path();
    foreach (const QString &qualifier, m_dirToQualifier.values(dir)) {
        registerQmlFile(canonicalFilePath, qualifier, document, addToLibrary);
    }
}

void SubComponentManagerPrivate::parseFile(const QString &canonicalFilePath)
{
    parseFile(canonicalFilePath, true, QString());
}

// dirInfo must already contain a canonical path
QList<QFileInfo> SubComponentManagerPrivate::watchedFiles(const QString &canonicalDirPath)
{
    QList<QFileInfo> files;

    foreach (const QString &monitoredFile, m_watcher.files()) {
        QFileInfo fileInfo(monitoredFile);
        if (fileInfo.dir().absolutePath() == canonicalDirPath) {
            files.append(fileInfo);
        }
    }
    return files;
}

void SubComponentManagerPrivate::unregisterQmlFile(const QFileInfo &fileInfo, const QString &qualifier)
{
    QString componentName = fileInfo.baseName();
    if (!qualifier.isEmpty())
        componentName = qualifier + '/' + componentName;

    if (m_metaInfo.hasNodeMetaInfo(componentName)) {
        NodeMetaInfo nodeInfo = m_metaInfo.nodeMetaInfo(componentName);
        m_metaInfo.removeNodeInfo(nodeInfo);
    }
}

void SubComponentManagerPrivate::registerQmlFile(const QFileInfo &fileInfo, const QString &qualifier,
                                                 const QDeclarativeDomDocument &document, bool addToLibrary)
{
    QString componentName = fileInfo.baseName();

    if (!qualifier.isEmpty()) {
        QString fixedQualifier = qualifier;
        if (qualifier.right(1) == QLatin1String("."))
            fixedQualifier.chop(1); //remove last char if it is a dot
        componentName = fixedQualifier + '/' + componentName;
    }

    if (debug)
        qDebug() << "SubComponentManager" << __FUNCTION__ << componentName;

    if (m_metaInfo.hasNodeMetaInfo(componentName) && addToLibrary) {
        NodeMetaInfo nodeInfo = m_metaInfo.nodeMetaInfo(componentName);
        m_metaInfo.removeNodeInfo(nodeInfo);
    }

    const QDeclarativeDomObject rootObject = document.rootObject();

    NodeMetaInfo nodeInfo(m_metaInfo);
    nodeInfo.setType(componentName, -1, -1);
    nodeInfo.setQmlFile(fileInfo.filePath());
    nodeInfo.setSuperClass(rootObject.objectType(),
                           rootObject.objectTypeMajorVersion(),
                           rootObject.objectTypeMinorVersion());

    if (addToLibrary) {
        // Add file components to the library
        ItemLibraryEntry itemLibraryEntry;
        itemLibraryEntry.setType(nodeInfo.typeName(), nodeInfo.majorVersion(), nodeInfo.minorVersion());
        itemLibraryEntry.setName(componentName);
        itemLibraryEntry.setCategory(tr("QML Components"));
        m_metaInfo.itemLibraryInfo()->addEntry(itemLibraryEntry);
    }

    m_metaInfo.addNodeInfo(nodeInfo);

    //document.rootObject().d

    foreach (const QDeclarativeDomDynamicProperty &dynamicProperty, document.rootObject().dynamicProperties()) {
        Q_ASSERT(!dynamicProperty.propertyName().isEmpty());
        Q_ASSERT(!dynamicProperty.propertyTypeName().isEmpty());

        if (dynamicProperty.isDefaultProperty())
            nodeInfo.setDefaultProperty(dynamicProperty.propertyName());

        PropertyMetaInfo propertyMetaInfo;
        propertyMetaInfo.setName(dynamicProperty.propertyName());
        propertyMetaInfo.setType(dynamicProperty.propertyTypeName());
        propertyMetaInfo.setValid(true);
        propertyMetaInfo.setReadable(true);
        propertyMetaInfo.setWritable(true);

        QDeclarativeDomProperty defaultValue = dynamicProperty.defaultValue();
        if (defaultValue.value().isLiteral()) {
            QVariant defaultValueVariant(defaultValue.value().toLiteral().literal());
            defaultValueVariant.convert((QVariant::Type) dynamicProperty.propertyType());
            propertyMetaInfo.setDefaultValue(nodeInfo, defaultValueVariant);
        }

        nodeInfo.addProperty(propertyMetaInfo);
    }
    if (!nodeInfo.hasDefaultProperty())
        nodeInfo.setDefaultProperty(nodeInfo.directSuperClass().defaultProperty());
}

} // namespace Internal

/*!
  \class SubComponentManager

  Detects & monitors (potential) component files in a list of directories, and registers
  these in the metatype system.
*/

SubComponentManager::SubComponentManager(MetaInfo metaInfo, QObject *parent) :
        QObject(parent),
        m_d(new Internal::SubComponentManagerPrivate(metaInfo, this))
{
}

SubComponentManager::~SubComponentManager()
{
    delete m_d;
}

QStringList SubComponentManager::directories() const
{
    return m_d->m_watcher.directories();
}

QStringList SubComponentManager::qmlFiles() const
{
    return m_d->m_watcher.files();
}

static bool importEqual(const QDeclarativeDomImport &import1, const QDeclarativeDomImport &import2)
{
    return import1.type() == import2.type()
           && import1.uri() == import2.uri()
           && import1.version() == import2.version()
           && import1.qualifier() == import2.qualifier();
}

void SubComponentManager::update(const QUrl &filePath, const QByteArray &data)
{
    QDeclarativeEngine engine;
    QDeclarativeDomDocument document;

    QList<QDeclarativeDomImport> imports;
    if (document.load(&engine, data, filePath))
        imports = document.imports();

    update(filePath, imports);
}

void SubComponentManager::update(const QUrl &filePath, const QList<QDeclarativeDomImport> &imports)
{
    if (debug)
        qDebug() << Q_FUNC_INFO << filePath << imports.size();

    QFileInfo oldDir, newDir;

    if (!m_d->m_filePath.isEmpty()) {
        const QString file = m_d->m_filePath.toLocalFile();
        oldDir = QFileInfo(QFileInfo(file).path());
    }
    if (!filePath.isEmpty()) {
        const QString file = filePath.toLocalFile();
        newDir = QFileInfo(QFileInfo(file).path());
    }

    m_d->m_filePath = filePath;

    //
    // (implicit) import of local directory
    //
    if (oldDir != newDir) {
        if (!oldDir.filePath().isEmpty()) {
            m_d->m_dirToQualifier.remove(oldDir.canonicalFilePath(), QString());
            if (!m_d->m_dirToQualifier.contains(oldDir.canonicalFilePath()))
                m_d->m_watcher.removePath(oldDir.filePath());
        }

        if (!newDir.filePath().isEmpty()) {
            m_d->m_watcher.addPath(newDir.filePath());
            m_d->m_dirToQualifier.insertMulti(newDir.canonicalFilePath(), QString());
        }
    }

    //
    // Imports
    //

    // skip first list items until the lists differ
    int i = 0;
    while (i < qMin(imports.size(), m_d->m_imports.size())) {
        if (!importEqual(imports.at(i), m_d->m_imports.at(i)))
            break;
        ++i;
    }

    for (int ii = m_d->m_imports.size() - 1; ii >= i; --ii)
        m_d->removeImport(ii);

    for (int ii = i; ii < imports.size(); ++ii) {
        m_d->addImport(ii, imports.at(ii));
    }

    m_d->parseDirectories();
}

} // namespace QmlDesigner

#include "subcomponentmanager.moc"
