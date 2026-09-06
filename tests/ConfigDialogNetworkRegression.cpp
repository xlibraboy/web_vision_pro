#include "ConfigDialogNetworkRegression.h"

#include "gui/ConfigDialog.h"

#include <QApplication>
#include <QDebug>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QLineEdit* findEndpointEdit(QWidget& root) {
    const QString placeholder = QStringLiteral("opc.tcp://127.0.0.1:4840");
    const auto edits = root.findChildren<QLineEdit*>();
    for (QLineEdit* edit : edits) {
        if (edit->placeholderText() == placeholder) {
            return edit;
        }
    }
    return nullptr;
}

QPushButton* findDetectButton(QWidget& root) {
    const auto buttons = root.findChildren<QPushButton*>();
    for (QPushButton* button : buttons) {
        if (button->text() == QStringLiteral("Detect Server")) {
            return button;
        }
    }
    return nullptr;
}

// The single discovery status label that refreshOpcUaEndpointDiscovery() writes
// to. Located by matching any of the states that discovery can produce.
QLabel* findDiscoveryStatusLabel(QWidget& root) {
    const QString scanning = QStringLiteral("Scanning for OPC UA servers…");
    const QString noServerPrefix = QStringLiteral("No OPC UA server");
    const QString detectedPrefix = QStringLiteral("Detected OPC UA server");
    const QString enterPrefix = QStringLiteral("Enter the OPC UA endpoint URL");
    const QString savedPrefix = QStringLiteral("Saved endpoint URL loaded");
    const auto labels = root.findChildren<QLabel*>();
    for (QLabel* label : labels) {
        const QString text = label->text();
        if (text == scanning
            || text.contains(QStringLiteral("backend"))
            || text.startsWith(noServerPrefix)
            || text.startsWith(detectedPrefix)
            || text.startsWith(enterPrefix)
            || text.startsWith(savedPrefix)) {
            return label;
        }
    }
    return nullptr;
}

} // namespace

void ConfigDialogNetworkRegression::entryAndDetectPathsDoNotBlockOnOpcUaProbe()
{
    // Isolate settings (cameras + OPC UA endpoint) from any real machine config
    // so the test always runs against clean defaults with the default endpoint.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);

    qInfo() << "[RegressionTest] Building ConfigDialog (pylon enumeration can take a moment)...";
    ConfigDialog dialog(nullptr);

    QLineEdit* endpointEdit = findEndpointEdit(dialog);
    QVERIFY2(endpointEdit, "OPC UA endpoint field not found in ConfigDialog");
    QPushButton* detectButton = findDetectButton(dialog);
    QVERIFY2(detectButton, "OPC UA 'Detect Server' button not found in ConfigDialog");

    // ── Scenario 1: showEvent auto-discovery (the System Configuration entry) ──
    {
        // A listener that accepts but never answers: an OPC UA probe to it can
        // only end through the per-candidate timeout (750ms). If entry blocked
        // on the probe again, show() below would not return until that timeout
        // elapsed and the dialog would already show a final result.
        QTcpServer silentServer;
        QVERIFY(silentServer.listen(QHostAddress::LocalHost, 0));
        endpointEdit->setText(QStringLiteral("opc.tcp://127.0.0.1:%1").arg(silentServer.serverPort()));

        dialog.show(); // showEvent fires refreshOpcUaEndpointDiscovery(true, false)

        QLabel* status = findDiscoveryStatusLabel(dialog);
        const QString statusText = status ? status->text() : QString();
        if (statusText.contains(QStringLiteral("backend"), Qt::CaseInsensitive)) {
            QSKIP("Qt OPC UA 'open62541' backend is unavailable in this environment; "
                  "the async probe contract cannot be exercised");
        }

        QVERIFY2(!detectButton->isEnabled(),
                 "OPC UA discovery resolved synchronously during show(): the System "
                 "Configuration entry path blocks on network I/O again");
        QVERIFY2(statusText == QStringLiteral("Scanning for OPC UA servers…"),
                 "Discovery should still be in flight immediately after show()");

        // The probe must then complete on its own through the event loop.
        QTRY_VERIFY_WITH_TIMEOUT(detectButton->isEnabled(), 20000);
    }

    // ── Scenario 2: 'Detect Server' button (multi-candidate manual scan) ──
    {
        QTcpServer silentServer;
        QVERIFY(silentServer.listen(QHostAddress::LocalHost, 0));
        endpointEdit->setText(QStringLiteral("opc.tcp://127.0.0.1:%1").arg(silentServer.serverPort()));

        QVERIFY(detectButton->isEnabled());
        detectButton->click(); // runs refreshOpcUaEndpointDiscovery(false, true)

        QLabel* status = findDiscoveryStatusLabel(dialog);
        const QString statusText = status ? status->text() : QString();
        QVERIFY2(!detectButton->isEnabled(),
                 "Detect Server resolved synchronously: the manual scan blocks the UI thread");
        QVERIFY2(statusText == QStringLiteral("Scanning for OPC UA servers…"),
                 "Detect Server should return with the scan still in flight");

        // The scan walks the silent endpoint first, then the local fallbacks;
        // allow enough headroom for several silent/refused candidates.
        QTRY_VERIFY_WITH_TIMEOUT(detectButton->isEnabled(), 30000);
    }

    dialog.hide();
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    ConfigDialogNetworkRegression testCase;
    return QTest::qExec(&testCase, argc, argv);
}
