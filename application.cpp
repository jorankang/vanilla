#include "switch.hpp"
#include "const.hpp"

#include "application.hpp"
#include <VERSION>

#include <QCoreApplication>
#include <QDomDocument>
#include <QDomElement>
#include <QTextStream>
#include <QThread>
#include <QDateTime>
#include <QUrl>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QSharedMemory>
#include <QOpenGLContext>
#include <QDesktopWidget>
#include <QAuthenticator>
#include <QFileSystemModel>
#include <QFileIconProvider>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLocale>
#include <QRegExp>

#ifdef QTWEBKIT
#  include <QWebPage>
#  include <QWebElement>
#endif

#include <functional>
#include <stdlib.h>
#include <time.h>

#if defined(Q_OS_WIN)
#  include <windows.h>
#endif

#include "gadgets.hpp"
#include "networkcontroller.hpp"
#include "mainwindow.hpp"
#include "treebank.hpp"
#include "notifier.hpp"
#include "saver.hpp"
#include "transmitter.hpp"
#include "receiver.hpp"
#include "localview.hpp"
#include "dialog.hpp"

Application *Application::m_Instance = 0;
int Application::m_DelayFileCount = 0;

static bool ReadXMLFile(QIODevice &device, QSettings::SettingsMap &map);
static bool WriteXMLFile(QIODevice &device, const QSettings::SettingsMap &map);

QSettings::Format Application::XMLFormat =
    QSettings::registerFormat(QStringLiteral("xml"), ReadXMLFile, WriteXMLFile);

NetworkController* Application::m_NetworkController = 0;
AutoSaver*  Application::m_AutoSaver         = 0;
QThread*    Application::m_SaverThread       = 0;
QSettings*  Application::m_GlobalSettings    = 0;

bool        Application::m_EnableGoogleSuggest = false;
bool        Application::m_EnableFramelessWindow = false;
bool        Application::m_EnableAutoSave    = false;
bool        Application::m_EnableAutoLoad    = false;
int         Application::m_AutoSaveInterval  = 0;
int         Application::m_AutoLoadInterval  = 0;
int         Application::m_AutoSaveTimerID   = 0;
int         Application::m_AutoLoadTimerID   = 0;
int         Application::m_MaxBackUpFileCount= 0;
double      Application::m_WheelScrollRate   = 0.0;
QString     Application::m_DownloadDirectory = QString();
QString     Application::m_UploadDirectory   = QString();
QStringList Application::m_ChosenFiles       = QStringList();
bool        Application::m_SaveSessionCookie = false;
QString     Application::m_AcceptLanguage    = QString();
QStringList Application::m_AllowedHosts      = QStringList();
QStringList Application::m_BlockedHosts      = QStringList();
Application::SslErrorPolicy Application::m_SslErrorPolicy = Application::Undefined;
Application::DownloadPolicy Application::m_DownloadPolicy = Application::Undefined_;

WinMap      Application::m_MainWindows = WinMap();
MainWindow *Application::m_CurrentWindow = 0;
ModelessDialogFrame *Application::m_TemporaryDialogFrame = 0;

QString Application::m_UserAgent_IE        = QString();
QString Application::m_UserAgent_FF        = QString();
QString Application::m_UserAgent_Opera     = QString();
QString Application::m_UserAgent_OPR       = QString();
QString Application::m_UserAgent_Safari    = QString();
QString Application::m_UserAgent_Chrome    = QString();
QString Application::m_UserAgent_Sleipnir  = QString();
QString Application::m_UserAgent_Vivaldi   = QString();
QString Application::m_UserAgent_NetScape  = QString();
QString Application::m_UserAgent_SeaMonkey = QString();
QString Application::m_UserAgent_iCab      = QString();
QString Application::m_UserAgent_Camino    = QString();
QString Application::m_UserAgent_Gecko     = QString();
QString Application::m_UserAgent_Custom    = QString();

QString Application::m_BrowserPath_IE       = QString();
QString Application::m_BrowserPath_FF       = QString();
QString Application::m_BrowserPath_Opera    = QString();
QString Application::m_BrowserPath_OPR      = QString();
QString Application::m_BrowserPath_Safari   = QString();
QString Application::m_BrowserPath_Chrome   = QString();
QString Application::m_BrowserPath_Sleipnir = QString();
QString Application::m_BrowserPath_Vivaldi  = QString();
QString Application::m_BrowserPath_Custom   = QString();

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
    m_NetworkController = 0;
    m_AutoSaver = 0;
    m_SaverThread = 0;
    srand(static_cast<unsigned int>(time(NULL)));
}

Application::~Application(){
    if(m_NetworkController){
        m_NetworkController->deleteLater();
    }
    if(m_AutoSaver){
        m_AutoSaver->deleteLater();
    }
    if(m_SaverThread){
        m_SaverThread->quit();
        m_SaverThread->deleteLater();
    }
}

static void EmitErrorMessage(QObject *receiver, QEvent *ev, std::exception &e){
    qFatal("Error %s sending event %s to object %s (%s)", e.what(),
           typeid(*ev).name(), qPrintable(receiver->objectName()),
           typeid(*receiver).name());
}

static void EmitErrorMessage(QObject *receiver, QEvent *ev){
    qFatal("Error <unknown> sending event %s to object %s (%s)",
           typeid(*ev).name(), qPrintable(receiver->objectName()),
           typeid(*receiver).name());
}

bool Application::notify(QObject *receiver, QEvent *ev){
#if defined(Q_OS_WIN)
    __try{
        return QApplication::notify(receiver, ev);
    } __except(0){
        EmitErrorMessage(receiver, ev);
    }
#else
    try{
        return QApplication::notify(receiver, ev);
    } catch(std::exception &e){
        EmitErrorMessage(receiver, ev, e);
    } catch(...){
        EmitErrorMessage(receiver, ev);
    }
#endif

    // qFatal aborts, so this isn't really necessary
    // but you might continue if you use a different logging lib
    return false;
}

void Application::BootApplication(int &argc, char **argv, Application *instance){
    Q_UNUSED(argc);
    Q_UNUSED(argv);

    Transmitter *t = new Transmitter(instance);
    if(t->ServerAlreadyExists()){
        QStringList list;
        // skip first.
        for(int i = 1; i < argc; i++){
            list << QLatin1String(argv[i]);
        }
        t->SendCommandAndQuit(list.join(QStringLiteral(" ")));
        return;
    }

    // corner-cutting prohibition of multiple launch.
    static QSharedMemory mem(SharedMemoryKey());
    if(!mem.create(1)){
        QTimer::singleShot(100, instance, SLOT(quit()));
        return;
    }

    m_Instance = instance;
    // need?
    //setOrganizationName(QStringLiteral("vanilla"));
    setApplicationName(QStringLiteral("vanilla"));
    setApplicationVersion(VANILLA_VERSION);
    setQuitOnLastWindowClosed(false);

    const QString path = applicationDirPath() + QStringLiteral("/translations");
    const QStringList locales = QLocale::system().uiLanguages();
    const QStringList prefixes = QStringList()
        << QStringLiteral("qt")     << QStringLiteral("qtbase")
        << QStringLiteral("custom") << QStringLiteral("vanilla");

    foreach(QString locale, locales){
        locale = QLocale(locale).name();
        foreach(QString prefix, prefixes){
            QTranslator *translator = new QTranslator(instance);
            translator->load(prefix + QStringLiteral("_") + locale, path);
            installTranslator(translator);
        }
    }
    LoadGlobalSettings();
    CreateBackUpFiles();

    m_NetworkController = new NetworkController();
    m_AutoSaver = new AutoSaver();
    m_SaverThread = new QThread();
    TreeBank::Initialize();
    TreeBank::LoadTree();
    if(m_MainWindows.isEmpty()){
        MainWindow * win = NewWindow();
        win->GetTreeBank()->OpenOnSuitableNode(QUrl(QStringLiteral("https://google.com")), true);
    } else {
        QSettings *s = Application::GlobalSettings();
        s->beginGroup(QStringLiteral("mainwindow"));{
            QStringList keys = s->allKeys();
            QList<int> ids = m_MainWindows.keys();
            foreach(int id, ids){
                keys.removeOne(QStringLiteral("tableview%1").arg(id));
                keys.removeOne(QStringLiteral( "geometry%1").arg(id));
                keys.removeOne(QStringLiteral(  "menubar%1").arg(id));
                keys.removeOne(QStringLiteral(   "status%1").arg(id));
            }
            foreach(QString key, keys){
                s->remove(key);
            }
        }
        s->endGroup();
    }

    connect(instance,    SIGNAL(SaveAllDataRequest()),
            m_AutoSaver, SLOT(SaveAll()));
    m_AutoSaver->moveToThread(m_SaverThread);
    m_SaverThread->start();

    StartAutoSaveTimer();
    StartAutoLoadTimer();

    if(m_CurrentWindow){
        QStringList list;
        // skip first.
        for(int i = 1; i < argc; i++){
            list << QLatin1String(argv[i]);
        }
        if(m_CurrentWindow->GetTreeBank()->GetReceiver())
            m_CurrentWindow->GetTreeBank()->GetReceiver()->ReceiveCommand(list.join(QStringLiteral(" ")));
    }
}

/*
  standardPaths on Windows:

  QStandardPaths::DesktopLocation       : "C:/Users/<user-name>/Desktop"
  QStandardPaths::DocumentsLocation     : "C:/Users/<user-name>/Documents"
  QStandardPaths::FontsLocation         : "C:/Windows/Fonts"
  QStandardPaths::ApplicationsLocation  : "C:/Users/<user-name>/AppData/Roaming/Microsoft/Windows/Start Menu/Programs"
  QStandardPaths::MusicLocation         : "C:/Users/<user-name>/Music"
  QStandardPaths::MoviesLocation        : "C:/Users/<user-name>/Videos"
  QStandardPaths::PicturesLocation      : "C:/Users/<user-name>/Pictures"
  QStandardPaths::TempLocation          : "C:/Users/<user-name>/AppData/Local/Temp"
  QStandardPaths::HomeLocation          : "C:/Users/<user-name>"
  QStandardPaths::DataLocation          : "C:/Users/<user-name>/AppData/Local/<organization-name>/<application-name>"
  QStandardPaths::CacheLocation         : "C:/Users/<user-name>/AppData/Local/<organization-name>/<application-name>/cache"
  QStandardPaths::GenericDataLocation   : "C:/Users/<user-name>/AppData/Local"
  QStandardPaths::RuntimeLocation       : "C:/Users/<user-name>"
  QStandardPaths::ConfigLocation        : "C:/Users/<user-name>/AppData/Local/<organization-name>/<application-name>"
  QStandardPaths::GenericConfigLocation : "C:/Users/<user-name>/AppData/Local"
  QStandardPaths::DownloadLocation      : "C:/Users/<user-name>/Documents"
  QStandardPaths::GenericCacheLocation  : "C:/Users/<user-name>/AppData/Local/cache"
  QStandardPaths::AppDataLocation       : "C:/Users/<user-name>/AppData/Roaming/<organization-name>/<application-name>"
  QStandardPaths::AppLocalDataLocation  : "C:/Users/<user-name>/AppData/Local/<organization-name>/<application-name>"

  standardPaths on Mac:

  QStandardPaths::DesktopLocation       : "/Users/<user-name>/Desktop"
  QStandardPaths::DocumentsLocation     : "/Users/<user-name>/Documents"
  QStandardPaths::FontsLocation         : "/System/Library/Fonts"
  QStandardPaths::ApplicationsLocation  : "/Applications"
  QStandardPaths::MusicLocation         : "/Users/<user-name>/Music"
  QStandardPaths::MoviesLocation        : "/Users/<user-name>/Movies"
  QStandardPaths::PicturesLocation      : "/Users/<user-name>/Pictures"
  QStandardPaths::TempLocation          : "/var/folders/<randomly-generated-by-the-OS>"
  QStandardPaths::HomeLocation          : "/Users/<user-name>"
  QStandardPaths::DataLocation          : "/Users/<user-name>/Library/Application Support/<organization-name>/<application-name>"
  QStandardPaths::CacheLocation         : "/Users/<user-name>/Library/Caches/<organization-name>/<application-name>"
  QStandardPaths::GenericDataLocation   : "/Users/<user-name>/Library/Application Support"
  QStandardPaths::RuntimeLocation       : "/Users/<user-name>/Library/Application Support"
  QStandardPaths::ConfigLocation        : "/Users/<user-name>/Library/Preferences"
  QStandardPaths::GenericConfigLocation : "~/Library/Preferences"
  QStandardPaths::DownloadLocation      : "/Users/<user-name>/Documents"
  QStandardPaths::GenericCacheLocation  : "/Users/<user-name>/Library/Caches"
  QStandardPaths::AppDataLocation       : "~/Library/Application Support/<organization-name>/<application-name>"
  QStandardPaths::AppLocalDataLocation  : "~/Library/Application Support/<organization-name>/<application-name>"

  standardPaths on Linux:

  QStandardPaths::DesktopLocation       : "/home/<user-name>/Desktop"
  QStandardPaths::DocumentsLocation     : "/home/<user-name>/Documents"
  QStandardPaths::FontsLocation         : "/home/<user-name>/.fonts"
  QStandardPaths::ApplicationsLocation  : "/home/<user-name>/.local/share/applications"
  QStandardPaths::MusicLocation         : "/home/<user-name>/Music"
  QStandardPaths::MoviesLocation        : "/home/<user-name>/Videos"
  QStandardPaths::PicturesLocation      : "/home/<user-name>/Pictures"
  QStandardPaths::TempLocation          : "/tmp"
  QStandardPaths::HomeLocation          : "/home/<user-name>"
  QStandardPaths::DataLocation          : "/home/<user-name>/.local/share/<organization-name>/<application-name>"
  QStandardPaths::CacheLocation         : "/home/<user-name>/.cache/<organization-name>/<application-name>"
  QStandardPaths::GenericDataLocation   : "/home/<user-name>/.local/share"
  QStandardPaths::RuntimeLocation       : "/run/user/<user-name>"
  QStandardPaths::ConfigLocation        : "/home/<user-name>/.config"
  QStandardPaths::GenericConfigLocation : "/home/<user-name>/.config"
  QStandardPaths::DownloadLocation      : "/home/<user-name>/Download"
  QStandardPaths::GenericCacheLocation  : "/home/<user-name>/.cache"
  QStandardPaths::AppDataLocation       : "/home/<user-name>/.local/share/<APPNAME>"
  QStandardPaths::AppLocalDataLocation  : "/home/<user-name>/.local/share/<APPNAME>"

 */

