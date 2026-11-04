#include "toolsgitwidget.h"
#ifdef __EMSCRIPTEN__
#include "../rooms/dbstheme.h"
#endif
#include "ui_toolsgitwidget.h"
#include "../iconsmanager.h"
#include "../settings.h"
#include "../systemconsts.h"
#include "../utils.h"
#include "../mainwindow.h"

#include <QFileDialog>

ToolsGitWidget::ToolsGitWidget(const QString& name, const QString& group,IconsManager *iconsManager, QWidget *parent) :
    SettingsWidget(name,group,iconsManager, parent),
    ui(new Ui::ToolsGitWidget)
{
    ui->setupUi(this);
    ui->lblGitInfo->setVisible(false);
}

ToolsGitWidget::~ToolsGitWidget()
{
    delete ui;
}

void ToolsGitWidget::doLoad()
{
    ui->txtGitPath->setText(pSettings->vcs().gitPath());
}

void ToolsGitWidget::doSave()
{
    pSettings->vcs().setGitPath(ui->txtGitPath->text());
    pSettings->vcs().save();
    pMainWindow->applySettings();
}

void ToolsGitWidget::updateIcons(const QSize &/*size*/)
{
    iconsManager()->setIcon(ui->btnBrowseGit,IconsManager::ACTION_FILE_OPEN_FOLDER);
}

void ToolsGitWidget::on_btnBrowseGit_clicked()
{
#ifdef __EMSCRIPTEN__
    // Part 9 (web build): this button picks a file or folder on
    // the local computer - a browser has no local disk to pick
    // from, and the static getter is an exec(). The path field
    // stays editable as text.
    dbsWasmShowInfo(this, tr("Browse"),
                    tr("Picking files or folders from this "
                       "computer is not available in the web "
                       "version."));
    return;
#endif
    QString filename = QFileDialog::getOpenFileName(
                this,
                tr("Git Executable"),
                QString(),
                tr("All files (%1)").arg(ALL_FILE_WILDCARD));
    if (!filename.isEmpty() && fileExists(filename)) {
        ui->txtGitPath->setText(filename);
    }
}


void ToolsGitWidget::on_btnTestGit_clicked()
{
    QFileInfo fileInfo(ui->txtGitPath->text());
    if (!fileInfo.exists()) {
        ui->lblGitInfo->setVisible(false);
        return;
    }
    ui->lblGitInfo->setVisible(true);
    ui->lblGitInfo->setText("");
    QStringList args;
    args.append("--version");
    QString output = runAndGetOutput(
                fileInfo.fileName(),
                fileInfo.absolutePath(),
                args);
    ui->lblGitInfo->setText(output);
}

