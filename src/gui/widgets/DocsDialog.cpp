#include "DocsDialog.h"

#include <QListWidget>
#include <QTextBrowser>
#include <QSplitter>
#include <QVBoxLayout>
#include <QLabel>
#include <QColor>
#include "../../config/CameraConfig.h"

namespace {

QString sectionHtml(const QString& title, const QString& body) {
    return QString("<h2>%1</h2>\n%2").arg(title, body);
}

}  // namespace

DocsDialog::DocsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("PaperVision Documentation");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(920, 660);
    setMinimumSize(640, 420);

    const ThemeColors tc = CameraConfig::getThemeColors();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto* header = new QLabel(
        "<b>PaperVision</b> — industrial vision system for paper machine inspection", this);
    header->setStyleSheet(QString("color: %1; font-size: 14px;").arg(tc.text));
    layout->addWidget(header);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(splitter, 1);

    sectionList_ = new QListWidget(splitter);
    sectionList_->setObjectName("docsSectionList");
    sectionList_->setFixedWidth(190);
    sectionList_->setSelectionMode(QAbstractItemView::SingleSelection);

    contentBrowser_ = new QTextBrowser(splitter);
    contentBrowser_->setOpenExternalLinks(true);
    contentBrowser_->setOpenLinks(true);
    contentBrowser_->document()->setDefaultStyleSheet(QString(
        "h2 { color: %1; border-bottom: 1px solid %2; padding-bottom: 4px; font-size: 17px; }"
        "h3 { color: %1; font-size: 14px; margin-top: 14px; }"
        "p  { color: %3; font-size: 13px; }"
        "li { color: %3; font-size: 13px; margin-bottom: 3px; }"
        "code { color: %1; background-color: %4; padding: 1px 4px; border-radius: 3px; }"
        "pre { color: %3; background-color: %4; padding: 8px; border-radius: 4px;"
              " border: 1px solid %2; font-size: 12px;"
              " font-family: 'DejaVu Sans Mono', 'Courier New', monospace; }"
        "b  { color: %3; }"
        "a  { color: %1; }"
    ).arg(tc.primary, tc.border, tc.text, tc.btnBg));
    contentBrowser_->setStyleSheet(QString(
        "QTextBrowser { background-color: %1; color: %2; border: 1px solid %3;"
        " border-radius: 4px; padding: 10px; }").arg(tc.bg, tc.text, tc.border));
    contentBrowser_->setFrameShape(QFrame::NoFrame);

    // Soft selection: accent at ~40% opacity over the dark surface keeps the row
    // clearly highlighted without the loud solid fill.
    QColor selectionTint = QColor(tc.primary);
    selectionTint.setAlpha(102);
    const QString selectionTintCss = QString("rgba(%1, %2, %3, %4)")
        .arg(selectionTint.red()).arg(selectionTint.green())
        .arg(selectionTint.blue()).arg(selectionTint.alpha());

    splitter->addWidget(sectionList_);
    splitter->addWidget(contentBrowser_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({190, 700});

    setStyleSheet(QString(
        "QDialog { background-color: %1; }"
        "QLabel { color: %2; }"
        "QListWidget#docsSectionList { background-color: %4; color: %2; border: 1px solid %3;"
        " border-radius: 4px; font-size: 13px; padding: 4px; }"
        "QListWidget#docsSectionList::item { padding: 7px 8px; border-radius: 3px; }"
        "QListWidget#docsSectionList::item:selected { background-color: %5; color: %6; }"
        "QListWidget#docsSectionList::item:hover:!selected { background-color: %7; }"
    ).arg(tc.bg, tc.text, tc.border, tc.btnBg, selectionTintCss,
          tc.text,
          tc.btnHover));

    // ── Section 1: How the app works ──
    addSection(QStringLiteral("How the App Works"), sectionHtml(
        QStringLiteral("Overview"),
        QStringLiteral(
            "<p>PaperVision inspects the paper web on a machine line with multiple GigE "
            "cameras. Each camera continuously grabs frames; the app buffers them in memory, "
            "applies OpenCV processing, and writes events to disk whenever a trigger fires. "
            "Recorded events can then be replayed frame-by-frame in the Analysis view to "
            "inspect defects and confirm that cameras are synchronized.</p>"
            "<h3>Main views (top tabs)</h3>"
            "<ul>"
            "<li><b>Live View</b> — a camera grid fed by the camera service. Each card shows "
            "its feed, name, IP, link speed, and frame drops. The server button starts/stops "
            "grab; the Camera Mode selector in Settings switches between real hardware and "
            "emulated cameras (no hardware needed for testing).</li>"
            "<li><b>Analysis View</b> — replay of recorded events. Sub-tabs: <b>All Camera</b> "
            "(grid of that event's camera files), <b>Camera</b> (single selected camera with "
            "tools), and <b>Diagnostic</b> (per-camera drops-rate trend and link-speed "
            "monitor).</li>"
            "<li><b>System Configuration</b> — admin-only: camera cards, fixed IP list, machine "
            "groups, IP configurator, OPC UA connection/triggers/speed, and storage.</li>"
            "</ul>"
            "<h3>Triggering &amp; recording</h3>"
            "<ul>"
            "<li>Triggers come from <b>OPC UA tags</b> (push-hold: while a tag is held True, a "
            "trigger fires on its repeat interval) or the <b>Manual Trigger</b> button.</li>"
            "<li>Each event stores per-camera video, per-frame timestamps and frame counters, "
            "and the machine speed + camera positions captured at trigger time.</li>"
            "<li>Events stream to disk automatically; a status-bar badge warns when free "
            "storage is low.</li>"
            "</ul>"
            "<h3>Defect sync</h3>"
            "<p>Because cameras sit at different machine positions, the same defect appears in "
            "different cameras at different frame numbers. The Analysis view stores a "
            "<b>frame offset per camera</b> and shows the shared timeline through the "
            "offset-adjusted frame of each camera. Offsets come from <b>defect marks</b> you "
            "place (ground truth) or from machine speed + camera positions as a fallback.</p>"
            "<h3>Persistence</h3>"
            "<p>Annotations, defect marks, and camera offsets are saved per event in a sidecar "
            "<code>&lt;event&gt;_annotations.json</code> file next to the recordings, so a "
            "review session can be resumed later.</p>")));

    // ── Section 2: Workflows ──
    addSection(QStringLiteral("Workflows"), sectionHtml(
        QStringLiteral("Typical Workflows"),
        QStringLiteral(
            "<h3>1. Live monitoring</h3>"
            "<ol>"
            "<li>Pick a camera mode (hardware or emulation) in Settings &gt; Recording &amp; "
            "Triggers &gt; Camera Mode.</li>"
            "<li>Press the <b>server button</b> in the Live View sidebar to start grabbing.</li>"
            "<li>Watch the grid; each card shows live status (link speed, drops). A green/red "
            "card border reflects connection health.</li>"
            "<li>Pause grabbing with the record/pause control when you need to.</li>"
            "</ol>"
            "<h3>2. Capture an event</h3>"
            "<ol>"
            "<li>Configure OPC UA trigger tags (Connection/Triggers/Speed pages) or use the "
            "<b>Manual Trigger</b> button.</li>"
            "<li>When a trigger fires, an event is recorded to the configured storage and "
            "appears in the event list.</li>"
            "<li>The event list shows trigger time and reason; the newest event pulses "
            "briefly so it is easy to spot.</li>"
            "</ol>"
            "<h3>3. Review a defect</h3>"
            "<ol>"
            "<li>Open <b>Analysis View</b> and select the event in the left list.</li>"
            "<li>Scrub the media player slider, use the step buttons, or press Play. Choose a "
            "playback speed (Ultra Slow 0.05x → Fast 2.0x) — slow speeds step frame-by-frame "
            "for inspecting defects.</li>"
            "<li>Below 1x speed the event dashboard shows a <b>DETAIL</b> strip between the "
            "chart and the signal lanes: a magnified window around the playhead with per-frame "
            "points and defect ticks. Mouse-wheel over it to widen or narrow the window "
            "(±10–120 frames); click or drag inside it to seek.</li>"
            "<li>Use the <b>TOOLS</b> panel (right edge tab): marker tool (pen / rectangle / "
            "circle / arrow), zoom, and brightness. Annotations are saved per frame.</li>"
            "</ol>"
            "<h3>4. Sync defects across cameras</h3>"
            "<ol>"
            "<li>Find the same defect on two or more cameras and press <b>Mark Defect</b> on "
            "each while that camera displays it (the selected camera shows in the Camera tab).</li>"
            "<li>Press <b>Align</b>. Offsets are computed from your marks; unmarked cameras "
            "fall back to machine speed + positions.</li>"
            "<li>When all marked defects line up, a green <b>sync crosshair</b> appears on "
            "every camera view at the synced frame and the status line reads "
            "<i>all defects @ frame N</i>.</li>"
            "<li>Fine-tune a single camera with the offset spin box if needed; Reset clears "
            "all offsets.</li>"
            "</ol>"
            "<h3>5. Diagnose the network</h3>"
            "<ul>"
            "<li>Analysis &gt; <b>Diagnostic</b> shows per-camera frame-drops trend and link "
            "speed, so a failing cable or switch port shows up as rising drops.</li>"
            "<li>Drop counters reset on camera reconnect; the Live View cards flag "
            "degraded links.</li>"
            "</ul>"
            "<h3>6. Configure the system (admin)</h3>"
            "<ul>"
            "<li>Login on the Analysis View control panel unlocks System Configuration.</li>"
            "<li>Set camera positions and machine groups (Press-Part / Pre-Dryer / "
            "After-Dryer / Calender-Reel) — group distance + speed drive spatial trigger "
            "alignment.</li>"
            "<li>Configure storage path, thresholds, and the low-disk warning.</li>"
            "</ul>")));

    // ── Section 3: Frameworks & technology ──
    // The list-item title is plain text (raw &), while the h2 heading is HTML
    // (escaped &amp;) so both render as "Frameworks & Technology".
    addSection(QStringLiteral("Frameworks & Technology"), sectionHtml(
        QStringLiteral("Frameworks &amp; Technology"),
        QStringLiteral(
            "<ul>"
            "<li><b>Language:</b> C++17</li>"
            "<li><b>Build:</b> CMake (3.16+)</li>"
            "<li><b>GUI:</b> Qt 5 — Widgets, Gui, Concurrent, Svg, Network, OpcUa</li>"
            "<li><b>Image processing:</b> OpenCV 4 (frame scaling, color conversion, "
            "defect detection pipeline)</li>"
            "<li><b>Cameras:</b> Basler GigE cameras via Pylon 6 SDK (GenApi, pylonbase, "
            "pylonutility)</li>"
            "<li><b>Machine integration:</b> OPC UA client (Qt5 OpcUa) for machine speed "
            "and trigger tags</li>"
            "<li><b>Storage:</b> recorded events + per-event sidecar JSON annotations, "
            "TIFF/encoded video streaming to disk</li>"
            "<li><b>Deployment:</b> Docker image with X11 for running the GUI inside a "
            "container</li>"
            "</ul>"
            "<h3>Source layout</h3>"
            "<pre>src/"
            "  core/          Camera manager, event controller, event database, video reader\n"
            "  gui/           MainWindow, Live/Analysis/Detail views, config dialog\n"
            "  gui/widgets/   Reusable widgets (camera cards, video widget, toggles, docs)\n"
            "  processing/    OpenCV algorithms (defect detector, image buffer, encoder)\n"
            "  config/        Configuration + theme handling\n"
            "  communication/ External interfaces (OPC UA client)\n"
            "</pre>"
            "<p>Build: <code>mkdir -p build &amp;&amp; cd build &amp;&amp; cmake .. &amp;&amp; make</code> "
            "— or the Docker workflow described in the Tutorial.</p>")));

    // ── Section 4: Architecture — App vs Pylon SDK ──
    addSection(QStringLiteral("Architecture"), sectionHtml(
        QStringLiteral("App vs. the Pylon SDK"),
        QStringLiteral(
            "<p>The app never talks to the cameras directly — everything goes through the "
            "<b>Basler Pylon SDK</b>, which is linked in-process. The app's "
            "<b>CameraManager</b> owns the Pylon objects and hands raw frames up to the Qt "
            "GUI and the OpenCV pipeline.</p>"
            "<pre>"
            "┌──────────────────────────────────────────────────────────────────────────┐\n"
            "│PaperVision App  (C++17)                                                  │\n"
            "│Qt5 GUI  ·  OpenCV 4  ·  event recording + sidecar annotations            │\n"
            "│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │\n"
            "│  │  Live View   │  │   Analysis   │  │   Event +    │  │    OPC UA    │  │\n"
            "│  │ camera grid  │  │review / sync │  │   storage    │  │  speed/trig  │  │\n"
            "│  └───────┬──────┘  └───────┬──────┘  └───────┬──────┘  └───────┬──────┘  │\n"
            "│          │                 │                 │                 │         │\n"
            "│          ▼                 ▼                 ▼                 ▼         │\n"
            "│  ┌────────────────────────────────────────────────────────────────────┐  │\n"
            "│  CameraManager  —  one grab thread per camera                            │\n"
            "│    enumerate : CTlFactory + CDeviceInfo  (GigE device discovery)         │\n"
            "│    capture   : Pylon::CInstantCamera  →  CGrabResultPtr                  │\n"
            "│    configure : GenApi::INodeMap  (gain, exposure, chunks, UserSets)      │\n"
            "│    events    : Pylon::CConfigurationEventHandler  (hot unplug)           │\n"
            "│    UI links  : live cards, drops/link-speed monitor, diagnostic trend    │\n"
            "│  └────────────────────────────────────────────────────────────────────┘  │\n"
            "└──────────────────────────────────────────────────────────────────────────┘\n"
            "│                                     │                                    │\n"
            "│                                     ▼                                    │\n"
            "┌──────────────────────────────────────────────────────────────────────────┐\n"
            "│Basler Pylon SDK 6.x  (linked in-process, C++ API)                        │\n"
            "│  CInstantCamera         — camera object model (open/close, grab)         │\n"
            "│  GenApi::INodeMap       — feature tree (all camera settings)             │\n"
            "│  pylonbase / pylonutility — errors, image formats, helpers               │\n"
            "│  GigE Transport Layer   — GigE Vision protocol over UDP                  │\n"
            "└──────────────────────────────────────────────────────────────────────────┘\n"
            "│                                     │                                    │\n"
            "│                                     ▼                                    │\n"
            "┌──────────────────────────────────────────────────────────────────────────┐\n"
            "│Basler GigE cameras  (fixed IPs, e.g. 192.168.x.x)                        │\n"
            "└──────────────────────────────────────────────────────────────────────────┘\n"
            "</pre>"
            "<h3>What lives where</h3>"
            "<ul>"
            "<li><b>Inside the app:</b> UI, review and defect-sync logic, recording, OpenCV "
            "processing, OPC UA client. The app decides <i>what</i> to configure and "
            "<i>when</i> to grab.</li>"
            "<li><b>Inside the Pylon SDK:</b> the camera protocol itself. "
            "<code>CInstantCamera</code> wraps device open/close and grabbing; "
            "<code>GenApi</code> exposes every camera feature as a node tree; the GigE "
            "transport layer speaks GigE Vision over UDP.</li>"
            "<li><b>The seam:</b> CameraManager maps app-side camera indexes to "
            "<code>CInstantCamera</code> objects and configures them through "
            "<code>GenApi::INodeMap</code>. Swap Pylon for another SDK and only this layer "
            "needs to change.</li>"
            "</ul>")));

    // ── Section 5: Tutorial ──
    addSection(QStringLiteral("Tutorial"), sectionHtml(
        QStringLiteral("Getting Started, Step by Step"),
        QStringLiteral(
            "<h3>1. Build and run</h3>"
            "<p>On a machine with Docker:</p>"
            "<pre>docker compose -f .docker/docker-compose.yml \\\n"
            "  -f .docker/docker-compose.emulated.yml up -d --force-recreate\n"
            "docker logs -f paper_vision_node</pre>"
            "<p>Wait for <code>[100%] Built target PaperVision_App</code>; the GUI opens on "
            "the container's display.</p>"
            "<h3>2. Choose a camera mode</h3>"
            "<p>Settings &gt; Recording &amp; Triggers &gt; Camera Mode. Pick <b>Emulation</b> "
            "when no hardware is attached — a <i>⚡ Emulation Mode</i> badge appears in the "
            "status bar. Pick <b>Hardware</b> for the real GigE cameras.</p>"
            "<h3>3. Start the live view</h3>"
            "<p>In Live View, press the server button in the left sidebar. Camera cards should "
            "fill the grid; each shows its feed, link speed, and drops. Try the View menu to "
            "change the grid layout (1x1 … 5x4, or a custom grid).</p>"
            "<h3>4. Trigger a capture</h3>"
            "<p>Click <b>Manual Trigger</b> (or hold it for push-hold repeat), or set up OPC UA "
            "trigger tags in System Configuration. The new event appears in the list; its row "
            "pulses briefly.</p>"
            "<h3>5. Replay the event</h3>"
            "<p>Switch to <b>Analysis View</b>, select the event, then use the media player: "
            "the slider scrubs the shared timeline, the step buttons move one frame (or more at "
            "higher speeds), and the dropdown sets playback speed. The trigger position is "
            "flagged above the scrub line; red dots are your defect marks; the value row shows "
            "relative frame, time from trigger, and machine speed.</p>"
            "<h3>6. Annotate with the tools panel</h3>"
            "<p>In the Camera tab, hover the <b>TOOLS</b> tab on the right edge to slide the "
            "panel open (Lock pins it). Enable the marker tool, pick pen / rectangle / circle / "
            "arrow, and draw on the frame. Adjust zoom and brightness. Annotations are saved "
            "with the event.</p>"
            "<h3>7. Sync the cameras on a defect</h3>"
            "<ol>"
            "<li>Scrub to the defect on the first camera and press <b>Mark Defect</b>.</li>"
            "<li>Switch to the next camera (click its tile on the All Camera tab, or pick it "
            "from the camera selector on the Camera tab) and mark the same defect on it "
            "(repeat for a third camera to be sure).</li>"
            "<li>Press <b>Align</b>. The status line shows the computed offsets. Scrub to the "
            "synced frame: a green crosshair on every camera confirms the defect lines up, and "
            "the status reads <i>all defects @ frame N</i>.</li>"
            "<li>If one camera is slightly off, nudge its offset spin box, or press Reset to "
            "start over.</li>"
            "</ol>"
            "<h3>8. Check diagnostics</h3>"
            "<p>Analysis &gt; <b>Diagnostic</b>: watch the drops/second sparkline per camera. "
            "Persistent drops on one camera usually mean a cabling or switch issue at that "
            "camera's link.</p>"
            "<h3>9. Administrator tasks</h3>"
            "<p>Login on the Analysis View control panel, then System Configuration: set camera "
            "positions and machine groups for spatial alignment, configure OPC UA connection "
            "and speed tags, and pick the record storage location and low-disk threshold.</p>"
            "<h3>10. Handy tips</h3>"
            "<ul>"
            "<li>The left arrow / right arrow keys step one frame at the selected speed.</li>"
            "<li>Holding a step button repeats the step at the speed-matched rate — slow "
            "speeds walk frame-by-frame, fast speeds jump ahead quickly.</li>"
            "<li>Every annotation, mark, and offset is stored in the event's sidecar JSON, so "
            "reopening the event restores your review state.</li>"
            "</ul>")));

    connect(sectionList_, &QListWidget::currentRowChanged,
            this, [this](int row) {
        if (row >= 0 && row < sectionList_->count()) {
            contentBrowser_->setHtml(sectionList_->item(row)->data(Qt::UserRole).toString());
        }
    });
    sectionList_->setCurrentRow(0);  // loads the first section
}

void DocsDialog::addSection(const QString& title, const QString& html) {
    auto* item = new QListWidgetItem(title, sectionList_);
    item->setData(Qt::UserRole, html);
    sectionList_->addItem(item);
}
