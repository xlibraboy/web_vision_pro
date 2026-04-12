#pragma once

#include <QWidget>
#include <QFrame>
#include <QGraphicsDropShadowEffect>

class QLabel;
class QPushButton;
class QHBoxLayout;
class QVBoxLayout;
class QGraphicsDropShadowEffect;

/**
 * @brief NetworkSummaryHeader - Sticky network status dashboard
 *
 * A premium header widget that stays visible while scrolling through camera cards.
 * Features:
 * - Real-time network status display
 * - Animated status indicators
 * - Quick action buttons
 * - Elevation shadow effect
 * - Theme-aware styling
 */
class NetworkSummaryHeader : public QFrame {
    Q_OBJECT

public:
    explicit NetworkSummaryHeader(QWidget* parent = nullptr);
    ~NetworkSummaryHeader() override;

    // Status update methods
    void setNetworkStatus(const QString& status, const QColor& color);
    void setCameraCounts(int total, int online, int warning, int error, int offline);
    void updateSummary(const QString& summary, const QColor& color = QColor("#4CAF50"));

    // Getters for button connections
    QPushButton* refreshButton() const { return refreshBtn_; }

    // Theme
    void applyTheme(const QColor& primaryColor, const QColor& accentColor);

signals:
    void refreshRequested();
    void clearLogsRequested();
    void toggleLogsRequested();
    void addCameraRequested();

public slots:
    void onRefreshComplete();
    void setRefreshing(bool refreshing);

protected:
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void setupUI();
    void setupStyleSheet();
    void updateElevation(bool hovered);
    void updateStatusIndicators();
    void animateRefreshButton();

    // Layouts
    QVBoxLayout* mainLayout_;
    QHBoxLayout* headerLayout_;
    QHBoxLayout* statusLayout_;

    // Widgets
    QLabel* networkIcon_;
    QLabel* titleLabel_;
    QLabel* statusLabel_;
    QLabel* summaryLabel_;

    // Status indicators
    QWidget* indicatorsWidget_;
    QLabel* totalIndicator_;
    QLabel* onlineIndicator_;
    QLabel* warningIndicator_;
    QLabel* errorIndicator_;
    QLabel* offlineIndicator_;

    // Action buttons
    QPushButton* refreshBtn_;
    QPushButton* addBtn_;

    // Data
    int totalCount_;
    int onlineCount_;
    int warningCount_;
    int errorCount_;
    int offlineCount_;
    bool isRefreshing_;

    // Styling
    QColor primaryColor_;
    QColor accentColor_;
    QColor statusColor_;
    QGraphicsDropShadowEffect* shadowEffect_;
};
