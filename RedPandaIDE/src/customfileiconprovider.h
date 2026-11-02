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
#ifndef CUSTOMFILEICONPROVIDER_H
#define CUSTOMFILEICONPROVIDER_H

#include <QFileIconProvider>

class IconsManager;
#ifdef ENABLE_VCS
class GitRepository;
#endif
class CustomFileIconProvider : public QFileIconProvider
{
public:
    CustomFileIconProvider(IconsManager *iconsManager);
    ~CustomFileIconProvider();
    void setRootFolder(const QString& folder);
    void update();
    // Dev by Sufyan theme packs (v1.6.1): while set, Explorer
    // folder/file icons come from the active pack's user-supplied
    // icon set instead of the stock icon theme.
    void setDbsPackIcons(const QString &folder, const QString &cppFile,
                         const QString &hFile, const QString &cFile,
                         const QString &pyFile, const QString &genericFile);
    void clearDbsPackIcons();
private:
#ifdef ENABLE_VCS
    GitRepository* mVCSRepository;
#endif
    IconsManager *mIconsManager;
    QIcon mDbsFolderIcon, mDbsCppIcon, mDbsHIcon, mDbsCIcon,
          mDbsPyIcon, mDbsGenericIcon;
    bool mDbsPackIconsActive = false;
    // QFileIconProvider interface
public:
    QIcon icon(IconType type) const override;
    QIcon icon(const QFileInfo &info) const override;
#ifdef ENABLE_VCS
    GitRepository *VCSRepository() const;
#endif
};

#endif // CUSTOMFILEICONPROVIDER_H
