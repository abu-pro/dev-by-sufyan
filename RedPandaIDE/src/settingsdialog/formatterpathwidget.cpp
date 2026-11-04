#include "formatterpathwidget.h"
#ifdef __EMSCRIPTEN__
#include "../rooms/dbstheme.h"
#endif
#include "ui_formatterpathwidget.h"
#include "../iconsmanager.h"
#include "../settings.h"
#include "../systemconsts.h"

#include <QFileDialog>

FormatterPathWidget::FormatterPathWidget(const QString &name, const QString &group, IconsManager *iconsManager, QWidget *parent) :
    SettingsWidget(name,group,iconsManager,parent),
    ui(new Ui::FormatterPathWidget)
{
    ui->setupUi(this);
}

FormatterPathWidget::~FormatterPathWidget()
{
    delete ui;
}

void FormatterPathWidget::doLoad()
{
    ui->txtAstyle->setText(pSettings->environment().AStylePath());
}

void FormatterPathWidget::doSave()
{
    pSettings->environment().setAStylePath(ui->txtAstyle->text());
}

void FormatterPathWidget::updateIcons(const QSize &/*size*/)
{
    iconsManager()->setIcon(ui->btnChooseAstyle, IconsManager::ACTION_FILE_OPEN_FOLDER);
}

void FormatterPathWidget::on_btnChooseAstyle_clicked()
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
    QString fileName = QFileDialog::getOpenFileName(
                this,
                tr("Path to astyle"),
                QString(),
                tr("All files (%1)").arg(ALL_FILE_WILDCARD));
    if (!fileName.isEmpty() ) {
        ui->txtAstyle->setText(fileName);
    }
}

