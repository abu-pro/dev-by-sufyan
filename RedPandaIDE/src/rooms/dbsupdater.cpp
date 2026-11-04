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
#include "dbsupdater.h"


#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString mbText(qint64 bytes)
{
    return QString::number(bytes / 1048576.0, 'f', 1) + " MB";
}

} // namespace

// ---------------------------------------------------------------------------
// DbsUpdateDownloader
// ---------------------------------------------------------------------------

DbsUpdateDownloader::DbsUpdateDownloader(QObject *parent) :
    QObject{parent}
{
    mWatchdog = new QTimer{this};
    mWatchdog->setSingleShot(true);
    connect(mWatchdog, &QTimer::timeout, this, [this]() {
        if (mReply) {
            // No bytes for the whole stall window: abort; the reply's
            // finished handler schedules a resume from the offset.
            mWatchdogFired = true;
            mReply->abort();
        }
    });
    mResumeTimer = new QTimer{this};
    mResumeTimer->setSingleShot(true);
    connect(mResumeTimer, &QTimer::timeout, this, [this]() {
        if (mRunning && !mCancelled)
            issueRequest();
    });
}

DbsUpdateDownloader::~DbsUpdateDownloader()
{
    mCancelled = true;
    stopReply();
    if (mFile) {
        mFile->close();
        delete mFile;
        mFile = nullptr;
    }
}

bool DbsUpdateDownloader::isRunning() const
{
    return mRunning;
}

qint64 DbsUpdateDownloader::partFileSize() const
{
    return QFileInfo{mPartPath}.size();
}

qint64 DbsUpdateDownloader::downloadedBytes() const
{
    if (QFileInfo::exists(mFinalPath))
        return QFileInfo{mFinalPath}.size();
    return partFileSize();
}

void DbsUpdateDownloader::start(const QUrl &url, const QString &partPath,
                                const QString &finalPath,
                                qint64 expectedSize,
                                const QString &userAgent)
{
    if (mRunning)
        return;
    mUrl = url;
    mPartPath = partPath;
    mFinalPath = finalPath;
    mExpectedSize = expectedSize;
    mUserAgent = userAgent;
    mCancelled = false;
    mResumeAttempts = 0;
    mRunning = true;

    QDir{}.mkpath(QFileInfo{mPartPath}.absolutePath());

    // Already fully downloaded in an earlier session: nothing to do.
    // (Emissions are deferred: start() may be called before the
    // caller's event loop is running, and a synchronous finished()
    // would be lost.)
    if (mExpectedSize > 0 && QFileInfo::exists(mFinalPath)
            && QFileInfo{mFinalPath}.size() == mExpectedSize) {
        QTimer::singleShot(0, this, [this]() {
            if (!mRunning)
                return;
            mRunning = false;
            emit progressChanged(mExpectedSize, mExpectedSize);
            emit finished(true, false,
                          tr("This update was already downloaded."));
        });
        return;
    }
    // The .part already holds the whole file (e.g. the rename never
    // happened): verify the size and finish without any network.
    if (mExpectedSize > 0 && partFileSize() == mExpectedSize) {
        QTimer::singleShot(0, this, [this]() {
            if (mRunning)
                completeDownload();
        });
        return;
    }
    issueRequest();
}

void DbsUpdateDownloader::cancel()
{
    if (!mRunning)
        return;
    mCancelled = true;
    mWatchdog->stop();
    mResumeTimer->stop();
    if (mReply) {
        mReply->abort(); // finished handler reports the cancel
    } else {
        mRunning = false;
        emit finished(false, true,
                      tr("Download stopped. The part downloaded so "
                         "far (%1) is saved and will continue next "
                         "time.").arg(mbText(partFileSize())));
    }
}

