/*
 * Copyright (C) 2020-2022 Roy Qu (royqh1979@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "customfileiconprovider.h"
#include "iconsmanager.h"
#ifdef ENABLE_VCS
#include "vcs/gitrepository.h"
#endif

CustomFileIconProvider::CustomFileIconProvider(IconsManager *iconsManager)
{
    Q_ASSERT(iconsManager!=nullptr);
    mIconsManager = iconsManager;
    //provider delete it in the destructor
#ifdef ENABLE_VCS
    mVCSRepository = new GitRepository("");
#endif
}

CustomFileIconProvider::~CustomFileIconProvider()
{
#ifdef ENABLE_VCS
    delete mVCSRepository;
#endif
}

void CustomFileIconProvider::setRootFolder(const QString &folder)
{
#ifdef ENABLE_VCS
    mVCSRepository->setFolder(folder);
#else
    Q_UNUSED(folder);
#endif
}

void CustomFileIconProvider::update()
{
#ifdef ENABLE_VCS
    mVCSRepository->update();
#endif
}

#ifdef ENABLE_VCS
GitRepository *CustomFileIconProvider::VCSRepository() const
{
    return mVCSRepository;
}
#endif

void CustomFileIconProvider::setDbsPackIcons(
        const QString &folder, const QString &cppFile,
        const QString &hFile, const QString &cFile,
        const QString &pyFile, const QString &genericFile)
{
    mDbsFolderIcon = QIcon(folder);
    mDbsCppIcon = QIcon(cppFile);
    mDbsHIcon = QIcon(hFile);
    mDbsCIcon = QIcon(cFile);
    mDbsPyIcon = QIcon(pyFile);
    mDbsGenericIcon = QIcon(genericFile);
    mDbsPackIconsActive = true;
}

void CustomFileIconProvider::clearDbsPackIcons()
{
    mDbsPackIconsActive = false;
    mDbsFolderIcon = mDbsCppIcon = mDbsHIcon = mDbsCIcon =
        mDbsPyIcon = mDbsGenericIcon = QIcon();
}

QIcon CustomFileIconProvider::icon(IconType type) const
{
    if (type == IconType::Folder && mDbsPackIconsActive
            && !mDbsFolderIcon.isNull())
        return mDbsFolderIcon;
    if (type == IconType::Folder) {
        QIcon icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_FOLDER);
        if (!icon.isNull())
            return icon;
    }
    return QFileIconProvider::icon(type);

}

QIcon CustomFileIconProvider::icon(const QFileInfo &info) const
{
    // Dev by Sufyan: an active theme pack's icons win over the
    // stock set (folders, and files by kind).
    if (mDbsPackIconsActive) {
        if (info.isDir()) {
            if (!mDbsFolderIcon.isNull())
                return mDbsFolderIcon;
        } else if (info.isFile()) {
            if (isHFile(info.fileName()) && !mDbsHIcon.isNull())
                return mDbsHIcon;
            if (isCppFile(info.fileName()) && !mDbsCppIcon.isNull())
                return mDbsCppIcon;
            if (isCFile(info.fileName()) && !mDbsCIcon.isNull())
                return mDbsCIcon;
            if (info.suffix() == QStringLiteral("py")
                    && !mDbsPyIcon.isNull())
                return mDbsPyIcon;
            if (!mDbsGenericIcon.isNull())
                return mDbsGenericIcon;
        }
    }
    QIcon icon;
    if (info.isDir()) {
#ifdef ENABLE_VCS
        if (mVCSRepository->isFileInRepository(info)) {
            if (mVCSRepository->isFileConflicting(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_FOLDER_VCS_CONFLICT);
            else if (mVCSRepository->isFileStaged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_FOLDER_VCS_STAGED);
            else if (mVCSRepository->isFileChanged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_FOLDER_VCS_CHANGED);
            else
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_FOLDER_VCS_NOCHANGE);
        } else
#endif
            icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_FOLDER);
    } else if (!info.exists()) {
        icon = mIconsManager->getIcon(IconsManager::ACTION_MISC_CROSS);
    } else  if (isHFile(info.fileName())) {
#ifdef ENABLE_VCS
        if (mVCSRepository->isFileInRepository(info)) {
            if (mVCSRepository->isFileConflicting(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_HFILE_VCS_CONFLICT);
            else if (mVCSRepository->isFileStaged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_HFILE_VCS_STAGED);
            else if (mVCSRepository->isFileChanged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_HFILE_VCS_CHANGED);
            else
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_HFILE_VCS_NOCHANGE);
        } else
#endif
            icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_HFILE);
    } else if (isCppFile(info.fileName())) {
#ifdef ENABLE_VCS
        if (mVCSRepository->isFileInRepository(info)) {
            if (mVCSRepository->isFileConflicting(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_CPPFILE_VCS_CONFLICT);
            else if (mVCSRepository->isFileStaged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_CPPFILE_VCS_STAGED);
            else if (mVCSRepository->isFileChanged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_CPPFILE_VCS_CHANGED);
            else
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_CPPFILE_VCS_NOCHANGE);
        } else
#endif
            icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_CPPFILE);
    } else if (isCFile(info.fileName())) {
#ifdef ENABLE_VCS
        if (mVCSRepository->isFileInRepository(info)) {
            if (mVCSRepository->isFileConflicting(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_CFILE_VCS_CONFLICT);
            else if (mVCSRepository->isFileStaged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_CFILE_VCS_STAGED);
            else if (mVCSRepository->isFileChanged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_CFILE_VCS_CHANGED);
            else
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_CFILE_VCS_NOCHANGE);
        } else
#endif
            icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_CFILE);
    } else if (info.suffix()=="dev") {
#ifdef ENABLE_VCS
        if (mVCSRepository->isFileInRepository(info)) {
            if (mVCSRepository->isFileConflicting(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_PROJECTFILE_VCS_CONFLICT);
            else if (mVCSRepository->isFileStaged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_PROJECTFILE_VCS_STAGED);
            else if (mVCSRepository->isFileChanged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_PROJECTFILE_VCS_CHANGED);
            else
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_PROJECTFILE_VCS_NOCHANGE);
        } else
#endif
            icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_PROJECTFILE);
    } else {
#ifdef ENABLE_VCS
        if (mVCSRepository->isFileInRepository(info)) {
            if (mVCSRepository->isFileConflicting(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_FILE_VCS_CONFLICT);
            else if (mVCSRepository->isFileStaged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_FILE_VCS_STAGED);
            else if (mVCSRepository->isFileChanged(info))
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_FILE_VCS_CHANGED);
            else
                icon = mIconsManager->getIcon(IconsManager::FILESYSTEM_FILE_VCS_NOCHANGE);
        }
#endif
        //use default system icon
    }
    if (!icon.isNull())
        return icon;
    return QFileIconProvider::icon(info);
}
