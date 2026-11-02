/*
 * Copyright (C) 2026 Abu Sufyan
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
#ifndef ROOMS_DBSUPDATER_H
#define ROOMS_DBSUPDATER_H

// Dev by Sufyan v1.6.0 — resumable, stall-proof update downloading.
//
// The v1.5.0 in-app updater downloaded the setup into the temp folder
// in one uninterruptible stream: a stall hung it forever, and any
// failure threw the whole download away, so the update "downloaded
// but never installed" on a slow connection. This engine replaces
// that download:
//  - the partial file lives in a PERSISTENT updates folder as
//    "DevBySufyan-Setup.exe.part" and is never deleted on failure or
//    cancel, so the next attempt continues where it stopped;
//  - a resume issues an HTTP Range request and appends (a server
//    that answers 200 instead restarts the file cleanly);
//  - a stall watchdog aborts a reply that delivers no bytes for the
//    stall timeout and reconnects from the current offset (bounded
//    attempts), instead of hanging;
//  - the download only counts as complete when the final size equals
//    the size the releases API reported; only then is the .part
//    renamed to the real .exe name.
//
// The engine is UI-free so the standalone harness in harness-update/
// can drive the real code against a local test server.

#include <QDialog>
#include <QObject>
#include <QString>
#include <QUrl>

class QFile;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QTimer;

class DbsUpdateDownloader : public QObject
{
    Q_OBJECT
public:
    explicit DbsUpdateDownloader(QObject *parent = nullptr);
    ~DbsUpdateDownloader() override;

    // userAgent: the User-Agent header value for the requests.
    // expectedSize: byte size from the releases API (0 = unknown).
    void start(const QUrl &url, const QString &partPath,
               const QString &finalPath, qint64 expectedSize,
               const QString &userAgent);
    void cancel(); // stops; the .part file is KEPT for a later resume

    bool isRunning() const;

    // Test hooks (the dialog keeps the defaults: 20 s / 5 attempts).
    void setStallTimeoutMs(int ms) { mStallTimeoutMs = ms; }
    void setMaxResumeAttempts(int count) { mMaxResumeAttempts = count; }
    void setResumeDelayMs(int ms) { mResumeDelayMs = ms; }

    QString partPath() const { return mPartPath; }
    QString finalPath() const { return mFinalPath; }
    qint64 downloadedBytes() const; // current size of the .part file

signals:
    void statusChanged(const QString &text);
    void progressChanged(qint64 received, qint64 total); // total < 0: unknown
    void finished(bool ok, bool cancelled, const QString &message);

private:
    void issueRequest();
    void checkResponseHeaders(QNetworkReply *reply);
    void scheduleResume(const QString &reason, bool freshRestart);
    void completeDownload();
    void failDownload(const QString &message);
    void stopReply();
    qint64 partFileSize() const;

    QUrl mUrl;
    QString mPartPath;
    QString mFinalPath;
    QString mUserAgent;
    qint64 mExpectedSize = 0;

    QNetworkAccessManager *mNet = nullptr;
    QNetworkReply *mReply = nullptr;
    QFile *mFile = nullptr;
    QTimer *mWatchdog = nullptr;
    QTimer *mResumeTimer = nullptr;

    qint64 mBaseOffset = 0;   // bytes already in the file when the
                              // current request started
    qint64 mSessionBytes = 0; // bytes written by the current request
    int mResumeAttempts = 0;
    int mStallTimeoutMs = 20000;
    int mMaxResumeAttempts = 5;
    int mResumeDelayMs = 1500;
    bool mRunning = false;
    bool mCancelled = false;
    bool mWatchdogFired = false;
    bool mHeaderChecked = false;
    bool mCompleteVia416 = false;
    bool mRestartVia416 = false;
};

// The persistent post-download panel: shows the download progress,
// and when the setup is complete it stays open with the exact saved
// path and an "Open Installer" button, so a setup that could not be
// launched automatically is still one click (or one manual run) away.
class DbsUpdateDialog : public QDialog
{
    Q_OBJECT
public:
    DbsUpdateDialog(const QUrl &url, const QString &version,
                    qint64 expectedSize, const QString &userAgent,
                    QWidget *parent = nullptr);
    void startDownload();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setCompleteState(bool alreadyDownloaded);
    void tryLaunchInstaller(bool automatic);
    QString updatesDir() const;

    QUrl mUrl;
    QString mVersion;
    qint64 mExpectedSize = 0;
    QString mUserAgent;
    QString mFinalPath;
    QString mPartPath;

    DbsUpdateDownloader *mDownloader = nullptr;
    QLabel *mTitleLabel = nullptr;
    QLabel *mStatusLabel = nullptr;
    QLabel *mDetailLabel = nullptr;
    QLabel *mPathLabel = nullptr;
    QProgressBar *mBar = nullptr;
    QPushButton *mInstallButton = nullptr;
    QPushButton *mBrowserButton = nullptr;
    QPushButton *mCloseButton = nullptr;
    bool mComplete = false;
    bool mAutoLaunchTried = false;
};

// The app points the updater at the folder holding its own
// settings file (persistent, and immune to the upstream habit
// of rewriting the application name for window titles, which
// moves AppDataLocation). Empty anchor -> AppDataLocation
// fallback.
void dbsSetUpdatesDirAnchor(const QString &dir);

#endif // ROOMS_DBSUPDATER_H