void Application::Import(TreeBank *tb){
    /*
      IE:

      QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/Favorites/");

    */
    std::function<void()> importFromIE = [&](){
        std::function<void(ViewNode *parent, QString path)> traverse;
        std::function<ViewNode*(ViewNode *parent, QString path)> makenode;

        QFileDialog::Options options =
            QFileDialog::DontResolveSymlinks | QFileDialog::ShowDirsOnly;

        QString root =
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/Favorites");

        QString dirname =
            ModalDialog::GetExistingDirectory(QString::null, root, options);

        if(dirname.isEmpty()) return;

        traverse = [&](ViewNode *parent, QString path){
            ViewNode *vn = makenode(parent, path);
            if(QFileInfo(path).isDir()){
                QDir dir = path;
                QStringList files = dir.entryList(QDir::NoDotAndDotDot|QDir::AllEntries);
                foreach(QString file, files){
                    traverse(vn, path + QStringLiteral("/") + file);
                }
            }
        };

        makenode = [&](ViewNode *parent, QString path) -> ViewNode*{
            ViewNode *vn = parent->MakeChild(true);
            QString title = path.split(QStringLiteral("/")).last();
            if(title.endsWith(QStringLiteral(".url"))){
                title = title.left(title.length()-4);
            } else if(title.endsWith(QStringLiteral(".website"))){
                title = title.left(title.length()-8);
            }
            vn->SetTitle(title);

            if(!QFileInfo(path).isDir()){
                HistNode *hn = TreeBank::GetHistRoot()->MakeChild();
                QSettings info(path, QSettings::IniFormat);
                hn->SetTitle(title);
                hn->SetUrl(info.value(QStringLiteral("InternetShortcut/URL")).toUrl());
                hn->SetPartner(vn);
                vn->SetPartner(hn);
            }
            return vn;
        };
        traverse(TreeBank::GetViewRoot(), dirname);
    };

    /*
      Firefox:

      QStandardPaths::writableLocation(QStandardPaths::HomeLocation) +
      "/AppData/Roaming/Mozilla/Firefox/Profiles/~~~.default/" +
      "bookmarkbackups/bookmarks-yyyy-mm-dd_n.json";

    */
    std::function<void()> importFromFirefox = [&](){
        std::function<void(ViewNode *parent, QJsonObject obj)> traverse;
        std::function<ViewNode*(ViewNode *parent, QJsonObject obj)> makenode;

        QString bookmarks;

        QString profile =
#if defined(Q_OS_WIN)
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation) +
            QStringLiteral("/AppData/Roaming/Mozilla/Firefox/Profiles")
#elif defined(Q_OS_MAC)
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
            QStringLiteral("/Firefox/Profiles")
#else
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation) +
            QStringLiteral("/.mozilla/Firefox/Profiles")
#endif
        ;
        QDir profiledir = profile;
        QStringList list =
            profiledir.entryList(QStringList() << QStringLiteral("*.default"),
                                 QDir::NoFilter, QDir::Name);
        if(!list.isEmpty()){
            foreach(QString defaultdir, list){
                QString bookmarklist =
                    profile + QStringLiteral("/") + defaultdir + QStringLiteral("/bookmarkbackups");
                QDir bookmarksdir = bookmarklist;
                QStringList jsonlist =
                    bookmarksdir.entryList(QStringList() <<
                                           QStringLiteral("bookmarks-[0-9][0-9][0-9][0-9]-"
                                                          VV"[0-9][0-9]-[0-9][0-9]_[0-9]*.json"),
                                           QDir::NoFilter, QDir::Name);
                if(!jsonlist.isEmpty()){
                    bookmarks = bookmarklist + QStringLiteral("/") + jsonlist.takeLast();
                    break;
                }
            }
        }

        QString filename =
            ModalDialog::GetOpenFileName_(QString::null, bookmarks, QStringLiteral("Json Files (*.json)"));

        if(filename.isEmpty()) return;

        QFile file(filename);
        if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;

        QByteArray data = file.readAll();
        file.close();
        QJsonObject root = QJsonDocument::fromJson(data).object();

        traverse = [&](ViewNode *parent, QJsonObject obj){
            ViewNode *vn = makenode(parent, obj);
            const QString children = QStringLiteral("children");
            if(obj.contains(children)){
                QJsonValue val = obj[children];
                if(!val.isArray()) return;
                foreach(QJsonValue child, val.toArray()){
                    traverse(vn, child.toObject());
                }
            }
        };
        makenode = [&](ViewNode *parent, QJsonObject obj){
            ViewNode *vn = parent->MakeChild(true);
            const QString title = QStringLiteral("title");
            const QString children = QStringLiteral("children");
            const QString uri = QStringLiteral("uri");
            vn->SetTitle(obj[title].toString());
            if(!obj.contains(children)){
                HistNode *hn = TreeBank::GetHistRoot()->MakeChild();
                hn->SetTitle(obj[title].toString());
                hn->SetUrl(QUrl(obj[uri].toString()));
                hn->SetPartner(vn);
                vn->SetPartner(hn);
            }
            return vn;
        };
        traverse(TreeBank::GetViewRoot(), root);
    };

    /*
      Chrome:

      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
      "/Google/Chrome/User Data/Default/Bookmarks"

    */
    std::function<void()> importFromChrome = [&](){
        std::function<void(ViewNode *parent, QJsonObject obj)> traverse;
        std::function<ViewNode*(ViewNode *parent, QJsonObject obj)> makenode;

        QString bookmarks =
#if defined(Q_OS_WIN)
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
            QStringLiteral("/Google/Chrome/User Data/Default/Bookmarks")
#elif defined(Q_OS_MAC)
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
            QStringLiteral("/Google/Chrome/Default/Bookmarks")
#else
            QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
            QStringLiteral("/google-chrome/Default/Bookmarks")
#endif
        ;

        QString filename =
            ModalDialog::GetOpenFileName_(QString::null, bookmarks, QStringLiteral("Bookmarks"));

        if(filename.isEmpty()) return;

        QFile file(filename);
        if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;

        QByteArray data = file.readAll();
        file.close();
        const QString roots = QStringLiteral("roots");
        QJsonObject root = QJsonDocument::fromJson(data).object()[roots].toObject();

        traverse = [&](ViewNode *parent, QJsonObject obj){
            ViewNode *vn = makenode(parent, obj);
            const QString children = QStringLiteral("children");
            if(obj.contains(children)){
                QJsonValue val = obj[children];
                if(!val.isArray()) return;
                foreach(QJsonValue child, val.toArray()){
                    traverse(vn, child.toObject());
                }
            }
        };

        makenode = [&](ViewNode *parent, QJsonObject obj){
            ViewNode *vn = parent->MakeChild(true);
            const QString name = QStringLiteral("name");
            const QString children = QStringLiteral("children");
            const QString url = QStringLiteral("url");
            vn->SetTitle(obj[name].toString());

            if(!obj.contains(children)){
                HistNode *hn = TreeBank::GetHistRoot()->MakeChild();
                hn->SetTitle(obj[name].toString());
                hn->SetUrl(QUrl(obj[url].toString()));
                hn->SetPartner(vn);
                vn->SetPartner(hn);
            }
            return vn;
        };
        ViewNode *rootnode = TreeBank::GetViewRoot()->MakeChild(true);
        rootnode->SetTitle(QStringLiteral("Favorites"));
        foreach(QString key, root.keys()){
            traverse(rootnode, root[key].toObject());
        }
    };

    std::function<void()> importFromInternalFormat = [&](){

        QString filename = ModalDialog::GetOpenFileName_
            (QString::null,
             QStringLiteral("*.xml"),
             QStringLiteral("Xml Document (*.xml)"));

        if(filename.isEmpty()) return;

        std::function<void(QDomElement elem, ViewNode *parent)> collectViewDom;
        std::function<void(QDomElement elem, HistNode *parent, ViewNode *partner)> collectHistDom;

        collectViewDom = [&](QDomElement elem, ViewNode *parent){
            ViewNode *vn = parent->MakeChild(true);
            if(elem.attribute(QStringLiteral("primary"), QStringLiteral("false")) == QStringLiteral("true"))
                parent->SetPrimary(vn);
            if(elem.attribute(QStringLiteral("title"), QString()) != QString())
                vn->SetTitle(elem.attribute(QStringLiteral("title")));

            QString create = elem.attribute(QStringLiteral("create"), QString());
            QString lastAccess = elem.attribute(QStringLiteral("lastaccess"), QString());
            QString lastUpdate = elem.attribute(QStringLiteral("lastupdate"), QString());
            vn->SetCreateDate(create.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromString(create, NODE_DATETIME_FORMAT));
            vn->SetLastAccessDate(lastAccess.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromString(lastAccess, NODE_DATETIME_FORMAT));
            vn->SetLastUpdateDate(lastUpdate.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromString(lastUpdate, NODE_DATETIME_FORMAT));

            if(elem.attribute(QStringLiteral("holdview"), QStringLiteral("false")) == QStringLiteral("true")){
                collectHistDom(elem.firstChildElement(), TreeBank::GetHistRoot(), vn);
            } else {
                QDomNodeList children = elem.childNodes();

                for(uint i = 0; i < static_cast<uint>(children.length()); i++){
                    collectViewDom(children.item(i).toElement(), vn);
                }
            }
        };

        collectHistDom = [&](QDomElement elem, HistNode *parent, ViewNode *partner){
            HistNode *hn = parent->MakeChild();
            bool prt = elem.attribute(QStringLiteral("partner"), QStringLiteral("false")) == QStringLiteral("true");

            hn->SetTitle(elem.attribute(QStringLiteral("title")));
            hn->SetUrl(QUrl::fromEncoded(elem.attribute(QStringLiteral("url")).toLatin1()));

            hn->SetScrollX(elem.attribute(QStringLiteral("scrollx"), QStringLiteral("0")).toInt());
            hn->SetScrollY(elem.attribute(QStringLiteral("scrolly"), QStringLiteral("0")).toInt());
            hn->SetZoom(elem.attribute(QStringLiteral("zoom"), QStringLiteral("100")).toInt());

            QString create = elem.attribute(QStringLiteral("create"), QString());
            QString lastAccess = elem.attribute(QStringLiteral("lastaccess"), QString());
            QString lastUpdate = elem.attribute(QStringLiteral("lastupdate"), QString());
            hn->SetCreateDate(create.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromString(create, NODE_DATETIME_FORMAT));
            hn->SetLastAccessDate(lastAccess.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromString(lastAccess, NODE_DATETIME_FORMAT));
            hn->SetLastUpdateDate(lastUpdate.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromString(lastUpdate, NODE_DATETIME_FORMAT));

            hn->SetPartner(partner);

            if(prt){
                partner->SetPartner(hn);
                partner->SetTitle(hn->GetTitle());
            }

            QDomNodeList children = elem.childNodes();
            for(uint i = 0; i < static_cast<uint>(children.length()); i++){
                collectHistDom(children.item(i).toElement(), hn, partner);
            }
        };

        QDomDocument doc;
        QFile file(filename);
        bool check = doc.setContent(&file);
        file.close();
        if(!check) return;

        QDomNodeList children = doc.documentElement().childNodes();
        for(uint i = 0; i < static_cast<uint>(children.length()); i++){
            collectViewDom(children.item(i).toElement(), TreeBank::GetViewRoot());
        }
    };

    std::function<void()> importFromXbel = [&](){

        QString filename = ModalDialog::GetOpenFileName_
            (QString::null,
             QStringLiteral("*.xbel"),
             QStringLiteral("Xbel Files (*.xbel)"));

        if(filename.isEmpty()) return;

        std::function<void(QDomElement elem, ViewNode *parent)> collectDom;

        collectDom = [&](QDomElement elem, ViewNode *parent){
            if(elem.tagName() == QStringLiteral("folder")){
                ViewNode *vn = parent->MakeChild(true);
                QString added = elem.attribute(QStringLiteral("added"), QString());

                vn->SetCreateDate    (added.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromString(added, Qt::ISODate));
                vn->SetLastAccessDate(added.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromString(added, Qt::ISODate));
                vn->SetLastUpdateDate(added.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromString(added, Qt::ISODate));

                QDomNodeList children = elem.childNodes();
                for(uint i = 0; i < static_cast<uint>(children.length()); i++){
                    collectDom(children.item(i).toElement(), vn);
                }
            } else if(elem.tagName() == QStringLiteral("bookmark")){
                ViewNode *vn = parent->MakeChild(true);
                HistNode *hn = TreeBank::GetHistRoot()->MakeChild();

                QString href = elem.attribute(QStringLiteral("href"), QString());
                QString added = elem.attribute(QStringLiteral("added"), QString());
                QString visited = elem.attribute(QStringLiteral("visited"), QString());
                QString modified = elem.attribute(QStringLiteral("modified"), QString());

                hn->SetUrl(QUrl::fromEncoded(href.toLatin1()));
                hn->SetCreateDate    (added.isEmpty()    ? QDateTime::currentDateTime() : QDateTime::fromString(added, Qt::ISODate));
                vn->SetCreateDate    (added.isEmpty()    ? QDateTime::currentDateTime() : QDateTime::fromString(added, Qt::ISODate));
                hn->SetLastAccessDate(visited.isEmpty()  ? QDateTime::currentDateTime() : QDateTime::fromString(visited, Qt::ISODate));
                vn->SetLastAccessDate(visited.isEmpty()  ? QDateTime::currentDateTime() : QDateTime::fromString(visited, Qt::ISODate));
                hn->SetLastUpdateDate(modified.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromString(modified, Qt::ISODate));
                vn->SetLastUpdateDate(modified.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromString(modified, Qt::ISODate));

                hn->SetPartner(vn);
                vn->SetPartner(hn);

                QDomNodeList children = elem.childNodes();

                for(uint i = 0; i < static_cast<uint>(children.length()); i++){
                    collectDom(children.item(i).toElement(), vn);
                }
            } else if(elem.tagName() == QStringLiteral("title")){
                parent->SetTitle(elem.text());
                if(Node *nd = parent->GetPartner()){
                    nd->SetTitle(elem.text());
                }
            } else {
                // do nothing.
                // alias, separator, info and metadata are unnecessary.
            }
        };

        QDomDocument doc;
        QFile file(filename);
        bool check = doc.setContent(&file);
        file.close();
        if(!check) return;

        QDomNodeList children = doc.documentElement().childNodes();
        for(uint i = 0; i < static_cast<uint>(children.length()); i++){
            collectDom(children.item(i).toElement(), TreeBank::GetViewRoot());
        }
    };

    std::function<void()> importFromHtml = [&](){

        QString filename = ModalDialog::GetOpenFileName_
            (QString::null,
             QStringLiteral("*.html"),
             QStringLiteral("Html Files (*.html)"));

        QFile file(filename);
        if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return;

        QByteArray data = file.readAll();
        file.close();
#ifdef QTWEBKIT
        QWebPage page;
        page.mainFrame()->setHtml(QString::fromUtf8(data));

        std::function<void(QWebElement elem, ViewNode *parent)> collectDom;

        collectDom = [&](QWebElement elem, ViewNode *parent){
            QWebElement first = elem.firstChild();

            if(first.tagName().toLower() == QStringLiteral("a")){
                ViewNode *vn = parent->MakeChild(true);
                HistNode *hn = TreeBank::GetHistRoot()->MakeChild();

                QString title = first.toPlainText();
                QString href = first.attribute(QStringLiteral("HREF"), QString());
                QString added = first.attribute(QStringLiteral("ADD_DATE"), QString());
                QString visited = first.attribute(QStringLiteral("LAST_VISIT"), QString());
                QString modified = first.attribute(QStringLiteral("LAST_MODIFIED"), QString());

                hn->SetTitle(title);
                vn->SetTitle(title);
                hn->SetUrl(QUrl::fromEncoded(href.toLatin1()));
                hn->SetCreateDate(added.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromTime_t(added.toUInt()));
                vn->SetCreateDate(added.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromTime_t(added.toUInt()));
                hn->SetLastAccessDate(visited.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromTime_t(visited.toUInt()));
                vn->SetLastAccessDate(visited.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromTime_t(visited.toUInt()));
                hn->SetLastUpdateDate(modified.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromTime_t(modified.toUInt()));
                vn->SetLastUpdateDate(modified.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromTime_t(modified.toUInt()));

                hn->SetPartner(vn);
                vn->SetPartner(hn);
            } else if(first.tagName().toLower() == QStringLiteral("h3")){
                ViewNode *vn = parent->MakeChild(true);

                QString title = first.toPlainText();
                QString added = first.attribute(QStringLiteral("ADD_DATE"), QString());
                QString visited = first.attribute(QStringLiteral("LAST_VISIT"), QString());
                QString modified = first.attribute(QStringLiteral("LAST_MODIFIED"), QString());
                QString folded = first.attribute(QStringLiteral("FOLDED"), QStringLiteral("true"));

                vn->SetTitle(first.toPlainText());
                vn->SetCreateDate(added.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromTime_t(added.toUInt()));
                vn->SetLastAccessDate(visited.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromTime_t(visited.toUInt()));
                vn->SetLastUpdateDate(modified.isEmpty() ? QDateTime::currentDateTime() : QDateTime::fromTime_t(modified.toUInt()));
                vn->SetFolded(folded == QStringLiteral("true") ? true : false);

                QWebElement child = elem.findFirst(QStringLiteral("dt"));
                while(!child.isNull()){
                    collectDom(child, vn);
                    child = child.nextSibling();
                }
            }
        };

        QWebElement child = page.mainFrame()->findFirstElement(QStringLiteral("dt"));
        while(!child.isNull()){
            collectDom(child, TreeBank::GetViewRoot());
            child = child.nextSibling();
        }
#else
        QString source = QString::fromUtf8(data);
        ViewNode *vn = TreeBank::GetViewRoot();
        QRegExp tagrx("<(/?)([a-zA-Z0-9]+)([^<>]*)>([^<>]*)");
        QRegExp attrrx(" ([a-zA-Z-_]+)=([^ <>]+)");
        int pos = 0;
        while((pos = tagrx.indexIn(source, pos)) != -1 && vn){
            QStringList capture = tagrx.capturedTexts();
            capture.takeFirst();

            QString close = capture.takeFirst();
            QString tagName = capture.takeFirst();

            if(tagName == QStringLiteral("DT")){
                if(close.isEmpty())
                    vn = vn->MakeChild(true);
            } else if(tagName == QStringLiteral("DL")){
                if(!close.isEmpty() && vn->GetParent())
                    vn = vn->GetParent()->ToViewNode();
            } else if(tagName == QStringLiteral("H3") && close.isEmpty()){
                QString attrs = capture.takeFirst();
                int attrpos = 0;
                while((attrpos = attrrx.indexIn(attrs, attrpos)) != -1){
                    QStringList attrcapture = attrrx.capturedTexts();
                    attrcapture.takeFirst();
                    QString name = attrcapture.takeFirst();
                    QString attr = attrcapture.takeFirst();
                    attr.replace("\"", "");
                    if(name == QStringLiteral("ADD_DATE")){
                        vn->SetCreateDate(QDateTime::fromTime_t(attr.toUInt()));
                    } else if(name == QStringLiteral("LAST_VISIT")){
                        vn->SetLastAccessDate(QDateTime::fromTime_t(attr.toUInt()));
                    } else if(name == QStringLiteral("LAST_MODIFIED")){
                        vn->SetLastUpdateDate(QDateTime::fromTime_t(attr.toUInt()));
                    } else if(name == QStringLiteral("FOLDED")){
                        vn->SetFolded(attr == QStringLiteral("true") ? true : false);
                    }
                    attrpos += attrrx.matchedLength();
                }
                vn->SetTitle(capture.takeFirst());
            } else if(tagName == QStringLiteral("A") && close.isEmpty()){
                HistNode *hn = TreeBank::GetHistRoot()->MakeChild();
                QString attrs = capture.takeFirst();
                int attrpos = 0;
                while((attrpos = attrrx.indexIn(attrs, attrpos)) != -1){
                    QStringList attrcapture = attrrx.capturedTexts();
                    attrcapture.takeFirst();
                    QString name = attrcapture.takeFirst();
                    QString attr = attrcapture.takeFirst();
                    attr.replace("\"", "");
                    if(name == QStringLiteral("HREF")){
                        hn->SetUrl(QUrl::fromEncoded(attr.toLatin1()));
                    } else if(name == QStringLiteral("ADD_DATE")){
                        hn->SetCreateDate(QDateTime::fromTime_t(attr.toUInt()));
                        vn->SetCreateDate(QDateTime::fromTime_t(attr.toUInt()));
                    } else if(name == QStringLiteral("LAST_VISIT")){
                        hn->SetLastAccessDate(QDateTime::fromTime_t(attr.toUInt()));
                        vn->SetLastAccessDate(QDateTime::fromTime_t(attr.toUInt()));
                    } else if(name == QStringLiteral("LAST_MODIFIED")){
                        hn->SetLastUpdateDate(QDateTime::fromTime_t(attr.toUInt()));
                        vn->SetLastUpdateDate(QDateTime::fromTime_t(attr.toUInt()));
                    }
                    attrpos += attrrx.matchedLength();
                }
                QString title = capture.takeFirst();
                hn->SetTitle(title);
                vn->SetTitle(title);
                hn->SetPartner(vn);
                vn->SetPartner(hn);
                vn = vn->GetParent()->ToViewNode();
            }
            pos += tagrx.matchedLength();
        }
#endif
    };

    bool ok = true;
    QString which = ModalDialog::GetItem(tr("Import Favorites"),
                                         tr("Select browser or file format"),
                                         QStringList()
#if defined(Q_OS_WIN)
                                         << QStringLiteral("IE")
#endif
                                         << QStringLiteral("Firefox")
                                         << QStringLiteral("Chrome")
                                         << QStringLiteral("Internal Format")
                                         << QStringLiteral("Xbel")
                                         << QStringLiteral("Html")
                                         ,
                                         false, &ok);
    if(!ok) return;
