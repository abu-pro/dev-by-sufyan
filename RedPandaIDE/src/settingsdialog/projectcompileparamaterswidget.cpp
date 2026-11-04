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
#include "projectcompileparamaterswidget.h"
#ifdef __EMSCRIPTEN__
#include "../rooms/dbstheme.h"
#endif
#include "ui_projectcompileparamaterswidget.h"
#include "../mainwindow.h"
#include "../project.h"
#include "../iconsmanager.h"
#include "../utils.h"
#include "../utils/escape.h"

#ifdef Q_OS_WIN
#include <sysinfoapi.h>
#endif

#include <QFileDialog>

ProjectCompileParamatersWidget::ProjectCompileParamatersWidget(const QString &name, const QString &group, IconsManager *iconsManager, QWidget *parent) :
    SettingsWidget(name,group,iconsManager,parent),
    ui(new Ui::ProjectCompileParamatersWidget)
{
    ui->setupUi(this);
#ifdef Q_OS_WIN
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    ui->spinParallelJobs->setMaximum(info.dwNumberOfProcessors);
#else
    ui->tabResource->setVisible(false);
#endif
    ui->tabCommands->setCurrentWidget(ui->tabCCompiler);
}

ProjectCompileParamatersWidget::~ProjectCompileParamatersWidget()
{
    delete ui;
}

void ProjectCompileParamatersWidget::doLoad()
{
    ui->txtCCompiler->setPlainText(pMainWindow->project()->options().compilerCmd);
    ui->txtCPPCompiler->setPlainText(pMainWindow->project()->options().cppCompilerCmd);
    ui->txtLinker->setPlainText(pMainWindow->project()->options().linkerCmd);
    ui->txtResource->setPlainText(pMainWindow->project()->options().resourceCmd);
    ui->grpAllowParallelBuilding->setChecked(pMainWindow->project()->options().allowParallelBuilding);
    ui->spinParallelJobs->setValue(pMainWindow->project()->options().parellelBuildingJobs);
}

void ProjectCompileParamatersWidget::doSave()
{
    pMainWindow->project()->options().compilerCmd = ui->txtCCompiler->toPlainText();
    pMainWindow->project()->options().cppCompilerCmd = ui->txtCPPCompiler->toPlainText();
    pMainWindow->project()->options().linkerCmd = ui->txtLinker->toPlainText();
    pMainWindow->project()->options().resourceCmd = ui->txtResource->toPlainText();
    pMainWindow->project()->options().allowParallelBuilding = ui->grpAllowParallelBuilding->isChecked();
    pMainWindow->project()->options().parellelBuildingJobs = ui->spinParallelJobs->value();
    pMainWindow->project()->saveOptions();
}

void ProjectCompileParamatersWidget::on_btnChooseLib_clicked()
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
#ifdef Q_OS_WIN
    QString filter = tr("Library Files")+" (*.a *.lib *.o)";
#else
    QString filter = tr("Library Files")+" (*.a *.o)";
#endif

    QStringList files = QFileDialog::getOpenFileNames(
                this,
                tr("Add Library Files"),
                QDir::currentPath(),
                filter
                );
    if (!files.isEmpty()) {
        foreach (const QString& file,files) {
            ui->txtLinker->appendPlainText(" " + escapeArgument(file, false, EscapeArgumentRule::BourneAgainShellPretty));
        }
    }
}

void ProjectCompileParamatersWidget::updateIcons(const QSize &/*size*/)
{
    iconsManager()->setIcon(ui->btnChooseLib, IconsManager::ACTION_MISC_FOLDER);
}


