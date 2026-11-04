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
#include "main.h"
#include "rooms/dbsfx.h"
#include "utils/os.h"
#include <QElapsedTimer>
#include <QIcon>

#ifdef Q_OS_WIN
static_assert(WM_APP_OPEN_FILE < 0xc000);

HWND prevAppInstance = NULL;
BOOL CALLBACK GetPreviousInstanceCallback(HWND hwnd, LPARAM param){
    BOOL result = TRUE;
    WCHAR buffer[4098];
    HINSTANCE hWindowModule = (HINSTANCE)GetWindowLongPtr(hwnd,GWLP_HINSTANCE);

    if (hWindowModule==0)
        return result;

    DWORD processID;

    // Get the ID of the process that created this window
    GetWindowThreadProcessId(hwnd,&processID);
    if (processID==0)
        return result;

    // Get the process associated with the ID
    HANDLE hWindowProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID);
    if (hWindowProcess == 0)
        return result;

    // Get its module filename
    if (GetModuleFileNameExW(hWindowProcess, hWindowModule, buffer, sizeof(buffer)) == 0)
        return TRUE;

    CloseHandle(hWindowProcess); // not needed anymore
    WCHAR * compareFilename=(WCHAR*)param;
    QString s1=QString::fromWCharArray(compareFilename);
    QString s2=QString::fromWCharArray(buffer);

    //Is from the "same" application?
    if (QString::compare(s1,s2,PATH_SENSITIVITY)==0) {
        //found, stop EnumWindows loop
        prevAppInstance = hwnd;
        return FALSE;
    }

    return TRUE;
}

HWND getPreviousInstance() {
    WCHAR buffer[4098];
    //ShowMessage('ERROR_ALREADY_EXISTS');
    // Store our own module filename
    if (GetModuleFileNameW(GetModuleHandle(NULL), buffer, sizeof(buffer)) == 0)
        return NULL;

    // If that's the case, walk all top level windows and find the previous instance
    // At this point, the program that created the mutex might not have created its MainForm yet
    if (EnumWindows(GetPreviousInstanceCallback, LPARAM(buffer))==0) {
        return prevAppInstance;
    } else
        return NULL;
}

#if QT_VERSION_MAJOR >= 6
bool WindowLogoutEventFilter::nativeEventFilter(const QByteArray & /*eventType*/, void *message, qintptr *result)
#else
bool WindowLogoutEventFilter::nativeEventFilter(const QByteArray & /*eventType*/, void *message, long *result)
#endif
{
    MSG * pMsg = static_cast<MSG *>(message);
    switch(pMsg->message) {
    case WM_QUERYENDSESSION:
        if (pMsg->lParam == 0
                || (pMsg->lParam & ENDSESSION_LOGOFF)) {
            if (!pMainWindow->close()) {
                *result = 0;
            } else {
                *result = 1;
            }
            return true;
        }
        break;
    case WM_DPICHANGED:{
        if (pMsg->hwnd == (HWND)pMainWindow->winId()) {
            int oldDPI = screenDPI();
            //postEvent takes the owner ship
            QEvent * dpiEvent = new QEvent(DPI_CHANGED_EVENT);
            qApp->postEvent(pMainWindow,dpiEvent);
            setScreenDPI(HIWORD(pMsg->wParam));
            int newDPI = screenDPI();
            pMainWindow->updateDPI(oldDPI,newDPI);
        } else if (pMainWindow->cpuDialog() &&
                   (HWND)pMainWindow->cpuDialog()->winId() == pMsg->hwnd) {
            int newDPI = HIWORD(pMsg->wParam);
            pMainWindow->cpuDialog()->updateDPI(newDPI);
        }
        break;
        }
    case WM_APP_OPEN_FILE: {
        QSharedMemory sharedMemory("RedPandaCpp/openfiles");
        if (sharedMemory.attach()) {
            QBuffer buffer;
            QDataStream in(&buffer);
            QStringList files;
            sharedMemory.lock();
            buffer.setData((char*)sharedMemory.constData(), sharedMemory.size());
            buffer.open(QBuffer::ReadOnly);
            in >> files;
            sharedMemory.unlock();
            if (pMainWindow->isMinimized()) {
                pMainWindow->showNormal();
            }
            pMainWindow->raise(); // for mac OS?
            pMainWindow->activateWindow();

            pMainWindow->openFiles(files);
            sharedMemory.detach();
        }
        return true;
        }
    }
    return false;
}

