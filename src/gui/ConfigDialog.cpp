#include "ConfigDialog.h"
#include "widgets/CameraCard.h"
#include "widgets/CameraWidget.h"
#include "widgets/AnalysisVideoWidget.h"
#include "widgets/CameraDeviceSettingsDialog.h"
#include "widgets/NetworkSummaryHeader.h"
#include "widgets/DeleteConfirmationDialog.h"
#include "widgets/IconManager.h"
#include "../config/CameraConfig.h"
#include "../core/CameraManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFormLayout>
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QApplication>
#include <QScrollArea>
#include <QGroupBox>
#include <QFrame>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QEvent>
#include <QDateTime>
#include <QDebug>
#include <QMap>
#include <QSet>
#include <QGridLayout>
#include <QFont>
#include <QBrush>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <utility>
#include <algorithm>

namespace {
    struct CuratedFontOption {
        const char* family;
        const char* label;
    };

    const CuratedFontOption kLiveViewFonts[] = {
        {"Aptos", "Aptos | CAM-01: DRYER"},
        {"Inter", "Inter | CAM-01: DRYER"},
        {"Segoe UI", "Segoe UI | CAM-01: DRYER"},
        {"Roboto", "Roboto | CAM-01: DRYER"},
        {"Noto Sans", "Noto Sans | CAM-01: DRYER"}
    };

    void populateCuratedFontCombo(QComboBox* combo) {
        if (!combo) {
            return;
        }
        combo->clear();
        for (const auto& option : kLiveViewFonts) {
            combo->addItem(QString::fromLatin1(option.label), QString::fromLatin1(option.family));
        }
    }

    void selectCuratedFont(QComboBox* combo, const QString& family) {
        if (!combo) {
            return;
        }
        const int index = combo->findData(family);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    }

    QString currentCuratedFontFamily(const QComboBox* combo) {
        if (!combo) {
            return QStringLiteral("Aptos");
        }
        const QVariant family = combo->currentData();
        return family.isValid() ? family.toString() : QStringLiteral("Aptos");
    }

    QString settingsTooltip(const QString& text) {
        return text;
    }

    void updateFontPreviewLabel(QLabel* label, const QString& family, int pixelSize, const QString& sampleText) {
        if (!label) {
            return;
        }
        QFont font(family);
        font.setPixelSize(pixelSize);
        font.setBold(true);
        label->setFont(font);
        label->setText(sampleText);
        label->setToolTip(QString("%1, %2 px").arg(family).arg(pixelSize));
    }

    QString normalizeLiveViewBackgroundStyle(const QString& backgroundStyle) {
        if (backgroundStyle == "textured" ||
            backgroundStyle == "textured_grid" ||
            backgroundStyle == "textured_diagonal" ||
            backgroundStyle == "textured_dots") {
            return QStringLiteral("textured_mesh");
        }

        if (backgroundStyle == "white_textured" ||
            backgroundStyle == "white_textured_grid" ||
            backgroundStyle == "white_textured_diagonal" ||
            backgroundStyle == "white_textured_dots") {
            return QStringLiteral("white_textured_mesh");
        }

        return backgroundStyle;
    }

    bool usesLightLiveViewBackground(const QString& backgroundStyle) {
        return backgroundStyle == "white" || backgroundStyle.startsWith("white_");
    }

    QColor liveViewBaseBackground(const QString& backgroundStyle) {
        return usesLightLiveViewBackground(backgroundStyle) ? QColor("#F2F2F2") : QColor("#000000");
    }

    QBrush liveViewBackgroundBrush(const QString& backgroundStyle) {
        const QString normalizedStyle = normalizeLiveViewBackgroundStyle(backgroundStyle);
        if (normalizedStyle == "textured_mesh" || normalizedStyle == "white_textured_mesh") {
            QPixmap texture(28, 28);
            const bool isWhite = normalizedStyle == "white_textured_mesh";
            texture.fill(isWhite ? QColor("#F3F3F3") : QColor("#0E0E0E"));

            QPainter texturePainter(&texture);
            texturePainter.setPen(QPen(isWhite ? QColor("#D8D8D8") : QColor("#1F1F1F"), 1));
            for (int offset = 0; offset <= 28; offset += 7) {
                texturePainter.drawLine(offset, 0, offset, 28);
                texturePainter.drawLine(0, offset, 28, offset);
            }
            texturePainter.setPen(QPen(isWhite ? QColor("#E6E6E6") : QColor("#181818"), 1));
            texturePainter.drawLine(0, 0, 28, 28);
            texturePainter.drawLine(28, 0, 0, 28);
            texturePainter.end();

            return QBrush(texture);
        }

        return QBrush(liveViewBaseBackground(normalizedStyle));
    }

    QString normalizeIp(const QString& ip) {
        return ip.trimmed();
    }

    QString normalizeMac(const QString& mac) {
        QString normalized;
        normalized.reserve(mac.size());
        for (const QChar ch : mac) {
            if (ch.isLetterOrNumber()) {
                normalized.append(ch.toUpper());
            }
        }
        return normalized;
    }

    bool cameraConfigEqual(const CameraInfo& lhs, const CameraInfo& rhs) {
        return lhs.id == rhs.id
            && lhs.source == rhs.source
            && lhs.name == rhs.name
            && lhs.location == rhs.location
            && lhs.side == rhs.side
            && lhs.machinePosition == rhs.machinePosition
            && normalizeIp(lhs.ipAddress) == normalizeIp(rhs.ipAddress)
            && normalizeMac(lhs.macAddress) == normalizeMac(rhs.macAddress)
            && normalizeIp(lhs.subnetMask) == normalizeIp(rhs.subnetMask)
            && normalizeIp(lhs.defaultGateway) == normalizeIp(rhs.defaultGateway)
            && lhs.fps == rhs.fps
            && lhs.enableAcquisitionFps == rhs.enableAcquisitionFps
            && lhs.width == rhs.width
            && lhs.height == rhs.height
            && lhs.offsetX == rhs.offsetX
            && lhs.offsetY == rhs.offsetY
            && lhs.pixelFormat == rhs.pixelFormat
            && lhs.exposureTimeAbs == rhs.exposureTimeAbs
            && lhs.enableExposureTimeBase == rhs.enableExposureTimeBase
            && lhs.exposureTimeBaseAbs == rhs.exposureTimeBaseAbs
            && lhs.exposureTimeRaw == rhs.exposureTimeRaw
            && lhs.chunkModeActive == rhs.chunkModeActive
            && lhs.enabledChunks == rhs.enabledChunks;
    }

    bool cameraConfigListEqual(const std::vector<CameraInfo>& lhs, const std::vector<CameraInfo>& rhs) {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (size_t i = 0; i < lhs.size(); ++i) {
            if (!cameraConfigEqual(lhs[i], rhs[i])) {
                return false;
            }
        }

        return true;
    }

    void persistCameraNetworkSelection(int cameraId, int source, const QString& ip, const QString& mac,
                                       const QString& mask, const QString& gateway) {
        std::vector<CameraInfo> cameras = CameraConfig::getCameras();
        for (auto& cam : cameras) {
            if (cam.id != cameraId) continue;

            cam.source = source;
            cam.ipAddress = normalizeIp(ip);
            cam.macAddress = normalizeMac(mac);
            cam.subnetMask = normalizeIp(mask);
            cam.defaultGateway = normalizeIp(gateway);
            CameraConfig::saveCameras(cameras);
            return;
        }
    }

    QString joinCameraIds(const QList<int>& ids) {
        QStringList parts;
        for (int id : ids) {
            parts.append(QString::number(id));
        }
        return parts.join(", ");
    }

    void stylePrimaryActionButton(QPushButton* button, const ThemeColors& tc) {
        button->setStyleSheet(QString(
            "QPushButton { "
            "  background-color: %1; "
            "  color: %2; "
            "  border: none; "
            "  border-radius: 8px; "
            "  padding: 8px 16px; "
            "  font-size: 13px; "
            "  font-weight: 600; "
            "} "
            "QPushButton:hover { "
            "  background-color: %3; "
            "} "
            "QPushButton:pressed { "
            "  background-color: %1; "
            "}"
        ).arg(tc.primary, tc.bg, tc.btnHover));
    }
}

bool ConfigDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == cameraScrollWidget_ && event->type() == QEvent::Resize) {
        relayoutCameraCards();
    }

    if (obj == (uiPreferencesScrollArea_ ? uiPreferencesScrollArea_->viewport() : nullptr)
            && event->type() == QEvent::Resize) {
        relayoutUiPreferencePanels();
    }

    return QWidget::eventFilter(obj, event);
}

void ConfigDialog::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    relayoutUiPreferencePanels();
}

void ConfigDialog::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayoutUiPreferencePanels();
}

ConfigDialog::ConfigDialog(CameraManager* cameraManager, QWidget *parent)
    : QWidget(parent)
    , cameraManager_(cameraManager)
    , networkSummaryHeader_(nullptr)
    , isAdminMode_(false)
    , primaryColor_("#E3E3E3")
    , accentColor_("#00E5FF") {

    setWindowTitle("System Configuration");

    currentGigEDevices_ = CameraManager::enumerateGigEDevices();

    setupUI();

    loadSettings();
    liveViewGridTitlePresets_.updateStyles();
    liveViewDetailTitlePresets_.updateStyles();
    liveViewDetailSectionPresets_.updateStyles();
    analysisVideoTitlePresets_.updateStyles();
    analysisTimestampPresets_.updateStyles();
    analysisTabPresets_.updateStyles();
    setupUiModificationTracking();
}

ConfigDialog::~ConfigDialog() = default;

