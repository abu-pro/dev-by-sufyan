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
#include "editorcolorschemewidget.h"
#ifdef __EMSCRIPTEN__
#include "../rooms/dbstheme.h"
#include <QInputDialog>
#endif
#include "ui_editorcolorschemewidget.h"
#include "../settings.h"
#include "../mainwindow.h"
#include "../systemconsts.h"
#include "../utils/parser.h"
#include <QAction>
#include <QMessageBox>
#include <QDebug>
#include <QInputDialog>
#include <QFileDialog>
#include <qsynedit/document.h>

EditorColorSchemeWidget::EditorColorSchemeWidget(ColorManager *colorManager, const QString& name,
                                                 const QString& group, IconsManager *iconsManager,
                                                 QWidget *parent) :
    SettingsWidget(name,group,iconsManager, parent),
    ui(new Ui::EditorColorSchemeWidget)
{
    ui->setupUi(this);
    mColorManager = colorManager;
    mStatementColors = std::make_shared<QHash<StatementKind, std::shared_ptr<ColorSchemeItem> >>();

    mItemDelegate = new ColorSchemeItemDelegate(this);
    ui->cbScheme->setItemDelegate(mItemDelegate);

    mDefaultSchemeComboFont = ui->cbScheme->font();
    mModifiedSchemeComboFont = mDefaultSchemeComboFont;
    mModifiedSchemeComboFont.setBold(true);
    int schemeCount=0;
    foreach (const QString &schemeName, mColorManager->getSchemes()) {
        PColorScheme scheme = mColorManager->get(schemeName);
        if (!scheme)
            return;
        ui->cbScheme->addItem(schemeName);
        if (scheme->customed())
            ui->cbScheme->setItemData(schemeCount,mModifiedSchemeComboFont,Qt::FontRole);
        else
            ui->cbScheme->setItemData(schemeCount,mDefaultSchemeComboFont,Qt::FontRole);
        schemeCount++;
    }
    QItemSelectionModel *m = ui->treeItems->selectionModel();
    ui->treeItems->setModel(&mDefinesModel);
    delete m;
    mDefinesModel.setHorizontalHeaderLabels(QStringList());        
    foreach (const QString &defineName, mColorManager->getDefines()) {
        addDefine(defineName, mColorManager->getDefine(defineName));
    }
    ui->treeItems->expandAll();
    QModelIndex groupIndex = mDefinesModel.index(0,0);
    QModelIndex index = mDefinesModel.index(0,0,groupIndex);
    ui->treeItems->setCurrentIndex(index);
    connect(ui->treeItems->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &EditorColorSchemeWidget::onItemSelectionChanged);
    connect(ui->cbScheme, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditorColorSchemeWidget::changeSchemeComboFont);
    connect(ui->cbScheme, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditorColorSchemeWidget::onItemSelectionChanged);
    connect(this, &SettingsWidget::settingsChanged,this,
            &EditorColorSchemeWidget::onSettingChanged);
    ui->editDemo->setEditorSettings(&pSettings->editor());
    ui->editDemo->setCodeCompletionSettings(&pSettings->codeCompletion());
    ui->editDemo->setColorManager(mColorManager);
    ui->editDemo->setIconsManager(iconsManager);
    ui->editDemo->applySettings();
    ui->editDemo->setUseCodeFolding(true);
    ui->editDemo->setFilename("editdemo.cpp");
    ui->editDemo->setFileType(FileType::CppSource);
    PCppParser parser = std::make_shared<CppParser>();
    parser->setSharedByFiles(false);
    parser->setLanguage(ui->editDemo->calcParserLanguage());
    parser->setOnGetFileStream([this](const QString& filename, QStringList& buffer){
        buffer=ui->editDemo->content();
           return true;
    });
    resetCppParser(parser);
    parser->setEnabled(true);
    ui->editDemo->setCppParser(parser);
    ui->editDemo->replaceContent(
            "#include <iostream>\n"
            "#include <conio.h>\n"
            "\n"
            "int x=10;\n"
            "\n"
            "int main(int argc, char **argv)\n"
            "{\n"
            "    int numbers[20]; // warning line\n"
            "    float average, total; // bookmark\n"
            "    for (int i = 0; i <= 19; i++) // active breakpoint\n"
            "    { // breakpoint\n"
            "        numbers[i] = i+x;\n"
            "        Total += i; // error line\n"
            "    }\n"
            "    average = total / 20; // comment\n"
            "    std::cout << \"total: \" << total <<\n"
            "    \"\\nAverage: \" << average;\n"
            "    getch();\n"
            "}\n"
                );
    ui->editDemo->setExtraKeystrokes();
    ui->editDemo->setReadOnly(true);
    ui->editDemo->toggleBreakpoint(10);
    ui->editDemo->toggleBookmark(8);
    ui->editDemo->addSyntaxIssues(12, 8, 13, CompileIssueType::Error, "[Error] 'Total' was not declared in this scope; did you mean 'total'?");
    ui->editDemo->addSyntaxIssues(7, 8, 15, CompileIssueType::Warning, "[Warning] variable 'numbers' set but not used [-Wunused-but-set-variable]");
    ui->editDemo->setActiveBreakpoint(9);
    ui->editDemo->setCaretXY(QSynedit::CharPos{0,0});

    //ui->editDemo->reparseDocument();
    //ui->editDemo->invalidate();
    onItemSelectionChanged();
}