#if defined(Q_OS_WIN)
    else if(which == QStringLiteral("IE")) importFromIE();
#endif
    else if(which == QStringLiteral("Firefox")) importFromFirefox();
    else if(which == QStringLiteral("Chrome")) importFromChrome();
    else if(which == QStringLiteral("Internal Format")) importFromInternalFormat();
    else if(which == QStringLiteral("Xbel")) importFromXbel();
    else if(which == QStringLiteral("Html")) importFromHtml();
    else return;
    if(tb){
        QTimer::singleShot(0, [tb](){
                // dialog gives focus to view(previous focused object?) later.
                tb->DisplayViewTree(TreeBank::GetViewRoot());
                tb->GetGadgets()->setFocus(Qt::OtherFocusReason);
            });
    }
}

void Application::Export(TreeBank *tb){
    Q_UNUSED(tb);
    std::function<void()> exportAsInternalFormat = [&](){

        QString filename = ModalDialog::GetSaveFileName_
            (QString::null,
             QStringLiteral("*.xml"),
             QStringLiteral("XML Document (*.xml)"));

        std::function<void(ViewNode *root, ViewNode *nd, QDomDocument doc, QDomElement elem)> collectViewNode;
        std::function<void(ViewNode *root, HistNode *nd, QDomDocument doc, QDomElement elem)> collectHistNode;

        collectViewNode = [&](ViewNode *root, ViewNode *nd, QDomDocument doc, QDomElement elem){
            QDomElement child = doc.createElement(QStringLiteral("viewnode"));
            child.setAttribute(QStringLiteral("primary"), nd->IsPrimaryOfParent() ? QStringLiteral("true") : QStringLiteral("false"));
            child.setAttribute(QStringLiteral("holdview"), nd->GetPartner() ? QStringLiteral("true") : QStringLiteral("false"));
            child.setAttribute(QStringLiteral("title"), nd->GetTitle());

            elem.appendChild(child);
            if(nd->GetPartner()){
                collectHistNode(root, TreeBank::GetRoot(nd->GetPartner())->ToHistNode(), doc, child);
            } else {
                foreach(Node *childnode, nd->GetChildren()){
                    collectViewNode(root, childnode->ToViewNode(), doc, child);
                }
            }
        };

        collectHistNode = [&](ViewNode *root, HistNode *nd, QDomDocument doc, QDomElement elem){
            QDomElement child = doc.createElement(QStringLiteral("histnode"));
            child.setAttribute(QStringLiteral("primary"), nd->IsPrimaryOfParent()  ? QStringLiteral("true") : QStringLiteral("false"));
            child.setAttribute(QStringLiteral("partner"), nd->IsPartnerOfPartner() ? QStringLiteral("true") : QStringLiteral("false"));
            child.setAttribute(QStringLiteral("index"), TreeBank::WinIndex(nd));
            if(!nd->GetTitle().isEmpty()) child.setAttribute(QStringLiteral("title"), nd->GetTitle());
            if(!nd->GetUrl().isEmpty())   child.setAttribute(QStringLiteral("url"), QString::fromUtf8(nd->GetUrl().toEncoded()));
            if(nd->GetScrollX()) child.setAttribute(QStringLiteral("scrollx"), QStringLiteral("%1").arg(nd->GetScrollX()));
            if(nd->GetScrollY()) child.setAttribute(QStringLiteral("scrolly"), QStringLiteral("%1").arg(nd->GetScrollY()));
            child.setAttribute(QStringLiteral("zoom"), QStringLiteral("%1").arg(nd->GetZoom()));

            elem.appendChild(child);
            foreach(Node *childnode, nd->GetChildren()){
                collectHistNode(root, childnode->ToHistNode(), doc, child);
            }
        };

        ViewNode *vn = TreeBank::GetViewRoot();
        QDomDocument doc;
        doc.appendChild(doc.createProcessingInstruction("xml", "version=\"1.0\" encoding=\"UTF-8\""));
        QDomElement root = doc.createElement(QStringLiteral("viewnode"));
        root.setAttribute(QStringLiteral("root"), QStringLiteral("true"));
        doc.appendChild(root);
        foreach(Node *child, vn->GetChildren()){
            collectViewNode(vn, child->ToViewNode(), doc, root);
        }
        QFile file(filename);
        if(file.open(QIODevice::WriteOnly)){
            QTextStream out(&file);
            doc.save(out, 2);
        }
        file.close();
    };

    std::function<void()> exportAsXbel = [&](){

        QString filename = ModalDialog::GetSaveFileName_
            (QString::null,
             QStringLiteral("*.xbel"),
             QStringLiteral("Xbel files (*.xbel)"));

        QDateTime local = QDateTime::currentDateTime();
        QDateTime utc = local.toUTC();
        utc.setTimeSpec(Qt::LocalTime);
        int utcOffset = utc.secsTo(local);

        std::function<void(ViewNode *nd, QDomDocument doc, QDomElement elem)> collectNode;

        collectNode = [&](ViewNode *nd, QDomDocument doc, QDomElement elem){
            if(nd->GetPartner()){
                HistNode *hn = nd->GetPartner()->ToHistNode();

                QDomElement child = doc.createElement(QStringLiteral("bookmark"));
                child.setAttribute(QStringLiteral("href"), hn->GetUrl().toString());
                QDateTime added = hn->GetCreateDate();
                QDateTime visited = hn->GetLastAccessDate();
                QDateTime modified = hn->GetLastUpdateDate();
                added.setUtcOffset(utcOffset);
                visited.setUtcOffset(utcOffset);
                modified.setUtcOffset(utcOffset);
                child.setAttribute(QStringLiteral("added"), added.toString(Qt::ISODate));
                child.setAttribute(QStringLiteral("visited"), visited.toString(Qt::ISODate));
                child.setAttribute(QStringLiteral("modified"), modified.toString(Qt::ISODate));

                QDomElement titleNode = doc.createElement(QStringLiteral("title"));
                QDomText title = doc.createTextNode(nd->GetTitle());
                titleNode.appendChild(title);
                child.appendChild(titleNode);

                elem.appendChild(child);
            } else {
                QDomElement child = doc.createElement(QStringLiteral("folder"));
                child.setAttribute(QStringLiteral("folded"), nd->GetFolded() ? QStringLiteral("yes") : QStringLiteral("no"));
                QDateTime added = nd->GetCreateDate();
                added.setUtcOffset(utcOffset);
                child.setAttribute(QStringLiteral("added"), added.toString(Qt::ISODate));

                QDomElement titleNode = doc.createElement(QStringLiteral("title"));
                QDomText title = doc.createTextNode(nd->GetTitle());
                titleNode.appendChild(title);
                child.appendChild(titleNode);

                elem.appendChild(child);
                foreach(Node *childnode, nd->GetChildren()){
                    collectNode(childnode->ToViewNode(), doc, child);
                }
            }
        };

        QDomDocument doc(QStringLiteral("xbel"));
        doc.appendChild(doc.createProcessingInstruction("xml", "version=\"1.0\" encoding=\"UTF-8\""));
        QDomElement root = doc.createElement(QStringLiteral("xbel"));
        root.setAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
        doc.appendChild(root);
        foreach(Node *child, TreeBank::GetViewRoot()->GetChildren()){
            collectNode(child->ToViewNode(), doc, root);
        }
        QFile file(filename);
        if(file.open(QIODevice::WriteOnly)){
            QTextStream out(&file);
            doc.save(out, 2);
        }
        file.close();
    };

    std::function<void()> exportAsHtml = [&](){
        QString filename = ModalDialog::GetSaveFileName_
            (QString::null,
             QStringLiteral("*.html"),
             QStringLiteral("Html Files (*.html)"));

        QDateTime local = QDateTime::currentDateTime();
        QDateTime utc = local.toUTC();
        utc.setTimeSpec(Qt::LocalTime);
        int utcOffset = utc.secsTo(local);

#ifdef QTWEBKIT
        std::function<void(ViewNode *vn, QWebElement elem)> collectNode;

        collectNode = [&](ViewNode *vn, QWebElement elem){
            elem.appendInside(QStringLiteral("<DT>"));
            QWebElement DT = elem.lastChild();

            if(vn->GetPartner()){
                HistNode *hn = vn->GetPartner()->ToHistNode();

                DT.appendInside(QStringLiteral("<A>"));
                QWebElement A = DT.firstChild();

                A.setAttribute(QStringLiteral("HREF"), hn->GetUrl().toString());
                QDateTime added    = hn->GetCreateDate();
                QDateTime visited  = hn->GetLastAccessDate();
                QDateTime modified = hn->GetLastUpdateDate();
                added.setUtcOffset(utcOffset);
                visited.setUtcOffset(utcOffset);
                modified.setUtcOffset(utcOffset);
                A.setAttribute(QStringLiteral("ADD_DATE"),      QStringLiteral("%1").arg(added.toTime_t()));
                A.setAttribute(QStringLiteral("LAST_VISIT"),    QStringLiteral("%1").arg(visited.toTime_t()));
                A.setAttribute(QStringLiteral("LAST_MODIFIED"), QStringLiteral("%1").arg(modified.toTime_t()));

                if(vn->GetTitle().isEmpty())
                    A.setPlainText(QStringLiteral("NoTitle"));
                else
                    A.setPlainText(vn->GetTitle());
            } else {
                DT.appendInside(QStringLiteral("<H3>"));
                DT.appendInside(QStringLiteral("<DL>"));
                DT.appendInside(QStringLiteral("<p>"));
                QWebElement H3 = DT.firstChild();
                QWebElement DL = H3.nextSibling();

                QDateTime added = vn->GetCreateDate();
                QDateTime visited = vn->GetLastAccessDate();
                QDateTime modified = vn->GetLastUpdateDate();
                added.setUtcOffset(utcOffset);
                visited.setUtcOffset(utcOffset);
                modified.setUtcOffset(utcOffset);
                QString folded = vn->GetFolded() ? QStringLiteral("true") : QStringLiteral("false");
                H3.setAttribute(QStringLiteral("ADD_DATE"),      QStringLiteral("%1").arg(added.toTime_t()));
                H3.setAttribute(QStringLiteral("LAST_VISIT"),    QStringLiteral("%1").arg(visited.toTime_t()));
                H3.setAttribute(QStringLiteral("LAST_MODIFIED"), QStringLiteral("%1").arg(modified.toTime_t()));
                H3.setAttribute(QStringLiteral("FOLDED"), folded);

                if(vn->GetTitle().isEmpty())
                    H3.setPlainText(QStringLiteral("NoTitle"));
                else
                    H3.setPlainText(vn->GetTitle());

                DL.appendInside(QStringLiteral("<p>"));
                QWebElement p = DL.firstChild();

                foreach(Node *childnode, vn->GetChildren()){
                    collectNode(childnode->ToViewNode(), p);
                }
            }
        };

        QWebPage page;
        QWebElement doc = page.mainFrame()->documentElement();
        doc.appendInside(QStringLiteral("<DL>"));
        QWebElement DL = doc.lastChild();
        doc.appendInside(QStringLiteral("<p>"));
        DL.appendInside(QStringLiteral("<p>"));
        foreach(Node *childnode, TreeBank::GetViewRoot()->GetChildren()){
            collectNode(childnode->ToViewNode(), DL.firstChild());
        }

        QFile file(filename);
        if(file.open(QIODevice::WriteOnly)){
            QTextStream out(&file);
            out.setCodec("UTF-8");
            out <<
                "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
                "<!-- This is an automatically generated file.\n"
                "     It will be read and overwritten.\n"
                "     DO NOT EDIT! -->\n"
                "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
                "<TITLE>Bookmarks</TITLE>\n"
                "<H1>Bookmarks</H1>\n";
            QString body = page.mainFrame()->toHtml();
            body.replace(QStringLiteral("<html><head></head><body></body>"), QString());
            body.replace(QStringLiteral("</html>"), QString());
            body.replace(QStringLiteral("</p>"), QString());
            body.replace(QStringLiteral("</dt>"), QString());
            body.replace(QStringLiteral("<dt>"), QStringLiteral("\n<DT>"));
            body.replace(QStringLiteral("<dl>"), QStringLiteral("\n<DL>"));
            body.replace(QStringLiteral("</dl>"), QStringLiteral("\n</DL>"));
            body.replace(QStringLiteral("<h1>"), QStringLiteral("<H1>"));
            body.replace(QStringLiteral("<h1 "), QStringLiteral("<H1 "));
            body.replace(QStringLiteral("</h1>"), QStringLiteral("</H1>"));
            body.replace(QStringLiteral("<h2>"), QStringLiteral("<H2>"));
            body.replace(QStringLiteral("<h2 "), QStringLiteral("<H2 "));
            body.replace(QStringLiteral("</h2>"), QStringLiteral("</H2>"));
            body.replace(QStringLiteral("<h3>"), QStringLiteral("<H3>"));
            body.replace(QStringLiteral("<h3 "), QStringLiteral("<H3 "));
            body.replace(QStringLiteral("</h3>"), QStringLiteral("</H3>"));
            body.replace(QStringLiteral("<a "), QStringLiteral("<A "));
            body.replace(QStringLiteral("</a>"), QStringLiteral("</A>"));
            body.replace(QStringLiteral(" href="), QStringLiteral(" HREF="));
            body.replace(QStringLiteral(" add_date="), QStringLiteral(" ADD_DATE="));
            body.replace(QStringLiteral(" last_visit="), QStringLiteral(" LAST_VISIT="));
            body.replace(QStringLiteral(" last_modified="), QStringLiteral(" LAST_MODIFIED="));
            body.replace(QStringLiteral(" folded="), QStringLiteral(" FOLDED="));
            out << body;
        }
        file.close();
#else
        std::function<void(ViewNode *vn, int nest, QTextStream &out)> collectNode;

        collectNode = [&](ViewNode *vn, int nest, QTextStream &out){
            out << QString(4*(nest+1), ' ');

            if(vn->GetPartner()){
                HistNode *hn = vn->GetPartner()->ToHistNode();
                QString title = hn->GetTitle();
                if(title.isEmpty()) title = QStringLiteral("NoTitle");
                QDateTime added    = hn->GetCreateDate();
                QDateTime visited  = hn->GetLastAccessDate();
                QDateTime modified = hn->GetLastUpdateDate();
                added.setUtcOffset(utcOffset);
                visited.setUtcOffset(utcOffset);
                modified.setUtcOffset(utcOffset);
                out <<
                    QStringLiteral("<DT><A HREF=\"%1\" ADD_DATE=\"%2\""
                                   VV" LAST_VISIT=\"%3\" LAST_MODIFIED=\"%4\">"
                                   VV"%5</A>\n").arg(hn->GetUrl().toString())
                                                .arg(added.toTime_t())
                                                .arg(visited.toTime_t())
                                                .arg(modified.toTime_t())
                                                .arg(title);
            } else {
                QString title = vn->GetTitle();
                if(title.isEmpty()) title = QStringLiteral("NoTitle");
                QDateTime added = vn->GetCreateDate();
                QDateTime visited = vn->GetLastAccessDate();
                QDateTime modified = vn->GetLastUpdateDate();
                added.setUtcOffset(utcOffset);
                visited.setUtcOffset(utcOffset);
                modified.setUtcOffset(utcOffset);
                QString folded = vn->GetFolded() ? QStringLiteral("true") : QStringLiteral("false");
                out <<
                    QStringLiteral("<DT><H3 ADD_DATE=\"%1\" LAST_VISIT=\"%2\""
                                   VV" LAST_MODIFIED=\"%3\" FOLDED=\"%4\">"
                                   VV"%5</H3>\n").arg(added.toTime_t())
                                                 .arg(visited.toTime_t())
                                                 .arg(modified.toTime_t())
                                                 .arg(folded)
                                                 .arg(title);
                out << QString(4*(nest+1), ' ') << QStringLiteral("<DL><p>\n");
                foreach(Node *child, vn->GetChildren()){
                    collectNode(child->ToViewNode(), nest+1, out);
                }
                out << QString(4*(nest+1), ' ') << QStringLiteral("</DL><p>\n");
            }
        };

        QFile file(filename);
        if(file.open(QIODevice::WriteOnly)){
            QTextStream out(&file);
            out.setCodec("UTF-8");
            out <<
                "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
                "<!-- This is an automatically generated file.\n"
                "     It will be read and overwritten.\n"
                "     DO NOT EDIT! -->\n"
                "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
                "<TITLE>Bookmarks</TITLE>\n"
                "<H1>Bookmarks</H1>\n\n";
            out << QStringLiteral("<DL><p>\n");
            foreach(Node *child, TreeBank::GetViewRoot()->GetChildren()){
                collectNode(child->ToViewNode(), 0, out);
            }
            out << QStringLiteral("</DL><p>\n");
        }
        file.close();
#endif
    };

    bool ok = true;
    QString which = ModalDialog::GetItem(tr("Export as Favorites"),
                                         tr("Select format"),
                                         QStringList()
                                         << QStringLiteral("Internal Format")
                                         << QStringLiteral("Xbel")
                                         << QStringLiteral("Html")
                                         //<< "Json(Chrome)" << "Json(Firefox)"
                                         ,
                                         false, &ok);
    if(!ok) return;
    else if(which == QStringLiteral("Internal Format")) exportAsInternalFormat();
    else if(which == QStringLiteral("Xbel")) exportAsXbel();
    else if(which == QStringLiteral("Html")) exportAsHtml();
}