void ConfigDialog::setupUI() {
    constexpr int kPageMargin = 16;
    constexpr int kSectionSpacing = 16;
    constexpr int kControlSpacing = 12;
    constexpr int kSidebarMinWidth = 220;
    constexpr int kSidebarContentPadding = 64;

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(kSectionSpacing);
    mainLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    ThemeColors tc = CameraConfig::getThemeColors();

    auto createPillStyle = [&tc](bool active) {
        if (active) {
            return QString(
                "QPushButton { background-color: %1; color: %2; border: 1px solid %1; border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 700; } "
                "QPushButton:hover { background-color: %3; } "
                "QPushButton:pressed { background-color: %1; }"
            ).arg(tc.primary, tc.bg, tc.text);
        }
        return QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 600; } "
            "QPushButton:hover { border-color: %4; } "
            "QPushButton:pressed { background-color: %4; }"
        ).arg(tc.btnBg, tc.text, tc.border, tc.primary);
    };

    auto createTypographyRow = [this, &tc, &createPillStyle](QWidget* parent, QComboBox* fontCombo, QSpinBox* sizeSpin,
            const QString& tooltip, int presetS, int presetM, int presetL, PresetButtonGroup& presets) {
        const QString formattedTooltip = settingsTooltip(tooltip);
        QWidget* row = new QWidget(parent);
        QVBoxLayout* rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);
        row->setToolTip(formattedTooltip);

        QHBoxLayout* controlsLayout = new QHBoxLayout();
        controlsLayout->setContentsMargins(0, 0, 0, 0);
        controlsLayout->setSpacing(8);
        controlsLayout->addWidget(fontCombo, 1);
        controlsLayout->addWidget(sizeSpin);
        rowLayout->addLayout(controlsLayout);

        QHBoxLayout* presetsLayout = new QHBoxLayout();
        presetsLayout->setContentsMargins(0, 0, 0, 0);
        presetsLayout->setSpacing(6);
        presetsLayout->addStretch();
        rowLayout->addLayout(presetsLayout);

        const QString inactiveStyle = createPillStyle(false);

        presets.targetSpin = sizeSpin;
        presets.presetS = presetS;
        presets.presetM = presetM;
        presets.presetL = presetL;
        presets.inactiveStyle = inactiveStyle;
        presets.activeStyle = createPillStyle(true);

        const auto addPill = [row, presetsLayout, sizeSpin, &presets](const QString& label, int preset) {
            QPushButton* btn = new QPushButton(label, row);
            btn->setFixedSize(28, 22);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setToolTip(settingsTooltip(QString("%1: %2 px").arg(label, QString::number(preset))));
            presetsLayout->addWidget(btn);
            QObject::connect(btn, &QPushButton::clicked, [sizeSpin, preset]() {
                sizeSpin->setValue(preset);
            });
            return btn;
        };

        presets.btnS = addPill("S", presetS);
        presets.btnM = addPill("M", presetM);
        presets.btnL = addPill("L", presetL);

        QObject::connect(sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), [&presets]() {
            presets.updateStyles();
        });

        return row;
    };
    
    // Create list widget for sidebar navigation
    QListWidget* sidebar = new QListWidget(this);
    sidebar->setIconSize(QSize(20, 20));
    sidebar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebar->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebar->setTextElideMode(Qt::ElideNone);
    sidebar->setFixedWidth(kSidebarMinWidth);
    sidebar->setStyleSheet(QString(
        "QListWidget { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 8px; "
        "  outline: 0; "
        "} "
        "QListWidget::item { "
        "  padding: 10px 12px; "
        "  color: %3; "
        "  font-size: 14px; "
        "  font-weight: 600; "
        "  border-bottom: 1px solid %2; "
        "} "
        "QListWidget::item:selected { "
        "  background-color: %4; "
        "  border-left: 4px solid %5; "
        "} "
        "QListWidget::item:hover:!selected { "
        "  background-color: %4; "
        "}"
    ).arg(tc.btnBg, tc.border, tc.text, tc.bg, tc.primary));

    QStackedWidget* stackedWidget = new QStackedWidget(this);
    stackedWidget->setStyleSheet("QStackedWidget { background: transparent; }");

    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(kSectionSpacing);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(sidebar);
    contentLayout->addWidget(stackedWidget, 1);


    // Camera Configuration Tab
    QWidget* camSetupGroup = new QWidget(this);
    QVBoxLayout* camSetupLayout = new QVBoxLayout(camSetupGroup);
    camSetupLayout->setSpacing(kSectionSpacing);
    camSetupLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    // Premium Network Summary Header
    networkSummaryHeader_ = new NetworkSummaryHeader(this);
    connect(networkSummaryHeader_, &NetworkSummaryHeader::refreshRequested,
            this, &ConfigDialog::onRefreshLogsClicked);
    // Removed clear/toggle signals as they belong to diagnostics tab now
    connect(networkSummaryHeader_, &NetworkSummaryHeader::addCameraRequested,
            this, &ConfigDialog::onAddCameraConfigClicked);
    camSetupLayout->addWidget(networkSummaryHeader_);

    // Scroll area for camera cards
    cameraScrollArea_ = new QScrollArea(this);
    cameraScrollArea_->setWidgetResizable(true);
    cameraScrollArea_->setFrameShape(QFrame::NoFrame);
    cameraScrollArea_->setStyleSheet(QString(
        "QScrollArea { border: none; background: transparent; } "
        "QScrollBar:vertical { "
        "  background-color: %1; "
        "  width: 12px; "
        "  border-radius: 6px; "
        "} "
        "QScrollBar::handle:vertical { "
        "  background-color: %2; "
        "  border-radius: 6px; "
        "  min-height: 30px; "
        "} "
        "QScrollBar::handle:vertical:hover { "
        "  background-color: %3; "
        "} "
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
        "  height: 0px; "
        "}"
    ).arg(tc.bg, tc.btnBg, tc.primary));

    cameraScrollWidget_ = new QWidget();
    cameraListLayout_ = new QGridLayout(cameraScrollWidget_);
    cameraListLayout_->setSpacing(kSectionSpacing);
    cameraListLayout_->setContentsMargins(0, 0, 8, 0);
    cameraListLayout_->setAlignment(Qt::AlignTop);
    cameraScrollWidget_->installEventFilter(this);

    cameraScrollArea_->setWidget(cameraScrollWidget_);
    camSetupLayout->addWidget(cameraScrollArea_, 1);

    QHBoxLayout* cameraActionsLayout = new QHBoxLayout();
    cameraActionsLayout->addStretch();
    cameraSaveBtn_ = new QPushButton("Save Camera Configuration", camSetupGroup);
    cameraSaveBtn_->setIcon(IconManager::instance().save(16));
    cameraSaveBtn_->setDefault(true);
    stylePrimaryActionButton(cameraSaveBtn_, tc);
    connect(cameraSaveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveCameraConfiguration);
    cameraActionsLayout->addWidget(cameraSaveBtn_);
    camSetupLayout->addLayout(cameraActionsLayout);

    QListWidgetItem* camSetupItem = new QListWidgetItem(IconManager::instance().settings(20), "Camera Configuration");
    sidebar->addItem(camSetupItem);
    stackedWidget->addWidget(camSetupGroup);

    // Recording & Triggers Tab
    QWidget* bufferGroup = new QWidget(this);
    QVBoxLayout* bufferLayout = new QVBoxLayout(bufferGroup);
    bufferLayout->setSpacing(kSectionSpacing);
    bufferLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    const QString sectionStyle = QString(
        "QGroupBox { font-weight: 600; color: %1; border: 1px solid %2; "
        "border-radius: 8px; margin-top: 8px; padding-top: 8px; font-size: 12px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
    ).arg(tc.primary, tc.border);

    auto createSectionForm = [&](const QString& title) {
        QGroupBox* group = new QGroupBox(title, bufferGroup);
        group->setStyleSheet(sectionStyle);
        QFormLayout* form = new QFormLayout(group);
        form->setSpacing(kControlSpacing);
        form->setContentsMargins(14, 18, 14, 14);
        form->setHorizontalSpacing(kSectionSpacing);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bufferLayout->addWidget(group);
        return form;
    };

    QFormLayout* recordingForm = createSectionForm("Recording");

    // Global FPS
    globalFpsSpin_ = new QSpinBox(bufferGroup);
    globalFpsSpin_->setRange(1, 200);
    globalFpsSpin_->setSuffix(" fps");
    globalFpsSpin_->setStyleSheet(QString(
        "QSpinBox { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  padding: 6px 10px; "
        "  color: %3; "
        "  min-width: 100px; "
        "} "
        "QSpinBox:hover { border-color: %4; } "
        "QSpinBox:focus { border-color: %4; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    recordingForm->addRow("Fallback FPS:", globalFpsSpin_);

    // Pre-Trigger
    preTriggerSpin_ = new QSpinBox(bufferGroup);
    preTriggerSpin_->setRange(1, 60);
    preTriggerSpin_->setSuffix(" sec");
    preTriggerSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    recordingForm->addRow("Pre-Trigger Buffer:", preTriggerSpin_);

    // Post-Trigger
    postTriggerSpin_ = new QSpinBox(bufferGroup);
    postTriggerSpin_->setRange(1, 60);
    postTriggerSpin_->setSuffix(" sec");
    postTriggerSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    recordingForm->addRow("Post-Trigger Recording:", postTriggerSpin_);

    QFormLayout* retentionForm = createSectionForm("Record Storage");

    eventRetentionSpin_ = new QSpinBox(bufferGroup);
    eventRetentionSpin_->setRange(1, 10000);
    eventRetentionSpin_->setSuffix(" records");
    eventRetentionSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    retentionForm->addRow("Keep Recent Records:", eventRetentionSpin_);

    QFormLayout* triggerForm = createSectionForm("Triggering");

    QLabel* defectNote = new QLabel("Defect trigger is controlled from the Live screen for immediate operation.", bufferGroup);
    defectNote->setWordWrap(true);
    defectNote->setStyleSheet(QString("color: %1; padding-top: 4px;").arg(tc.text));
    triggerForm->addRow("Defect Trigger:", defectNote);

    bufferLayout->addStretch();

    QHBoxLayout* recordingActionsLayout = new QHBoxLayout();
    recordingActionsLayout->addStretch();
    recordingSaveBtn_ = new QPushButton("Save Recording Settings", bufferGroup);
    recordingSaveBtn_->setIcon(IconManager::instance().save(16));
    stylePrimaryActionButton(recordingSaveBtn_, tc);
    connect(recordingSaveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveRecordingSettings);
    recordingActionsLayout->addWidget(recordingSaveBtn_);
    bufferLayout->addLayout(recordingActionsLayout);

    QListWidgetItem* globalGroupItem = new QListWidgetItem(IconManager::instance().warning(20), "Recording & Triggers");
    sidebar->addItem(globalGroupItem);
    stackedWidget->addWidget(bufferGroup);

    // UI Preferences Tab
    QWidget* uiGroup = new QWidget(this);
    QVBoxLayout* uiPageLayout = new QVBoxLayout(uiGroup);
    uiPageLayout->setContentsMargins(16, 8, 16, 6);
    uiPageLayout->setSpacing(5);

    uiPreferencesScrollArea_ = nullptr;

    // Header section
    QLabel* uiHeaderLabel = new QLabel("UI Preferences", uiGroup);
    uiHeaderLabel->setStyleSheet(QString(
        "color: %1; font-size: 17px; font-weight: 700;"
    ).arg(tc.primary));
    uiPageLayout->addWidget(uiHeaderLabel);

    QLabel* uiDescriptionLabel = new QLabel(
        "Customize storage, theme, and screen typography.", uiGroup);
    uiDescriptionLabel->setWordWrap(true);
    uiDescriptionLabel->setStyleSheet(QString(
        "color: %1; font-size: 11px; padding-bottom: 2px;"
    ).arg(tc.text));
    uiPageLayout->addWidget(uiDescriptionLabel);

    // Separator line
    QFrame* uiSeparator = new QFrame(uiGroup);
    uiSeparator->setFrameShape(QFrame::HLine);
    uiSeparator->setFrameShadow(QFrame::Plain);
    uiSeparator->setFixedHeight(1);
    uiSeparator->setStyleSheet(QString(
        "background-color: %1; border: none;"
    ).arg(tc.border));
    uiPageLayout->addWidget(uiSeparator);

    QWidget* uiPanel = new QWidget(uiGroup);
    QGridLayout* uiPanelLayout = new QGridLayout(uiPanel);
    uiPanelLayout->setContentsMargins(0, 0, 0, 0);
    uiPanelLayout->setHorizontalSpacing(8);
    uiPanelLayout->setVerticalSpacing(3);
    uiPanelLayout->setColumnStretch(0, 1);
    uiPanelLayout->setColumnStretch(1, 1);
    uiPanelLayout->setRowStretch(0, 0);
    uiPanelLayout->setRowStretch(1, 1);

    const QString uiSectionStyle = QString(
        "QGroupBox { font-weight: 600; color: %1; border: 1px solid %2; "
        "background-color: rgba(255, 255, 255, 0.02); border-radius: 10px; margin-top: 12px; padding: 12px 10px 7px 10px; font-size: 12px; } "
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 6px; top: 2px; }"
    ).arg(tc.primary, tc.border);

    int uiSectionIndex = 0;
    auto createUiSectionForm = [&](const QString& title) {
        QGroupBox* group = new QGroupBox(title, uiPanel);
        group->setStyleSheet(uiSectionStyle);
        QFormLayout* form = new QFormLayout(group);
        form->setSpacing(6);
        form->setHorizontalSpacing(10);
        form->setContentsMargins(10, 14, 10, 8);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        uiPanelLayout->addWidget(group, uiSectionIndex / 2, uiSectionIndex % 2);
        ++uiSectionIndex;
        return form;
    };

    QFormLayout* storageForm = createUiSectionForm("Data Storage");
    QFormLayout* themeForm = createUiSectionForm("Appearance");

    QTabWidget* uiDetailTabs = new QTabWidget(uiPanel);
    uiDetailTabs->setDocumentMode(true);
    uiDetailTabs->setStyleSheet(QString(
        "QTabWidget::pane { border: 1px solid %1; border-radius: 10px; top: -1px; background-color: rgba(255, 255, 255, 0.01); padding: 2px; } "
        "QTabBar::tab { background-color: %2; color: %3; border: 1px solid %1; border-bottom: none; padding: 6px 14px; min-width: 110px; border-top-left-radius: 8px; border-top-right-radius: 8px; font-weight: 600; font-size: 12px; } "
        "QTabBar::tab:selected { color: %4; background-color: rgba(255, 255, 255, 0.04); margin-bottom: -1px; } "
        "QTabBar::tab:!selected { margin-top: 3px; color: %5; } "
        "QTabBar::tab:hover { color: %4; }"
    ).arg(tc.border, tc.btnBg, tc.text, tc.primary, tc.text));
    uiPanelLayout->addWidget(uiDetailTabs, 1, 0, 1, 2);

    QWidget* liveViewTab = new QWidget(uiDetailTabs);
    QVBoxLayout* liveViewTabLayout = new QVBoxLayout(liveViewTab);
    liveViewTabLayout->setContentsMargins(10, 10, 10, 10);
    liveViewTabLayout->setSpacing(10);

    QWidget* liveViewGroup = new QWidget(liveViewTab);
    QVBoxLayout* liveViewGroupLayout = new QVBoxLayout(liveViewGroup);
    liveViewGroupLayout->setContentsMargins(0, 0, 0, 0);
    liveViewGroupLayout->setSpacing(10);
    liveViewTabLayout->addWidget(liveViewGroup, 0);
    liveViewTabLayout->addStretch(1);

    QLabel* liveViewSectionTitle = new QLabel("Live View", liveViewGroup);
    liveViewSectionTitle->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: 700; padding-bottom: 2px;").arg(tc.primary));
    liveViewGroupLayout->addWidget(liveViewSectionTitle);

    QWidget* liveViewContent = new QWidget(liveViewGroup);
    liveViewContentLayout_ = new QHBoxLayout(liveViewContent);
    liveViewContentLayout_->setContentsMargins(0, 0, 0, 0);
    liveViewContentLayout_->setSpacing(16);
    liveViewContentLayout_->setAlignment(Qt::AlignTop);
    liveViewGroupLayout->addWidget(liveViewContent);

    QWidget* liveViewSettingsPanel = new QWidget(liveViewContent);
    QVBoxLayout* liveViewSettingsLayout = new QVBoxLayout(liveViewSettingsPanel);
    liveViewSettingsLayout->setContentsMargins(0, 0, 0, 0);
    liveViewSettingsLayout->setSpacing(12);
    liveViewSettingsPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    liveViewContentLayout_->addWidget(liveViewSettingsPanel, 5, Qt::AlignTop);

    const QString settingsCardStyle = QString(
        "QGroupBox { font-weight: 600; color: %1; border: 1px solid %2; border-radius: 10px; margin-top: 8px; "
        "background-color: rgba(255, 255, 255, 0.02); padding: 10px; font-size: 12px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
    ).arg(tc.primary, tc.border);

    QGroupBox* liveSettingsCard = new QGroupBox("Live View Appearance", liveViewSettingsPanel);
    liveSettingsCard->setStyleSheet(settingsCardStyle);
    QVBoxLayout* liveSettingsCardLayout = new QVBoxLayout(liveSettingsCard);
    liveSettingsCardLayout->setContentsMargins(12, 16, 12, 12);
    liveSettingsCardLayout->setSpacing(10);
    liveViewSettingsLayout->addWidget(liveSettingsCard);

    const int kRowSpacing = 10;

    QFormLayout* liveViewForm = new QFormLayout();
    liveViewForm->setSpacing(kRowSpacing);
    liveViewForm->setHorizontalSpacing(16);
    liveViewForm->setContentsMargins(0, 0, 0, 0);
    liveViewForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    liveViewForm->setFormAlignment(Qt::AlignTop);

    auto createLiveViewRowLabel = [&](const QString& title, const QString& detail) {
        const QString formattedTooltip = settingsTooltip(detail);
        QWidget* labelWidget = new QWidget(liveViewGroup);
        QVBoxLayout* labelLayout = new QVBoxLayout(labelWidget);
        labelLayout->setContentsMargins(0, 1, 0, 0);
        labelLayout->setSpacing(0);

        QLabel* titleLabel = new QLabel(title, labelWidget);
        titleLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: 600;").arg(tc.text));
        titleLabel->setToolTip(formattedTooltip);
        labelLayout->addWidget(titleLabel);
        labelWidget->setToolTip(formattedTooltip);

        return labelWidget;
    };

    QWidget* eventStorageRowWidget = new QWidget(uiGroup);
    QHBoxLayout* eventStorageRowLayout = new QHBoxLayout(eventStorageRowWidget);
    eventStorageRowLayout->setContentsMargins(0, 0, 0, 0);
    eventStorageRowLayout->setSpacing(8);

    eventStoragePathEdit_ = new QLineEdit(eventStorageRowWidget);
    eventStoragePathEdit_->setReadOnly(true);
    eventStoragePathEdit_->setStyleSheet(QString(
        "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 6px 10px; font-size: 11px; }"
    ).arg(tc.btnBg, tc.text, tc.border));
    eventStorageRowLayout->addWidget(eventStoragePathEdit_, 1);

    browseEventStorageBtn_ = new QPushButton("Browse", eventStorageRowWidget);
    browseEventStorageBtn_->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 6px 12px; font-size: 11px; font-weight: 500; } "
        "QPushButton:hover { border-color: %4; background-color: rgba(255, 255, 255, 0.04); }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary));
    browseEventStorageBtn_->setToolTip("Choose a different folder for event storage.");
    connect(browseEventStorageBtn_, &QPushButton::clicked, this, [this]() {
        const QString selectedDir = QFileDialog::getExistingDirectory(
            this,
            "Select Event Storage Folder",
            eventStoragePathEdit_ ? eventStoragePathEdit_->text() : CameraConfig::getEventStoragePath());
        if (!selectedDir.isEmpty() && eventStoragePathEdit_) {
            eventStoragePathEdit_->setText(QDir::cleanPath(selectedDir));
        }
    });
    eventStorageRowLayout->addWidget(browseEventStorageBtn_);

    resetEventStorageBtn_ = new QPushButton("Default", eventStorageRowWidget);
    resetEventStorageBtn_->setStyleSheet(browseEventStorageBtn_->styleSheet());
    resetEventStorageBtn_->setToolTip("Restore the default event storage path.");
    connect(resetEventStorageBtn_, &QPushButton::clicked, this, [this]() {
        if (eventStoragePathEdit_) {
            eventStoragePathEdit_->setText(CameraConfig::getDefaultEventStoragePath());
        }
    });
    eventStorageRowLayout->addWidget(resetEventStorageBtn_);

    QLabel* storageFolderLabel = new QLabel("Folder:", uiGroup);
    storageFolderLabel->setToolTip("Directory where event recordings and metadata are saved.");
    storageForm->addRow(storageFolderLabel, eventStorageRowWidget);

    QLabel* eventStorageNote = new QLabel("Used by new recordings and historical event loading.", uiGroup);
    eventStorageNote->setWordWrap(true);
    eventStorageNote->setStyleSheet(QString("color: %1; font-size: 11px; font-style: italic;").arg(tc.text));
    storageForm->addRow("", eventStorageNote);

    // Theme selection dropdown
    themeGridWidget_ = nullptr;
    themeGridLayout_ = nullptr;
    themeButtonGroup_ = nullptr;

    QComboBox* themeCombo = new QComboBox(uiGroup);
    themeCombo_ = themeCombo;
    themeCombo->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 8px 12px; color: %3; min-width: 220px; font-size: 12px; font-weight: 600; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 8px; } "
        "QComboBox QAbstractItemView { background-color: %1; border: 1px solid %2; border-radius: 6px; color: %3; selection-background-color: %5; padding: 4px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary, tc.bg));
    QLabel* themeLabel = new QLabel("Color Theme:", uiGroup);
    themeLabel->setToolTip("Select a color theme for the application.");

    const struct { const char* name; int index; } themeEntries[] = {
        {"Industrial Dark - Cyan", 0},
        {"Classic Dark - Blue", 1},
        {"High Contrast - Orange", 2},
        {"Warning State - Yellow", 3},
        {"Precision - Green", 4},
        {"Visionary - Purple", 5},
        {"Alert - Deep Red", 6},
        {"Contrast Mono - B&W", 7}
    };

    for (int i = 0; i < 8; ++i) {
        const auto& entry = themeEntries[i];
        const ThemeColors entryColors = CameraConfig::getThemeColors(entry.index);

        // Create a color swatch icon
        QPixmap swatch(20, 20);
        swatch.fill(Qt::transparent);
        QPainter painter(&swatch);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(entryColors.bg));
        painter.setPen(QPen(QColor(entryColors.border), 1));
        painter.drawRoundedRect(1, 1, 18, 18, 4, 4);
        // Draw accent bar
        painter.setBrush(QColor(entryColors.primary));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(4, 8, 12, 4, 2, 2);
        painter.end();

        themeCombo->addItem(QIcon(swatch), entry.name, entry.index);
    }

    selectedThemeIndex_ = CameraConfig::getThemePreset();
    themeCombo->setCurrentIndex(selectedThemeIndex_);

    // Store null in themeCards_ since we no longer use card buttons
    for (int i = 0; i < 8; ++i) {
        themeCards_[i] = nullptr;
    }

    connect(themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, themeCombo](int index) {
        this->selectedThemeIndex_ = themeCombo->currentData().toInt();
        // Update combo text color to match selected theme
        const ThemeColors selectedColors = CameraConfig::getThemeColors(this->selectedThemeIndex_);
        themeCombo->setStyleSheet(QString(
            "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 8px 12px; color: %3; min-width: 220px; font-size: 12px; font-weight: 600; } "
            "QComboBox:hover { border-color: %4; } "
            "QComboBox:focus { border-color: %4; } "
            "QComboBox::drop-down { border: none; padding-right: 8px; } "
            "QComboBox QAbstractItemView { background-color: %1; border: 1px solid %2; border-radius: 6px; color: %5; selection-background-color: %6; padding: 4px; }"
        ).arg(selectedColors.btnBg, selectedColors.border, selectedColors.primary, selectedColors.primary, selectedColors.text, selectedColors.bg));
        emit this->themeSelectionChanged();
    });

    // Apply initial theme color to combo
    {
        const ThemeColors initColors = CameraConfig::getThemeColors(selectedThemeIndex_);
        themeCombo->setStyleSheet(QString(
            "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 8px 12px; color: %3; min-width: 220px; font-size: 12px; font-weight: 600; } "
            "QComboBox:hover { border-color: %4; } "
            "QComboBox:focus { border-color: %4; } "
            "QComboBox::drop-down { border: none; padding-right: 8px; } "
            "QComboBox QAbstractItemView { background-color: %1; border: 1px solid %2; border-radius: 6px; color: %5; selection-background-color: %6; padding: 4px; }"
        ).arg(initColors.btnBg, initColors.border, initColors.primary, initColors.primary, initColors.text, initColors.bg));
    }

    themeForm->addRow(themeLabel, themeCombo);

    QLabel* liveViewDescription = new QLabel("Tune card surface, title, and section typography.", uiGroup);
    liveViewDescription->setWordWrap(true);
    liveViewDescription->setStyleSheet(QString("color: %1; font-size: 11px; padding-bottom: 2px;").arg(tc.text));
    liveSettingsCardLayout->addWidget(liveViewDescription);

    liveViewBackgroundStyleCombo_ = new QComboBox(uiGroup);
    liveViewBackgroundStyleCombo_->addItem("Black", "black");
    liveViewBackgroundStyleCombo_->addItem("White", "white");
    liveViewBackgroundStyleCombo_->addItem("Textured Mesh", "textured_mesh");
    liveViewBackgroundStyleCombo_->addItem("White Textured Mesh", "white_textured_mesh");
    liveViewBackgroundStyleCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    liveViewBackgroundStyleCombo_->setFixedWidth(160);
    liveViewForm->addRow(
        createLiveViewRowLabel("Card Surface", "Background for the card body."),
        liveViewBackgroundStyleCombo_);

    liveViewGridTitleFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(liveViewGridTitleFontCombo_);
    liveViewGridTitleFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));

    liveViewGridTitleSizeSpin_ = new QSpinBox(uiGroup);
    liveViewGridTitleSizeSpin_->setRange(10, 40);
    liveViewGridTitleSizeSpin_->setSuffix(" px");
    liveViewGridTitleSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    liveViewGridTitleSizeSpin_->setFixedWidth(100);
    liveViewGridTitleSizeSpin_->setFixedWidth(72);

    liveViewForm->addRow(
        createLiveViewRowLabel("Grid Title", "Camera title shown on each grid tile."),
        createTypographyRow(liveViewGroup, liveViewGridTitleFontCombo_, liveViewGridTitleSizeSpin_, "Camera title shown on each grid tile.", 10, 14, 18, liveViewGridTitlePresets_));

    liveViewDetailTitleFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(liveViewDetailTitleFontCombo_);
    liveViewDetailTitleFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));

    liveViewDetailTitleSizeSpin_ = new QSpinBox(uiGroup);
    liveViewDetailTitleSizeSpin_->setRange(10, 40);
    liveViewDetailTitleSizeSpin_->setSuffix(" px");
    liveViewDetailTitleSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    
    liveViewDetailTitleSizeSpin_->setFixedWidth(72);

    liveViewForm->addRow(
        createLiveViewRowLabel("Detail Title", "Primary camera title in the detail card."),
        createTypographyRow(liveViewGroup, liveViewDetailTitleFontCombo_, liveViewDetailTitleSizeSpin_, "Primary camera title in the detail card.", 10, 14, 18, liveViewDetailTitlePresets_));

    liveViewDetailSectionFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(liveViewDetailSectionFontCombo_);
    liveViewDetailSectionFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));

    liveViewDetailSectionSizeSpin_ = new QSpinBox(uiGroup);
    liveViewDetailSectionSizeSpin_->setRange(9, 32);
    liveViewDetailSectionSizeSpin_->setSuffix(" px");
    liveViewDetailSectionSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());

    liveViewDetailSectionSizeSpin_->setFixedWidth(72);

    liveViewForm->addRow(
        createLiveViewRowLabel("Section Header", "Group titles inside the detail card."),
        createTypographyRow(liveViewGroup, liveViewDetailSectionFontCombo_, liveViewDetailSectionSizeSpin_, "Group titles inside the detail card.", 11, 13, 16, liveViewDetailSectionPresets_));

    liveSettingsCardLayout->addLayout(liveViewForm);
    liveSettingsCardLayout->addSpacing(2);

    QWidget* analysisViewTab = new QWidget(uiDetailTabs);
    QVBoxLayout* analysisViewTabLayout = new QVBoxLayout(analysisViewTab);
    analysisViewTabLayout->setContentsMargins(10, 10, 10, 10);
    analysisViewTabLayout->setSpacing(10);

    QWidget* analysisViewGroup = new QWidget(analysisViewTab);
    QVBoxLayout* analysisViewGroupLayout = new QVBoxLayout(analysisViewGroup);
    analysisViewGroupLayout->setContentsMargins(0, 0, 0, 0);
    analysisViewGroupLayout->setSpacing(10);
    analysisViewTabLayout->addWidget(analysisViewGroup, 0);
    analysisViewTabLayout->addStretch(1);

    QLabel* analysisViewSectionTitle = new QLabel("Analysis View", analysisViewGroup);
    analysisViewSectionTitle->setStyleSheet(QString("color: %1; font-size: 13px; font-weight: 700; padding-bottom: 2px;").arg(tc.primary));
    analysisViewGroupLayout->addWidget(analysisViewSectionTitle);

    QWidget* analysisViewContent = new QWidget(analysisViewGroup);
    analysisViewContentLayout_ = new QHBoxLayout(analysisViewContent);
    analysisViewContentLayout_->setContentsMargins(0, 0, 0, 0);
    analysisViewContentLayout_->setSpacing(16);
    analysisViewContentLayout_->setAlignment(Qt::AlignTop);
    analysisViewGroupLayout->addWidget(analysisViewContent);

    QWidget* analysisSettingsPanel = new QWidget(analysisViewContent);
    QVBoxLayout* analysisSettingsLayout = new QVBoxLayout(analysisSettingsPanel);
    analysisSettingsLayout->setContentsMargins(0, 0, 0, 0);
    analysisSettingsLayout->setSpacing(12);
    analysisViewContentLayout_->addWidget(analysisSettingsPanel, 4, Qt::AlignTop);

    QGroupBox* analysisSettingsCard = new QGroupBox("Analysis View Appearance", analysisSettingsPanel);
    analysisSettingsCard->setStyleSheet(settingsCardStyle);
    QVBoxLayout* analysisSettingsCardLayout = new QVBoxLayout(analysisSettingsCard);
    analysisSettingsCardLayout->setContentsMargins(12, 16, 12, 12);
    analysisSettingsCardLayout->setSpacing(10);
    analysisSettingsLayout->addWidget(analysisSettingsCard);

    QLabel* analysisViewDescription = new QLabel("Tune video titles, timestamps, tabs, and playback surface.", uiGroup);
    analysisViewDescription->setWordWrap(true);
    analysisViewDescription->setStyleSheet(QString("color: %1; font-size: 11px; padding-bottom: 2px;").arg(tc.text));
    analysisSettingsCardLayout->addWidget(analysisViewDescription);

    QFormLayout* analysisViewForm = new QFormLayout();
    analysisViewForm->setSpacing(kRowSpacing);
    analysisViewForm->setHorizontalSpacing(12);
    analysisViewForm->setContentsMargins(0, 0, 0, 0);
    analysisViewForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    analysisViewForm->setFormAlignment(Qt::AlignTop);

    analysisVideoTitleFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(analysisVideoTitleFontCombo_);
    analysisVideoTitleFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    analysisVideoTitleSizeSpin_ = new QSpinBox(uiGroup);
    analysisVideoTitleSizeSpin_->setRange(8, 24);
    analysisVideoTitleSizeSpin_->setSuffix(" px");
    analysisVideoTitleSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    analysisVideoTitleSizeSpin_->setFixedWidth(72);
    analysisViewForm->addRow(
        createLiveViewRowLabel("Video Title", "Title overlay on analysis video tiles."),
        createTypographyRow(analysisViewGroup, analysisVideoTitleFontCombo_, analysisVideoTitleSizeSpin_, "Title overlay on analysis video tiles.", 8, 10, 14, analysisVideoTitlePresets_));

    analysisTimestampFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(analysisTimestampFontCombo_);
    analysisTimestampFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    analysisTimestampSizeSpin_ = new QSpinBox(uiGroup);
    analysisTimestampSizeSpin_->setRange(7, 20);
    analysisTimestampSizeSpin_->setSuffix(" px");
    analysisTimestampSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    analysisTimestampSizeSpin_->setFixedWidth(72);
    analysisViewForm->addRow(
        createLiveViewRowLabel("Timestamp", "Timecode shown on the analysis video footer."),
        createTypographyRow(analysisViewGroup, analysisTimestampFontCombo_, analysisTimestampSizeSpin_, "Timecode shown on the analysis video footer.", 7, 8, 12, analysisTimestampPresets_));

    analysisTabFontCombo_ = new QComboBox(uiGroup);
    populateCuratedFontCombo(analysisTabFontCombo_);
    analysisTabFontCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    analysisTabSizeSpin_ = new QSpinBox(uiGroup);
    analysisTabSizeSpin_->setRange(10, 24);
    analysisTabSizeSpin_->setSuffix(" px");
    analysisTabSizeSpin_->setStyleSheet(globalFpsSpin_->styleSheet());
    analysisTabSizeSpin_->setFixedWidth(72);
    analysisViewForm->addRow(
        createLiveViewRowLabel("Tab Label", "Tabs and playback label typography."),
        createTypographyRow(analysisViewGroup, analysisTabFontCombo_, analysisTabSizeSpin_, "Tabs and playback label typography.", 10, 12, 16, analysisTabPresets_));

    analysisPlaybackSurfaceCombo_ = new QComboBox(uiGroup);
    analysisPlaybackSurfaceCombo_->addItem("Dark", "dark");
    analysisPlaybackSurfaceCombo_->addItem("Light", "light");
    analysisPlaybackSurfaceCombo_->setStyleSheet(QString(
        "QComboBox { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 5px 8px; color: %3; font-size: 11px; } "
        "QComboBox:hover { border-color: %4; } "
        "QComboBox:focus { border-color: %4; } "
        "QComboBox::drop-down { border: none; padding-right: 6px; }"
    ).arg(tc.btnBg, tc.border, tc.text, tc.primary));
    analysisPlaybackSurfaceCombo_->setFixedWidth(160);
    analysisViewForm->addRow(
        createLiveViewRowLabel("Playback Surface", "Background used by the analysis playback bar and video blank surface."),
        analysisPlaybackSurfaceCombo_);

    analysisSettingsCardLayout->addLayout(analysisViewForm);

    QHBoxLayout* liveViewActionsLayout = new QHBoxLayout();
    liveViewActionsLayout->setContentsMargins(0, 6, 0, 0);
    liveViewActionsLayout->addStretch();

    const QString secondaryActionButtonStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 6px 14px; font-size: 11px; font-weight: 600; } "
        "QPushButton:hover { border-color: %4; background-color: rgba(255, 255, 255, 0.04); }"
    ).arg(tc.btnBg, tc.text, tc.border, tc.primary);

    liveViewResetBtn_ = new QPushButton("Reset Live View", uiGroup);
    liveViewResetBtn_->setToolTip("Restore all Live View card settings to defaults.");
    liveViewResetBtn_->setStyleSheet(secondaryActionButtonStyle);
    connect(liveViewResetBtn_, &QPushButton::clicked, this, &ConfigDialog::resetLiveViewCardSettings);
    liveViewActionsLayout->addWidget(liveViewResetBtn_);
    liveSettingsCardLayout->addLayout(liveViewActionsLayout);

    QHBoxLayout* analysisActionsLayout = new QHBoxLayout();
    analysisActionsLayout->setContentsMargins(0, 6, 0, 0);
    analysisActionsLayout->addStretch();
    analysisResetBtn_ = new QPushButton("Reset Analysis View", uiGroup);
    analysisResetBtn_->setToolTip("Restore all Analysis View settings to defaults.");
    analysisResetBtn_->setStyleSheet(secondaryActionButtonStyle);
    connect(analysisResetBtn_, &QPushButton::clicked, this, &ConfigDialog::resetAnalysisViewSettings);
    analysisActionsLayout->addWidget(analysisResetBtn_);
    analysisSettingsCardLayout->addLayout(analysisActionsLayout);

    auto refreshTypographyPreviews = [this]() {
        if (!liveViewDetailPreviewWidget_ ||
            !liveViewCardPreviewFrame_ ||
            !liveViewCardPreviewTitleLabel_ ||
            !liveViewCardPreviewMetaLabel_ ||
            !liveViewCardPreviewStatusLabel_) {
            return;
        }

        const int selectedThemePreset = selectedThemeIndex_;
        const ThemeColors previewThemeColors = CameraConfig::getThemeColors(selectedThemePreset);
        const QString backgroundStyle = normalizeLiveViewBackgroundStyle(liveViewBackgroundStyleCombo_->currentData().toString());
        const QBrush surfaceBrush = liveViewBackgroundBrush(backgroundStyle);
        const QColor themeBorderColor(previewThemeColors.border);
        const QColor themeTextColor(previewThemeColors.text);
        const QColor themePrimaryColor(previewThemeColors.primary);
        const QColor themePanelColor(previewThemeColors.btnBg);
        const QColor borderColor = themeBorderColor;
        const QColor titleColor = themeTextColor;
        const QColor subtitleColor = themeTextColor.lighter(145);
        const QColor groupTextColor = themeTextColor;
        const QColor groupBorderColor = themeBorderColor;
        const QColor statusTextColor = QColor("#0B1116");
        const QColor statusBackgroundColor = themePrimaryColor;
        const QColor detailPanelBackground(themePanelColor.red(), themePanelColor.green(), themePanelColor.blue(), 224);

        QFont detailPreviewFont(currentCuratedFontFamily(liveViewDetailTitleFontCombo_));
        detailPreviewFont.setPixelSize(liveViewDetailTitleSizeSpin_->value());
        detailPreviewFont.setBold(true);
        liveViewDetailPreviewWidget_->setPreviewThemeColors(previewThemeColors);
        liveViewDetailPreviewWidget_->setPreviewBackgroundStyle(backgroundStyle);
        liveViewDetailPreviewWidget_->setOverlayFont(detailPreviewFont);
        liveViewDetailPreviewWidget_->setOverlayText("CAM-01: DRYER");
        liveViewDetailPreviewWidget_->setTemperatureStatus(-1.0, TempStatus::Unknown);
        liveViewDetailPreviewWidget_->update();

        QPalette previewPalette = liveViewCardPreviewFrame_->palette();
        previewPalette.setBrush(QPalette::Window, surfaceBrush);
        previewPalette.setColor(QPalette::WindowText, titleColor);
        liveViewCardPreviewFrame_->setAutoFillBackground(true);
        liveViewCardPreviewFrame_->setPalette(previewPalette);
        liveViewCardPreviewFrame_->setStyleSheet(QString(
            "QFrame#liveViewCardPreview { border: 1px solid %1; border-radius: 12px; }"
        ).arg(borderColor.name()));

        updateFontPreviewLabel(
            liveViewCardPreviewTitleLabel_,
            currentCuratedFontFamily(liveViewDetailTitleFontCombo_),
            liveViewDetailTitleSizeSpin_->value(),
            "CAM-01: DRYER"
        );
        liveViewCardPreviewTitleLabel_->setStyleSheet(QString("color: %1; background: transparent;").arg(titleColor.name()));

        liveViewCardPreviewMetaLabel_->setText("Dryer Section | Operator Side | Mono8");
        liveViewCardPreviewMetaLabel_->setStyleSheet(QString("color: %1; background: transparent; font-size: 10px;").arg(subtitleColor.name()));

        updateFontPreviewLabel(
            liveViewCardPreviewStatusLabel_,
            currentCuratedFontFamily(liveViewDetailTitleFontCombo_),
            std::max(11, liveViewDetailTitleSizeSpin_->value() - 3),
            "Connected"
        );
        liveViewCardPreviewStatusLabel_->setStyleSheet(QString(
            "color: %1; background-color: %2; border-radius: 10px; padding: 4px 10px;"
        ).arg(statusTextColor.name(), statusBackgroundColor.name(QColor::HexArgb)));

        QFont sectionPreviewFont(currentCuratedFontFamily(liveViewDetailSectionFontCombo_));
        sectionPreviewFont.setPixelSize(liveViewDetailSectionSizeSpin_->value());
        sectionPreviewFont.setBold(true);
        const QString groupStyle = QString(
            "QGroupBox { color: %1; border: 1px solid %2; border-radius: 8px; margin-top: 10px; padding-top: 10px; background-color: %4; } "
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; } "
            "QLabel { color: %3; background: transparent; }"
        ).arg(groupTextColor.name(), groupBorderColor.name(), subtitleColor.name(), detailPanelBackground.name(QColor::HexArgb));
        if (liveViewCardPreviewInfoGroup_) {
            liveViewCardPreviewInfoGroup_->setFont(sectionPreviewFont);
            liveViewCardPreviewInfoGroup_->setStyleSheet(groupStyle);
        }
        if (liveViewCardPreviewControlGroup_) {
            liveViewCardPreviewControlGroup_->setFont(sectionPreviewFont);
            liveViewCardPreviewControlGroup_->setStyleSheet(groupStyle);
        }

        liveViewCardPreviewFrame_->update();
    };

    connect(liveViewGridTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshTypographyPreviews);
    connect(liveViewGridTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshTypographyPreviews);
    connect(liveViewDetailTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshTypographyPreviews);
    connect(liveViewDetailTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshTypographyPreviews);
    connect(liveViewDetailSectionFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshTypographyPreviews);
    connect(liveViewDetailSectionSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshTypographyPreviews);
    connect(liveViewBackgroundStyleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshTypographyPreviews);
    connect(this, &ConfigDialog::themeSelectionChanged, this, refreshTypographyPreviews);

    auto refreshAnalysisPreview = [this]() {
        if (!analysisPreviewVideoWidget_ || !analysisPreviewFrame_ || !analysisPreviewSectionLabel_ ||
            !analysisPreviewTabLabel_ || !analysisPreviewFrameLabel_) {
            return;
        }

        const int selectedThemePreset = selectedThemeIndex_;
        const ThemeColors previewThemeColors = CameraConfig::getThemeColors(selectedThemePreset);
        AnalysisViewStyle style = CameraConfig::getDefaultAnalysisViewStyle();
        style.videoTitleFontFamily = currentCuratedFontFamily(analysisVideoTitleFontCombo_);
        style.videoTitleFontSize = analysisVideoTitleSizeSpin_->value();
        style.timestampFontFamily = currentCuratedFontFamily(analysisTimestampFontCombo_);
        style.timestampFontSize = analysisTimestampSizeSpin_->value();
        style.tabFontFamily = currentCuratedFontFamily(analysisTabFontCombo_);
        style.tabFontSize = analysisTabSizeSpin_->value();
        style.playbackSurfaceStyle = analysisPlaybackSurfaceCombo_->currentData().toString();

        const QColor borderColor(previewThemeColors.border);
        const QColor textColor(previewThemeColors.text);
        const QColor primaryColor(previewThemeColors.primary);
        const QColor panelColor(previewThemeColors.btnBg);
        const QColor playbackSurface = style.playbackSurfaceStyle == "light" ? QColor("#F2F2F2") : QColor("#000000");

        analysisPreviewVideoWidget_->setPreviewThemeColors(previewThemeColors);
        analysisPreviewVideoWidget_->setPreviewStyle(style);
        analysisPreviewVideoWidget_->setTimestamp("00:00:12.4");

        QPalette analysisPalette = analysisPreviewFrame_->palette();
        analysisPalette.setColor(QPalette::Window, panelColor);
        analysisPreviewFrame_->setAutoFillBackground(true);
        analysisPreviewFrame_->setPalette(analysisPalette);
        analysisPreviewFrame_->setStyleSheet(QString(
            "QFrame#analysisPreviewFrame { border: 1px solid %1; border-radius: 12px; }"
        ).arg(borderColor.name()));

        QFont sectionFont(style.tabFontFamily);
        sectionFont.setPixelSize(std::max(11, style.tabFontSize));
        sectionFont.setBold(true);
        analysisPreviewSectionLabel_->setFont(sectionFont);
        analysisPreviewSectionLabel_->setStyleSheet(QString("color: %1; background: transparent;").arg(textColor.name()));

        analysisPreviewTabLabel_->setFont(sectionFont);
        analysisPreviewTabLabel_->setStyleSheet(QString(
            "color: %1; background-color: %2; border: 1px solid %3; border-radius: 8px; padding: 6px 12px;"
        ).arg(textColor.name(), panelColor.name(), primaryColor.name()));

        QFont frameFont(style.timestampFontFamily);
        frameFont.setPixelSize(style.timestampFontSize);
        analysisPreviewFrameLabel_->setFont(frameFont);
        analysisPreviewFrameLabel_->setStyleSheet(QString(
            "color: %1; background-color: %2; border: 1px solid %3; border-radius: 6px; padding: 4px 8px;"
        ).arg(textColor.name(), playbackSurface.name(), borderColor.name()));
    };

    connect(analysisVideoTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshAnalysisPreview);
    connect(analysisVideoTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshAnalysisPreview);
    connect(analysisTimestampFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshAnalysisPreview);
    connect(analysisTimestampSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshAnalysisPreview);
    connect(analysisTabFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshAnalysisPreview);
    connect(analysisTabSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, refreshAnalysisPreview);
    connect(analysisPlaybackSurfaceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, refreshAnalysisPreview);
    connect(this, &ConfigDialog::themeSelectionChanged, this, refreshAnalysisPreview);

    QFrame* livePreviewContainer = new QFrame(liveViewGroup);
    livePreviewContainer_ = livePreviewContainer;
    livePreviewContainer->setFrameShape(QFrame::NoFrame);
    livePreviewContainer->setObjectName("livePreviewContainer");
    livePreviewContainer->setStyleSheet(QString(
        "QFrame#livePreviewContainer { background-color: %1; border: 1px solid %2; border-radius: 12px; }"
    ).arg(tc.btnBg, tc.border));
    livePreviewContainer->setMinimumWidth(360);
    livePreviewContainer->setMaximumWidth(390);
    livePreviewContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    QVBoxLayout* livePreviewPageLayout = new QVBoxLayout(livePreviewContainer);
    livePreviewPageLayout->setContentsMargins(10, 8, 10, 8);
    livePreviewPageLayout->setSpacing(8);

    QLabel* previewTitle = new QLabel("Detail Card Preview", livePreviewContainer);
    previewTitle->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: 700; background: transparent; border: none;").arg(tc.primary));
    livePreviewPageLayout->addWidget(previewTitle);

    QLabel* previewNote = new QLabel("The preview uses the current detail title and section styling with representative live card data.", livePreviewContainer);
    previewNote->setWordWrap(true);
    previewNote->setStyleSheet(QString("color: %1; font-size: 10px; background: transparent; border: none;").arg(tc.text));
    livePreviewPageLayout->addWidget(previewNote);

    liveViewCardPreviewFrame_ = new QFrame(livePreviewContainer);
    liveViewCardPreviewFrame_->setFrameShape(QFrame::NoFrame);
    liveViewCardPreviewFrame_->setObjectName("liveViewCardPreview");
    liveViewCardPreviewFrame_->setMinimumWidth(0);
    liveViewCardPreviewFrame_->setMaximumWidth(QWIDGETSIZE_MAX);
    liveViewCardPreviewFrame_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    QVBoxLayout* liveViewCardPreviewLayout = new QVBoxLayout(liveViewCardPreviewFrame_);
    liveViewCardPreviewLayout->setContentsMargins(12, 12, 12, 12);
    liveViewCardPreviewLayout->setSpacing(10);

    liveViewDetailPreviewWidget_ = new CameraWidget(liveViewCardPreviewFrame_);
    liveViewDetailPreviewWidget_->setCameraId(0);
    liveViewDetailPreviewWidget_->setFixedHeight(212);
    liveViewDetailPreviewWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    liveViewCardPreviewLayout->addWidget(liveViewDetailPreviewWidget_);

    QHBoxLayout* liveViewCardPreviewHeaderLayout = new QHBoxLayout();
    liveViewCardPreviewHeaderLayout->setContentsMargins(0, 0, 0, 0);
    liveViewCardPreviewHeaderLayout->setSpacing(8);

    QVBoxLayout* liveViewCardPreviewTitleLayout = new QVBoxLayout();
    liveViewCardPreviewTitleLayout->setContentsMargins(0, 0, 0, 0);
    liveViewCardPreviewTitleLayout->setSpacing(2);

    liveViewCardPreviewTitleLabel_ = new QLabel(liveViewCardPreviewFrame_);
    liveViewCardPreviewTitleLabel_->setWordWrap(true);
    liveViewCardPreviewTitleLabel_->setMinimumHeight(18);
    liveViewCardPreviewTitleLayout->addWidget(liveViewCardPreviewTitleLabel_);

    liveViewCardPreviewMetaLabel_ = new QLabel(liveViewCardPreviewFrame_);
    liveViewCardPreviewMetaLabel_->setWordWrap(true);
    liveViewCardPreviewMetaLabel_->setMinimumHeight(14);
    liveViewCardPreviewTitleLayout->addWidget(liveViewCardPreviewMetaLabel_);

    liveViewCardPreviewHeaderLayout->addLayout(liveViewCardPreviewTitleLayout, 1);

    liveViewCardPreviewStatusLabel_ = new QLabel(liveViewCardPreviewFrame_);
    liveViewCardPreviewStatusLabel_->setAlignment(Qt::AlignCenter);
    liveViewCardPreviewStatusLabel_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    liveViewCardPreviewStatusLabel_->setMinimumWidth(96);
    liveViewCardPreviewHeaderLayout->addWidget(liveViewCardPreviewStatusLabel_, 0, Qt::AlignTop);

    liveViewCardPreviewLayout->addLayout(liveViewCardPreviewHeaderLayout);

    liveViewCardPreviewInfoGroup_ = new QGroupBox("Section Header", liveViewCardPreviewFrame_);
    QVBoxLayout* infoGroupLayout = new QVBoxLayout(liveViewCardPreviewInfoGroup_);
    infoGroupLayout->setContentsMargins(8, 12, 8, 8);
    infoGroupLayout->setSpacing(2);
    infoGroupLayout->addWidget(new QLabel("Sample value text", liveViewCardPreviewInfoGroup_));
    infoGroupLayout->addWidget(new QLabel("Secondary line", liveViewCardPreviewInfoGroup_));
    liveViewCardPreviewLayout->addWidget(liveViewCardPreviewInfoGroup_);

    liveViewCardPreviewControlGroup_ = new QGroupBox("Camera Parameters", liveViewCardPreviewFrame_);
    QVBoxLayout* controlGroupLayout = new QVBoxLayout(liveViewCardPreviewControlGroup_);
    controlGroupLayout->setContentsMargins(8, 12, 8, 8);
    controlGroupLayout->setSpacing(2);
    controlGroupLayout->addWidget(new QLabel("Exposure: 40880 us", liveViewCardPreviewControlGroup_));
    controlGroupLayout->addWidget(new QLabel("Pixel Format: Mono8", liveViewCardPreviewControlGroup_));
    liveViewCardPreviewLayout->addWidget(liveViewCardPreviewControlGroup_);

    livePreviewPageLayout->addWidget(liveViewCardPreviewFrame_, 0, Qt::AlignTop);

    liveViewContentLayout_->addWidget(livePreviewContainer, 3, Qt::AlignTop);

    QFrame* analysisPreviewContainer = new QFrame(analysisViewGroup);
    analysisPreviewContainer_ = analysisPreviewContainer;
    analysisPreviewContainer->setFrameShape(QFrame::NoFrame);
    analysisPreviewContainer->setObjectName("analysisPreviewContainer");
    analysisPreviewContainer->setStyleSheet(QString(
        "QFrame#analysisPreviewContainer { background-color: %1; border: 1px solid %2; border-radius: 12px; }"
    ).arg(tc.btnBg, tc.border));
    analysisPreviewContainer->setMinimumWidth(360);
    analysisPreviewContainer->setMaximumWidth(390);
    analysisPreviewContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    QVBoxLayout* analysisPreviewPageLayout = new QVBoxLayout(analysisPreviewContainer);
    analysisPreviewPageLayout->setContentsMargins(10, 8, 10, 8);
    analysisPreviewPageLayout->setSpacing(8);

    QLabel* analysisPreviewTitle = new QLabel("Analysis Preview", analysisPreviewContainer);
    analysisPreviewTitle->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: 700; background: transparent; border: none;").arg(tc.primary));
    analysisPreviewPageLayout->addWidget(analysisPreviewTitle);

    QLabel* analysisPreviewNote = new QLabel("The preview uses the current title and timestamp styling with representative analysis card data.", analysisPreviewContainer);
    analysisPreviewNote->setWordWrap(true);
    analysisPreviewNote->setStyleSheet(QString("color: %1; font-size: 10px; background: transparent; border: none;").arg(tc.text));
    analysisPreviewPageLayout->addWidget(analysisPreviewNote);

    // Two-row layout: header row + video row
    analysisPreviewFrame_ = new QFrame(analysisPreviewContainer);
    analysisPreviewFrame_->setFrameShape(QFrame::NoFrame);
    analysisPreviewFrame_->setObjectName("analysisPreviewFrame");
    analysisPreviewFrame_->setMinimumWidth(0);
    analysisPreviewFrame_->setMaximumWidth(QWIDGETSIZE_MAX);
    analysisPreviewFrame_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    QVBoxLayout* analysisPreviewLayout = new QVBoxLayout(analysisPreviewFrame_);
    analysisPreviewLayout->setContentsMargins(12, 12, 12, 12);
    analysisPreviewLayout->setSpacing(10);

    QHBoxLayout* analysisPreviewHeaderLayout = new QHBoxLayout();
    analysisPreviewHeaderLayout->setContentsMargins(0, 0, 0, 0);
    analysisPreviewHeaderLayout->setSpacing(8);
    analysisPreviewSectionLabel_ = new QLabel("Paper Break Log", analysisPreviewFrame_);
    analysisPreviewHeaderLayout->addWidget(analysisPreviewSectionLabel_, 1);
    analysisPreviewTabLabel_ = new QLabel("All Camera", analysisPreviewFrame_);
    analysisPreviewTabLabel_->setAlignment(Qt::AlignCenter);
    analysisPreviewHeaderLayout->addWidget(analysisPreviewTabLabel_, 0);
    analysisPreviewLayout->addLayout(analysisPreviewHeaderLayout);

    analysisPreviewVideoWidget_ = new AnalysisVideoWidget(0, "CAM-01: DRYER", analysisPreviewFrame_);
    analysisPreviewVideoWidget_->setFixedHeight(152);
    analysisPreviewVideoWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    analysisPreviewLayout->addWidget(analysisPreviewVideoWidget_);

    analysisPreviewFrameLabel_ = new QLabel("Frame: 12.4", analysisPreviewFrame_);
    analysisPreviewLayout->addWidget(analysisPreviewFrameLabel_, 0, Qt::AlignLeft);

    analysisPreviewPageLayout->addWidget(analysisPreviewFrame_);
    analysisViewContentLayout_->addWidget(analysisPreviewContainer, 3, Qt::AlignTop);

    uiDetailTabs->addTab(liveViewTab, "Live View");
    uiDetailTabs->addTab(analysisViewTab, "Analysis View");

    refreshTypographyPreviews();
    refreshAnalysisPreview();

    uiPageLayout->addWidget(uiPanel, 1);

    QFrame* uiActionsSeparator = new QFrame(uiGroup);
    uiActionsSeparator->setFrameShape(QFrame::HLine);
    uiActionsSeparator->setFrameShadow(QFrame::Plain);
    uiActionsSeparator->setFixedHeight(1);
    uiActionsSeparator->setStyleSheet(QString(
        "background-color: %1; border: none;"
    ).arg(tc.border));
    uiPageLayout->addWidget(uiActionsSeparator);

    QVBoxLayout* uiActionsLayout = new QVBoxLayout();
    uiActionsLayout->setSpacing(6);

    uiUnsavedIndicator_ = new QLabel(uiGroup);
    uiUnsavedIndicator_->setStyleSheet(QString(
        "color: #FFB020; font-size: 11px; font-weight: 600;"
    ));
    uiUnsavedIndicator_->setText("");
    uiUnsavedIndicator_->setToolTip("You have unsaved changes in this section.");
    uiActionsLayout->addWidget(uiUnsavedIndicator_, 0, Qt::AlignRight);

    QHBoxLayout* uiActionButtonsLayout = new QHBoxLayout();
    uiActionButtonsLayout->setSpacing(14);
    uiActionButtonsLayout->addStretch();

    uiApplyBtn_ = new QPushButton("Apply Now", uiGroup);
    uiApplyBtn_->setToolTip("Apply changes without closing this section.");
    uiApplyBtn_->setStyleSheet(secondaryActionButtonStyle);
    uiApplyBtn_->setEnabled(false);
    connect(uiApplyBtn_, &QPushButton::clicked, this, &ConfigDialog::applyUiSettings);
    uiActionButtonsLayout->addWidget(uiApplyBtn_);

    uiSaveBtn_ = new QPushButton("Save", uiGroup);
    uiSaveBtn_->setToolTip("Save all UI preferences and apply changes.");
    uiSaveBtn_->setIcon(IconManager::instance().save(16));
    stylePrimaryActionButton(uiSaveBtn_, tc);
    connect(uiSaveBtn_, &QPushButton::clicked, this, &ConfigDialog::saveUiSettings);
    uiActionButtonsLayout->addWidget(uiSaveBtn_);
    uiActionsLayout->addLayout(uiActionButtonsLayout);
    uiPageLayout->addLayout(uiActionsLayout);

    QListWidgetItem* uiGroupItem = new QListWidgetItem(IconManager::instance().settings(20), "UI Preferences");
    sidebar->addItem(uiGroupItem);
    stackedWidget->addWidget(uiGroup);

    // Diagnostics Tab
    QWidget* diagnosticsGroup = new QWidget(this);
    QVBoxLayout* diagnosticsLayout = new QVBoxLayout(diagnosticsGroup);
    diagnosticsLayout->setSpacing(kSectionSpacing);
    diagnosticsLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);

    QGroupBox* diagLogsGroup = new QGroupBox("Connection Diagnostics", diagnosticsGroup);
    diagLogsGroup->setStyleSheet(QString(
        "QGroupBox { font-weight: 600; color: %1; border: 1px solid %2; "
        "border-radius: 8px; margin-top: 8px; padding-top: 8px; font-size: 12px; } "
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
    ).arg(tc.primary, tc.border));
    
    QVBoxLayout* diagLogsLayout = new QVBoxLayout(diagLogsGroup);
    diagLogsLayout->setSpacing(kControlSpacing);
    diagLogsLayout->setContentsMargins(14, 18, 14, 14);
    
    connectionLogsBrowser_ = new QTextEdit(diagLogsGroup);
    connectionLogsBrowser_->setReadOnly(true);
    connectionLogsBrowser_->setStyleSheet(QString(
        "QTextEdit { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  color: %3; "
        "  font-family: 'SF Mono', Monaco, Consolas, monospace; "
        "  font-size: 12px; "
        "  padding: 8px; "
        "}"
    ).arg(tc.bg, tc.border, tc.text));
    diagLogsLayout->addWidget(connectionLogsBrowser_);
    
    QHBoxLayout* diagBtnsLayout = new QHBoxLayout();
    diagBtnsLayout->setSpacing(kControlSpacing);
    
    QPushButton* diagRefreshBtn = new QPushButton("Refresh Network", diagLogsGroup);
    diagRefreshBtn->setIcon(IconManager::instance().refresh(16));
    diagRefreshBtn->setStyleSheet(QString(
        "QPushButton { "
        "  background-color: %1; "
        "  color: %2; "
        "  border: none; "
        "  border-radius: 6px; "
        "  padding: 6px 12px; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { background-color: %3; }"
    ).arg(tc.primary, tc.bg, tc.btnHover));
    connect(diagRefreshBtn, &QPushButton::clicked, this, &ConfigDialog::onRefreshLogsClicked);
    
    QPushButton* diagClearBtn = new QPushButton("Clear Logs", diagLogsGroup);
    diagClearBtn->setIcon(IconManager::instance().trash(16));
    diagClearBtn->setStyleSheet(QString(
        "QPushButton { "
        "  background-color: transparent; "
        "  color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 6px; "
        "  padding: 6px 12px; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { background-color: rgba(255, 90, 90, 0.1); border-color: #FF5A5A; color: #FF5A5A; }"
    ).arg(tc.text, tc.border));
    connect(diagClearBtn, &QPushButton::clicked, this, &ConfigDialog::onClearLogsClicked);
    
    diagBtnsLayout->addWidget(diagRefreshBtn);
    diagBtnsLayout->addWidget(diagClearBtn);
    diagBtnsLayout->addStretch();
    diagLogsLayout->addLayout(diagBtnsLayout);
    
    diagnosticsLayout->addWidget(diagLogsGroup);
    
    QListWidgetItem* diagnosticsItem = new QListWidgetItem(IconManager::instance().info(20), "Diagnostics");
    sidebar->addItem(diagnosticsItem);
    stackedWidget->addWidget(diagnosticsGroup);

    int widestSidebarLabel = 0;
    for (int i = 0; i < sidebar->count(); ++i) {
        const QListWidgetItem* item = sidebar->item(i);
        widestSidebarLabel = std::max(widestSidebarLabel, sidebar->fontMetrics().horizontalAdvance(item->text()));
    }
    sidebar->setFixedWidth(std::max(kSidebarMinWidth, widestSidebarLabel + kSidebarContentPadding));

    mainLayout->addLayout(contentLayout, 1);
    
    connect(sidebar, &QListWidget::currentRowChanged, stackedWidget, &QStackedWidget::setCurrentIndex);
    connect(sidebar, &QListWidget::currentRowChanged, this, [this]() {
        relayoutUiPreferencePanels();
    });
    sidebar->setCurrentRow(0);

    // Initialize logs
    onRefreshLogsClicked();
}

