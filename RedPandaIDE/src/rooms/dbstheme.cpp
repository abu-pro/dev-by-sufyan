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
            // v1.6.1 Part 10: the Normal theme — clean, professional,
            // non-anime. No wallpapers and no loading art on purpose:
            // Home and the splash run the animated "Dev by Sufyan"
            // brand scene instead (see dbsfx / welcomepage).
            QStringLiteral("normal"),
            QStringLiteral("Normal"),
            QStringLiteral("Normal"),
            QColor(QStringLiteral("#E5484D")),
            {},
            QString(),
            QString(),
            QString(), QString(), QString(), QString(), QString(),
            QString(),
            {},
        },
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
                // v1.6.1: the rest of the user's Demon Slayer
                // collection joins Home rotation — every one was
                // composition-audited so its face zone (if any)
                // stays clear of the tile column (see
                // artInfoFor in welcomepage.cpp).
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-05.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-06.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-07.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-08.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-09.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-10.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-11.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-12.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-13.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-14.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-15.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-16.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-17.jpg"),
            },
            kThemesDir
                + QStringLiteral("/demonslayer/theme-ds-loading.jpg"),
            kThemesDir + QStringLiteral("/demonslayer/icon.png"),
            QStringLiteral(":/icons/images/themes/demonslayer/icons/folder.png"),
            QStringLiteral(":/icons/images/themes/demonslayer/icons/file-cpp.png"),
            QStringLiteral(":/icons/images/themes/demonslayer/icons/file-h.png"),
            QStringLiteral(":/icons/images/themes/demonslayer/icons/file-c.png"),
            QStringLiteral(":/icons/images/themes/demonslayer/icons/file-py.png"),
            QStringLiteral(":/icons/images/themes/demonslayer/icons/file-generic.png"),
            {{QStringLiteral("new"), QStringLiteral(":/icons/images/themes/demonslayer/icons/tile-new.png")}, {QStringLiteral("open"), QStringLiteral(":/icons/images/themes/demonslayer/icons/tile-open.png")}, {QStringLiteral("project"), QStringLiteral(":/icons/images/themes/demonslayer/icons/tile-project.png")}, {QStringLiteral("continue"), QStringLiteral(":/icons/images/themes/demonslayer/icons/tile-continue.png")}, {QStringLiteral("rooms"), QStringLiteral(":/icons/images/themes/demonslayer/icons/tile-rooms.png")}, {QStringLiteral("font"), QStringLiteral(":/icons/images/themes/demonslayer/icons/tile-font.png")}, {QStringLiteral("templates"), QStringLiteral(":/icons/images/themes/demonslayer/icons/tile-templates.png")}, {QStringLiteral("theme"), QStringLiteral(":/icons/images/themes/demonslayer/icons/tile-theme.png")}},
            // v1.6.1 backdropArt: faces fill these frames (demon
            // face strips, the Hashira group, Tanjiro & Nezuko in
            // flames x3 grades, the B/W Tanjiro close-up, the two
            // mask-motif patterns) — loading-screen / backdrop
            // rotation only, never Home.
            {
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-bg01.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-bg02.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-bg03.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-bg04.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-bg05.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-bg06.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-bg07.jpg"),
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-bg08.jpg"),
                // The centred "Danger High Voltage" sign: its
                // chibi face sits mid-frame, reachable by either
                // tile column — backdrop rotation only.
                kThemesDir
                    + QStringLiteral("/demonslayer/theme-ds-bg09.jpg"),
            },
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
                // v1.6.1: the rest of the user's Dragon Ball
                // collection joins Home rotation (composition-
                // audited like the Demon Slayer additions).
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-03.jpg"),
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-04.jpg"),
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-05.jpg"),
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-06.jpg"),
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-07.jpg"),
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-08.jpg"),
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-09.jpg"),
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-10.jpg"),
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-11.jpg"),
            },
            kThemesDir
                + QStringLiteral("/dragonball/theme-db-loading.jpg"),
            kThemesDir + QStringLiteral("/dragonball/icon.png"),
            QStringLiteral(":/icons/images/themes/dragonball/icons/folder.png"),
            QStringLiteral(":/icons/images/themes/dragonball/icons/file-cpp.png"),
            QStringLiteral(":/icons/images/themes/dragonball/icons/file-h.png"),
            QStringLiteral(":/icons/images/themes/dragonball/icons/file-c.png"),
            QStringLiteral(":/icons/images/themes/dragonball/icons/file-py.png"),
            QStringLiteral(":/icons/images/themes/dragonball/icons/file-generic.png"),
            {{QStringLiteral("new"), QStringLiteral(":/icons/images/themes/dragonball/icons/tile-new.png")}, {QStringLiteral("open"), QStringLiteral(":/icons/images/themes/dragonball/icons/tile-open.png")}, {QStringLiteral("project"), QStringLiteral(":/icons/images/themes/dragonball/icons/tile-project.png")}, {QStringLiteral("continue"), QStringLiteral(":/icons/images/themes/dragonball/icons/tile-continue.png")}, {QStringLiteral("rooms"), QStringLiteral(":/icons/images/themes/dragonball/icons/tile-rooms.png")}, {QStringLiteral("font"), QStringLiteral(":/icons/images/themes/dragonball/icons/tile-font.png")}, {QStringLiteral("templates"), QStringLiteral(":/icons/images/themes/dragonball/icons/tile-templates.png")}, {QStringLiteral("theme"), QStringLiteral(":/icons/images/themes/dragonball/icons/tile-theme.png")}},
            // v1.6.1 backdropArt: the centred Ultra Instinct
            // silhouette, the frame-filling Kid Goku face and the
            // classic cast group — loading-screen / backdrop
            // rotation only, never Home.
            {
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-bg01.jpg"),
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-bg02.jpg"),
                kThemesDir
                    + QStringLiteral("/dragonball/theme-db-bg03.jpg"),
            },
        },
        {
            QStringLiteral("mha"),
            QStringLiteral("My Hero Academia"),
            QStringLiteral("My Hero Academia"),
            QColor(QStringLiteral("#35D07F")),
            {
                // The Tsuyu pond shot keeps its face zone clear of
                // the Home tile column. v1.6.1: the two Bakugo
                // artworks from the same folder join it — in these
                // compositions his face sits fully on the right
                // with the lettering-only left side under the
                // tiles (composition-audited; see artInfoFor).
                kThemesDir + QStringLiteral("/mha/theme-mha-01.jpg"),
                kThemesDir + QStringLiteral("/mha/theme-mha-03.jpg"),
                kThemesDir + QStringLiteral("/mha/theme-mha-04.jpg"),
            },
            // The All Might manga poster doubles as loading art.
            kThemesDir + QStringLiteral("/mha/theme-mha-02.jpg"),
            QString(),
            QString(), QString(), QString(), QString(), QString(),
            QString(),
            {},
            // v1.6.1 backdropArt: the dark Tsuyu grade, the light
            // All Might collage and the two cast-group grades —
            // faces everywhere, so loading-screen / backdrop
            // rotation only, never Home.
            {
                kThemesDir + QStringLiteral("/mha/theme-mha-bg01.jpg"),
                kThemesDir + QStringLiteral("/mha/theme-mha-bg02.jpg"),
                kThemesDir + QStringLiteral("/mha/theme-mha-bg03.jpg"),
                kThemesDir + QStringLiteral("/mha/theme-mha-bg04.jpg"),
            },
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


const DbsThemePack *dbsResolvedPack(const QString &schemeName)
{
    if (const DbsThemePack *pack = dbsThemePackById(dbsActivePackId()))
        return pack;
    if (const DbsThemePack *pack = dbsPackForScheme(schemeName))
        return pack;
    // The Normal theme is the default only when no theme choice
    // exists at all: the packs store has never recorded one (the
    // key is written on every apply, so its absence means the
    // user has never run the theme system or picked anything).
    // Note the editor scheme cannot decide this — the upstream
    // first-run wizard itself assigns one ("VS Code") before the
    // user has chosen anything.
    QSettings settings(QStringLiteral("DevBySufyan"),
                       QStringLiteral("Dev by Sufyan"));
    if (!settings.contains(QStringLiteral("dbs/themePack")))
        return dbsThemePackById(QStringLiteral("normal"));
    // A recorded-but-empty choice (Classic picked, or a scheme
    // chosen on purpose) stands: keep the classic experience.
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
    // v1.6.1: the rest of the pack's collection is listed too,
    // greyed out — these artworks' faces fill the frame, so they
    // rotate through the loading screen and the panel backdrop
    // instead of Home and cannot be pinned here.
    if (pack) {
        for (const QString &extra : pack->backdropArt) {
            QPixmap pix(extra);
            if (pix.isNull())
                continue;
            auto *item = new QListWidgetItem(
                QIcon(pix.scaled(208, 117, Qt::KeepAspectRatioByExpanding,
                                 Qt::SmoothTransformation)),
                QFileInfo(extra).fileName()
                    + QObject::tr(" (loading screen)"),
                list);
            item->setFlags(Qt::NoItemFlags);
            item->setToolTip(
                QObject::tr("This picture's faces fill the frame, "
                            "so it rotates through the loading "
                            "screen and the panel backdrop instead "
                            "of Home."));
        }
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

#ifdef __EMSCRIPTEN__
#include <QMessageBox>
// Part 9 (web build): the non-modal twin of
// dbsShowCustomizeHomeDialog — identical contents and accept
// behavior (the pin is written with dbsSetPinnedHomeArt on
// accept), but the dialog is heap-allocated, shown with open(),
// and reports through a continuation instead of blocking in
// exec() (the WASM wall).
void dbsShowCustomizeHomeDialogAsync(
        QWidget *parent, const DbsThemePack *pack,
        std::function<void(bool changed)> onDone)
{
    const QString packId = pack ? pack->id
                                : QStringLiteral("classic");
    const QStringList arts = pack ? pack->wallpapers
                                  : dbsClassicArtworks();
    if (arts.isEmpty()) {
        onDone(false);
        return;
    }
    const QString currentPin = dbsPinnedHomeArt(packId);

    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QCoreApplication::translate(
                "dbs", "Customize Home"));
    dialog->resize(680, 520);

    auto *layout = new QVBoxLayout(dialog);
    auto *label = new QLabel(QCoreApplication::translate("dbs",
                "Choose the picture for the Home background. "
                "\"Rotate all wallpapers\" shows a different one "
                "each time."), dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *list = new QListWidget(dialog);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(208, 117));
    list->setGridSize(QSize(224, 160));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setSpacing(8);
    auto addArt = [list](const QString &path, bool enabled) {
        QPixmap pm(path);
        auto *item = new QListWidgetItem(
                    QIcon(pm.scaled(416, 234,
                                    Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation)),
                    QFileInfo(path).completeBaseName());
        item->setData(Qt::UserRole, path);
        item->setSizeHint(QSize(216, 152));
        if (!enabled)
            item->setFlags(Qt::NoItemFlags);
        list->addItem(item);
        return item;
    };
    auto *rotate = new QListWidgetItem(QCoreApplication::translate(
                "dbs", "Rotate all wallpapers"), list);
    rotate->setData(Qt::UserRole, QString());
    rotate->setSizeHint(QSize(216, 152));
    int selectRow = 0;
    int row = 1;
    for (const QString &path : arts) {
        addArt(path, true);
        if (path == currentPin)
            selectRow = row;
        ++row;
    }
    if (pack) {
        for (const QString &path : pack->backdropArt) {
            auto *item = addArt(path, false);
            item->setText(QCoreApplication::translate("dbs",
                        "%1 (loading screen)")
                          .arg(QFileInfo(path).completeBaseName()));
            item->setToolTip(QCoreApplication::translate("dbs",
                        "This one fills the whole frame, so it "
                        "rotates through the loading screen and "
                        "the window backdrop instead of Home."));
        }
    }
    list->setCurrentRow(selectRow);
    layout->addWidget(list, 1);

    auto *buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     dialog, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemDoubleClicked,
                     dialog, [dialog](QListWidgetItem *) {
        dialog->accept();
    });
    QObject::connect(dialog, &QDialog::accepted, dialog,
                     [list, currentPin, packId, onDone]() {
        QListWidgetItem *item = list->currentItem();
        bool changed = false;
        if (item) {
            const QString chosen =
                    item->data(Qt::UserRole).toString();
            if (chosen != currentPin) {
                dbsSetPinnedHomeArt(packId, chosen);
                changed = true;
            }
        }
        onDone(changed);
    });
    QObject::connect(dialog, &QDialog::rejected, dialog,
                     [onDone]() { onDone(false); });
    dialog->open();  // non-modal — never exec()
}

void dbsWasmShowInfo(QWidget *parent, const QString &title,
                     const QString &text)
{
    auto *box = new QMessageBox(QMessageBox::Information, title,
                                text, QMessageBox::Ok, parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();  // non-modal — the statics exec() under the hood
}
#endif // __EMSCRIPTEN__