void Application::AboutVanilla(QWidget *parent){
    Q_UNUSED(parent);
    QString text = tr("Vanilla is a simple web browser.");
    QMessageBox::about(CurrentWidget(), QStringLiteral("Vanilla"), text);
}

void Application::AboutQt(QWidget *parent){
    Q_UNUSED(parent);
    QMessageBox::aboutQt(CurrentWidget());
}

void Application::Quit(){
    foreach(MainWindow *win, m_MainWindows.values()){
        win->hide();
    }
    LocalView::ClearCache();
    // can't release QuickWebView...
    TreeBank::ReleaseAllView();

    connect(m_AutoSaver, SIGNAL(Finished()), m_Instance, SLOT(quit()));
    if(!m_AutoSaver->IsSaving()) m_Instance->SaveAllDataAsync();
}

QSettings *Application::GlobalSettings(){
    if(!m_GlobalSettings){
        QString path = DataDirectory() + GlobalSettingsFileName();
        m_GlobalSettings = new QSettings(path, XMLFormat);

        if(m_GlobalSettings->allKeys().isEmpty()){

            QString filename = GlobalSettingsFileName();
            QDir dir = QDir(DataDirectory());
            QStringList list =
                dir.entryList(BackUpFileFilters(),
                              QDir::NoFilter, QDir::Name | QDir::Reversed);

            if(list.isEmpty()) return m_GlobalSettings;
            foreach(QString backup, list){

                if(!backup.contains(filename)) continue;

                QFile backupfile(DataDirectory() + backup);
                m_GlobalSettings->deleteLater();

                if(QFile::exists(path))
                    QFile::remove(path);
                QFile::copy(DataDirectory() + backup, path);

                m_GlobalSettings = new QSettings(path, XMLFormat);

                if(m_GlobalSettings->allKeys().isEmpty()) continue;

                ModelessDialog::Information
                    (tr("Restored from a back up file")+ QStringLiteral(" [") + backup + QStringLiteral("]."),
                     tr("Because of a failure to read the latest file, it was restored from a backup file."));
                break;
            }
        }
    }
    return m_GlobalSettings;
}

