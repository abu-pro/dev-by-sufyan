// dbstheme.h — Dev by Sufyan v1.6.0: full theme packs.
//
// A theme pack ties together everything that makes a theme "proper":
// an editor colour scheme, an accent colour, a curated set of Home
// wallpapers (hero first), loading-screen art and a special-effects
// kind. The packs built here use only assets the user supplied for
// v1.6.0 (see theme-assets/ in the project workspace); every Home
// wallpaper was picked so the main character's face sits clear of the
// Home tile column and stays fully visible.
#ifndef DBSTHEME_H
#define DBSTHEME_H

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>

class QWidget;

struct DbsThemePack {
    QString id;              // "demonslayer" | "dragonball" | "mha"
    QString name;            // "Demon Slayer"
    QString schemeName;      // editor colour scheme applied with the pack
    QColor accent;
    QStringList wallpapers;  // Qt resource paths, hero wallpaper first
    QString loadingArt;      // Qt resource path (may repeat a wallpaper)
    QString iconPath;        // Qt resource path, may be empty
    // User-supplied icon-pack wiring (v1.6.1): Explorer icons by
    // file kind + Home tile leading icons by tile id, all Qt
    // resource paths; empty means "keep the stock icons".
    QString explorerFolder;
    QString explorerFileCpp;
    QString explorerFileH;
    QString explorerFileC;
    QString explorerFilePy;
    QString explorerFileGeneric;
    QHash<QString, QString> tileIcons;  // "new" -> App Icon path
    // v1.6.1: the rest of the user's collection — artworks whose
    // faces fill the frame, so no Home tile column can ever stay
    // clear of them. They rotate through the loading screen and
    // the IDE panel backdrop instead of Home, and Customize Home
    // lists them (greyed out) so the full collection is visible.
    QStringList backdropArt;  // Qt resource paths
};

const QVector<DbsThemePack> &dbsThemePacks();
// The pack in effect for a cold start: the saved pack, else the
// pack whose scheme is in use, else nullptr for the classic anime
// schemes / a deliberately saved scheme, else the Normal theme —
// the v1.6.1 default when no theme choice exists at all.
const DbsThemePack *dbsResolvedPack(const QString &schemeName);
const DbsThemePack *dbsThemePackById(const QString &id);
const DbsThemePack *dbsPackForScheme(const QString &schemeName);

// Persisted choices (QSettings, "dbs/..." keys).
QString dbsActivePackId();  // "" = no pack (classic themes)
void dbsSetActivePackId(const QString &id);
// Which wallpaper of a set is pinned as the Home background;
// "" means "rotate all of them". Pack id "classic" covers the
// built-in welcome artworks when no pack is active.
QString dbsPinnedHomeArt(const QString &packId);
void dbsSetPinnedHomeArt(const QString &packId, const QString &path);
bool dbsAnimationsEnabled();  // v1.6.0 Part 7, default: on
void dbsSetAnimationsEnabled(bool on);

// The built-in welcome artworks (resource paths), same scan the
// WelcomePage performs (bundled art + user drop-in folders).
QStringList dbsClassicArtworks();

// Thumbnail picker for the Home background of the active set.
// pack == nullptr picks among the classic artworks. Returns true
// when the saved choice changed.
bool dbsShowCustomizeHomeDialog(QWidget *parent,
                                const DbsThemePack *pack);

#endif // DBSTHEME_H