void EditorColorSchemeWidget::addDefine(const QString& name, PColorSchemeItemDefine define)
{
    QList<QStandardItem*> items = mDefinesModel.findItems(define->group());
    QStandardItem* pGroupItem;
    if (items.count() == 0 ) {
        //delete in the destructor
        pGroupItem = new QStandardItem(define->group());
        pGroupItem->setData("", NameRole);
        mDefinesModel.appendRow(pGroupItem);
    } else {
        pGroupItem = items[0];
    }
    //delete in the destructor
    QStandardItem* pWidgetItem = new QStandardItem(define->displayName());
    pWidgetItem->setData(name, NameRole);
    pGroupItem->appendRow(pWidgetItem);
}

PColorSchemeItem EditorColorSchemeWidget::getCurrentItem()
{
    QItemSelectionModel * selectionModel = ui->treeItems->selectionModel();
    QString name =mDefinesModel.data(selectionModel->currentIndex(),NameRole).toString();
    if (name.isEmpty())
        return PColorSchemeItem();
    return mColorManager->getItem(ui->cbScheme->currentText(), name);
}

PColorScheme EditorColorSchemeWidget::getCurrentScheme()
{
    return mColorManager->get(ui->cbScheme->currentText());
}

void EditorColorSchemeWidget::connectModificationSlots()
{
    connect(ui->cbBackground,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onBackgroundChanged);
    connect(ui->colorBackground,&ColorEdit::colorChanged,
            this, &EditorColorSchemeWidget::onBackgroundChanged);
    connect(ui->cbForeground,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onForegroundChanged);
    connect(ui->colorForeground,&ColorEdit::colorChanged,
            this, &EditorColorSchemeWidget::onForegroundChanged);
    connect(ui->cbBold,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onFontStyleChanged);
    connect(ui->cbItalic,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onFontStyleChanged);
    connect(ui->cbStrikeout,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onFontStyleChanged);
    connect(ui->cbUnderlined,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onFontStyleChanged);
}

void EditorColorSchemeWidget::disconnectModificationSlots()
{
    disconnect(ui->cbBackground,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onBackgroundChanged);
    disconnect(ui->colorBackground,&ColorEdit::colorChanged,
            this, &EditorColorSchemeWidget::onBackgroundChanged);
    disconnect(ui->cbForeground,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onForegroundChanged);
    disconnect(ui->colorForeground,&ColorEdit::colorChanged,
            this, &EditorColorSchemeWidget::onForegroundChanged);
    disconnect(ui->cbBold,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onFontStyleChanged);
    disconnect(ui->cbItalic,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onFontStyleChanged);
    disconnect(ui->cbStrikeout,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onFontStyleChanged);
    disconnect(ui->cbUnderlined,&QCheckBox::stateChanged,
            this, &EditorColorSchemeWidget::onFontStyleChanged);
}