void Application::SaveGlobalSettings(){
    QSettings *s = GlobalSettings();
    s->beginGroup(QStringLiteral("application"));{
        s->setValue(QStringLiteral("@EnableGoogleSuggest"),   m_EnableGoogleSuggest);
        s->setValue(QStringLiteral("@EnableFramelessWindow"), m_EnableFramelessWindow);
        s->setValue(QStringLiteral("@EnableAutoSave"),        m_EnableAutoSave);
        s->setValue(QStringLiteral("@EnableAutoLoad"),        m_EnableAutoLoad);
        s->setValue(QStringLiteral("@AutoSaveInterval"),      m_AutoSaveInterval);
        s->setValue(QStringLiteral("@AutoLoadInterval"),      m_AutoLoadInterval);
        s->setValue(QStringLiteral("@WheelScrollRate"),       m_WheelScrollRate);
        s->setValue(QStringLiteral("@MaxBackupFileCount"),    m_MaxBackUpFileCount);
        s->setValue(QStringLiteral("@FileSaveDirectory"),     m_DownloadDirectory);
        s->setValue(QStringLiteral("@FileOpenDirectory"),     m_UploadDirectory);
        s->setValue(QStringLiteral("@SaveSessionCookie"),     m_SaveSessionCookie);
#ifdef WEBENGINEPROFILE_ACCEPTLANGUAGE
        s->setValue(QStringLiteral("@AcceptLanguage"),        m_AcceptLanguage);
#endif
        s->setValue(QStringLiteral("@AllowedHosts"),          m_AllowedHosts);
        s->setValue(QStringLiteral("@BlockedHosts"),          m_BlockedHosts);

        SslErrorPolicy sslPolicy = m_SslErrorPolicy;
        if(sslPolicy == Undefined)             s->setValue(QStringLiteral("@SslErrorPolicy"), QStringLiteral("Undefined"));
        if(sslPolicy == BlockAccess)           s->setValue(QStringLiteral("@SslErrorPolicy"), QStringLiteral("BlockAccess"));
        if(sslPolicy == IgnoreSslErrors)       s->setValue(QStringLiteral("@SslErrorPolicy"), QStringLiteral("IgnoreSslErrors"));
        if(sslPolicy == AskForEachAccess)      s->setValue(QStringLiteral("@SslErrorPolicy"), QStringLiteral("AskForEachAccess"));
        if(sslPolicy == AskForEachHost)        s->setValue(QStringLiteral("@SslErrorPolicy"), QStringLiteral("AskForEachHost"));
        if(sslPolicy == AskForEachCertificate) s->setValue(QStringLiteral("@SslErrorPolicy"), QStringLiteral("AskForEachCertificate"));

        DownloadPolicy downPolicy = m_DownloadPolicy;
        if(downPolicy == Undefined_)         s->setValue(QStringLiteral("@DownloadPolicy"), QStringLiteral("Undefined"));
        if(downPolicy == FixedLocale)        s->setValue(QStringLiteral("@DownloadPolicy"), QStringLiteral("FixedLocale"));
        if(downPolicy == DownloadFolder)     s->setValue(QStringLiteral("@DownloadPolicy"), QStringLiteral("DownloadFolder"));
        if(downPolicy == AskForEachDownload) s->setValue(QStringLiteral("@DownloadPolicy"), QStringLiteral("AskForEachDownload"));

        s->setValue(QStringLiteral("UserAgent_IE"),        DEFAULT_USER_AGENT_IE        == m_UserAgent_IE        ? QString() : m_UserAgent_IE        );
        s->setValue(QStringLiteral("UserAgent_Firefox"),   DEFAULT_USER_AGENT_FIREFOX   == m_UserAgent_FF        ? QString() : m_UserAgent_FF        );
        s->setValue(QStringLiteral("UserAgent_Opera"),     DEFAULT_USER_AGENT_OPERA     == m_UserAgent_Opera     ? QString() : m_UserAgent_Opera     );
        s->setValue(QStringLiteral("UserAgent_OPR"),       DEFAULT_USER_AGENT_OPR       == m_UserAgent_OPR       ? QString() : m_UserAgent_OPR       );
        s->setValue(QStringLiteral("UserAgent_Safari"),    DEFAULT_USER_AGENT_SAFARI    == m_UserAgent_Safari    ? QString() : m_UserAgent_Safari    );
        s->setValue(QStringLiteral("UserAgent_Chrome"),    DEFAULT_USER_AGENT_CHROME    == m_UserAgent_Chrome    ? QString() : m_UserAgent_Chrome    );
        s->setValue(QStringLiteral("UserAgent_Sleipnir"),  DEFAULT_USER_AGENT_SLEIPNIR  == m_UserAgent_Sleipnir  ? QString() : m_UserAgent_Sleipnir  );
        s->setValue(QStringLiteral("UserAgent_Vivaldi"),   DEFAULT_USER_AGENT_VIVALDI   == m_UserAgent_Vivaldi   ? QString() : m_UserAgent_Vivaldi   );
        s->setValue(QStringLiteral("UserAgent_NetScape"),  DEFAULT_USER_AGENT_NETSCAPE  == m_UserAgent_NetScape  ? QString() : m_UserAgent_NetScape  );
        s->setValue(QStringLiteral("UserAgent_SeaMonkey"), DEFAULT_USER_AGENT_SEAMONKEY == m_UserAgent_SeaMonkey ? QString() : m_UserAgent_SeaMonkey );
        s->setValue(QStringLiteral("UserAgent_Gecko"),     DEFAULT_USER_AGENT_GECKO     == m_UserAgent_Gecko     ? QString() : m_UserAgent_Gecko     );
        s->setValue(QStringLiteral("UserAgent_iCab"),      DEFAULT_USER_AGENT_ICAB      == m_UserAgent_iCab      ? QString() : m_UserAgent_iCab      );
        s->setValue(QStringLiteral("UserAgent_Camino"),    DEFAULT_USER_AGENT_CAMINO    == m_UserAgent_Camino    ? QString() : m_UserAgent_Camino    );
        s->setValue(QStringLiteral("UserAgent_Custom"), m_UserAgent_Custom);

        s->setValue(QStringLiteral("BrowserPath_IE"),       m_BrowserPath_IE       );
        s->setValue(QStringLiteral("BrowserPath_Firefox"),  m_BrowserPath_FF       );
        s->setValue(QStringLiteral("BrowserPath_Opera"),    m_BrowserPath_Opera    );
        s->setValue(QStringLiteral("BrowserPath_OPR"),      m_BrowserPath_OPR      );
        s->setValue(QStringLiteral("BrowserPath_Safari"),   m_BrowserPath_Safari   );
        s->setValue(QStringLiteral("BrowserPath_Chrome"),   m_BrowserPath_Chrome   );
        s->setValue(QStringLiteral("BrowserPath_Sleipnir"), m_BrowserPath_Sleipnir );
        s->setValue(QStringLiteral("BrowserPath_Vivaldi"),  m_BrowserPath_Vivaldi  );
        s->setValue(QStringLiteral("BrowserPath_Custom"),   m_BrowserPath_Custom   );
    }
    s->endGroup();
}

void Application::LoadGlobalSettings(){
    QSettings *s = GlobalSettings();
    s->beginGroup(QStringLiteral("application"));{
        m_EnableGoogleSuggest   = s->value(QStringLiteral("@EnableGoogleSuggest"), false).value<bool>();
        m_EnableFramelessWindow = s->value(QStringLiteral("@EnableFramelessWindow"), false).value<bool>();
        m_EnableAutoSave        = s->value(QStringLiteral("@EnableAutoSave"), true).value<bool>();
        m_EnableAutoLoad        = s->value(QStringLiteral("@EnableAutoLoad"), true).value<bool>();
        m_AutoSaveInterval      = s->value(QStringLiteral("@AutoSaveInterval"), 300000).value<int>();
        m_AutoLoadInterval      = s->value(QStringLiteral("@AutoLoadInterval"),   1000).value<int>();
        m_MaxBackUpFileCount    = s->value(QStringLiteral("@MaxBackupFileCount"),   50).value<int>();
        m_WheelScrollRate       = s->value(QStringLiteral("@WheelScrollRate"),     1.0).value<double>();
        m_DownloadDirectory     = s->value(QStringLiteral("@FileSaveDirectory"), QString()).value<QString>();
        m_UploadDirectory       = s->value(QStringLiteral("@FileOpenDirectory"), QString()).value<QString>();
        m_SaveSessionCookie     = s->value(QStringLiteral("@SaveSessionCookie"), false).value<bool>();
#ifdef WEBENGINEPROFILE_ACCEPTLANGUAGE
        m_AcceptLanguage        = s->value(QStringLiteral("@AcceptLanguage"), tr("en-US")).value<QString>();
#endif
        m_AllowedHosts          = s->value(QStringLiteral("@AllowedHosts"), QStringList()).value<QStringList>();
        m_BlockedHosts          = s->value(QStringLiteral("@BlockedHosts"), QStringList()).value<QStringList>();

        QString sslPolicy = s->value(QStringLiteral("@SslErrorPolicy"), QStringLiteral("Undefined")).value<QString>();
        if(sslPolicy == QStringLiteral("Undefined"))             m_SslErrorPolicy = Undefined;
        if(sslPolicy == QStringLiteral("BlockAccess"))           m_SslErrorPolicy = BlockAccess;
        if(sslPolicy == QStringLiteral("IgnoreSslErrors"))       m_SslErrorPolicy = IgnoreSslErrors;
        if(sslPolicy == QStringLiteral("AskForEachAccess"))      m_SslErrorPolicy = AskForEachAccess;
        if(sslPolicy == QStringLiteral("AskForEachHost"))        m_SslErrorPolicy = AskForEachHost;
        if(sslPolicy == QStringLiteral("AskForEachCertificate")) m_SslErrorPolicy = AskForEachCertificate;

        QString downPolicy = s->value(QStringLiteral("@DownloadPolicy"), QStringLiteral("Undefined")).value<QString>();
        if(downPolicy == QStringLiteral("Undefined"))          m_DownloadPolicy = Undefined_;
        if(downPolicy == QStringLiteral("FixedLocale"))        m_DownloadPolicy = FixedLocale;
        if(downPolicy == QStringLiteral("DownloadFolder"))     m_DownloadPolicy = DownloadFolder;
        if(downPolicy == QStringLiteral("AskForEachDownload")) m_DownloadPolicy = AskForEachDownload;

        m_UserAgent_IE        = s->value(QStringLiteral("UserAgent_IE"),        DEFAULT_USER_AGENT_IE        ).value<QString>();
        m_UserAgent_FF        = s->value(QStringLiteral("UserAgent_Firefox"),   DEFAULT_USER_AGENT_FIREFOX   ).value<QString>();
        m_UserAgent_Opera     = s->value(QStringLiteral("UserAgent_Opera"),     DEFAULT_USER_AGENT_OPERA     ).value<QString>();
        m_UserAgent_OPR       = s->value(QStringLiteral("UserAgent_OPR"),       DEFAULT_USER_AGENT_OPR       ).value<QString>();
        m_UserAgent_Safari    = s->value(QStringLiteral("UserAgent_Safari"),    DEFAULT_USER_AGENT_SAFARI    ).value<QString>();
        m_UserAgent_Chrome    = s->value(QStringLiteral("UserAgent_Chrome"),    DEFAULT_USER_AGENT_CHROME    ).value<QString>();
        m_UserAgent_Sleipnir  = s->value(QStringLiteral("UserAgent_Sleipnir"),  DEFAULT_USER_AGENT_SLEIPNIR  ).value<QString>();
        m_UserAgent_Vivaldi   = s->value(QStringLiteral("UserAgent_Vivaldi"),   DEFAULT_USER_AGENT_VIVALDI   ).value<QString>();
        m_UserAgent_NetScape  = s->value(QStringLiteral("UserAgent_NetScape"),  DEFAULT_USER_AGENT_NETSCAPE  ).value<QString>();
        m_UserAgent_SeaMonkey = s->value(QStringLiteral("UserAgent_SeaMonkey"), DEFAULT_USER_AGENT_SEAMONKEY ).value<QString>();
        m_UserAgent_Gecko     = s->value(QStringLiteral("UserAgent_Gecko"),     DEFAULT_USER_AGENT_GECKO     ).value<QString>();
        m_UserAgent_iCab      = s->value(QStringLiteral("UserAgent_iCab"),      DEFAULT_USER_AGENT_ICAB      ).value<QString>();
        m_UserAgent_Camino    = s->value(QStringLiteral("UserAgent_Camino"),    DEFAULT_USER_AGENT_CAMINO    ).value<QString>();
        m_UserAgent_Custom    = s->value(QStringLiteral("UserAgent_Custom"),    QString()).value<QString>();
        if(m_UserAgent_IE        .isEmpty()) m_UserAgent_IE        = DEFAULT_USER_AGENT_IE        ;
        if(m_UserAgent_FF        .isEmpty()) m_UserAgent_FF        = DEFAULT_USER_AGENT_FIREFOX   ;
        if(m_UserAgent_Opera     .isEmpty()) m_UserAgent_Opera     = DEFAULT_USER_AGENT_OPERA     ;
        if(m_UserAgent_OPR       .isEmpty()) m_UserAgent_OPR       = DEFAULT_USER_AGENT_OPR       ;
        if(m_UserAgent_Safari    .isEmpty()) m_UserAgent_Safari    = DEFAULT_USER_AGENT_SAFARI    ;
        if(m_UserAgent_Chrome    .isEmpty()) m_UserAgent_Chrome    = DEFAULT_USER_AGENT_CHROME    ;
        if(m_UserAgent_Sleipnir  .isEmpty()) m_UserAgent_Sleipnir  = DEFAULT_USER_AGENT_SLEIPNIR  ;
        if(m_UserAgent_Vivaldi   .isEmpty()) m_UserAgent_Vivaldi   = DEFAULT_USER_AGENT_VIVALDI   ;
        if(m_UserAgent_NetScape  .isEmpty()) m_UserAgent_NetScape  = DEFAULT_USER_AGENT_NETSCAPE  ;
        if(m_UserAgent_SeaMonkey .isEmpty()) m_UserAgent_SeaMonkey = DEFAULT_USER_AGENT_SEAMONKEY ;
        if(m_UserAgent_Gecko     .isEmpty()) m_UserAgent_Gecko     = DEFAULT_USER_AGENT_GECKO     ;
        if(m_UserAgent_iCab      .isEmpty()) m_UserAgent_iCab      = DEFAULT_USER_AGENT_ICAB      ;
        if(m_UserAgent_Camino    .isEmpty()) m_UserAgent_Camino    = DEFAULT_USER_AGENT_CAMINO    ;

        m_BrowserPath_IE       = s->value(QStringLiteral("BrowserPath_IE"),       QString()).value<QString>();
        m_BrowserPath_FF       = s->value(QStringLiteral("BrowserPath_Firefox"),  QString()).value<QString>();
        m_BrowserPath_Opera    = s->value(QStringLiteral("BrowserPath_Opera"),    QString()).value<QString>();
        m_BrowserPath_OPR      = s->value(QStringLiteral("BrowserPath_OPR"),      QString()).value<QString>();
        m_BrowserPath_Safari   = s->value(QStringLiteral("BrowserPath_Safari"),   QString()).value<QString>();
        m_BrowserPath_Chrome   = s->value(QStringLiteral("BrowserPath_Chrome"),   QString()).value<QString>();
        m_BrowserPath_Sleipnir = s->value(QStringLiteral("BrowserPath_Sleipnir"), QString()).value<QString>();
        m_BrowserPath_Vivaldi  = s->value(QStringLiteral("BrowserPath_Vivaldi"),  QString()).value<QString>();
        m_BrowserPath_Custom   = s->value(QStringLiteral("BrowserPath_Custom"),   QString()).value<QString>();
    }
    s->endGroup();
}