void ConfigDialog::loadSettings() {
    // Load camera configurations and create cards
    std::vector<CameraInfo> cameras = CameraConfig::getCameras();
    for (const auto& cam : cameras) {
        createCameraWidgetBlock(cam);
    }

    // Load global settings
    globalFpsSpin_->setValue(CameraConfig::getFps());
    preTriggerSpin_->setValue(CameraConfig::getPreTriggerSeconds());
    postTriggerSpin_->setValue(CameraConfig::getPostTriggerSeconds());
    eventRetentionSpin_->setValue(CameraConfig::getEventRetentionCount());
    eventStoragePathEdit_->setText(CameraConfig::getEventStoragePath());
    selectedThemeIndex_ = CameraConfig::getThemePreset();
    const int savedThemeIdx = selectedThemeIndex_;
    if (themeCombo_) {
        themeCombo_->setCurrentIndex(savedThemeIdx);
    }
    for (int i = 0; i < 8; ++i) {
        if (themeCards_[i]) {
            const ThemeColors c = CameraConfig::getThemeColors(i);
            if (i == savedThemeIdx) {
                themeCards_[i]->setStyleSheet(QString(
                    "QPushButton { background-color: rgba(255, 255, 255, 0.06); border: 2px solid %1; border-radius: 10px; padding: 0px; }"
                ).arg(c.primary));
            } else {
                themeCards_[i]->setStyleSheet(QString(
                    "QPushButton { background-color: rgba(255, 255, 255, 0.02); border: 1px solid %1; border-radius: 10px; padding: 0px; }"
                    "QPushButton:hover { border: 1.5px solid %2; background-color: rgba(255, 255, 255, 0.05); }"
                ).arg(c.border, c.primary));
            }
        }
    }
    const LiveViewCardStyle liveViewStyle = CameraConfig::getLiveViewCardStyle();
    selectCuratedFont(liveViewGridTitleFontCombo_, liveViewStyle.gridTitleFontFamily);
    liveViewGridTitleSizeSpin_->setValue(liveViewStyle.gridTitleFontSize);
    selectCuratedFont(liveViewDetailTitleFontCombo_, liveViewStyle.detailTitleFontFamily);
    liveViewDetailTitleSizeSpin_->setValue(liveViewStyle.detailTitleFontSize);
    selectCuratedFont(liveViewDetailSectionFontCombo_, liveViewStyle.detailSectionFontFamily);
    liveViewDetailSectionSizeSpin_->setValue(liveViewStyle.detailSectionFontSize);
    const QString backgroundStyle = normalizeLiveViewBackgroundStyle(liveViewStyle.backgroundStyle);

    const int backgroundStyleIndex = liveViewBackgroundStyleCombo_->findData(backgroundStyle);
    if (backgroundStyleIndex != -1) {
        liveViewBackgroundStyleCombo_->setCurrentIndex(backgroundStyleIndex);
    }

    const AnalysisViewStyle analysisStyle = CameraConfig::getAnalysisViewStyle();
    selectCuratedFont(analysisVideoTitleFontCombo_, analysisStyle.videoTitleFontFamily);
    analysisVideoTitleSizeSpin_->setValue(analysisStyle.videoTitleFontSize);
    selectCuratedFont(analysisTimestampFontCombo_, analysisStyle.timestampFontFamily);
    analysisTimestampSizeSpin_->setValue(analysisStyle.timestampFontSize);
    selectCuratedFont(analysisTabFontCombo_, analysisStyle.tabFontFamily);
    analysisTabSizeSpin_->setValue(analysisStyle.tabFontSize);
    const int analysisSurfaceIndex = analysisPlaybackSurfaceCombo_->findData(analysisStyle.playbackSurfaceStyle);
    if (analysisSurfaceIndex != -1) {
        analysisPlaybackSurfaceCombo_->setCurrentIndex(analysisSurfaceIndex);
    }

    // Initial network status update
    refreshNetworkStatus();
}