void EditorColorSchemeWidget::setCurrentSchemeModified()
{
    PColorScheme scheme = getCurrentScheme();
    if (scheme) {
        scheme->setCustomed(true);
    }
//    if (mModifiedSchemes.contains(ui->cbScheme->currentText()))
//        return;
    mModifiedSchemes.insert(ui->cbScheme->currentText());
    ui->cbScheme->setItemData(ui->cbScheme->currentIndex(),
                              mModifiedSchemeComboFont,Qt::FontRole);
    ui->cbScheme->setFont(mModifiedSchemeComboFont);
    //ui->cbScheme->view()->setFont(mDefaultSchemeComboFont);
    //we must reset the editor here, because this slot is processed after the onSettingChanged
    onSettingChanged();
}

EditorColorSchemeWidget::~EditorColorSchemeWidget()
{
    ui->editDemo->parser()->setOnGetFileStream(nullptr);
    delete ui;
    //mDefinesModel.clear();
}

static void setColorProp(ColorEdit* ce, QCheckBox* cb, const QColor& color) {
    if (color.isValid()) {
        cb->setChecked(true);
        ce->setColor(color);
        ce->setVisible(true);
    } else {
        cb->setChecked(false);
        ce->setVisible(false);
    }
}

void EditorColorSchemeWidget::onItemSelectionChanged()
{
    disconnectModificationSlots();
    QItemSelectionModel * selectionModel = ui->treeItems->selectionModel();
    QString name =mDefinesModel.data(selectionModel->currentIndex(),NameRole).toString();
    bool found = false;
    if (!name.isEmpty()) {
        PColorSchemeItemDefine define = mColorManager->getDefine(name);
        if (define) {
            found = true;
            ui->cbBackground->setEnabled(define->hasBackground());
            ui->colorBackground->setEnabled(define->hasBackground());
            ui->cbForeground->setEnabled(define->hasForeground());
            ui->colorForeground->setEnabled(define->hasForeground());
            ui->grpFontStyles->setEnabled(define->hasFontStyle());
            PColorSchemeItem item = mColorManager->getItem(ui->cbScheme->currentText(), name);
            if (!item) {
                PColorScheme scheme = mColorManager->get(ui->cbScheme->currentText());
                if (scheme) {
                    scheme->addItem(name);
                }
            }
            if (define->hasBackground() && item) {
                setColorProp(ui->colorBackground, ui->cbBackground,item->background());
            } else {
                setColorProp(ui->colorBackground, ui->cbBackground,QColor());
            }
            if (define->hasForeground() && item) {
                setColorProp(ui->colorForeground, ui->cbForeground,item->foreground());
            } else {
                setColorProp(ui->colorForeground, ui->cbForeground,QColor());
            }
            if (define->hasFontStyle() && item) {
                ui->cbBold->setChecked(item->bold());
                ui->cbItalic->setChecked(item->italic());
                ui->cbUnderlined->setChecked(item->underlined());
                ui->cbStrikeout->setChecked(item->strikeout());
            } else {
                ui->cbBold->setChecked(false);
                ui->cbItalic->setChecked(false);
                ui->cbUnderlined->setChecked(false);
                ui->cbStrikeout->setChecked(false);
            }
        }
    }

    ui->widgetSchemeItem->setEnabled(found);
    connectModificationSlots();
}

void EditorColorSchemeWidget::onSettingChanged()
{
    mColorManager->updateStatementColors(mStatementColors,ui->cbScheme->currentText());
    ui->editDemo->applyColorScheme(ui->cbScheme->currentText());
    ui->editDemo->setStatementColors(mStatementColors);
}

void EditorColorSchemeWidget::onForegroundChanged()
{
    PColorSchemeItem item = getCurrentItem();
    if (!item)
        return;
    ui->colorForeground->setVisible(ui->cbForeground->isChecked());
    if (ui->cbForeground->isChecked()) {
        item->setForeground(ui->colorForeground->color());
    } else {
        ui->colorForeground->setColor(QColor());
        item->setForeground(QColor());
    }
    setCurrentSchemeModified();
}

