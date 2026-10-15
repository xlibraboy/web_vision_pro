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

    // Re-applies every theme-derived style after a preset change; the
    // constructor calls it too, so the style strings live in one place.
    void updateTheme();

    // Status update methods. An invalid color resolves to the ok token.
    void setNetworkStatus(const QString& status, const QColor& color);
    void setCameraCounts(int total, int online, int warning, int error, int offline);
    void setLinkSpeedMbps(int mbps);
    void updateSummary(const QString& summary, const QColor& color = QColor());

    // Getters for button connections
    QPushButton* refreshButton() const { return refreshBtn_; }

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
    void applyStatusLabelStyle();
    void refreshLinkIndicator();

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
    QLabel* linkIndicator_ = nullptr;

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

    // Last values updateTheme() needs to re-derive the preset-dependent
    // styles: the status ink (invalid until setNetworkStatus() is called)
    // and the link badge speed (<= 0 renders "Link: --").
    QColor statusColor_;
    int linkSpeedMbps_;

    // Styling
    QGraphicsDropShadowEffect* shadowEffect_;
};
