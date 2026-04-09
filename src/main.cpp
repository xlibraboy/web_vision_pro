#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <pylon/PylonIncludes.h>
#include "gui/MainWindow.h"

#include <iostream>

int main(int argc, char *argv[]) {
    try {
        Pylon::PylonAutoInitTerm autoInitTerm;
        QApplication app(argc, argv);
        const QString serverName = "papervision_instance_server";

        // Prevent duplicate windows if the desktop launcher is clicked twice.
        QLockFile instanceLock(QDir::temp().absoluteFilePath("papervision_app.lock"));
        instanceLock.setStaleLockTime(0);
        if (!instanceLock.tryLock(100)) {
            QLocalSocket socket;
            socket.connectToServer(serverName);
            if (socket.waitForConnected(300)) {
                socket.write("raise");
                socket.flush();
                socket.waitForBytesWritten(300);
                socket.disconnectFromServer();
            }
            std::cerr << "PaperVision_App is already running." << std::endl;
            return 0;
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