void EditorColorSchemeWidget::onBackgroundChanged()
{
    PColorSchemeItem item = getCurrentItem();
    if (!item)
        return;
    ui->colorBackground->setVisible(ui->cbBackground->isChecked());
    if (ui->cbBackground->isChecked()) {
        item->setBackground(ui->colorBackground->color());
    } else {
        ui->colorBackground->setColor(QColor());
        item->setBackground(QColor());
    }
    setCurrentSchemeModified();
}

void EditorColorSchemeWidget::onFontStyleChanged()
{
    PColorSchemeItem item = getCurrentItem();
    if (!item)
        return;
    item->setBold(ui->cbBold->isChecked());
    item->setItalic(ui->cbItalic->isChecked());
    item->setStrikeout(ui->cbStrikeout->isChecked());
    item->setUnderlined(ui->cbUnderlined->isChecked());
    setCurrentSchemeModified();
}

void EditorColorSchemeWidget::changeSchemeComboFont()
{
    QString name = ui->cbScheme->currentText();
    PColorScheme scheme = mColorManager->get(name);
    if (scheme && scheme->customed()) {
        ui->cbScheme->setFont(mModifiedSchemeComboFont);
    } else {
        ui->cbScheme->setFont(mDefaultSchemeComboFont);
    }
    //ui->cbScheme->view()->setFont(mDefaultSchemeComboFont);
}

void EditorColorSchemeWidget::doLoad()
{
    ui->cbScheme->setCurrentText(pSettings->editor().colorScheme());
    changeSchemeComboFont();
    ui->chkRainborParenthesis->setChecked(pSettings->editor().rainbowParenthesis());
    ui->chkRainbowIndentGuides->setChecked(pSettings->editor().rainbowIndentGuides());
    ui->chkRainbowIndents->setChecked(pSettings->editor().rainbowIndents());
}

void EditorColorSchemeWidget::doSave()
{
    try {
        foreach (const QString &name, mModifiedSchemes) {
            mColorManager->saveScheme(name);
        }
        pSettings->editor().setColorScheme(ui->cbScheme->currentText());
        pSettings->editor().setRainbowParenthesis(ui->chkRainborParenthesis->isChecked());
        pSettings->editor().setRainbowIndentGuides(ui->chkRainbowIndentGuides->isChecked());
        pSettings->editor().setRainbowIndents(ui->chkRainbowIndents->isChecked());
        pSettings->editor().save();
        pMainWindow->updateEditorColorSchemes();
    } catch (FileError e) {
        #ifdef __EMSCRIPTEN__
            dbsWasmShowInfo(this, tr("Error"), e.reason());
#else
            QMessageBox::critical(this,tr("Error"),e.reason());
#endif
    }
}

void EditorColorSchemeWidget::on_actionCopy_Scheme_triggered()
{
    QString newName = mColorManager->copy(ui->cbScheme->currentText());
    if (newName.isEmpty())
        return;
    ui->cbScheme->addItem(newName);
    ui->cbScheme->setCurrentText(newName);
}

void EditorColorSchemeWidget::on_btnSchemeMenu_pressed()
{
#ifdef __EMSCRIPTEN__
    // Part 9 (web build): heap menu + popup() — exec() is the
    // wall, and a stack menu would die at scope exit under popup.
    auto *dbsMenuPtr = new QMenu(this);
    dbsMenuPtr->setAttribute(Qt::WA_DeleteOnClose);
    QMenu &menu = *dbsMenuPtr;
#else
    QMenu menu;
#endif

    PColorScheme scheme = getCurrentScheme();
    if (scheme) {
        if (scheme->customed()) {
            menu.addAction(ui->actionReset_Scheme);
        }
        if (!scheme->bundled()) {
            menu.addAction(ui->actionRename_Scheme);
            menu.addAction(ui->actionDelete_Scheme);
        }
        QString name = ui->cbScheme->currentText();
        if (!mColorManager->exists(name+ " Copy"))
            menu.addAction(ui->actionCopy_Scheme);
        menu.addAction(ui->actionExport_Scheme);
        menu.addSeparator();
    }
    menu.addAction(ui->actionImport_Scheme);
    QPoint p;
    p.setX(0);
    p.setY(ui->btnSchemeMenu->height()+2);
#ifdef __EMSCRIPTEN__
    menu.popup(ui->btnSchemeMenu->mapToGlobal(p));
#else
    menu.exec(ui->btnSchemeMenu->mapToGlobal(p));
#endif
}