void ConfigDialog::setupUiModificationTracking() {
    connect(eventStoragePathEdit_, &QLineEdit::textChanged, this, &ConfigDialog::checkUiSettingsModified);
    connect(this, &ConfigDialog::themeSelectionChanged, this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewGridTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewGridTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewDetailTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewDetailTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewDetailSectionFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewDetailSectionSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewBackgroundStyleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisVideoTitleFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisVideoTitleSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisTimestampFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisTimestampSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisTabFontCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisTabSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisPlaybackSurfaceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ConfigDialog::checkUiSettingsModified);
    connect(browseEventStorageBtn_, &QPushButton::clicked, this, &ConfigDialog::checkUiSettingsModified);
    connect(resetEventStorageBtn_, &QPushButton::clicked, this, &ConfigDialog::checkUiSettingsModified);
    connect(liveViewResetBtn_, &QPushButton::clicked, this, &ConfigDialog::checkUiSettingsModified);
    connect(analysisResetBtn_, &QPushButton::clicked, this, &ConfigDialog::checkUiSettingsModified);

    originalValues_ = captureCurrentSettings();
}

ConfigDialog::UiSettingsSnapshot ConfigDialog::captureCurrentSettings() const {
    UiSettingsSnapshot snap;
    snap.eventStoragePath = eventStoragePathEdit_ ? eventStoragePathEdit_->text() : QString();
    snap.themePreset = selectedThemeIndex_;
    snap.liveViewGridTitleFont = liveViewGridTitleFontCombo_ ? currentCuratedFontFamily(liveViewGridTitleFontCombo_) : QString();
    snap.liveViewGridTitleSize = liveViewGridTitleSizeSpin_ ? liveViewGridTitleSizeSpin_->value() : 0;
    snap.liveViewDetailTitleFont = liveViewDetailTitleFontCombo_ ? currentCuratedFontFamily(liveViewDetailTitleFontCombo_) : QString();
    snap.liveViewDetailTitleSize = liveViewDetailTitleSizeSpin_ ? liveViewDetailTitleSizeSpin_->value() : 0;
    snap.liveViewDetailSectionFont = liveViewDetailSectionFontCombo_ ? currentCuratedFontFamily(liveViewDetailSectionFontCombo_) : QString();
    snap.liveViewDetailSectionSize = liveViewDetailSectionSizeSpin_ ? liveViewDetailSectionSizeSpin_->value() : 0;
    snap.liveViewBackgroundStyle = liveViewBackgroundStyleCombo_ ? liveViewBackgroundStyleCombo_->currentData().toString() : QString();
    snap.analysisVideoTitleFont = analysisVideoTitleFontCombo_ ? currentCuratedFontFamily(analysisVideoTitleFontCombo_) : QString();
    snap.analysisVideoTitleSize = analysisVideoTitleSizeSpin_ ? analysisVideoTitleSizeSpin_->value() : 0;
    snap.analysisTimestampFont = analysisTimestampFontCombo_ ? currentCuratedFontFamily(analysisTimestampFontCombo_) : QString();
    snap.analysisTimestampSize = analysisTimestampSizeSpin_ ? analysisTimestampSizeSpin_->value() : 0;
    snap.analysisTabFont = analysisTabFontCombo_ ? currentCuratedFontFamily(analysisTabFontCombo_) : QString();
    snap.analysisTabSize = analysisTabSizeSpin_ ? analysisTabSizeSpin_->value() : 0;
    snap.analysisPlaybackSurface = analysisPlaybackSurfaceCombo_ ? analysisPlaybackSurfaceCombo_->currentData().toString() : QString();
    return snap;
}