void DbsUpdateDownloader::stopReply()
{
    if (mReply) {
        QNetworkReply *reply = mReply;
        mReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
}

void DbsUpdateDownloader::issueRequest()
{
    if (!mNet)
        mNet = new QNetworkAccessManager{this};

    mBaseOffset = QFileInfo::exists(mPartPath) ? partFileSize() : 0;
    // A partial bigger than the announced size cannot be trusted.
    if (mExpectedSize > 0 && mBaseOffset > mExpectedSize) {
        QFile::remove(mPartPath);
        mBaseOffset = 0;
    }
    if (mFile) {
        mFile->close();
        delete mFile;
        mFile = nullptr;
    }
    mFile = new QFile{mPartPath};
    const QIODevice::OpenMode mode = mBaseOffset > 0
            ? QIODevice::Append : QIODevice::WriteOnly;
    if (!mFile->open(mode)) {
        delete mFile;
        mFile = nullptr;
        failDownload(tr("The setup could not be saved on this "
                        "computer (the updates folder is not "
                        "writable)."));
        return;
    }
    mSessionBytes = 0;
    mHeaderChecked = false;
    mCompleteVia416 = false;
    mRestartVia416 = false;
    mWatchdogFired = false;

    QNetworkRequest request{mUrl};
    request.setHeader(QNetworkRequest::UserAgentHeader, mUserAgent);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (mBaseOffset > 0)
        request.setRawHeader("Range",
                             "bytes=" + QByteArray::number(mBaseOffset)
                             + "-");
    mReply = mNet->get(request);
    QNetworkReply *reply = mReply;
    mWatchdog->start(mStallTimeoutMs);

    connect(reply, &QNetworkReply::metaDataChanged, this,
            [this, reply]() { checkResponseHeaders(reply); });
    connect(reply, &QNetworkReply::readyRead, this,
            [this, reply]() {
        if (reply != mReply)
            return;
        checkResponseHeaders(reply);
        const QByteArray data = reply->readAll();
        if (!data.isEmpty() && mFile) {
            mFile->write(data);
            mFile->flush();
            mSessionBytes += data.size();
            mWatchdog->start(mStallTimeoutMs);
            const qint64 received = mBaseOffset + mSessionBytes;
            const qint64 total = mExpectedSize > 0 ? mExpectedSize : -1;
            emit progressChanged(received, total);
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() {
        if (reply != mReply)
            return;
        mReply = nullptr;
        mWatchdog->stop();
        // Trailing bytes that never raised readyRead.
        checkResponseHeaders(reply);
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty() && mFile) {
            mFile->write(tail);
            mSessionBytes += tail.size();
        }
        if (mFile) {
            mFile->close();
            delete mFile;
            mFile = nullptr;
        }
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();

        if (mCancelled) {
            mRunning = false;
            emit finished(false, true,
                          tr("Download stopped. The part downloaded "
                             "so far (%1) is saved and will continue "
                             "next time.").arg(mbText(partFileSize())));
            return;
        }
        if (mCompleteVia416) {
            completeDownload();
            return;
        }
        if (mRestartVia416) {
            QFile::remove(mPartPath);
            scheduleResume(tr("The saved partial download did not "
                              "match this update"), true);
            return;
        }
        if (error == QNetworkReply::NoError) {
            const qint64 size = partFileSize();
            if (mExpectedSize > 0 && size < mExpectedSize) {
                scheduleResume(tr("The connection closed early"),
                               false);
            } else if (mExpectedSize > 0 && size > mExpectedSize) {
                QFile::remove(mPartPath);
                scheduleResume(tr("The downloaded data did not match "
                                  "the expected size"), true);
            } else {
                completeDownload();
            }
            return;
        }
        if (mWatchdogFired) {
            scheduleResume(tr("The download stalled (no data "
                              "arrived)"), false);
            return;
        }
        scheduleResume(errorText, false);
    });
}

void DbsUpdateDownloader::checkResponseHeaders(QNetworkReply *reply)
{
    if (mHeaderChecked || reply != mReply)
        return;
    const QVariant statusVar = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusVar.isValid())
        return; // headers not in yet
    mHeaderChecked = true;
    const int status = statusVar.toInt();
    if (status == 416) { // Range Not Satisfiable
        if (mExpectedSize > 0 && mBaseOffset >= mExpectedSize)
            mCompleteVia416 = true;
        else
            mRestartVia416 = true;
        return;
    }
    if (mBaseOffset > 0) {
        if (status == 206) {
            emit statusChanged(
                        tr("Resumed at %1 — continuing the earlier "
                           "download.").arg(mbText(mBaseOffset)));
        } else if (status == 200) {
            // The server ignored the Range header: restart the file
            // cleanly rather than appending a second copy.
            if (mFile) {
                mFile->close();
                delete mFile;
            }
            mFile = new QFile{mPartPath};
            if (mFile->open(QIODevice::WriteOnly)) {
                mBaseOffset = 0;
                mSessionBytes = 0;
                emit statusChanged(
                            tr("The server restarted the download "
                               "from the beginning."));
            } else {
                delete mFile;
                mFile = nullptr;
            }
        }
    } else if (mResumeAttempts > 0) {
        emit statusChanged(tr("Reconnected — downloading again from "
                              "the start."));
    } else {
        emit statusChanged(tr("Downloading..."));
    }
}

void DbsUpdateDownloader::scheduleResume(const QString &reason,
                                         bool freshRestart)
{
    if (freshRestart)
        mBaseOffset = 0;
    if (mResumeAttempts >= mMaxResumeAttempts) {
        failDownload(tr("%1. The download was retried %2 times and "
                        "still did not finish. The part downloaded "
                        "so far (%3) is saved — the next attempt will "
                        "continue from there, not from zero.")
                     .arg(reason).arg(mMaxResumeAttempts)
                     .arg(mbText(partFileSize())));
        return;
    }
    ++mResumeAttempts;
    emit statusChanged(
                tr("%1 — reconnecting (try %2 of %3), continuing at "
                   "%4...")
                .arg(reason).arg(mResumeAttempts + 1)
                .arg(mMaxResumeAttempts + 1)
                .arg(mbText(partFileSize())));
    mResumeTimer->start(mResumeDelayMs);
}

void DbsUpdateDownloader::completeDownload()
{
    if (mFile) {
        mFile->close();
        delete mFile;
        mFile = nullptr;
    }
    // Only reached when the size matched the releases API exactly
    // (or no size was known and the stream ended cleanly): rename.
    QFile::remove(mFinalPath);
    if (!QFile::rename(mPartPath, mFinalPath)) {
        // Same-volume rename should work; fall back to a copy.
        if (!QFile::copy(mPartPath, mFinalPath)) {
            failDownload(tr("The download finished but the setup "
                            "file could not be prepared for "
                            "installing."));
            return;
        }
        QFile::remove(mPartPath);
    }
    mRunning = false;
    const qint64 size = QFileInfo{mFinalPath}.size();
    emit progressChanged(size, size);
    emit finished(true, false, tr("Download complete."));
}

void DbsUpdateDownloader::failDownload(const QString &message)
{
    mRunning = false;
    mWatchdog->stop();
    mResumeTimer->stop();
    emit finished(false, false, message);
}

// ---------------------------------------------------------------------------
// DbsUpdateDialog
// ---------------------------------------------------------------------------

DbsUpdateDialog::DbsUpdateDialog(const QUrl &url, const QString &version,
                                 qint64 expectedSize,
                                 const QString &userAgent,
                                 QWidget *parent) :
    QDialog{parent},
    mUrl{url},
    mVersion{version},
    mExpectedSize{expectedSize},
    mUserAgent{userAgent}
{
    setWindowTitle(tr("Update download"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(520, 300);
    mFinalPath = updatesDir() + "/DevBySufyan-Setup.exe";
    mPartPath = mFinalPath + ".part";

    auto *layout = new QVBoxLayout{this};
    mTitleLabel = new QLabel{
            tr("Dev by Sufyan v%1").arg(version), this};
    mTitleLabel->setStyleSheet("font-size: 14pt; font-weight: bold;");
    layout->addWidget(mTitleLabel);
    mStatusLabel = new QLabel{tr("Preparing the download..."), this};
    mStatusLabel->setWordWrap(true);
    layout->addWidget(mStatusLabel);
    mBar = new QProgressBar{this};
    mBar->setRange(0, 100);
    mBar->setValue(0);
    layout->addWidget(mBar);
    mDetailLabel = new QLabel{this};
    layout->addWidget(mDetailLabel);
    mPathLabel = new QLabel{this};
    mPathLabel->setWordWrap(true);
    mPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mPathLabel->setStyleSheet("color: gray;");
    layout->addWidget(mPathLabel);
    layout->addStretch();

    auto *buttons = new QHBoxLayout;
    mInstallButton = new QPushButton{tr("Open Installer"), this};
    mInstallButton->setEnabled(false);
    mBrowserButton = new QPushButton{tr("Open download page"), this};
    mCloseButton = new QPushButton{tr("Cancel"), this};
    buttons->addWidget(mInstallButton);
    buttons->addWidget(mBrowserButton);
    buttons->addStretch();
    buttons->addWidget(mCloseButton);
    layout->addLayout(buttons);

    connect(mBrowserButton, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(mUrl);
    });
    connect(mInstallButton, &QPushButton::clicked, this, [this]() {
        tryLaunchInstaller(false);
    });
    connect(mCloseButton, &QPushButton::clicked, this, [this]() {
        if (mDownloader && mDownloader->isRunning())
            mDownloader->cancel(); // the panel stays; shows Close next
        else
            close();
    });
}

namespace {
QString sUpdatesDirAnchor;
}

void dbsSetUpdatesDirAnchor(const QString &dir)
{
    sUpdatesDirAnchor = dir;
}

QString DbsUpdateDialog::updatesDir() const
{
    if (!sUpdatesDirAnchor.isEmpty())
        return sUpdatesDirAnchor + QStringLiteral("/updates");
    return QStandardPaths::writableLocation(
                QStandardPaths::AppDataLocation)
           + QStringLiteral("/updates");
}

void DbsUpdateDialog::startDownload()
{
    // A leftover .part only joins THIS download if it belongs to the
    // same release asset: a tiny sidecar records the URL it came
    // from. A different update replaces it instead of appending
    // wrong bytes to it.
    const QString metaPath = mPartPath + ".meta";
    QDir{}.mkpath(updatesDir());
    bool keepPart = false;
    if (QFileInfo::exists(mPartPath) && QFileInfo::exists(metaPath)) {
        QFile metaFile{metaPath};
        if (metaFile.open(QIODevice::ReadOnly)) {
            const QJsonObject meta = QJsonDocument::fromJson(
                        metaFile.readAll()).object();
            keepPart = meta.value("url").toString()
                    == mUrl.toString();
        }
    }
    if (!keepPart)
        QFile::remove(mPartPath);
    {
        QFile metaFile{metaPath};
        if (metaFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QJsonObject meta;
            meta["url"] = mUrl.toString();
            meta["version"] = mVersion;
            metaFile.write(QJsonDocument{meta}.toJson());
        }
    }
    const qint64 existing = QFileInfo{mPartPath}.size();
    if (existing > 0)
        mStatusLabel->setText(
                    tr("Found an earlier partial download (%1) — "
                       "continuing it.").arg(mbText(existing)));

    // Already complete on disk? Offer the installer at once.
    if (mExpectedSize > 0 && QFileInfo::exists(mFinalPath)
            && QFileInfo{mFinalPath}.size() == mExpectedSize) {
        setCompleteState(true);
        return;
    }

    mDownloader = new DbsUpdateDownloader{this};
    connect(mDownloader, &DbsUpdateDownloader::statusChanged,
            this, [this](const QString &text) {
        mStatusLabel->setText(text);
    });
    connect(mDownloader, &DbsUpdateDownloader::progressChanged,
            this, [this](qint64 received, qint64 total) {
        if (total > 0) {
            mBar->setRange(0, 100);
            mBar->setValue(int(received * 100 / total));
            mDetailLabel->setText(
                        tr("%1 of %2 (%3%)")
                        .arg(mbText(received), mbText(total))
                        .arg(int(received * 100 / total)));
        } else {
            mBar->setRange(0, 0); // busy bar while the size is unknown
            mDetailLabel->setText(tr("%1 received").arg(mbText(received)));
        }
    });
    connect(mDownloader, &DbsUpdateDownloader::finished,
            this, [this](bool ok, bool cancelled,
                         const QString &message) {
        if (ok) {
            setCompleteState(false);
            return;
        }
        mBar->setRange(0, 100);
        mBar->setValue(0);
        mStatusLabel->setText(message);
        mCloseButton->setText(tr("Close"));
        if (cancelled)
            mDetailLabel->setText(
                        tr("You can close this window — nothing was "
                           "lost."));
    });
    mDownloader->start(mUrl, mPartPath, mFinalPath, mExpectedSize,
                       mUserAgent);
}

void DbsUpdateDialog::setCompleteState(bool alreadyDownloaded)
{
    mComplete = true;
    mBar->setRange(0, 100);
    mBar->setValue(100);
    const qint64 size = QFileInfo{mFinalPath}.size();
    mDetailLabel->setText(tr("%1 — ready to install.").arg(mbText(size)));
    mPathLabel->setText(tr("Saved at: %1").arg(mFinalPath));
    mInstallButton->setEnabled(true);
    mCloseButton->setText(tr("Close"));
    if (alreadyDownloaded) {
        mStatusLabel->setText(
                    tr("This update is already downloaded. Press "
                       "Open Installer to run the setup."));
        return;
    }
    mStatusLabel->setText(
                tr("Download complete. The setup is saved on this "
                   "computer and ready to open."));
    if (!mAutoLaunchTried) {
        mAutoLaunchTried = true;
        tryLaunchInstaller(true);
    }
}

void DbsUpdateDialog::tryLaunchInstaller(bool automatic)
{
#ifdef __EMSCRIPTEN__
    // Web build: there is no installer to launch (and
    // QProcess::startDetached does not exist in Qt for WebAssembly).
    // Unreachable in practice — MainWindow::dbsCheckForUpdates() is a
    // no-op on the web build, so this dialog is never shown there.
    Q_UNUSED(automatic);
    mStatusLabel->setText(
                tr("Updates are automatic in the web version."));
    return;
#else
    const bool launched = QProcess::startDetached(mFinalPath,
                                                  QStringList{});
    if (launched) {
        mStatusLabel->setText(
                    tr("The setup was asked to open. If the setup "
                       "window does not appear, press Open Installer "
                       "again. Tip: the setup may ask you to close "
                       "Dev by Sufyan while it replaces the files."));
        if (!automatic) {
            QMessageBox box{this};
            box.setWindowTitle(tr("Setup started"));
            box.setText(tr("The setup is starting. Close Dev by "
                           "Sufyan now so the setup can replace the "
                           "program files?"));
            QPushButton *closeBtn = box.addButton(
                        tr("Close Dev by Sufyan"),
                        QMessageBox::AcceptRole);
            box.addButton(tr("Keep it open"), QMessageBox::RejectRole);
            box.exec();
            if (box.clickedButton() == closeBtn)
                qApp->quit();
        }
    } else {
        mStatusLabel->setText(
                    tr("The setup did not open by itself."));
        QMessageBox::warning(
                    this, tr("Update download"),
                    tr("The setup was downloaded but could not be "
                       "started from here. You can run it yourself "
                       "from this exact place:\n%1").arg(mFinalPath));
    }
#endif // __EMSCRIPTEN__
}

void DbsUpdateDialog::closeEvent(QCloseEvent *event)
{
    // Closing the panel mid-download counts as a cancel: the partial
    // file stays on disk and the next attempt resumes from it.
    if (mDownloader && mDownloader->isRunning())
        mDownloader->cancel();
    QDialog::closeEvent(event);
}
