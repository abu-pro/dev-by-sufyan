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
#include "projectdllhostwidget.h"
#ifdef __EMSCRIPTEN__
#include "../rooms/dbstheme.h"
#endif
#include "ui_projectdllhostwidget.h"
#include "../project.h"
#include "../mainwindow.h"
#include "../iconsmanager.h"
#include "../systemconsts.h"

#include <QFileDialog>

ProjectDLLHostWidget::ProjectDLLHostWidget(const QString &name, const QString &group,IconsManager *iconsManager,  QWidget *parent) :
    SettingsWidget(name,group,iconsManager, parent),
    ui(new Ui::ProjectDLLHostWidget)
{
    ui->setupUi(this);
}

ProjectDLLHostWidget::~ProjectDLLHostWidget()
{
    delete ui;
}

void ProjectDLLHostWidget::doLoad()
{
    ui->txtHost->setText(pMainWindow->project()->options().hostApplication);
}

void ProjectDLLHostWidget::doSave()
{
    pMainWindow->project()->options().hostApplication = ui->txtHost->text();
    pMainWindow->project()->saveOptions();
}

void ProjectDLLHostWidget::on_btnBrowse_clicked()
{
#ifdef __EMSCRIPTEN__
    // Part 9 (web build): local file/folder picker (static
    // getter = exec()) - honest-disable, see the other settings
    // pickers. The path field stays editable as text.
    dbsWasmShowInfo(this, tr("Browse"),
                    tr("Picking files or folders from this "
                       "computer is not available in the web "
                       "version."));
    return;
#endif
    QString currentFile=ui->txtHost->text();
    if (currentFile.isEmpty()) {
        currentFile = pMainWindow->project()->directory();
    }
    QString filename = QFileDialog::getOpenFileName(
                this,
                tr("Choose host application"),
                currentFile,
                tr("All files (%1)").arg(ALL_FILE_WILDCARD));
    if (!filename.isEmpty() && fileExists(filename)) {
        ui->txtHost->setText(filename);
    }
}

void ProjectDLLHostWidget::updateIcons(const QSize &/*size*/)
{
    iconsManager()->setIcon(ui->btnBrowse, IconsManager::ACTION_FILE_OPEN_FOLDER);
}

