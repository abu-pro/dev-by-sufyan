// dbstheme.cpp — see dbstheme.h for the design notes.
#include "dbstheme.h"

#include "dbsfx.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
const QString kThemesDir = QStringLiteral(":/icons/images/themes");
}

const QVector<DbsThemePack> &dbsThemePacks()
{
    static const QVector<DbsThemePack> packs = {
        {
            QStringLiteral("demonslayer"),
            QStringLiteral("Demon Slayer"),
            QStringLiteral("Demon Slayer"),
            QColor(QStringLiteral("#FFC21C")),
            {
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-01.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-02.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-03.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-04.jpg"),
            },
            kThemesDir
                + QStringLiteral("/demonslayer/theme-ds-loading.jpg"),
            kThemesDir + QStringLiteral("/demonslayer/icon.png"),
            DbsFxKind::DemonSlayer,
        },
        {
            QStringLiteral("dragonball"),
            QStringLiteral("Dragon Ball"),
            QStringLiteral("Dragon Ball"),
            QColor(QStringLiteral("#FFB300")),
            {
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-01.jpg"),
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-02.jpg"),
            },
            kThemesDir
                + QStringLiteral("/dragonball/theme-db-loading.jpg"),
            kThemesDir + QStringLiteral("/dragonball/icon.png"),
            DbsFxKind::DragonBall,
        },
        {
            QStringLiteral("mha"),
            QStringLiteral("My Hero Academia"),
            QStringLiteral("My Hero Academia"),
            QColor(QStringLiteral("#35D07F")),
            {
                // Only the Tsuyu pond shot of the user's MHA folder
                // keeps its face zone completely clear of the Home
                // tile column; Bakugo and the All Might poster carry
                // faces/hair under any tile placement, so they are
                // not Home backgrounds (the poster is loading art).
                kThemesDir + QStringLiteral("/mha/theme-mha-01.jpg"),
            },
            // The All Might manga poster doubles as loading art.
            kThemesDir + QStringLiteral("/mha/theme-mha-02.jpg"),
            QString(),
            DbsFxKind::Mha,
        },
    };
    return packs;
}

const DbsThemePack *dbsThemePackById(const QString &id)
{
    if (id.isEmpty())
        return nullptr;
    for (const DbsThemePack &pack : dbsThemePacks())
        if (pack.id == id)
            return &pack;
    return nullptr;
}

const DbsThemePack *dbsPackForScheme(const QString &schemeName)
{
    for (const DbsThemePack &pack : dbsThemePacks())
        if (pack.schemeName == schemeName)
            return &pack;
    return nullptr;
}

// NOTE: every QSettings in this unit is pinned to a fixed
// organization/application pair on purpose. The upstream code
// rewrites QApplication's application name for window titles
// ("file.cpp - Dev by Sufyan"), so a bare QSettings() would
// resolve to a DIFFERENT store depending on which file is open
// -- the splash (created before the main window) even read a
// store that never had the theme pack in it. The pinned pair
// keeps dbs/* keys in exactly one place on every platform.
QString dbsActivePackId()
{
    QSettings settings(QStringLiteral("DevBySufyan"),
                       QStringLiteral("Dev by Sufyan"));
    return settings.value(QStringLiteral("dbs/themePack")).toString();
}

void dbsSetActivePackId(const QString &id)
{
    QSettings settings(QStringLiteral("DevBySufyan"),
                       QStringLiteral("Dev by Sufyan"));
    settings.setValue(QStringLiteral("dbs/themePack"), id);
}

QString dbsPinnedHomeArt(const QString &packId)
{
    QSettings settings(QStringLiteral("DevBySufyan"),
                       QStringLiteral("Dev by Sufyan"));
    return settings
        .value(QStringLiteral("dbs/homeArt/") + packId)
        .toString();
}

void dbsSetPinnedHomeArt(const QString &packId, const QString &path)
{
    QSettings settings(QStringLiteral("DevBySufyan"),
                       QStringLiteral("Dev by Sufyan"));
    settings.setValue(QStringLiteral("dbs/homeArt/") + packId, path);
}

bool dbsSpecialEffectsEnabled()
{
    QSettings settings(QStringLiteral("DevBySufyan"),
                       QStringLiteral("Dev by Sufyan"));
    return settings
        .value(QStringLiteral("dbs/specialEffects"), true)
        .toBool();
}

