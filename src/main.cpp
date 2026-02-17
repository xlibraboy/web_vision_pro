#include <QApplication>
#include <pylon/PylonIncludes.h>
#include "gui/MainWindow.h"

#include <iostream>

int main(int argc, char *argv[]) {
    try {
        Pylon::PylonAutoInitTerm autoInitTerm;
        QApplication app(argc, argv);
    
    // Set style
    app.setStyle("Fusion");

    MainWindow window;
    window.show();

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
