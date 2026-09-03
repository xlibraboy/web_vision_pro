#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <pylon/PylonIncludes.h>
#include "gui/MainWindow.h"

#include <execinfo.h>
#include <csignal>
#include <unistd.h>
#include <iostream>

namespace {
class WheelInputGuard : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Wheel) {
            if (QAbstractSpinBox* spinBox = qobject_cast<QAbstractSpinBox*>(obj)) {
                event->ignore();
                return true;
            }

            if (QComboBox* comboBox = qobject_cast<QComboBox*>(obj)) {
                event->ignore();
                return true;
            }
        }

        return QObject::eventFilter(obj, event);
    }
};

// Fatal-signal backtrace: a SIGSEGV in the field (e.g. inside fragile GenApi
// register access on scA780-class cameras) must leave a stack in the logs,
// otherwise the crash is undiagnosable after the fact.
void fatalSignalHandler(int sig) {
    void* frames[32];
    const int n = backtrace(frames, 32);
    static const char msg[] = "\n*** FATAL SIGNAL received ***\n";
    (void)!write(STDERR_FILENO, msg, sizeof(msg) - 1);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    // Restore default action so a core still drops if enabled.
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
}

int main(int argc, char *argv[]) {
    std::signal(SIGSEGV, fatalSignalHandler);
    std::signal(SIGBUS, fatalSignalHandler);
    std::signal(SIGFPE, fatalSignalHandler);
    try {
        Pylon::PylonAutoInitTerm autoInitTerm;
        QApplication app(argc, argv);

        // Persist QSettings inside the mounted data volume: the container's
        // home directory is wiped whenever the container is recreated, which
        // silently reset System Configuration to defaults. /app/data survives
        // recreation (same volume the event files live on).
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, "/app/data/config");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        {
            // One-time migration from the old container-home location.
            QDir oldSettingsDir(QDir::homePath() + "/.config/PaperVision");
            QDir newSettingsDir("/app/data/config/PaperVision");
            if (oldSettingsDir.exists() && !newSettingsDir.exists()) {
                newSettingsDir.mkpath(".");
                const QFileInfoList legacy = oldSettingsDir.entryInfoList(
                    QStringList() << "*.conf", QDir::Files);
                for (const QFileInfo& fi : legacy) {
                    QFile::copy(fi.absoluteFilePath(),
                                newSettingsDir.filePath(fi.completeBaseName() + ".ini"));
                }
            }
        }

        WheelInputGuard wheelInputGuard;
        app.installEventFilter(&wheelInputGuard);
        const QString serverName = "papervision_instance_server";

        // Prevent duplicate windows if the desktop launcher is clicked twice.
        const QString lockFilePath = QDir::temp().absoluteFilePath("papervision_app.lock");
        QLockFile instanceLock(lockFilePath);
        instanceLock.setStaleLockTime(0);
        if (!instanceLock.tryLock(100)) {
            QLocalSocket socket;
            socket.connectToServer(serverName);
            const bool liveInstance = socket.waitForConnected(300);
            if (liveInstance) {
                socket.write("raise");
                socket.flush();
                socket.waitForBytesWritten(300);
                socket.disconnectFromServer();
                std::cerr << "PaperVision_App is already running." << std::endl;
                return 0;
            }
            // No live instance actually owns the lock (the recorded PID is
            // stale - e.g. the previous process was replaced at the same PID,
            // typical when the app runs as PID 1 inside a container). Remove
            // the orphaned lock file and retry once.
            QFile::remove(lockFilePath);
            if (!instanceLock.tryLock(100)) {
                std::cerr << "PaperVision_App is already running." << std::endl;
                return 0;
            }
        }

        QLocalServer::removeServer(serverName);
        QLocalServer activationServer;
        if (!activationServer.listen(serverName)) {
            std::cerr << "Failed to listen for PaperVision activation requests." << std::endl;
        }
    
        // Set style
        app.setStyle("Fusion");

        MainWindow window;
        QObject::connect(&activationServer, &QLocalServer::newConnection, [&]() {
            while (QLocalSocket* client = activationServer.nextPendingConnection()) {
                QObject::connect(client, &QLocalSocket::readyRead, [client, &window]() {
                    client->readAll();
                    window.raiseAndActivate();
                });
                QObject::connect(client, &QLocalSocket::disconnected, client, &QLocalSocket::deleteLater);
            }
        });

        window.setWindowState(window.windowState() | Qt::WindowMaximized);
        window.showMaximized();

        return app.exec();
    } catch (const Pylon::GenericException& e) {
        std::cerr << "Pylon Exception in main: " << e.GetDescription() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Standard Exception in main: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown Exception in main." << std::endl;
        return 1;
    }
}