void ConfigDialog::checkUiSettingsModified() {
    bool modified = (captureCurrentSettings() != originalValues_);
    if (uiUnsavedIndicator_) {
        uiUnsavedIndicator_->setText(modified ? "Unsaved changes - apply or save" : "");
    }
    if (uiApplyBtn_) {
        uiApplyBtn_->setEnabled(isAdminMode_ && modified);
    }
}

void ConfigDialog::createCameraWidgetBlock(const CameraInfo& cam) {
    CameraCard* card = new CameraCard(cam, cameraScrollWidget_);
    connectCameraCardSignals(card);

    cameraCards_.push_back(card);
    relayoutCameraCards();
}

void ConfigDialog::relayoutCameraCards() {
    if (!cameraListLayout_ || !cameraScrollWidget_) {
        return;
    }

    while (QLayoutItem* item = cameraListLayout_->takeAt(0)) {
        if (item->widget()) {
            item->widget()->setParent(cameraScrollWidget_);
        }
        delete item;
    }

    const int availableWidth = cameraScrollArea_ ? cameraScrollArea_->viewport()->width() : cameraScrollWidget_->width();

    int columnCount = 1;
    int columnMinWidth = 720;

    if (availableWidth >= 1400) {
        columnCount = 3;
        columnMinWidth = 420;
    } else if (availableWidth >= 980) {
        columnCount = 2;
        columnMinWidth = 0;
    }

    for (int i = 0; i < columnCount; ++i) {
        cameraListLayout_->setColumnStretch(i, 1);
        cameraListLayout_->setColumnMinimumWidth(i, columnMinWidth);
    }

    for (int index = columnCount; index < 4; ++index) {
        cameraListLayout_->setColumnStretch(index, 0);
        cameraListLayout_->setColumnMinimumWidth(index, 0);
    }

    for (int index = 0; index < static_cast<int>(cameraCards_.size()); ++index) {
        CameraCard* card = cameraCards_[index];
        const int row = index / columnCount;
        const int column = index % columnCount;
        cameraListLayout_->addWidget(card, row, column, Qt::AlignTop);
    }
}