void Application::Reconfigure(){
    m_GlobalSettings->sync();
    LoadGlobalSettings();
    // 'TreeBank::LoadSettings' calls 'View::LoadSettings' and 'Gadgets::LoadSettings'.
    TreeBank::LoadSettings();
}

bool Application::EnableAutoSave(){
    return m_EnableAutoSave;
}

bool Application::EnableAutoLoad(){
    return m_EnableAutoLoad;
}

double Application::WheelScrollRate(){
    return m_WheelScrollRate;
}

bool Application::EnableGoogleSuggest(){
    return m_EnableGoogleSuggest;
}

bool Application::EnableFramelessWindow(){
    return m_EnableFramelessWindow;
}

static bool ReadXMLFile(QIODevice &device, QSettings::SettingsMap &map){
    QDomDocument doc;
    bool check = doc.setContent(&device);
    device.close();
    if(!check) return false;

    QDomNodeList allsettings = doc.elementsByTagName(QStringLiteral("setting"));

    QDomElement settingtag;
    for(uint i = 0; i < static_cast<uint>(allsettings.length()); i++){
        settingtag = allsettings.item(i).toElement();
        QDomNode node = settingtag.parentNode();
        QStringList path;
        path << settingtag.attribute(QStringLiteral("id"));

        while(node.nodeName() != QStringLiteral("body")){
            path.insert(0, node.nodeName());
            node = node.parentNode();
        }

        QVariant var(settingtag.text().replace(QStringLiteral("\\0\\0\\0"), QStringLiteral("\n")));
        if(var.toString().startsWith(QStringLiteral("@"))){
            QStringList args = var.toString().split(QStringLiteral("/"));
            if     (args[0] == QStringLiteral("@bool"))
                var = QVariant(args[1] == QStringLiteral("true") ? true : false);
            else if(args[0] == QStringLiteral("@url"))
                var = QVariant(QUrl(args[1]));
            else if(args[0] == QStringLiteral("@int"))
                var = QVariant(args[1].toInt());
            else if(args[0] == QStringLiteral("@uint"))
                var = QVariant(args[1].toUInt());
            else if(args[0] == QStringLiteral("@longlong"))
                var = QVariant(args[1].toLongLong());
            else if(args[0] == QStringLiteral("@ulonglong"))
                var = QVariant(args[1].toULongLong());
            else if(args[0] == QStringLiteral("@double"))
                var = QVariant(args[1].toDouble());
            else if(args[0] == QStringLiteral("@size"))
                var = QVariant(QSize(args[1].toInt(), args[2].toInt()));
            else if(args[0] == QStringLiteral("@sizef"))
                var = QVariant(QSizeF(args[1].toFloat(), args[2].toFloat()));
            else if(args[0] == QStringLiteral("@point"))
                var = QVariant(QPoint(args[1].toInt(), args[2].toInt()));
            else if(args[0] == QStringLiteral("@pointf"))
                var = QVariant(QPointF(args[1].toFloat(), args[2].toFloat()));
            else if(args[0] == QStringLiteral("@rect"))
                var = QVariant(QRect(args[1].toInt(), args[2].toInt(),
                                     args[3].toInt(), args[4].toInt()));
            else if(args[0] == QStringLiteral("@rectf"))
                var = QVariant(QRectF(args[1].toFloat(), args[2].toFloat(),
                                      args[3].toFloat(), args[4].toFloat()));
            else if(args[0] == QStringLiteral("@color"))
                var = QVariant(QColor(args[1].toInt(), args[2].toInt(),
                                      args[3].toInt(), args[4].toInt()));
            else if(args[0] == QStringLiteral("@stringlist")){
                args.removeFirst();
                QStringList list = args.join(QStringLiteral("/")).split(QStringLiteral(","));
                // if list is [""], use variant.
                // QStringLiteral("@stringlist/") is empty string list.
                if(list.length() == 1 && list[0] == QString())
                    list.removeFirst();
                var = QVariant(list);
            }
            else if(args[0] == QStringLiteral("@variant")){
                QByteArray ba(settingtag.text().toLatin1().mid(9));
                QDataStream stream(&ba, QIODevice::ReadOnly);
                stream >> var;
            }
        }
        map.insert(path.join(QStringLiteral("/")), var);
    }
    return true;
}

static bool WriteXMLFile(QIODevice &device, const QSettings::SettingsMap &map){
    QDomDocument doc;
    doc.appendChild(doc.createProcessingInstruction("xml", "version=\"1.0\" encoding=\"UTF-8\""));
    QDomElement root = doc.createElement(QStringLiteral("body"));
    doc.appendChild(root);

    foreach(QString key, map.keys()){
        QDomNode node = root;
        QVariant var = map.value(key);
        QStringList path = key.split(QStringLiteral("/"));
        QString last = path.takeLast();

        foreach(QString cat, path){
            if(node.firstChildElement(cat).isNull()){
                QDomElement child = doc.createElement(cat);
                node.appendChild(child);
            }
            node = node.firstChildElement(cat);
        }

        QDomElement settingtag = doc.createElement(QStringLiteral("setting"));
        QString text;
        settingtag.setAttribute(QStringLiteral("id"), last);
        switch (var.type()){
        case QVariant::String:
            text = var.toString().replace(QStringLiteral("\n"), QStringLiteral("\\0\\0\\0")); break;
        case QVariant::Url:
            text = QStringLiteral("@url/") + var.toUrl().toString(); break;
        case QVariant::Bool:
            text = QStringLiteral("@bool/") + var.toString(); break;
        case QVariant::Int:
            text = QStringLiteral("@int/") + var.toString(); break;
        case QVariant::UInt:
            text = QStringLiteral("@uint/") + var.toString(); break;
        case QVariant::LongLong:
            text = QStringLiteral("@longlong/") + var.toString(); break;
        case QVariant::ULongLong:
            text = QStringLiteral("@ulonglong/") + var.toString(); break;
        case QVariant::Double:
            text = QStringLiteral("@double/") + var.toString(); break;

        case QVariant::Size:
            text = QStringLiteral("@size/%1/%2").
                arg(var.value<QSize>().width()).
                arg(var.value<QSize>().height());
            break;
        case QVariant::SizeF:
            text = QStringLiteral("@sizef/%1/%2").
                arg(var.value<QSizeF>().width()).
                arg(var.value<QSizeF>().height());
            break;
        case QVariant::Point:
            text = QStringLiteral("@point/%1/%2").
                arg(var.value<QPoint>().x()).
                arg(var.value<QPoint>().y());
            break;
        case QVariant::PointF:
            text = QStringLiteral("@pointf/%1/%2").
                arg(var.value<QPointF>().x()).
                arg(var.value<QPointF>().y());
            break;
        case QVariant::Rect:
            text = QStringLiteral("@rect/%1/%2/%3/%4").
                arg(var.value<QRect>().x()).
                arg(var.value<QRect>().y()).
                arg(var.value<QRect>().width()).
                arg(var.value<QRect>().height());
            break;
        case QVariant::RectF:
            text = QStringLiteral("@rectf/%1/%2/%3/%4").
                arg(var.value<QRectF>().x()).
                arg(var.value<QRectF>().y()).
                arg(var.value<QRectF>().width()).
                arg(var.value<QRectF>().height());
            break;
        case QVariant::Color:
            text = QStringLiteral("@color/%1/%2/%3/%4").
                arg(var.value<QColor>().red()).
                arg(var.value<QColor>().green()).
                arg(var.value<QColor>().blue()).
                arg(var.value<QColor>().alpha());
            break;
        case QVariant::StringList:{
            QStringList list = var.value<QStringList>();
            // if list is [""], use variant.
            // "@stringlist/" is empty string list.
            if(!(list.length() == 1 && list[0].isEmpty()) &&
               list.indexOf(QRegExp(QStringLiteral("(?:\n|[^\n])*[\n\r\t,](?:\n|[^\n])*"))) == -1){
                text = QStringLiteral("@stringlist/") + list.join(QStringLiteral(","));
                break;
            }
        }
            // fall through.
        default:
            QByteArray ba;
            QDataStream stream(&ba, QIODevice::WriteOnly);
            stream << var;
            text = QString(QStringLiteral("@variant/%1")).
                arg(QString::fromLatin1(ba.data(), ba.size()));
            break;
        }
        settingtag.appendChild(doc.createTextNode(text).toText());
        node.appendChild(settingtag);
    }

    QTextStream out(&device);
    doc.save(out, 2);
    return true;
}

void Application::SetDownloadDirectory(QString path){
    m_DownloadDirectory = path;
}

QString Application::GetDownloadDirectory(){
    if(m_DownloadDirectory.isEmpty())
        m_DownloadDirectory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QStringLiteral("/");
    return m_DownloadDirectory;
}

void Application::SetUploadDirectory(QString path){
    m_UploadDirectory = path;
}

QString Application::GetUploadDirectory(){
    if(m_UploadDirectory.isEmpty())
        m_UploadDirectory = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + QStringLiteral("/");
    return m_UploadDirectory;
}

void Application::SetMaxBackUpFileCount(int count){
    m_MaxBackUpFileCount = count;
}

int Application::GetMaxBackupFileCount(){
    return m_MaxBackUpFileCount;
}

QStringList Application::BackUpFileFilters(){
    static QStringList filters =
        QStringList() << QStringLiteral(
            "[0-9][0-9][0-9][0-9]-" //yyyy-
          VV"[0-9][0-9]-" //MM-
          VV"[0-9][0-9]-" //dd-
          VV"[0-9][0-9]-" //hh-
          VV"[0-9][0-9]-" //mm-
          VV"[0-9][0-9]-" //ss-
          VV"*.xml") //(main_tree|trash_tree|cookie|config).xml
          ;
    return filters;
}

QString Application::BaseDirectory(){
    static bool checked = false;
    static QString dir = applicationDirPath() + QStringLiteral("/");
    if(checked) return dir;
#if defined(Q_OS_WIN)
    // %SystemRoot% or %ProgramFiles% (UAC protected directory.)
    if(dir.startsWith(QStringLiteral("C:/Windows/")) || // <= have never seen the person who put application here but...
       dir.startsWith(QStringLiteral("C:/Program Files/")) ||
       dir.startsWith(QStringLiteral("C:/Program Files (x86)/")))
        dir = QStandardPaths::writableLocation(QStandardPaths::DataLocation)+QStringLiteral("/");
#endif
    checked = true;
    return dir;
}