void EditorColorSchemeWidget::on_actionImport_Scheme_triggered()
{
#ifdef __EMSCRIPTEN__
    // Part 9 (web build): honest-disable — importing a scheme
    // reads a file from the local disk (static getter = exec()).
    // The built-in schemes and every per-item color stay editable.
    dbsWasmShowInfo(this, tr("Import Scheme"),
                    tr("Importing a color scheme from a file is "
                       "not available in the web version."));
    return;
#endif
    QString filename = QFileDialog::getOpenFileName(this,
        tr("Open"), QString(), tr("Color Scheme Files (*.scheme)"));
    if (filename.isEmpty())
        return;
    QFileInfo fileInfo(filename);
    QString name = fileInfo.fileName();
    QString suffix = EXT_COLOR_SCHEME;
    if (!name.endsWith(suffix, PATH_SENSITIVITY))
        return;
    name.remove(name.length()-suffix.length(),suffix.length());
    name.replace('_',' ');
    if (!mColorManager->isValidName(name)) {
        #ifdef __EMSCRIPTEN__
        dbsWasmShowInfo(this, tr("Error"), tr("'%1' is not a valid name for color scheme file."));
#else
        QMessageBox::critical(this,tr("Error"),tr("'%1' is not a valid name for color scheme file."));
#endif
        return;
    }
    try {
        PColorScheme scheme = ColorScheme::load(filename);
        mColorManager->add(name, scheme);
        ui->cbScheme->addItem(name);
        ui->cbScheme->setCurrentText(name);
    } catch (FileError e) {
        #ifdef __EMSCRIPTEN__
            dbsWasmShowInfo(this, tr("Error"), e.reason());
#else
            QMessageBox::critical(this,tr("Error"),e.reason());
#endif
        return;
    }
}

void EditorColorSchemeWidget::on_actionRename_Scheme_triggered()
{
    QString name = ui->cbScheme->currentText();
#ifdef __EMSCRIPTEN__
    // Part 9 (web build): non-modal input dialog; the validate +
    // rename + refresh body runs from textValueSelected.
    {
        auto *dialog = new QInputDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setInputMode(QInputDialog::TextInput);
        dialog->setWindowTitle(tr("New scheme name"));
        dialog->setLabelText(tr("New scheme name"));
        dialog->setTextValue(name);
        connect(dialog, &QInputDialog::textValueSelected, this,
                [this, name](const QString &newName) {
            if (!mColorManager->isValidName(newName)) {
                dbsWasmShowInfo(this, tr("Error"),
                                tr("'%1' is not a valid scheme name!").arg(newName));
                return;
            }
            try {
                mColorManager->rename(name, newName);
                ui->cbScheme->setItemText(ui->cbScheme->currentIndex(), newName);
                if (mModifiedSchemes.contains(name))
                    mModifiedSchemes.remove(name);
                mModifiedSchemes.insert(newName);
            } catch (FileError e) {
                dbsWasmShowInfo(this, tr("Error"), e.reason());
            }
        });
        dialog->open();
    }
    return;
#endif
    bool isOk;
    QString newName = QInputDialog::getText(this,tr("New scheme name"),tr("New scheme name"),
                                            QLineEdit::Normal,name,&isOk);
    if (isOk) {
        if (!mColorManager->isValidName(newName)) {
            QMessageBox::critical(this,tr("Error"),tr("'%1' is not a valid scheme name!").arg(newName));
            return;
        }
        try {
            mColorManager->rename(name,newName);
            ui->cbScheme->setItemText(
                        ui->cbScheme->currentIndex(),
                        newName
                        );
            if (mModifiedSchemes.contains(name))
                mModifiedSchemes.remove(name);
            mModifiedSchemes.insert(newName);
        } catch(FileError e) {
            #ifdef __EMSCRIPTEN__
            dbsWasmShowInfo(this, tr("Error"), e.reason());
#else
            QMessageBox::critical(this,tr("Error"),e.reason());
#endif
        }
    }
}