void ConfigDialog::relayoutUiPreferencePanels() {
    if (!isVisible()) {
        return;
    }

    const int availableWidth = uiPreferencesScrollArea_ && uiPreferencesScrollArea_->viewport()
        ? uiPreferencesScrollArea_->viewport()->width()
        : width();
    if (availableWidth <= 0) {
        return;
    }
    const bool stackLivePreview = availableWidth < 1040;
    const bool stackAnalysisPreview = availableWidth < 1040;
    const int themeGridWidth = themeGridWidget_ ? themeGridWidget_->width() : availableWidth;

    if (liveViewContentLayout_) {
        liveViewContentLayout_->setDirection(stackLivePreview ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        liveViewContentLayout_->setSpacing(stackLivePreview ? 12 : 16);
    }

    if (analysisViewContentLayout_) {
        analysisViewContentLayout_->setDirection(stackAnalysisPreview ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        analysisViewContentLayout_->setSpacing(stackAnalysisPreview ? 12 : 16);
    }

    const int livePreviewMinWidth = stackLivePreview ? 0 : 360;
    const int livePreviewMaxWidth = stackLivePreview ? QWIDGETSIZE_MAX : 390;
    const int analysisPreviewMinWidth = stackAnalysisPreview ? 0 : 360;
    const int analysisPreviewMaxWidth = stackAnalysisPreview ? QWIDGETSIZE_MAX : 390;
    const QSizePolicy::Policy livePreviewHorizontalPolicy = stackLivePreview ? QSizePolicy::Expanding : QSizePolicy::Preferred;
    const QSizePolicy::Policy analysisPreviewHorizontalPolicy = stackAnalysisPreview ? QSizePolicy::Expanding : QSizePolicy::Preferred;

    if (livePreviewContainer_) {
        livePreviewContainer_->setMinimumWidth(livePreviewMinWidth);
        livePreviewContainer_->setMaximumWidth(livePreviewMaxWidth);
        livePreviewContainer_->setSizePolicy(livePreviewHorizontalPolicy, QSizePolicy::Maximum);
    }

    if (liveViewCardPreviewFrame_) {
        liveViewCardPreviewFrame_->setMinimumWidth(livePreviewMinWidth);
        liveViewCardPreviewFrame_->setMaximumWidth(livePreviewMaxWidth);
    }

    if (analysisPreviewContainer_) {
        analysisPreviewContainer_->setMinimumWidth(analysisPreviewMinWidth);
        analysisPreviewContainer_->setMaximumWidth(analysisPreviewMaxWidth);
        analysisPreviewContainer_->setSizePolicy(analysisPreviewHorizontalPolicy, QSizePolicy::Maximum);
    }

    if (themeGridLayout_) {
        while (QLayoutItem* item = themeGridLayout_->takeAt(0)) {
            delete item;
        }

        int columnCount = 4;
        if (themeGridWidth < 300) {
            columnCount = 2;
        } else if (themeGridWidth < 450) {
            columnCount = 3;
        }

        for (int i = 0; i < columnCount; ++i) {
            themeGridLayout_->setColumnStretch(i, 1);
            themeGridLayout_->setColumnMinimumWidth(i, 0);
        }

        for (int i = columnCount; i < 4; ++i) {
            themeGridLayout_->setColumnStretch(i, 0);
            themeGridLayout_->setColumnMinimumWidth(i, 0);
        }

        for (int index = 0; index < 8; ++index) {
            QPushButton* card = qobject_cast<QPushButton*>(themeCards_[index]);
            if (!card) {
                continue;
            }

            const int row = index / columnCount;
            const int column = index % columnCount;
            themeGridLayout_->addWidget(card, row, column);
        }
    }

    updateGeometry();
}

void ConfigDialog::connectCameraCardSignals(CameraCard* card) {
    connect(card, &CameraCard::editToggled, this, &ConfigDialog::onCameraCardEditToggled);
    connect(card, &CameraCard::removeClicked, this, &ConfigDialog::onCameraCardRemoveClicked);
    connect(card, &CameraCard::sourceChanged, this, &ConfigDialog::onCameraCardSourceChanged);
    connect(card, &CameraCard::macChanged, this, &ConfigDialog::onCameraCardMacChanged);
    connect(card, &CameraCard::writeIpClicked, this, &ConfigDialog::onCameraCardWriteIpClicked);
    connect(card, &CameraCard::deviceSettingsClicked, this, &ConfigDialog::onCameraCardDeviceSettingsClicked);
}

CameraCard* ConfigDialog::findCameraCard(int cameraId) const {
    for (auto* card : cameraCards_) {
        if (card->cameraId() == cameraId) {
            return card;
        }
    }
    return nullptr;
}

CameraCard* ConfigDialog::findCameraCard(QObject* sender) const {
    for (auto* card : cameraCards_) {
        if (card == sender || card->findChild<QObject*>(sender->objectName()) == sender) {
            return card;
        }
    }
    return qobject_cast<CameraCard*>(sender);
}

void ConfigDialog::onCameraCardEditToggled(bool checked) {
    if (CameraCard* card = findCameraCard(sender())) {
        card->setEditable(checked && isAdminMode_);
    }
}

void ConfigDialog::onCameraCardRemoveClicked() {
    CameraCard* card = findCameraCard(sender());
    if (!card) return;

    // Show premium delete confirmation dialog
    DeleteConfirmationDialog dialog(
        QString("Camera %1: %2").arg(card->cameraId()).arg(card->name()),
        this
    );

    if (dialog.exec() == QDialog::Accepted) {
        // Remove from vector
        cameraCards_.erase(std::remove(cameraCards_.begin(), cameraCards_.end(), card),
                          cameraCards_.end());

        // Remove from UI
        cameraListLayout_->removeWidget(card);
        delete card;

        relayoutCameraCards();

        // Update network status
        refreshNetworkStatus();
    }
}

void ConfigDialog::onCameraCardSourceChanged(int) {
    refreshNetworkStatus();
}

void ConfigDialog::onCameraCardMacChanged(const QString&) {
    QSet<QString> reservedMacs;
    for (auto* card : cameraCards_) {
        const QString configuredMac = normalizeMac(card->macAddress());
        if (!configuredMac.isEmpty() && configuredMac != "NONE/AUTO") {
            reservedMacs.insert(configuredMac);
        }
    }

    for (auto* card : cameraCards_) {
        card->updateMacCombo(currentGigEDevices_, card->macAddress(), reservedMacs);
    }

    refreshNetworkStatus();
}

void ConfigDialog::onCameraCardWriteIpClicked() {
    CameraCard* card = findCameraCard(sender());
    if (!card) return;

    // Write IP logic (similar to original)
    if (card->sourceType() != 1) {
        QMessageBox::warning(this, "Write IP", "IP writing is only available for cameras configured as Real.");
        return;
    }

    QString mac = card->macAddress();
    QString ip = card->ipAddress();
    QString mask = card->subnetMask();
    QString gw = card->gateway();
    const QString normalizedMac = normalizeMac(mac);

    if (normalizedMac.isEmpty()) {
        QMessageBox::warning(this, "Write IP", "Please select or enter a valid MAC Address first.");
        return;
    }

    bool macVisible = false;
    for (const auto& dev : currentGigEDevices_) {
        if (normalizeMac(QString::fromStdString(dev.macAddress)) == normalizedMac) {
            macVisible = true;
            break;
        }
    }

    if (!macVisible) {
        QMessageBox::warning(this, "Write IP",
            "The selected MAC is not currently visible in GigE discovery. "
            "Refresh discovery and verify the physical camera is connected before writing its IP.");
        return;
    }

    persistCameraNetworkSelection(
        card->cameraId(),
        card->sourceType(),
        ip, mac, mask, gw
    );

    bool wasRunning = false;
    if (cameraManager_) {
        cameraManager_->stopAcquisition();
        wasRunning = true;
    }

    bool writeOk = CameraManager::applyIpConfiguration(
        normalizedMac.toStdString(),
        ip.toStdString(),
        mask.toStdString(),
        gw.toStdString()
    );

    if (!writeOk) {
        QMessageBox::critical(this, "Write IP",
            "Failed to write IP configuration to camera " + mac + ".\n"
            "Please check connection and MAC address.");
        if (wasRunning && cameraManager_) {
            cameraManager_->startAcquisition();
        }
        return;
    }

    onRefreshLogsClicked();
    refreshNetworkStatus();

    // Check if IP matches
    QString detectedIp = "Offline";
    for (const auto& dev : currentGigEDevices_) {
        if (normalizeMac(QString::fromStdString(dev.macAddress)) == normalizeMac(mac)) {
            detectedIp = QString::fromStdString(dev.ipAddress);
            break;
        }
    }

    if (normalizeIp(detectedIp) == normalizeIp(ip)) {
        QMessageBox::information(this, "Write IP",
            "Camera " + mac + " is now detected at " + ip + ".");
    } else {
        QMessageBox::warning(this, "Write IP",
            "IP write sent to camera " + mac + ", but it has not been rediscovered at " + ip + " yet.\n"
            "Refresh after reconnecting the camera if needed.");
    }

    if (wasRunning && cameraManager_) {
        cameraManager_->startAcquisition();
    }
}

void ConfigDialog::onAddCameraConfigClicked() {
    CameraInfo cam;

    int maxId = 0;
    for (auto* card : cameraCards_) {
        if (card->cameraId() > maxId) maxId = card->cameraId();
    }
    cam.id = maxId + 1;
    cam.source = 0;
    cam.name = QString("DRYER %1").arg(cam.id);
    cam.location = QString("CYLINDER %1").arg(10 + cam.id);
    cam.side = "DRIVE SIDE";
    cam.machinePosition = 16000 + (cam.id * 500);
    cam.ipAddress = QString("172.20.2.%1").arg(cam.id);
    cam.macAddress = "";
    cam.subnetMask = "255.255.255.0";
    cam.defaultGateway = "0.0.0.0";
    cam.fps = 50;
    cam.enableAcquisitionFps = false;
    cam.width = 780;
    cam.height = 580;
    cam.offsetX = 0;
    cam.offsetY = 0;
    cam.pixelFormat = "Mono8";
    cam.exposureTimeAbs = 40880.0;
    cam.enableExposureTimeBase = false;
    cam.exposureTimeBaseAbs = 20.0;
    cam.exposureTimeRaw = 2044;
    cam.chunkModeActive = false;
    cam.enabledChunks = QStringList() << "Timestamp" << "Framecounter";
    cam.temperature = 0.0;

    createCameraWidgetBlock(cam);

    // Scroll to the new card
    cameraScrollArea_->ensureWidgetVisible(cameraCards_.back());
}

void ConfigDialog::onCameraCardDeviceSettingsClicked() {
    CameraCard* card = findCameraCard(sender());
    if (!card) return;

    const auto it = std::find(cameraCards_.begin(), cameraCards_.end(), card);
    const int cameraIndex = it == cameraCards_.end()
        ? 0
        : static_cast<int>(std::distance(cameraCards_.begin(), it));

    CameraDeviceSettingsDialog dialog(cameraIndex, card->cameraInfo(), cameraManager_, isAdminMode_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    card->setCameraInfo(dialog.updatedInfo());
}

void ConfigDialog::onRemoveCameraConfigClicked() {
    // Handled by CameraCard signals
}

bool ConfigDialog::validateAndPrepareEventStorage(QString* normalizedPath, QString* errorMessage) const {
    const QString eventStoragePath = QDir::cleanPath(eventStoragePathEdit_->text().trimmed());
    if (eventStoragePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = "Please select a valid event storage folder.";
        }
        return false;
    }

    QDir storageDir(eventStoragePath);
    if (!storageDir.exists() && !QDir().mkpath(eventStoragePath)) {
        if (errorMessage) {
            *errorMessage = QString("Unable to create event storage folder:\n%1").arg(eventStoragePath);
        }
        return false;
    }

    QFileInfo storageInfo(eventStoragePath);
    if (!storageInfo.isDir() || !storageInfo.isWritable()) {
        if (errorMessage) {
            *errorMessage = QString("Event storage folder is not writable:\n%1").arg(eventStoragePath);
        }
        return false;
    }

    if (normalizedPath) {
        *normalizedPath = eventStoragePath;
    }
    return true;
}

void ConfigDialog::emitConfigUpdated(bool requiresCameraRestart) {
    QMetaObject::invokeMethod(this, [this, requiresCameraRestart]() {
        qInfo() << "[ConfigDialog] Emitting configUpdated" << requiresCameraRestart;
        emit configUpdated(requiresCameraRestart);
    }, Qt::QueuedConnection);
}

void ConfigDialog::saveCameraConfiguration() {
    qInfo() << "[ConfigDialog] Camera save requested. cardCount=" << cameraCards_.size();

    QStringList validationErrors;
    if (!validateConfiguration(&validationErrors)) {
        qWarning() << "[ConfigDialog] Validation failed:" << validationErrors;
        QMessageBox::warning(this, "Invalid Camera Configuration", validationErrors.join("\n"));
        return;
    }

    const std::vector<CameraInfo> previousCameras = CameraConfig::getCameras();
    std::vector<CameraInfo> newCameras;
    for (auto* card : cameraCards_) {
        CameraInfo cam = card->cameraInfo();
        cam.id = card->cameraId();
        cam.source = card->sourceType();
        cam.name = card->name();
        cam.location = card->location();
        cam.side = card->side();
        cam.machinePosition = card->position();
        cam.ipAddress = card->ipAddress();
        cam.macAddress = normalizeMac(card->macAddress());
        if (cam.macAddress.isEmpty()) cam.macAddress = "";
        cam.subnetMask = card->subnetMask();
        cam.defaultGateway = card->gateway();
        cam.temperature = 0.0;
        newCameras.push_back(cam);
    }

    CameraConfig::saveCameras(newCameras);

    const bool requiresCameraRestart = !cameraConfigListEqual(previousCameras, newCameras);
    qInfo() << "[ConfigDialog] Camera save complete. requiresCameraRestart=" << requiresCameraRestart;
    QMessageBox::information(
        this,
        "Camera Configuration Saved",
        requiresCameraRestart
            ? "Camera configuration saved. Acquisition will be restarted to apply the changes."
            : "Camera configuration saved.");
    emitConfigUpdated(requiresCameraRestart);
}

void ConfigDialog::saveRecordingSettings() {
    qInfo() << "[ConfigDialog] Recording save requested";

    CameraConfig::setFps(globalFpsSpin_->value());
    CameraConfig::setPreTriggerSeconds(preTriggerSpin_->value());
    CameraConfig::setPostTriggerSeconds(postTriggerSpin_->value());
    CameraConfig::setEventRetentionCount(eventRetentionSpin_->value());

    QMessageBox::information(this, "Recording Settings Saved", "Recording settings saved.");
    emitConfigUpdated(false);
}

void ConfigDialog::saveUiSettings() {
    qInfo() << "[ConfigDialog] UI save requested";

    QString eventStoragePath;
    QString eventStorageError;
    if (!validateAndPrepareEventStorage(&eventStoragePath, &eventStorageError)) {
        QMessageBox::warning(this, "Invalid Event Storage", eventStorageError);
        return;
    }

    CameraConfig::setEventStoragePath(eventStoragePath);
    CameraConfig::setThemePreset(selectedThemeIndex_);
    CameraConfig::setLiveViewCardStyle({
        currentCuratedFontFamily(liveViewGridTitleFontCombo_),
        liveViewGridTitleSizeSpin_->value(),
        currentCuratedFontFamily(liveViewDetailTitleFontCombo_),
        liveViewDetailTitleSizeSpin_->value(),
        currentCuratedFontFamily(liveViewDetailSectionFontCombo_),
        liveViewDetailSectionSizeSpin_->value(),
        liveViewBackgroundStyleCombo_->currentData().toString()
    });
    CameraConfig::setAnalysisViewStyle({
        currentCuratedFontFamily(analysisVideoTitleFontCombo_),
        analysisVideoTitleSizeSpin_->value(),
        currentCuratedFontFamily(analysisTimestampFontCombo_),
        analysisTimestampSizeSpin_->value(),
        currentCuratedFontFamily(analysisTabFontCombo_),
        analysisTabSizeSpin_->value(),
        analysisPlaybackSurfaceCombo_->currentData().toString()
    });

    QMessageBox::information(this, "UI Preferences Saved", "UI preferences saved.");
    originalValues_ = captureCurrentSettings();
    clearUiSettingsModified();
    emitConfigUpdated(false);
}

void ConfigDialog::applyUiSettings() {
    qInfo() << "[ConfigDialog] UI apply requested";

    QString eventStoragePath;
    QString eventStorageError;
    if (!validateAndPrepareEventStorage(&eventStoragePath, &eventStorageError)) {
        QMessageBox::warning(this, "Invalid Event Storage", eventStorageError);
        return;
    }

    CameraConfig::setEventStoragePath(eventStoragePath);
    CameraConfig::setThemePreset(selectedThemeIndex_);
    CameraConfig::setLiveViewCardStyle({
        currentCuratedFontFamily(liveViewGridTitleFontCombo_),
        liveViewGridTitleSizeSpin_->value(),
        currentCuratedFontFamily(liveViewDetailTitleFontCombo_),
        liveViewDetailTitleSizeSpin_->value(),
        currentCuratedFontFamily(liveViewDetailSectionFontCombo_),
        liveViewDetailSectionSizeSpin_->value(),
        liveViewBackgroundStyleCombo_->currentData().toString()
    });
    CameraConfig::setAnalysisViewStyle({
        currentCuratedFontFamily(analysisVideoTitleFontCombo_),
        analysisVideoTitleSizeSpin_->value(),
        currentCuratedFontFamily(analysisTimestampFontCombo_),
        analysisTimestampSizeSpin_->value(),
        currentCuratedFontFamily(analysisTabFontCombo_),
        analysisTabSizeSpin_->value(),
        analysisPlaybackSurfaceCombo_->currentData().toString()
    });

    originalValues_ = captureCurrentSettings();
    clearUiSettingsModified();
    emitConfigUpdated(false);
}

void ConfigDialog::clearUiSettingsModified() {
    if (uiUnsavedIndicator_) {
        uiUnsavedIndicator_->setText("");
    }
    if (uiApplyBtn_) {
        uiApplyBtn_->setEnabled(false);
    }
}

void ConfigDialog::resetLiveViewCardSettings() {
    const LiveViewCardStyle defaults = CameraConfig::getDefaultLiveViewCardStyle();
    selectCuratedFont(liveViewGridTitleFontCombo_, defaults.gridTitleFontFamily);
    liveViewGridTitleSizeSpin_->setValue(defaults.gridTitleFontSize);
    selectCuratedFont(liveViewDetailTitleFontCombo_, defaults.detailTitleFontFamily);
    liveViewDetailTitleSizeSpin_->setValue(defaults.detailTitleFontSize);
    selectCuratedFont(liveViewDetailSectionFontCombo_, defaults.detailSectionFontFamily);
    liveViewDetailSectionSizeSpin_->setValue(defaults.detailSectionFontSize);

    const int backgroundStyleIndex = liveViewBackgroundStyleCombo_->findData(normalizeLiveViewBackgroundStyle(defaults.backgroundStyle));
    if (backgroundStyleIndex != -1) {
        liveViewBackgroundStyleCombo_->setCurrentIndex(backgroundStyleIndex);
    }
}

void ConfigDialog::resetAnalysisViewSettings() {
    const AnalysisViewStyle defaults = CameraConfig::getDefaultAnalysisViewStyle();
    selectCuratedFont(analysisVideoTitleFontCombo_, defaults.videoTitleFontFamily);
    analysisVideoTitleSizeSpin_->setValue(defaults.videoTitleFontSize);
    selectCuratedFont(analysisTimestampFontCombo_, defaults.timestampFontFamily);
    analysisTimestampSizeSpin_->setValue(defaults.timestampFontSize);
    selectCuratedFont(analysisTabFontCombo_, defaults.tabFontFamily);
    analysisTabSizeSpin_->setValue(defaults.tabFontSize);

    const int playbackSurfaceIndex = analysisPlaybackSurfaceCombo_->findData(defaults.playbackSurfaceStyle);
    if (playbackSurfaceIndex != -1) {
        analysisPlaybackSurfaceCombo_->setCurrentIndex(playbackSurfaceIndex);
    }
}

void ConfigDialog::setAdminMode(bool isAdmin) {
    isAdminMode_ = isAdmin;

    // Global settings
    if (networkSummaryHeader_) {
        // Update add button visibility
    }
    globalFpsSpin_->setEnabled(isAdmin);
    preTriggerSpin_->setEnabled(isAdmin);
    postTriggerSpin_->setEnabled(isAdmin);
    eventRetentionSpin_->setEnabled(isAdmin);
    eventStoragePathEdit_->setEnabled(isAdmin);
    browseEventStorageBtn_->setEnabled(isAdmin);
    resetEventStorageBtn_->setEnabled(isAdmin);
    for (int i = 0; i < 8; ++i) {
        if (themeCards_[i]) themeCards_[i]->setEnabled(isAdmin);
    }
    if (themeCombo_) themeCombo_->setEnabled(isAdmin);
    if (liveViewBackgroundStyleCombo_) liveViewBackgroundStyleCombo_->setEnabled(isAdmin);
    if (liveViewGridTitleFontCombo_) liveViewGridTitleFontCombo_->setEnabled(isAdmin);
    if (liveViewGridTitleSizeSpin_) liveViewGridTitleSizeSpin_->setEnabled(isAdmin);
    if (liveViewDetailTitleFontCombo_) liveViewDetailTitleFontCombo_->setEnabled(isAdmin);
    if (liveViewDetailTitleSizeSpin_) liveViewDetailTitleSizeSpin_->setEnabled(isAdmin);
    if (liveViewDetailSectionFontCombo_) liveViewDetailSectionFontCombo_->setEnabled(isAdmin);
    if (liveViewDetailSectionSizeSpin_) liveViewDetailSectionSizeSpin_->setEnabled(isAdmin);
    if (analysisVideoTitleFontCombo_) analysisVideoTitleFontCombo_->setEnabled(isAdmin);
    if (analysisVideoTitleSizeSpin_) analysisVideoTitleSizeSpin_->setEnabled(isAdmin);
    if (analysisTimestampFontCombo_) analysisTimestampFontCombo_->setEnabled(isAdmin);
    if (analysisTimestampSizeSpin_) analysisTimestampSizeSpin_->setEnabled(isAdmin);
    if (analysisTabFontCombo_) analysisTabFontCombo_->setEnabled(isAdmin);
    if (analysisTabSizeSpin_) analysisTabSizeSpin_->setEnabled(isAdmin);
    if (analysisPlaybackSurfaceCombo_) analysisPlaybackSurfaceCombo_->setEnabled(isAdmin);
    if (liveViewResetBtn_) liveViewResetBtn_->setEnabled(isAdmin);
    if (analysisResetBtn_) analysisResetBtn_->setEnabled(isAdmin);
    if (uiSaveBtn_) uiSaveBtn_->setEnabled(isAdmin);
    if (uiApplyBtn_) uiApplyBtn_->setEnabled(isAdmin && (captureCurrentSettings() != originalValues_));
    if (uiUnsavedIndicator_) uiUnsavedIndicator_->setVisible(isAdmin);

    // Per-camera: only the edit checkbox is admin-gated
    for (auto* card : cameraCards_) {
        // Card handles its own edit state
    }
}

void ConfigDialog::onRefreshLogsClicked() {
    networkSummaryHeader_->setRefreshing(true);

    currentGigEDevices_ = CameraManager::enumerateGigEDevices();
    QString refreshTs = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    connectionLogsBrowser_->append(QString("--- Refresh at %1 ---").arg(refreshTs));

    static QMap<QString, QString> cameraConnectionTimes;

    for (const auto& dev : currentGigEDevices_) {
        QString mac = QString::fromStdString(dev.macAddress);

        if (!cameraConnectionTimes.contains(mac)) {
            cameraConnectionTimes.insert(mac, QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        }

        QString devTs = cameraConnectionTimes.value(mac);
        connectionLogsBrowser_->append(
            QString("[%1] %2 | MAC: %3 | IP: %4 | Subnet: %5 | Gateway: %6")
            .arg(devTs)
            .arg(QString::fromStdString(dev.friendlyName))
            .arg(QString::fromStdString(dev.macAddress))
            .arg(QString::fromStdString(dev.ipAddress))
            .arg(QString::fromStdString(dev.subnetMask))
            .arg(QString::fromStdString(dev.defaultGateway))
        );
    }

    if (currentGigEDevices_.empty()) {
        cameraConnectionTimes.clear();
        QString emptyTs = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        connectionLogsBrowser_->append(QString("[%1] No online Real cameras detected.").arg(emptyTs));
    }

    QSet<QString> reservedMacs;
    for (auto* card : cameraCards_) {
        const QString configuredMac = normalizeMac(card->macAddress());
        if (!configuredMac.isEmpty() && configuredMac != "NONE/AUTO") {
            reservedMacs.insert(configuredMac);
        }
    }

    for (auto* card : cameraCards_) {
        card->updateMacCombo(currentGigEDevices_, card->macAddress(), reservedMacs);
    }

    refreshNetworkStatus();
    networkSummaryHeader_->setRefreshing(false);
}

void ConfigDialog::onClearLogsClicked() {
    connectionLogsBrowser_->clear();
}

void ConfigDialog::onToggleLogsClicked() {
    // Left empty or we can remove the slot. Currently not used as logs are always visible in Diagnostics tab.
}

void ConfigDialog::onOpenIpConfiguratorClicked() {
    QProcess::startDetached("/opt/pylon/bin/IpConfigurator", QStringList());
}

bool ConfigDialog::validateConfiguration(QStringList* errors) const {
    QMap<QString, QList<int>> ipUsage;
    QMap<QString, QList<int>> macUsage;

    for (auto* card : cameraCards_) {
        int source = card->sourceType();
        if (source == 2) continue;

        QString configuredIp = normalizeIp(card->ipAddress());
        if (!configuredIp.isEmpty()) {
            ipUsage[configuredIp].append(card->cameraId());
        }

        if (source == 1) {
            QString configuredMac = normalizeMac(card->macAddress());
            if (configuredMac.isEmpty() || configuredMac == "NONE/AUTO") {
                if (errors) {
                    errors->append(QString("Camera ID %1 is set to Real but has no MAC assigned.").arg(card->cameraId()));
                }
            } else {
                macUsage[configuredMac].append(card->cameraId());
            }
        }
    }

    for (auto it = ipUsage.cbegin(); it != ipUsage.cend(); ++it) {
        if (it.value().size() > 1 && errors) {
            errors->append(QString("Configured IP %1 is assigned to multiple camera IDs (%2).")
                          .arg(it.key(), joinCameraIds(it.value())));
        }
    }

    for (auto it = macUsage.cbegin(); it != macUsage.cend(); ++it) {
        if (it.value().size() > 1 && errors) {
            errors->append(QString("MAC %1 is assigned to multiple camera IDs (%2).")
                          .arg(it.key(), joinCameraIds(it.value())));
        }
    }

    return !errors || errors->isEmpty();
}

void ConfigDialog::refreshNetworkStatus() {
    QMap<QString, QList<QString>> liveIpToMacs;
    QMap<QString, GigEDeviceInfo> macToDevice;

    for (const auto& dev : currentGigEDevices_) {
        QString ip = normalizeIp(QString::fromStdString(dev.ipAddress));
        QString mac = normalizeMac(QString::fromStdString(dev.macAddress));
        if (!mac.isEmpty()) {
            macToDevice.insert(mac, dev);
        }
        if (!ip.isEmpty()) {
            liveIpToMacs[ip].append(mac);
        }
    }

    QSet<QString> duplicateConfiguredIps;
    QSet<QString> duplicateConfiguredMacs;
    QMap<QString, int> configuredIpCounts;
    QMap<QString, int> configuredMacCounts;

    for (auto* card : cameraCards_) {
        if (card->sourceType() == 2) continue;

        QString configuredIp = normalizeIp(card->ipAddress());
        if (!configuredIp.isEmpty()) {
            configuredIpCounts[configuredIp] += 1;
        }

        if (card->sourceType() == 1) {
            QString configuredMac = normalizeMac(card->macAddress());
            if (!configuredMac.isEmpty() && configuredMac != "NONE/AUTO") {
                configuredMacCounts[configuredMac] += 1;
            }
        }
    }

    for (auto it = configuredIpCounts.cbegin(); it != configuredIpCounts.cend(); ++it) {
        if (it.value() > 1) {
            duplicateConfiguredIps.insert(it.key());
        }
    }

    for (auto it = configuredMacCounts.cbegin(); it != configuredMacCounts.cend(); ++it) {
        if (it.value() > 1) {
            duplicateConfiguredMacs.insert(it.key());
        }
    }

    int mismatchCount = 0;
    int missingCount = 0;
    int blockingCount = 0;
    bool liveDuplicateSeen = false;
    int onlineCount = 0;
    int warningCount = 0;
    int errorCount = 0;
    int offlineCount = 0;

    for (auto* card : cameraCards_) {
        int source = card->sourceType();
        QString configuredIp = normalizeIp(card->ipAddress());
        QString configuredMac = normalizeMac(card->macAddress());

        QString detectedIp = "Offline";
        QString statusText = "Disabled";
        QColor statusColor("#888888");

        if (source != 2) {
            statusText = "Unassigned MAC";
            statusColor = QColor("#E0A800");

            if (duplicateConfiguredIps.contains(configuredIp)) {
                statusText = "Duplicate IP";
                statusColor = QColor("#FF5A5A");
                blockingCount++;
                errorCount++;
            } else if (source == 1 && (configuredMac.isEmpty() || configuredMac == "NONE/AUTO")) {
                missingCount++;
                warningCount++;
            } else if (source == 1 && duplicateConfiguredMacs.contains(configuredMac)) {
                statusText = "Duplicate MAC";
                statusColor = QColor("#FF5A5A");
                blockingCount++;
                errorCount++;
            } else if (source == 0) {
                statusText = "Emulated";
                statusColor = QColor("#4CAF50");
                onlineCount++;
            } else if (macToDevice.contains(configuredMac)) {
                const GigEDeviceInfo& dev = macToDevice[configuredMac];
                detectedIp = QString::fromStdString(dev.ipAddress);
                QString normalizedDetectedIp = normalizeIp(detectedIp);

                if (liveIpToMacs.value(normalizedDetectedIp).size() > 1) {
                    statusText = "Duplicate live IP";
                    statusColor = QColor("#FF5A5A");
                    liveDuplicateSeen = true;
                    blockingCount++;
                    errorCount++;
                } else if (normalizedDetectedIp == configuredIp) {
                    statusText = "Online";
                    statusColor = QColor("#4CAF50");
                    onlineCount++;
                } else {
                    statusText = "IP mismatch";
                    statusColor = QColor("#E0A800");
                    mismatchCount++;
                    warningCount++;
                }
            } else if (source == 1) {
                statusText = "Offline";
                statusColor = QColor("#6E7681");
                missingCount++;
                offlineCount++;
            }
        } else {
            offlineCount++;
        }

        card->setDetectedIp(detectedIp);
        card->setStatus(statusText, statusColor);
    }

    // Update network summary header
    int totalCount = cameraCards_.size();
    networkSummaryHeader_->setCameraCounts(totalCount, onlineCount, warningCount, errorCount, offlineCount);

    // Update summary text
    QStringList summary;
    QColor summaryColor = QColor("#4CAF50");

    if (blockingCount > 0) {
        summaryColor = QColor("#FF5A5A");
        if (!duplicateConfiguredIps.isEmpty()) {
            summary << "Duplicate configured IPs detected";
        }
        if (!duplicateConfiguredMacs.isEmpty()) {
            summary << "Duplicate configured MACs detected";
        }
        if (liveDuplicateSeen) {
            summary << "Duplicate live IP detected";
        }
    }

    if (mismatchCount > 0) {
        if (summaryColor != QColor("#FF5A5A")) {
            summaryColor = QColor("#E0A800");
        }
        summary << QString("%1 camera%2 have IP mismatch").arg(mismatchCount).arg(mismatchCount == 1 ? "" : "s");
    }

    if (missingCount > 0) {
        if (summaryColor == QColor("#4CAF50")) {
            summaryColor = QColor("#E0A800");
        }
        summary << QString("%1 camera%2 not visible").arg(missingCount).arg(missingCount == 1 ? "" : "s");
    }

    if (summary.isEmpty()) {
        networkSummaryHeader_->setNetworkStatus("Network OK: All cameras configured correctly", QColor("#4CAF50"));
    } else {
        networkSummaryHeader_->setNetworkStatus(summary.join(" | "), summaryColor);
    }
}