QString Application::DataDirectory(){
    static QString dir = BaseDirectory() + QStringLiteral("data/");
    return dir;
}

QString Application::ThumbnailDirectory(){
    static QString dir = DataDirectory() + QStringLiteral("image/");
    return dir;
}

QString Application::HistoryDirectory(){
    static QString dir = DataDirectory() + QStringLiteral("history/");
    return dir;
}

QString Application::TemporaryDirectory(){
    static QString dir = BaseDirectory() + QStringLiteral("temp/");
    return dir;
}

QString Application::BackUpPreposition(){
    return QStringLiteral("~");
}

void Application::ClearTemporaryDirectory(){
    QDir tmpdir = TemporaryDirectory();
    QStringList files = tmpdir.entryList(QDir::NoDotAndDotDot|QDir::AllEntries);
    if(files.isEmpty()){
        tmpdir.mkpath(TemporaryDirectory());
    } else {
        foreach(QString file, files){
            QFile::remove(TemporaryDirectory() + QStringLiteral("/") + file);
        }
    }
}

QString Application::PrimaryTreeFileName(bool tmp){
    return (tmp ? BackUpPreposition() : QString()) + QStringLiteral("main_tree.xml");
}

QString Application::SecondaryTreeFileName(bool tmp){
    return (tmp ? BackUpPreposition() : QString()) + QStringLiteral("trash_tree.xml");
}

QString Application::CookieFileName(bool tmp){
    return (tmp ? BackUpPreposition() : QString()) + QStringLiteral("cookie.xml");
}

QString Application::GlobalSettingsFileName(){
    return QStringLiteral("config.xml");
}

void Application::AppendChosenFile(QString file){
    m_ChosenFiles << file;
}

void Application::RemoveChosenFile(QString file){
    m_ChosenFiles.removeOne(file);
}

QStringList Application::ChosenFiles(){
    return m_ChosenFiles;
}

bool Application::SaveSessionCookie(){
    return m_SaveSessionCookie;
}

QString Application::GetAcceptLanguage(){
    return m_AcceptLanguage;
}

void Application::SetAcceptLanguage(QString acceptLanguage){
    m_AcceptLanguage = acceptLanguage;
}

QStringList Application::GetAllowedHosts(){
    return m_AllowedHosts;
}

void Application::AppendToAllowedHosts(QString host){
    if(!m_AllowedHosts.contains(host))
        m_AllowedHosts << host;
}

void Application::RemoveFromAllowedHosts(QString host){
    if(m_AllowedHosts.contains(host))
        m_AllowedHosts.removeOne(host);
}

QStringList Application::GetBlockedHosts(){
    return m_BlockedHosts;
}

void Application::AppendToBlockedHosts(QString host){
    if(!m_BlockedHosts.contains(host))
        m_BlockedHosts << host;
}

void Application::RemoveFromBlockedHosts(QString host){
    if(m_BlockedHosts.contains(host))
        m_BlockedHosts.removeOne(host);
}

Application::SslErrorPolicy Application::GetSslErrorPolicy(){
    return m_SslErrorPolicy;
}

void Application::AskSslErrorPolicyIfNeed(){
    if(m_SslErrorPolicy == Undefined){
        QStringList policies;
        policies << tr("BlockAccess")
                 << tr("IgnoreSslErrors")
                 << tr("AskForEachAccess")
                 << tr("AskForEachHost");
        bool ok;

        QString policy = ModalDialog::GetItem
            (tr("Ssl error policy"),
             tr("Select ssl error policy."),
             policies, false, &ok);

        if(!ok) return;

        if     (policy == tr("BlockAccess"))
            m_SslErrorPolicy = BlockAccess;
        else if(policy == tr("IgnoreSslErrors"))
            m_SslErrorPolicy = IgnoreSslErrors;
        else if(policy == tr("AskForEachAccess"))
            m_SslErrorPolicy = AskForEachAccess;
        else if(policy == tr("AskForEachHost"))
            m_SslErrorPolicy = AskForEachHost;
    }
}

Application::DownloadPolicy Application::GetDownloadPolicy(){
    return m_DownloadPolicy;
}

void Application::AskDownloadPolicyIfNeed(){
    if(m_DownloadPolicy == Undefined_){
        QStringList policies;
        policies << tr("FixedLocale")
                 << tr("DownloadFolder")
                 << tr("AskForEachDownload");
        bool ok;

        QString policy = ModalDialog::GetItem
            (tr("Download policy"),
             tr("Select download policy."),
             policies, false, &ok);

        if(!ok) return;

        if(policy == tr("FixedLocale")){
            QString directory =
                ModalDialog::GetExistingDirectory(QString(), GetDownloadDirectory());
            if(!directory.isEmpty()){
                SetDownloadDirectory(directory + QStringLiteral("/"));
                m_DownloadPolicy = FixedLocale;
            }
        } else if(policy == tr("DownloadFolder")){
            SetDownloadDirectory(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QStringLiteral("/"));
            m_DownloadPolicy = DownloadFolder;
        } else if(policy == tr("AskForEachDownload")){
            m_DownloadPolicy = AskForEachDownload;
        }
    }
}

QString Application::LocalServerName(){
    return VANILLA_LOCAL_SERVER_NAME_PREFIX +
        QString::fromLatin1(QCryptographicHash::hash(applicationDirPath().toUtf8(), QCryptographicHash::Md5).toHex());
}

QString Application::SharedMemoryKey(){
    return VANILLA_SHARED_MEMORY_KEY_PREFIX +
        QString::fromLatin1(QCryptographicHash::hash(applicationDirPath().toUtf8(), QCryptographicHash::Md5).toHex());
}

int Application::EventKey(){
    static int key = 0;
    while(!key) key = rand() + 1;
    return key;
}

MainWindow *Application::ShadeWindow(MainWindow *win){
    if(!win) win = GetCurrentWindow();
    if(win) win->Shade();
    return win;
}

MainWindow *Application::UnshadeWindow(MainWindow *win){
    if(!win) win = GetCurrentWindow();
    if(win) win->Unshade();
    return win;
}

MainWindow *Application::NewWindow(int id){
    if(id == 0)
        forever
            if(!m_MainWindows.value(id = rand() + 1, 0))
                break;
    TreeBank::LiftMaxViewCountIfNeed(m_MainWindows.count());
    return m_MainWindows[id] = m_CurrentWindow = new MainWindow(id);
}

MainWindow *Application::CloseWindow(MainWindow *win){
    if(!win) win = GetCurrentWindow();
    if(win) win->close();
    return GetCurrentWindow();
}

MainWindow *Application::SwitchWindow(bool next){
    WinMap::iterator i = m_MainWindows.find(m_CurrentWindow->GetIndex());
    MainWindow *win = 0;
    if(next){

        ++i;
        if(i == m_MainWindows.end()) i = m_MainWindows.begin();

    } else {

        if(i == m_MainWindows.begin()) i = m_MainWindows.end();
        --i;
    }

    win = i.value();

    m_CurrentWindow = win;

    if(m_CurrentWindow) m_CurrentWindow->SetFocus();

    return m_CurrentWindow;
}

MainWindow *Application::NextWindow(){
    return SwitchWindow(true);
}

MainWindow *Application::PrevWindow(){
    return SwitchWindow(false);
}

void Application::RemoveWindow(MainWindow *win){
    m_MainWindows.remove(win->GetIndex());

    if(m_CurrentWindow == win){
        if(m_MainWindows.count() > 0){
            m_CurrentWindow = m_MainWindows.begin().value();
        } else {
            m_CurrentWindow = 0;
        }
    }
    if(m_CurrentWindow) m_CurrentWindow->SetFocus();
}

void Application::RemoveWindow(int id){
    m_MainWindows.remove(id);

    MainWindow *win = m_MainWindows[id];

    if(m_CurrentWindow == win){
        if(m_MainWindows.count() > 0){
            m_CurrentWindow = m_MainWindows.begin().value();
        } else {
            m_CurrentWindow = 0;
        }
    }
    if(m_CurrentWindow) m_CurrentWindow->SetFocus();
}

void Application::SetCurrentWindow(MainWindow *win){
    m_CurrentWindow = win;
}

void Application::SetCurrentWindow(int id){
    m_CurrentWindow = m_MainWindows[id];
}

int Application::WindowID(MainWindow *win){
    return m_MainWindows.key(win);
}

MainWindow *Application::Window(int id){
    return m_MainWindows[id];
}

int Application::GetCurrentWindowID(){
    return m_MainWindows.key(m_CurrentWindow);
}

MainWindow *Application::GetCurrentWindow(){
    return m_CurrentWindow;
}

QWidget *Application::CurrentWidget(){
    QWidget *widget = 0;
    if(m_CurrentWindow){
        TreeBank *tb = m_CurrentWindow->GetTreeBank();
        if(tb->IsDisplayingTableView()){
            widget = tb;
        } else if(SharedView view = tb->GetCurrentView()){
            if(QWidget *w = qobject_cast<QWidget*>(view->base())){
                widget = w;
            } else {
                widget = tb;
            }
        } else {
            widget = m_CurrentWindow;
        }
    }
    return widget;
}

WinMap Application::GetMainWindows(){
    return m_MainWindows;
}

ModelessDialogFrame *Application::MakeTemporaryDialogFrame(){
    m_TemporaryDialogFrame = new ModelessDialogFrame();
    QDesktopWidget desktop;
    if(desktop.screenCount()){
        QRect rect = desktop.screenGeometry(0);
        QRect def = DEFAULT_WINDOW_RECT;

        m_TemporaryDialogFrame->setGeometry
            (rect.x() + (rect.width()  - def.width())  / 2,
             rect.y() + (rect.height() - def.height()) / 2,
             def.width(), def.height());
    }
    m_TemporaryDialogFrame->show();
    return m_TemporaryDialogFrame;
}

ModelessDialogFrame *Application::GetTemporaryDialogFrame(){
    return m_TemporaryDialogFrame;
}

void Application::SetTemporaryDialogFrame(ModelessDialogFrame *frame){
    m_TemporaryDialogFrame = frame;
}

QString Application::UserAgent_IE(){
    return m_UserAgent_IE;
}

QString Application::UserAgent_FF(){
    return m_UserAgent_FF;
}

QString Application::UserAgent_Opera(){
    return m_UserAgent_Opera;
}

QString Application::UserAgent_OPR(){
    return m_UserAgent_OPR;
}

QString Application::UserAgent_Safari(){
    return m_UserAgent_Safari;
}

QString Application::UserAgent_Chrome(){
    return m_UserAgent_Chrome;
}

QString Application::UserAgent_Sleipnir(){
    return m_UserAgent_Sleipnir;
}

QString Application::UserAgent_Vivaldi(){
    return m_UserAgent_Vivaldi;
}

QString Application::UserAgent_NetScape(){
    return m_UserAgent_NetScape;
}

QString Application::UserAgent_SeaMonkey(){
    return m_UserAgent_SeaMonkey;
}

QString Application::UserAgent_iCab(){
    return m_UserAgent_iCab;
}

QString Application::UserAgent_Camino(){
    return m_UserAgent_Camino;
}

QString Application::UserAgent_Gecko(){
    return m_UserAgent_Gecko;
}

QString Application::UserAgent_Custom(){
    return m_UserAgent_Custom;
}

QString Application::BrowserPath_IE(){
    static QString path;
    if(!path.isEmpty()) return path == DISABLE_FILENAME ? QString() : path;
    if(!m_BrowserPath_IE.isEmpty()){
        path = m_BrowserPath_IE;
        if(QFile::exists(path)) return path;
    }
#if defined(Q_OS_WIN)
    path = QStringLiteral("C:/Program Files/Internet Explorer/iexplore.exe");
    if(QFile::exists(path)) return path;
    path = QStringLiteral("C:/Program Files (x86)/Internet Explorer/iexplore.exe");
    if(QFile::exists(path)) return path;
#endif
    path = DISABLE_FILENAME;
    return QString();
}

QString Application::BrowserPath_FF(){
    static QString path;
    if(!path.isEmpty()) return path == DISABLE_FILENAME ? QString() : path;
    if(!m_BrowserPath_FF.isEmpty()){
        path = m_BrowserPath_FF;
        if(QFile::exists(path)) return path;
    }
#if defined(Q_OS_WIN)
    path = QStringLiteral("C:/Program Files/Mozilla Firefox/firefox.exe");
    if(QFile::exists(path)) return path;
    path = QStringLiteral("C:/Program Files (x86)/Mozilla Firefox/firefox.exe");
    if(QFile::exists(path)) return path;
#elif defined(Q_OS_MAC)
    path = QStringLiteral("/Applications/Firefox.app/Contents/MacOS/firefox");
    if(QFile::exists(path)) return path;
#else
    path = QStringLiteral("/usr/bin/firefox");
    if(QFile::exists(path)) return path;
#endif
    path = DISABLE_FILENAME;
    return QString();
}

QString Application::BrowserPath_Opera(){
    static QString path;
    if(!path.isEmpty()) return path == DISABLE_FILENAME ? QString() : path;
    if(!m_BrowserPath_Opera.isEmpty()){
        path = m_BrowserPath_Opera;
        if(QFile::exists(path)) return path;
    }
#if defined(Q_OS_WIN)
    path = QStringLiteral("C:/Program Files/Opera/opera.exe");
    if(QFile::exists(path)) return path;
    path = QStringLiteral("C:/Program Files/Opera x64/opera.exe");
    if(QFile::exists(path)) return path;
    path = QStringLiteral("C:/Program Files (x86)/Opera/opera.exe");
    if(QFile::exists(path)) return path;
#elif defined(Q_OS_MAC)
    path = QStringLiteral("/Applications/Opera.app/Contents/MacOS/Opera");
    if(QFile::exists(path)) return path;
#else
    path = QStringLiteral("/usr/bin/opera");
    if(QFile::exists(path)) return path;
#endif
    path = DISABLE_FILENAME;
    return QString();
}