bool sendFilesToInstance() {
    HWND prevInstance = getPreviousInstance();
    if (prevInstance != NULL) {
        QSharedMemory sharedMemory("RedPandaCpp/openfiles");
        QBuffer buffer;
        buffer.open(QBuffer::ReadWrite);
        QDataStream out(&buffer);
        QStringList filesToOpen = qApp->arguments();
        filesToOpen.pop_front();
        out<<filesToOpen;
        int size = buffer.size();
        if (sharedMemory.create(size)) {
            sharedMemory.lock();
            char *to = (char*)sharedMemory.data();
            const char *from = buffer.data().data();
            memcpy(to, from, qMin(sharedMemory.size(), size));
            sharedMemory.unlock();
            SendMessage(prevInstance,WM_APP_OPEN_FILE,0,0);
            return true;
        }
    }
    return false;
}
#endif

BlockWheelEventFiler::BlockWheelEventFiler(QObject* parent):QObject(parent) {}

BlockWheelEventFiler::~BlockWheelEventFiler() {}

bool BlockWheelEventFiler::eventFilter(QObject *watched, QEvent *event)
{
    //Prevent QComboBox wheel event
    if (event->type() == QEvent::Wheel
            && !pSettings->environment().comboboxWheel()) {
        QComboBox *p=qobject_cast<QComboBox*>(watched);
        if (p && !(p->view() && p->view()->isVisible()))
            return true;
    }
    return false;
}

QString getSettingFilename(const QString& filepath, bool& firstRun) {
    QString filename;
    if (filepath.isEmpty()) {
        if (usePortableConfigPath()) {
            filename = includeTrailingPathDelimiter(QApplication::applicationDirPath()) +
                    "config/"  + APP_SETTSINGS_FILENAME;
        } else {
            QStringList appLocations = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
            filename =includeTrailingPathDelimiter(appLocations.first())
                 + APP_SETTSINGS_FILENAME;
        }
    } else {
        filename = filepath;
    }

    QFileInfo fileInfo(filename);
    firstRun = !fileInfo.exists();
    QDir dir(fileInfo.absoluteDir());
    if (!dir.exists()) {
        if (!dir.mkpath(dir.absolutePath())) {
            QMessageBox::critical(nullptr, QObject::tr("Error"),
                QString(QObject::tr("Can't create configuration folder %1")).arg(dir.absolutePath()));
            return "";
        }
    }

    if (fileInfo.exists() && !fileInfo.isWritable()) {
        QMessageBox::critical(nullptr, QObject::tr("Error"),
            QString(QObject::tr("Can't write to configuration file %1")).arg(filename));

        return "";
    }
    return filename;
}

void setTheme(const QString& theme) {
    pSettings->environment().setTheme(theme);
    ThemeManager themeManager;
    PAppTheme appTheme = themeManager.theme(theme);
    if (appTheme && !appTheme->defaultColorScheme().isEmpty()) {
        pSettings->editor().setColorScheme(appTheme->defaultColorScheme());
        pSettings->editor().save();
    }
    if (appTheme && !appTheme->defaultIconSet().isEmpty()) {
        pSettings->environment().setIconSet(appTheme->defaultIconSet());
    }
    pSettings->environment().save();

}