void EditorColorSchemeWidget::on_actionReset_Scheme_triggered()
{
    try {
        if (mColorManager->restoreToDefault(ui->cbScheme->currentText())) {
            ui->cbScheme->setItemData(
                        ui->cbScheme->currentIndex(),
                        mDefaultSchemeComboFont,
                        Qt::FontRole);
            ui->cbScheme->setFont(mDefaultSchemeComboFont);
            //ui->cbScheme->view()->setFont(mDefaultSchemeComboFont);
        }
    } catch (FileError e) {
        #ifdef __EMSCRIPTEN__
            dbsWasmShowInfo(this, tr("Error"), e.reason());
#else
            QMessageBox::critical(this,tr("Error"),e.reason());
#endif
    }

}

void EditorColorSchemeWidget::on_actionExport_Scheme_triggered()
{
#ifdef __EMSCRIPTEN__
    // Part 9 (web build): honest-disable — exporting a scheme
    // writes a file to the local disk (static getter = exec()).
    dbsWasmShowInfo(this, tr("Export Scheme"),
                    tr("Exporting a color scheme to a file is "
                       "not available in the web version."));
    return;
#endif
    QString filename = QFileDialog::getSaveFileName(this,
        tr("Save"), QString(), tr("Color Scheme Files (*.scheme)"));
    if (filename.isEmpty())
        return;
    try {
        PColorScheme scheme = getCurrentScheme();
        scheme->save(filename);
    } catch (FileError e) {
        #ifdef __EMSCRIPTEN__
            dbsWasmShowInfo(this, tr("Error"), e.reason());
#else
            QMessageBox::critical(this,tr("Error"),e.reason());
#endif
        return;
    }
}

void EditorColorSchemeWidget::on_actionDelete_Scheme_triggered()
{

    QString name = ui->cbScheme->currentText();
#ifdef __EMSCRIPTEN__
    // Part 9 (web build): the static warning() exec()s — an
    // async box instead; deletion runs only on an explicit Yes.
    {
        auto *box = new QMessageBox(QMessageBox::Warning,
                    tr("Confirm Delete Scheme"),
                    tr("Scheme '%1' will be deleted!<br />Do you really want to continue?").arg(name),
                    QMessageBox::Yes | QMessageBox::No, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        connect(box, &QMessageBox::buttonClicked, this,
                [this, box, name](QAbstractButton *button) {
            if (button != box->button(QMessageBox::Yes))
                return;
            try {
                if (mColorManager->remove(name)) {
                    if (mModifiedSchemes.contains(name))
                        mModifiedSchemes.remove(name);
                    ui->cbScheme->removeItem(ui->cbScheme->currentIndex());
                    if (name == pSettings->editor().colorScheme())
                        doSave();
                }
            } catch (FileError e) {
                dbsWasmShowInfo(this, tr("Error"), e.reason());
            }
        });
        box->open();
    }
    return;
#endif
    if (QMessageBox::warning(this,tr("Confirm Delete Scheme"),
                   tr("Scheme '%1' will be deleted!<br />Do you really want to continue?")
                   .arg(name),
                   QMessageBox::Yes, QMessageBox::No)!=QMessageBox::Yes)
        return;
    try {
        if (mColorManager->remove(name)) {
            if (mModifiedSchemes.contains(name))
                mModifiedSchemes.remove(name);
            ui->cbScheme->removeItem(ui->cbScheme->currentIndex());
            if (name == pSettings->editor().colorScheme())
                doSave();
        }
    }  catch (FileError e) {
        #ifdef __EMSCRIPTEN__
            dbsWasmShowInfo(this, tr("Error"), e.reason());
#else
            QMessageBox::critical(this,tr("Error"),e.reason());
#endif
    }
}



ColorSchemeItemDelegate::ColorSchemeItemDelegate(QObject *parent):
    QStyledItemDelegate{parent}
{

}

void ColorSchemeItemDelegate::initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option,index);
    QVariant value = index.data(Qt::FontRole);
    if (value.isValid() && !value.isNull()) {
        option->font = qvariant_cast<QFont>(value);
        option->fontMetrics = QFontMetrics(option->font);
    }
}