QString Application::BrowserPath_OPR(){
    static QString path;
    if(!path.isEmpty()) return path == DISABLE_FILENAME ? QString() : path;
    if(!m_BrowserPath_OPR.isEmpty()){
        path = m_BrowserPath_OPR;
        if(QFile::exists(path)) return path;
    }
#if defined(Q_OS_WIN)
    path = QStringLiteral("C:/Program Files/Opera/launcher.exe");
    if(QFile::exists(path)) return path;
    path = QStringLiteral("C:/Program Files/Opera x64/launcher.exe");
    if(QFile::exists(path)) return path;
    path = QStringLiteral("C:/Program Files (x86)/Opera/launcher.exe");
    if(QFile::exists(path)) return path;
#elif defined(Q_OS_MAC)
    path = QStringLiteral("/Applications/Opera.app/Contents/MacOS/Opera");
    if(QFile::exists(path)) return path;
#else
    path = QStringLiteral("/usr/bin/opera");
    if(QFile::exists(path)) return path;
#endif
    path = DISABLE_FILENAME;
    return QString();
}

QString Application::BrowserPath_Safari(){
    static QString path;
    if(!path.isEmpty()) return path == DISABLE_FILENAME ? QString() : path;
    if(!m_BrowserPath_OPR.isEmpty()){
        path = m_BrowserPath_OPR;
        if(QFile::exists(path)) return path;
    }
#if defined(Q_OS_MAC)
    path = QStringLiteral("/Applications/Safari.app/Contents/MacOS/Safari");
    if(QFile::exists(path)) return path;
#else
    path = QStringLiteral("/usr/bin/safari");
    if(QFile::exists(path)) return path;
#endif
    path = DISABLE_FILENAME;
    return QString();
}

QString Application::BrowserPath_Chrome(){
    static QString path;
    if(!path.isEmpty()) return path == DISABLE_FILENAME ? QString() : path;
    if(!m_BrowserPath_Chrome.isEmpty()){
        path = m_BrowserPath_Chrome;
        if(QFile::exists(path)) return path;
    }
#if defined(Q_OS_WIN)
    path = QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe");
    if(QFile::exists(path)) return path;
    path = QStringLiteral("C:/Program Files (x86)/Google/Chrome/Application/chrome.exe");
    if(QFile::exists(path)) return path;
#elif defined(Q_OS_MAC)
    path = QStringLiteral("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome");
    if(QFile::exists(path)) return path;
#else
    path = QStringLiteral("/usr/bin/google-chrome");
    if(QFile::exists(path)) return path;
#endif
    path = DISABLE_FILENAME;
    return QString();
}

QString Application::BrowserPath_Sleipnir(){
    static QString path;
    if(!path.isEmpty()) return path == DISABLE_FILENAME ? QString() : path;
    if(!m_BrowserPath_Sleipnir.isEmpty()){
        path = m_BrowserPath_Sleipnir;
        if(QFile::exists(path)) return path;
    }
#if defined(Q_OS_WIN)
    path = QStringLiteral("C:/Program Files/Fenrir Inc/Sleipnir/bin/Sleipnir.exe");
    if(QFile::exists(path)) return path;
    path = QStringLiteral("C:/Program Files (x86)/Fenrir Inc/Sleipnir/bin/Sleipnir.exe");
    if(QFile::exists(path)) return path;
    path = QStringLiteral("C:/Program Files/Fenrir Inc/Sleipnir5/bin/Sleipnir.exe");
    if(QFile::exists(path)) return path;
    path = QStringLiteral("C:/Program Files (x86)/Fenrir Inc/Sleipnir5/bin/Sleipnir.exe");
    if(QFile::exists(path)) return path;
#elif defined(Q_OS_MAC)
    path = QStringLiteral("/Applications/Sleipnir.app/Contents/MacOS/Sleipnir");
    if(QFile::exists(path)) return path;
#else
    path = QStringLiteral("/usr/bin/sleipnir");
    if(QFile::exists(path)) return path;
#endif
    path = DISABLE_FILENAME;
    return QString();
}

QString Application::BrowserPath_Vivaldi(){
    static QString path;
    if(!path.isEmpty()) return path == DISABLE_FILENAME ? QString() : path;
    if(!m_BrowserPath_Vivaldi.isEmpty()){
        path = m_BrowserPath_Vivaldi;
        if(QFile::exists(path)) return path;
    }
#if defined(Q_OS_WIN)
    path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
        QStringLiteral("/Vivaldi/Application/vivaldi.exe");
    if(QFile::exists(path)) return path;
#elif defined(Q_OS_MAC)
    path = QStringLiteral("/Applications/Vivaldi.app/Contents/MacOS/Vivaldi");
    if(QFile::exists(path)) return path;
#else
    path = QStringLiteral("/usr/bin/vivaldi");
    if(QFile::exists(path)) return path;
#endif
    path = DISABLE_FILENAME;
    return QString();
}

QString Application::BrowserPath_Custom(){
    static QString path;
    if(!path.isEmpty()) return path == DISABLE_FILENAME ? QString() : path;
    if(!m_BrowserPath_Custom.isEmpty()){
        path = m_BrowserPath_Custom;
        if(QFile::exists(path)) return path;
    }
    path = DISABLE_FILENAME;
    return QString();
}

QIcon Application::BrowserIcon_IE(){
    if(BrowserPath_IE().isEmpty()) return QIcon();
    static QIcon icon;
    if(icon.isNull()){
        icon = QFileIconProvider().icon(QFileInfo(BrowserPath_IE()));
        icon = QIcon(icon.pixmap(icon.availableSizes().first()));
    }
    return icon;
}

QIcon Application::BrowserIcon_FF(){
    if(BrowserPath_FF().isEmpty()) return QIcon();
    static QIcon icon;
    if(icon.isNull()){
        icon = QFileIconProvider().icon(QFileInfo(BrowserPath_FF()));
        icon = QIcon(icon.pixmap(icon.availableSizes().first()));
    }
    return icon;
}

QIcon Application::BrowserIcon_Opera(){
    if(BrowserPath_Opera().isEmpty()) return QIcon();
    static QIcon icon;
    if(icon.isNull()){
        icon = QFileIconProvider().icon(QFileInfo(BrowserPath_Opera()));
        icon = QIcon(icon.pixmap(icon.availableSizes().first()));
    }
    return icon;
}

QIcon Application::BrowserIcon_OPR(){
    if(BrowserPath_OPR().isEmpty()) return QIcon();
    static QIcon icon;
    if(icon.isNull()){
        icon = QFileIconProvider().icon(QFileInfo(BrowserPath_OPR()));
        icon = QIcon(icon.pixmap(icon.availableSizes().first()));
    }
    return icon;
}

QIcon Application::BrowserIcon_Safari(){
    if(BrowserPath_Safari().isEmpty()) return QIcon();
    static QIcon icon;
    if(icon.isNull()){
        icon = QFileIconProvider().icon(QFileInfo(BrowserPath_Safari()));
        icon = QIcon(icon.pixmap(icon.availableSizes().first()));
    }
    return icon;
}

QIcon Application::BrowserIcon_Chrome(){
    if(BrowserPath_Chrome().isEmpty()) return QIcon();
    static QIcon icon;
    if(icon.isNull()){
        icon = QFileIconProvider().icon(QFileInfo(BrowserPath_Chrome()));
        icon = QIcon(icon.pixmap(icon.availableSizes().first()));
    }
    return icon;
}

QIcon Application::BrowserIcon_Sleipnir(){
    if(BrowserPath_Sleipnir().isEmpty()) return QIcon();
    static QIcon icon;
    if(icon.isNull()){
        icon = QFileIconProvider().icon(QFileInfo(BrowserPath_Sleipnir()));
        icon = QIcon(icon.pixmap(icon.availableSizes().first()));
    }
    return icon;
}

QIcon Application::BrowserIcon_Vivaldi(){
    if(BrowserPath_Vivaldi().isEmpty()) return QIcon();
    static QIcon icon;
    if(icon.isNull()){
        icon = QFileIconProvider().icon(QFileInfo(BrowserPath_Vivaldi()));
        icon = QIcon(icon.pixmap(icon.availableSizes().first()));
    }
    return icon;
}

QIcon Application::BrowserIcon_Custom(){
    if(BrowserPath_Custom().isEmpty()) return QIcon();
    static QIcon icon;
    if(icon.isNull()){
        icon = QFileIconProvider().icon(QFileInfo(BrowserPath_Custom()));
        icon = QIcon(icon.pixmap(icon.availableSizes().first()));
    }
    return icon;
}

bool Application::OpenUrlWith_IE(QUrl url){
    if(url.isEmpty() || BrowserPath_IE().isEmpty()) return false;
    return QProcess::startDetached(BrowserPath_IE(), QStringList() << QString::fromUtf8(url.toEncoded()));
}

bool Application::OpenUrlWith_FF(QUrl url){
    if(url.isEmpty() || BrowserPath_FF().isEmpty()) return false;
    return QProcess::startDetached(BrowserPath_FF(), QStringList() << QString::fromUtf8(url.toEncoded()));
}

bool Application::OpenUrlWith_Opera(QUrl url){
    if(url.isEmpty() || BrowserPath_Opera().isEmpty()) return false;
    return QProcess::startDetached(BrowserPath_Opera(), QStringList() << QString::fromUtf8(url.toEncoded()));
}

bool Application::OpenUrlWith_OPR(QUrl url){
    if(url.isEmpty() || BrowserPath_OPR().isEmpty()) return false;
    return QProcess::startDetached(BrowserPath_OPR(), QStringList() << QString::fromUtf8(url.toEncoded()));
}

bool Application::OpenUrlWith_Safari(QUrl url){
    if(url.isEmpty() || BrowserPath_Safari().isEmpty()) return false;
    return QProcess::startDetached(BrowserPath_Safari(), QStringList() << QString::fromUtf8(url.toEncoded()));
}

bool Application::OpenUrlWith_Chrome(QUrl url){
    if(url.isEmpty() || BrowserPath_Chrome().isEmpty()) return false;
    return QProcess::startDetached(BrowserPath_Chrome(), QStringList() << QString::fromUtf8(url.toEncoded()));
}

bool Application::OpenUrlWith_Sleipnir(QUrl url){
    if(url.isEmpty() || BrowserPath_Sleipnir().isEmpty()) return false;
    return QProcess::startDetached(BrowserPath_Sleipnir(), QStringList() << QString::fromUtf8(url.toEncoded()));
}

bool Application::OpenUrlWith_Vivaldi(QUrl url){
    if(url.isEmpty() || BrowserPath_Vivaldi().isEmpty()) return false;
    return QProcess::startDetached(BrowserPath_Vivaldi(), QStringList() << QString::fromUtf8(url.toEncoded()));
}

bool Application::OpenUrlWith_Custom(QUrl url){
    if(url.isEmpty() || BrowserPath_Custom().isEmpty()) return false;
    return QProcess::startDetached(BrowserPath_Custom(), QStringList() << QString::fromUtf8(url.toEncoded()));
}

void Application::timerEvent(QTimerEvent *ev){
    QApplication::timerEvent(ev);
    if(ev->timerId() == m_AutoSaveTimerID)
        SaveAllDataAsync();
    if(ev->timerId() == m_AutoLoadTimerID)
        TreeBank::AutoLoad();
}

void Application::CreateBackUpFiles(){

    QString date = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-hh-mm-ss-"));

    QStringList backupfiles =
        QStringList() << GlobalSettingsFileName()
                      << PrimaryTreeFileName()
                      << SecondaryTreeFileName()
                      << CookieFileName();

    foreach(QString file, backupfiles){
        QString original = DataDirectory() + file;
        QString backup   = DataDirectory() + date + file;

        if(QFile::exists(backup))
            QFile::remove(backup);
        QFile::copy(original, backup);
    }

    QDir dir = QDir(DataDirectory());

    QStringList list = dir.entryList(BackUpFileFilters(), QDir::NoFilter, QDir::Name);

    while(list.length() > m_MaxBackUpFileCount){
        QFile::remove(Application::DataDirectory() + list.takeFirst());
    }
}

void Application::SaveAllDataAsync(){
    emit SaveAllDataRequest();
}

QThread* Application::GetSaverThread(){
    return m_SaverThread;
}

void Application::StartAutoSaveTimer(){
    // do nothing if this timer is started already.
    if(m_EnableAutoSave && !m_AutoSaveTimerID)
        m_AutoSaveTimerID = m_Instance->startTimer(m_AutoSaveInterval);
}

void Application::StartAutoLoadTimer(){
    // do nothing if this timer is started already.
    if(m_EnableAutoLoad && !m_AutoLoadTimerID)
        m_AutoLoadTimerID = m_Instance->startTimer(m_AutoLoadInterval);
}

void Application::StopAutoSaveTimer(){
    if(m_AutoSaveTimerID){
        m_Instance->killTimer(m_AutoSaveTimerID);
        m_AutoSaveTimerID = 0;
    }
}

void Application::StopAutoLoadTimer(){
    if(m_AutoLoadTimerID){
        m_Instance->killTimer(m_AutoLoadTimerID);
        m_AutoLoadTimerID = 0;
    }
}

void Application::RestartAutoSaveTimer(){
    StopAutoSaveTimer();
    StartAutoSaveTimer();
}

void Application::RestartAutoLoadTimer(){
    StopAutoLoadTimer();
    StartAutoLoadTimer();
}