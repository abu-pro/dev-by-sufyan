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

class QWidget;

// Ambient effect families, one per pack (see dbsfx.h).
enum class DbsFxKind { None = 0, DemonSlayer, DragonBall, Mha };

struct DbsThemePack {
    QString id;              // "demonslayer" | "dragonball" | "mha"
    QString name;            // "Demon Slayer"
    QString schemeName;      // editor colour scheme applied with the pack
    QColor accent;
    QStringList wallpapers;  // Qt resource paths, hero wallpaper first
    QString loadingArt;      // Qt resource path (may repeat a wallpaper)
    QString iconPath;        // Qt resource path, may be empty
    DbsFxKind fxKind = DbsFxKind::None;
};

const QVector<DbsThemePack> &dbsThemePacks();
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
bool dbsSpecialEffectsEnabled();  // default: on
void dbsSetSpecialEffectsEnabled(bool on);
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