void dbsSetSpecialEffectsEnabled(bool on)
{
    QSettings settings(QStringLiteral("DevBySufyan"),
                       QStringLiteral("Dev by Sufyan"));
    settings.setValue(QStringLiteral("dbs/specialEffects"), on);
}

bool dbsAnimationsEnabled()
{
    QSettings settings(QStringLiteral("DevBySufyan"),
                       QStringLiteral("Dev by Sufyan"));
    return settings.value(QStringLiteral("dbs/animations"), true)
        .toBool();
}

void dbsSetAnimationsEnabled(bool on)
{
    QSettings settings(QStringLiteral("DevBySufyan"),
                       QStringLiteral("Dev by Sufyan"));
    settings.setValue(QStringLiteral("dbs/animations"), on);
}

QStringList dbsClassicArtworks()
{
    // Mirrors WelcomePage::loadArtworks(): bundled art first, then
    // user drop-ins next to the executable and in the app data dir.
    QStringList files;
    QStringList names;
    for (int i = 1; i <= 30; i++) {
        QString name = QStringLiteral("welcome-art-%1.jpg")
                           .arg(i, 2, 10, QLatin1Char('0'));
        QString path = QStringLiteral(":/icons/images/") + name;
        if (QFileInfo::exists(path)) {
            files << path;
            names << name;
        }
    }
    QStringList dropDirs = {
        QDir(QApplication::applicationDirPath())
            .filePath(QStringLiteral("welcome-art")),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/welcome-art"),
    };
    const QStringList filters = {QStringLiteral("*.jpg"),
                                 QStringLiteral("*.jpeg"),
                                 QStringLiteral("*.png")};
    for (const QString &dirPath : dropDirs) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;
        const QFileInfoList entries =
            dir.entryInfoList(filters, QDir::Files, QDir::Name);
        for (const QFileInfo &info : entries) {
            if (names.contains(info.fileName()))
                continue;  // bundled art wins over a same-named drop-in
            files << info.absoluteFilePath();
            names << info.fileName();
        }
    }
    return files;
}

bool dbsShowCustomizeHomeDialog(QWidget *parent,
                                const DbsThemePack *pack)
{
    const QString packId =
        pack ? pack->id : QStringLiteral("classic");
    const QStringList arts =
        pack ? pack->wallpapers : dbsClassicArtworks();
    if (arts.isEmpty())
        return false;
    const QString currentPin = dbsPinnedHomeArt(packId);

    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Customize Home"));
    dialog.resize(680, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(
        pack ? QObject::tr("Pick the Home background for the %1 theme. "
                           "The faces in every picture stay clear of "
                           "the Home tiles.")
                 .arg(pack->name)
             : QObject::tr("Pick the Home background. The faces in "
                           "every picture stay clear of the Home "
                           "tiles."),
        &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *list = new QListWidget(&dialog);
    list->setViewMode(QListWidget::IconMode);
    list->setIconSize(QSize(208, 117));
    list->setGridSize(QSize(224, 152));
    list->setMovement(QListWidget::Static);
    list->setResizeMode(QListWidget::Adjust);
    list->setSpacing(6);
    list->setWordWrap(true);

    auto *rotateItem = new QListWidgetItem(
        QObject::tr("Rotate all wallpapers"), list);
    rotateItem->setData(Qt::UserRole, QString());
    rotateItem->setToolTip(
        QObject::tr("Show a different wallpaper on every start."));
    int selectRow = 0;
    for (int i = 0; i < arts.size(); i++) {
        QPixmap pix(arts.at(i));
        auto *item = new QListWidgetItem(
            QIcon(pix.scaled(208, 117, Qt::KeepAspectRatioByExpanding,
                             Qt::SmoothTransformation)),
            QFileInfo(arts.at(i)).fileName(), list);
        item->setData(Qt::UserRole, arts.at(i));
        if (arts.at(i) == currentPin)
            selectRow = i + 1;
    }
    list->setCurrentRow(selectRow);
    layout->addWidget(list, 1);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog,
                     [&dialog](QListWidgetItem *) { dialog.accept(); });

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);
    layout->addWidget(buttons);

    dbsFadeInWindow(&dialog);  // Part 7 fade-in
    if (dialog.exec() != QDialog::Accepted)
        return false;
    QListWidgetItem *item = list->currentItem();
    if (!item)
        return false;
    const QString chosen = item->data(Qt::UserRole).toString();
    if (chosen == currentPin)
        return false;
    dbsSetPinnedHomeArt(packId, chosen);
    return true;
}
