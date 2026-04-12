#pragma once

#include <QWidget>
#include <QFrame>
#include <QSize>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QGraphicsDropShadowEffect>
#include <QSet>
#include <vector>
#include "../../config/CameraConfig.h"
#include "../../core/CameraManager.h"
#include "IconManager.h"

// Forward declarations
class QComboBox;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QCheckBox;
class QVBoxLayout;
class QHBoxLayout;
class QGridLayout;
class QGroupBox;
class QToolButton;
class QSize;

/**
 * @brief CameraCard - Premium camera configuration card widget
 *
 * A redesigned card component with:
 * - Smooth expand/collapse animations
 * - Status-based styling with color coding
 * - Hover effects with elevation
 * - Smart auto-expand/collapse behavior
 * - Duotone icon integration
 */
class CameraCard : public QFrame {
    Q_OBJECT
    Q_PROPERTY(int contentHeight READ contentHeight WRITE setContentHeight)
    Q_PROPERTY(qreal shadowBlurRadius READ shadowBlurRadius WRITE setShadowBlurRadius)
    Q_PROPERTY(QColor shadowColor READ shadowColor WRITE setShadowColor)

public:
    explicit CameraCard(const CameraInfo& info, QWidget* parent = nullptr);
    ~CameraCard() override;

    // Getters for camera data
    int cameraId() const { return cameraId_; }
    int sourceType() const;
    QString name() const;
    QString location() const;
    QString side() const;
    int position() const;
    QString ipAddress() const;
    QString macAddress() const;
    QString subnetMask() const;
    QString gateway() const;
    QString detectedIp() const;
    QSize sizeHint() const override;
    CameraInfo cameraInfo() const;
    void setCameraInfo(const CameraInfo& info);

    // Status
    QString statusText() const;
    QColor statusColor() const;

    // Actions
    void setDetectedIp(const QString& ip);
    void setStatus(const QString& text, const QColor& color);
    void setEditable(bool editable);
    void updateMacCombo(const std::vector<GigEDeviceInfo>& devices, const QString& current,
                        const QSet<QString>& reservedMacs);

    // Animation
    void setExpanded(bool expanded, bool animate = true);
    bool isExpanded() const { return isExpanded_; }
    void expandWithAnimation();
    void collapseWithAnimation();

    // Smart behavior
    void updateSmartState(); // Auto-expand based on status

    // Theme
    void applyTheme(const QColor& primaryColor, const QColor& accentColor);

signals:
    void editToggled(bool checked);
    void sourceChanged(int source);
    void macChanged(const QString& mac);
    void writeIpClicked();
    void removeClicked();
    void deviceSettingsClicked();
    void expansionChanged(bool expanded);

protected:
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void setupUI(const CameraInfo& info);
    void setupAnimations();
    void setupStyleSheet();
    void createHeader();
    void createContent(const CameraInfo& info);
    void updateHeader(const CameraInfo& info);
    void createStatusIndicator();
    void updateElevation(bool hovered);
    QString currentMacValue() const;

    // Animation properties
    int contentHeight() const { return contentHeight_; }
    void setContentHeight(int height);
    qreal shadowBlurRadius() const;
    void setShadowBlurRadius(qreal radius);
    QColor shadowColor() const;
    void setShadowColor(const QColor& color);

    // Layouts
    QVBoxLayout* mainLayout_;
    QHBoxLayout* headerLayout_;
    QWidget* contentWidget_;
    QVBoxLayout* contentLayout_;

    // Header widgets
    QLabel* cameraIcon_;
    QLabel* cameraTitle_;
    QLabel* cameraMetaLabel_;
    QLabel* statusLabel_;
    QToolButton* expandButton_;
    QCheckBox* editCheck_;
    QToolButton* removeButton_;

    // Content widgets
    QGroupBox* basicInfoGroup_;
    QGroupBox* networkInfoGroup_;
    QGridLayout* basicFieldsLayout_;
    QGridLayout* networkFieldsLayout_;

    // Form fields
    QComboBox* sourceCombo_;
    QLineEdit* nameEdit_;
    QLineEdit* locationEdit_;
    QComboBox* sideCombo_;
    QSpinBox* positionSpin_;
    QLabel* ipLabel_;
    QLabel* detectedIpLabel_;
    QComboBox* macCombo_;
    QLineEdit* subnetEdit_;
    QLineEdit* gatewayEdit_;
    QPushButton* writeIpBtn_;
    QPushButton* deviceSettingsBtn_;

    // Data
    int cameraId_;
    bool isExpanded_;
    bool isHovered_;
    bool hasIssues_;
    int contentHeight_;

    // Animation
    QPropertyAnimation* heightAnimation_;
    QPropertyAnimation* shadowAnimation_;
    QParallelAnimationGroup* expandAnimationGroup_;
    QGraphicsDropShadowEffect* shadowEffect_;

    // Styling
    QColor statusColor_;
    QColor primaryColor_;
    QColor accentColor_;
    CameraInfo cameraInfo_;
    static constexpr int ANIMATION_DURATION = 250; // ms
    static constexpr int CARD_MIN_WIDTH = 0;
    static constexpr int CARD_MAX_WIDTH = 16777215;
};