#ifdef ENABLE_GLIBC_HWCAPS
extern "C" int Q_DECL_EXPORT RedPandaIDE_main(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
//#ifdef Q_OS_WINDOWS
//    // Make title bar and palette follow system-wide dark mode setting on recent Windows releases.
//    // Use freetype as the fontengine
//    qputenv("QT_QPA_PLATFORM", "windows:darkmode=2:fontengine=freetype");
//#endif

#ifdef Q_OS_MACOS
    // in macOS GUI apps, `/usr/local/bin` is not in PATH by default
    // follow the Unix way by prepending it to `/usr/bin`
    {
        QStringList pathList = getExecutableSearchPaths();
        if (!pathList.contains("/usr/local/bin")) {
            auto idxUsrBin = pathList.indexOf("/usr/bin");
            if (idxUsrBin >= 0)
                pathList.insert(idxUsrBin, "/usr/local/bin");
            else
                pathList.append("/usr/local/bin");
        }
        QString newPath = pathList.join(PATH_SEPARATOR);
        qputenv("PATH", newPath.toUtf8());
    }
#endif

    QApplication app(argc, argv);
#if QT_VERSION_MAJOR >= 6
    app.setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
#if QT_VERSION_MAJOR < 6
    app.setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    app.setWindowIcon(QIcon(":/icons/images/devcpp.ico"));
    QDir startupDir = QDir::current();
    ExternalResource resource;

    QLockFile lockFile(QDir::tempPath()+QDir::separator()+"RedPandaDevCppStartUp.lock");
    {
        bool firstRun;
        QString settingFilename = getSettingFilename(QString(), firstRun);
        bool openInSingleInstance = false;
        if (!settingFilename.isEmpty() && !firstRun) {
            QSettings envSetting(settingFilename,QSettings::IniFormat);
            envSetting.beginGroup(SETTING_ENVIRONMENT);
            openInSingleInstance = envSetting.value("open_files_in_single_instance",false).toBool();
        } else if (!settingFilename.isEmpty() && firstRun)
            openInSingleInstance = false;
        if (app.arguments().contains("-ns")) {
            openInSingleInstance = false;
        } else if (app.arguments().contains("-s"))
            openInSingleInstance = true;
        if (openInSingleInstance) {
            int openCount = 0;
            while (true) {
                if (lockFile.tryLock(100))
                    break;
                openCount++;
                if (openCount>100)
                    break;
            }

            if (app.arguments().length()>=2 && openCount<100) {
#ifdef Q_OS_WIN
                if (sendFilesToInstance()) {
                    lockFile.unlock();
                    return 0;
                }
#endif
            }
        }
    }
    //Translation must be loaded first
    QTranslator trans,transQt,transUtils;
    bool firstRun;
    QString settingFilename = getSettingFilename(QString(), firstRun);
    if (!usePortableConfigPath()) {
        QStringList documentLocations = QStandardPaths::standardLocations(QStandardPaths::DocumentsLocation);
        QDir::setCurrent(documentLocations.first());
    }
    if (settingFilename.isEmpty()) {
        lockFile.unlock();
        return -1;
    }
    QString language;
    {
        QSettings languageSetting(settingFilename,QSettings::IniFormat);
        languageSetting.beginGroup(SETTING_ENVIRONMENT);
        language = languageSetting.value("language",QLocale::system().name()).toString();

        if (trans.load("RedPandaIDE_"+language,":/i18n/")) {
            app.installTranslator(&trans);
        }
        if (transUtils.load("qt_utils_"+language,":/i18n/")) {
            app.installTranslator(&transUtils);
        }
        QString translationsPath(QLibraryInfo::location(QLibraryInfo::TranslationsPath));
        if (
            // since Qt 5.15.3, `qt_xx.qm` is a wrapper for `qtbase_xx.qm` and other (unused) `qm`s.
            // first, try loading `qt_xx.qm` from standard location (dynamic build) so that it works on Debian 11 (with Qt 5.15.2),
            transQt.load("qt_" + language, translationsPath) ||
            // and then bundled `qtbase_xx.qm` (static build) for simplicity in qmake project.
            transQt.load("qtbase_" + language, ":/translations")
        ) {
            app.installTranslator(&transQt);
        }
    }
    qRegisterMetaType<POJProblem>("POJProblem");
    qRegisterMetaType<PCompileIssue>("PCompileIssue");
    qRegisterMetaType<PCompileIssue>("PCompileIssue&");
    qRegisterMetaType<QVector<int>>("QVector<int>");
    qRegisterMetaType<QHash<int,QString>>("QHash<int,QString>");

    initParser();

    try {

        SystemConsts systemConsts;
        pSystemConsts = &systemConsts;
        CharsetInfoManager charsetInfoManager(language);
        pCharsetInfoManager=&charsetInfoManager;

        //We must use smarter point here, to manually control it's lifetime:
        // when restore default settings, it must be destoyed before we remove all setting files.
        auto settings = std::make_unique<Settings>(settingFilename);
        //load settings
        pSettings = settings.get();
        if (firstRun) {
            pSettings->compilerSets().findSets(true);
            pSettings->compilerSets().saveSets();
        }
        pSettings->load();
        // v1.6.0 Part 8: bundled-font defaults. They apply ONLY
        // where the saved value is still the untouched upstream
        // platform default — a font the user picked themselves in
        // Change Font is never overridden.
        {
            const QString uiFont = pSettings->environment().interfaceFont();
            if (uiFont == defaultUiFont() || uiFont.isEmpty()
                    || uiFont == QStringLiteral("Sans Serif"))
                pSettings->environment().setInterfaceFont(
                    QStringLiteral("Inter"));
            // The saved editor value is a fallback LIST whose first
            // entry is the platform default when untouched.
            const QStringList fams = pSettings->editor().fontFamilies();
            if (!fams.isEmpty() && fams.first() == defaultMonoFont())
                pSettings->editor().setFontFamilies(
                    {QStringLiteral("JetBrains Mono")});
            if (pSettings->editor().gutterFontName() == defaultMonoFont())
                pSettings->editor().setGutterFontName(
                    QStringLiteral("JetBrains Mono"));
        }
        if (firstRun) {
            //set theme
#ifdef __EMSCRIPTEN__
            // Part 7 (WASM startup-abort hunt): do NOT run the modal
            // ChooseThemeDialog on the web. Every fresh browser
            // profile lands here on its first open, before the main
            // window exists, and this exec() is the only modal nested
            // event loop on the whole startup path — the prime
            // suspect for the traceless post-download "Application
            // exit" seen on real devices (download completes, the IDE
            // never appears). Apply the dialog's own defaults
            // instead — Dark theme, C++, shared parser (the radio
            // buttons its constructor pre-checks; the Auto option is
            // hidden without the Lua add-on) — which is exactly what
            // an immediate OK would persist. The theme can still be
            // changed later from inside the app.
            const ChooseThemeDialog::Theme firstRunTheme =
                    ChooseThemeDialog::Theme::Dark;
            const bool firstRunDefaultCpp = true;
            const bool firstRunShareParser = true;
#else
            ChooseThemeDialog themeDialog;
            themeDialog.setFont(QFont(defaultUiFont(),11));
            themeDialog.exec();
            const ChooseThemeDialog::Theme firstRunTheme = themeDialog.theme();
            const bool firstRunDefaultCpp =
                    themeDialog.language()==ChooseThemeDialog::Language::CPlusPlus;
            const bool firstRunShareParser = themeDialog.shareParser();
#endif
            switch (firstRunTheme) {
            case ChooseThemeDialog::Theme::AutoFollowSystem:
                setTheme("system");
                break;
            case ChooseThemeDialog::Theme::Dark:
                setTheme("dark");
                break;
            case ChooseThemeDialog::Theme::Light:
                setTheme("default");
                break;
            default:
                setTheme("default");
            }

            pSettings->editor().setDefaultFileCpp(firstRunDefaultCpp);
            if (firstRunShareParser) {
                pSettings->codeCompletion().setShareParser(true);
                pSettings->codeCompletion().setClearWhenEditorHidden(true);
            } else {
                pSettings->codeCompletion().setShareParser(false);
            }
            pSettings->editor().save();

            //auto detect git in path
#ifdef ENABLE_VCS
            pSettings->vcs().detectGitInPath();
#endif
        }
        //Color scheme settings must be loaded after translation
        AutolinkManager autolinkManager;
        pAutolinkManager = &autolinkManager;
        try {
            pAutolinkManager->load();
        } catch (FileError e) {
            QMessageBox::critical(nullptr,
                                  QObject::tr("Can't load autolink settings"),
                                  e.reason(),
                                  QMessageBox::Ok);
        }
        // qDebug()<<"Load font";
        QFontDatabase::addApplicationFont(":/fonts/asciicontrol.ttf");
        // v1.6.0 Part 8: bundled fonts — Inter (UI) and JetBrains
        // Mono (editor), SIL OFL, subset to Latin + common symbols.
        // Registered before the splash/main window so every surface
        // (and the Change Font dialogs, which list QFontDatabase
        // families) can use them.
        QFontDatabase::addApplicationFont(
            QStringLiteral(":/fonts/Inter-Regular.otf"));
        QFontDatabase::addApplicationFont(
            QStringLiteral(":/fonts/Inter-Bold.otf"));
        QFontDatabase::addApplicationFont(
            QStringLiteral(":/fonts/Inter-ExtraBold.otf"));
        QFontDatabase::addApplicationFont(
            QStringLiteral(":/fonts/JetBrainsMono-Regular.ttf"));
        QFontDatabase::addApplicationFont(
            QStringLiteral(":/fonts/JetBrainsMono-Bold.ttf"));

        QDir::setCurrent(pSettings->environment().defaultOpenFolder());

        // v1.6.0: GTA-style loading screen. Theme-aware artwork,
        // shown while the main window and the session are built;
        // progress is advanced at the real startup stages below.
        DbsSplashScreen *splash =
            dbsCreateSplashScreen(pSettings->editor().colorScheme());
        if (splash) {
            splash->show();
            splash->setProgress(30, QObject::tr("Loading the editor"));
            app.processEvents();
            // v1.6.1: force the splash's first paint NOW, before
            // the blocking MainWindow construction below. A single
            // processEvents was not enough on Windows — the splash
            // window sat unpainted while the constructor ran, so
            // the loading screen was never seen at all. repaint()
            // paints synchronously; the short pump afterwards lets
            // the compositor present the frame.
            splash->repaint();
            {
                QElapsedTimer pump;
                pump.start();
                while (pump.elapsed() < 150)
                    app.processEvents(QEventLoop::AllEvents, 20);
            }
        }

        MainWindow mainWindow;
        pMainWindow = &mainWindow;
        if (mainWindow.screen())
            setScreenDPI(mainWindow.screen()->logicalDotsPerInch());

        mainWindow.show();
        if (splash) {
            splash->setProgress(65,
                                QObject::tr("Preparing your workspace"));
            app.processEvents();
        }

        QStringList filesToOpen = app.arguments();
        filesToOpen.pop_front();
        // v1.3.2: files on the command line suppress the startup Home
        // page; a restored session does not.
        mainWindow.noteCommandLineFiles(!filesToOpen.isEmpty());
        if (!filesToOpen.isEmpty()) {
            QStringList absoluteFiles;
            //convert all relative path to absolute path
            foreach (const QString& path, filesToOpen) {
                QFileInfo info{path};
                if (info.isAbsolute()) {
                    absoluteFiles.append(path);
                } else {
                    absoluteFiles.append(startupDir.absoluteFilePath(path));
                }
            }
            mainWindow.openFiles(absoluteFiles);
            // Keep the old fallback for this path only: if every named
            // file failed to open, leave the user an editor to work in.
            if (mainWindow.editorManager()->pageCount()==0 && !mainWindow.project()) {
                mainWindow.newEditor();
            }
        } else {
            if (pSettings->editor().autoLoadLastFiles())
                mainWindow.loadLastOpens();
            // v1.3.2: NO forced blank editor behind the Home page.
            // Whatever the session restored stays open as tabs, any
            // untouched blank editor is dropped, and the Home tab is
            // put in front — like Word's Start screen.
            mainWindow.finishStartupWithHome();
        }

        //reset default open folder
        mainWindow.setFilesViewRoot(pSettings->environment().currentFolder());
        if (splash) {
            splash->setProgress(90, QObject::tr("Almost ready"));
            splash->finishAndClose();
            splash = nullptr;
        }

#ifdef Q_OS_WIN
        WindowLogoutEventFilter filter;
        app.installNativeEventFilter(&filter);
#endif
        //Event filter to prevent QCombobox receive wheel event;
        BlockWheelEventFiler *blockWheelFilter=new BlockWheelEventFiler(&app);
        app.installEventFilter(blockWheelFilter);

        if (lockFile.isLocked()) {
            lockFile.unlock();
        }

        int retCode = app.exec();
        if (mainWindow.shouldRemoveAllSettings()) {
            QString configDir = pSettings->dirs().config();
            settings.release();
            delete pSettings;
            QDir dir(configDir);
            dir.removeRecursively();
        }
        return retCode;
    }  catch (BaseError e) {
        lockFile.unlock();
        QMessageBox::critical(nullptr,QApplication::tr("Error"),e.reason());
        return -1;
    }
}
