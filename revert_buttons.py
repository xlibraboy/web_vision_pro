#!/usr/bin/env python3
"""
Revert playback buttons to text labels (Unicode not supported in Docker).
Keep the improved order but use simple ASCII text.
"""

import re

# Read the file
with open('src/gui/AnalysisView.cpp', 'r') as f:
    content = f.read()

# Define the reverted playback toolbar code (text labels, not Unicode)
new_toolbar = '''    // === PLAYBACK CONTROL TOOLBAR (Standard Layout, Text Labels) ===
    
    // Group 1: Navigation Controls
    beginButton_ = new QPushButton("|<", playbackPanel_);
    beginButton_->setFixedSize(28, 24);
    beginButton_->setToolTip("Go to First Frame");
    beginButton_->setStyleSheet(
        "QPushButton { background: #555; color: white; border-radius: 3px; font-weight: bold; font-size: 10px; }"
        "QPushButton:hover { background: #666; }"
        "QPushButton:pressed { background: #444; }"
    );
    connect(beginButton_, &QPushButton::clicked, this, &AnalysisView::onBeginClicked);
    toolbarLayout->addWidget(beginButton_);
    
    prevButton_ = new QPushButton("<", playbackPanel_);
    prevButton_->setFixedSize(28, 24);
    prevButton_->setToolTip("Previous Frame / Rewind (Hold)");
    prevButton_->setStyleSheet(
        "QPushButton { background: #555; color: white; border-radius: 3px; font-weight: bold; font-size: 10px; }"
        "QPushButton:hover { background: #666; }"
        "QPushButton:pressed { background: #444; }"
    );
    prevButton_->setAutoRepeat(true);
    prevButton_->setAutoRepeatDelay(200);
    prevButton_->setAutoRepeatInterval(50);
    connect(prevButton_, &QPushButton::pressed, this, &AnalysisView::onPreviousPressed);
    connect(prevButton_, &QPushButton::released, this, &AnalysisView::onPreviousReleased);
    toolbarLayout->addWidget(prevButton_);
    
    playPauseButton_ = new QPushButton("Play", playbackPanel_);
    playPauseButton_->setFixedSize(40, 24);
    playPauseButton_->setToolTip("Play/Pause");
    playPauseButton_->setStyleSheet(
        "QPushButton { background: #4CAF50; color: white; border-radius: 3px; font-size: 10px; font-weight: bold; }"
        "QPushButton:hover { background: #66BB6A; }"
        "QPushButton:pressed { background: #388E3C; }"
    );
    connect(playPauseButton_, &QPushButton::clicked, this, &AnalysisView::onPlayPauseClicked);
    toolbarLayout->addWidget(playPauseButton_);
    
    nextButton_ = new QPushButton(">", playbackPanel_);
    nextButton_->setFixedSize(28, 24);
    nextButton_->setToolTip("Next Frame / Fast Forward (Hold)");
    nextButton_->setStyleSheet(
        "QPushButton { background: #555; color: white; border-radius: 3px; font-weight: bold; font-size: 10px; }"
        "QPushButton:hover { background: #666; }"
        "QPushButton:pressed { background: #444; }"
    );
    nextButton_->setAutoRepeat(true);
    nextButton_->setAutoRepeatDelay(200);
    nextButton_->setAutoRepeatInterval(50);
    connect(nextButton_, &QPushButton::pressed, this, &AnalysisView::onNextPressed);
    connect(nextButton_, &QPushButton::released, this, &AnalysisView::onNextReleased);
    toolbarLayout->addWidget(nextButton_);
    
    endButton_ = new QPushButton(">|", playbackPanel_);
    endButton_->setFixedSize(28, 24);
    endButton_->setToolTip("Go to Last Frame");
    endButton_->setStyleSheet(
        "QPushButton { background: #555; color: white; border-radius: 3px; font-weight: bold; font-size: 10px; }"
        "QPushButton:hover { background: #666; }"
        "QPushButton:pressed { background: #444; }"
    );
    connect(endButton_, &QPushButton::clicked, this, &AnalysisView::onEndClicked);
    toolbarLayout->addWidget(endButton_);
    
    auto divider1 = new QFrame(playbackPanel_);
    divider1->setFrameShape(QFrame::VLine);
    divider1->setFrameShadow(QFrame::Sunken);
    toolbarLayout->addWidget(divider1);
    
    // Group 2: Jump to Trigger
    resetButton_ = new QPushButton("0", playbackPanel_);
    resetButton_->setFixedSize(28, 24);
    resetButton_->setToolTip("Jump to Trigger Point (Frame 0)");
    resetButton_->setStyleSheet(
        "QPushButton { background: #FF5722; color: white; border-radius: 3px; font-weight: bold; font-size: 12px; }"
        "QPushButton:hover { background: #FF7043; }"
        "QPushButton:pressed { background: #E64A19; }"
    );
    connect(resetButton_, &QPushButton::clicked, this, &AnalysisView::onResetClicked);
    toolbarLayout->addWidget(resetButton_);
    
    auto divider2 = new QFrame(playbackPanel_);
    divider2->setFrameShape(QFrame::VLine);
    divider2->setFrameShadow(QFrame::Sunken);
    toolbarLayout->addWidget(divider2);
    
    // Group 3: Frame Input
    QLabel* frameLabel = new QLabel("Frame:", playbackPanel_);
    frameLabel->setStyleSheet("color: #aaa; font-size: 10px;");
    toolbarLayout->addWidget(frameLabel);
    
    frameInput_ = new QLineEdit("0.0", playbackPanel_);
    frameInput_->setFixedSize(60, 24);
    frameInput_->setAlignment(Qt::AlignCenter);
    frameInput_->setToolTip("Jump to Frame (relative to trigger)");
    frameInput_->setStyleSheet("QLineEdit { background: #333; color: #0f0; padding: 2px; border: 1px solid #555; border-radius: 3px; font-family: Consolas; font-size: 10px; }");
    connect(frameInput_, &QLineEdit::editingFinished, this, &AnalysisView::onFrameInputChanged);
    toolbarLayout->addWidget(frameInput_);
    
    auto divider3 = new QFrame(playbackPanel_);
    divider3->setFrameShape(QFrame::VLine);
    divider3->setFrameShadow(QFrame::Sunken);
    toolbarLayout->addWidget(divider3);
    
    // Group 4: Playback Speed
    speedButton_ = new QPushButton("1.0x", playbackPanel_);
    speedButton_->setFixedSize(50, 24);
    speedButton_->setToolTip("Playback Speed");
    speedButton_->setStyleSheet(
        "QPushButton { background: #2196F3; color: white; border-radius: 3px; font-weight: bold; font-size: 10px; }"
        "QPushButton:hover { background: #42A5F5; }"
        "QPushButton:pressed { background: #1976D2; }"
    );
    speedMenu_ = new QMenu(speedButton_);
    speedMenu_->addAction("Very Slow (0.25x)")->setData(0.25);
    speedMenu_->addAction("Slow (0.5x)")->setData(0.5);
    speedMenu_->addAction("Normal (1.0x)")->setData(1.0);
    speedMenu_->addAction("Fast (2.0x)")->setData(2.0);
    speedMenu_->addAction("Very Fast (4.0x)")->setData(4.0);
    speedButton_->setMenu(speedMenu_);
    connect(speedMenu_, &QMenu::triggered, this, &AnalysisView::onSpeedChanged);
    toolbarLayout->addWidget(speedButton_);
    
    auto divider4 = new QFrame(playbackPanel_);
    divider4->setFrameShape(QFrame::VLine);
    divider4->setFrameShadow(QFrame::Sunken);
    toolbarLayout->addWidget(divider4);
    
    // Group 5: Export
    saveAviButton_ = new QPushButton("Export MP4", playbackPanel_);
    saveAviButton_->setFixedSize(75, 24);
    saveAviButton_->setToolTip("Export Video to MP4 (Pylon)");
    saveAviButton_->setStyleSheet(
        "QPushButton { background: #FF9800; color: white; border-radius: 3px; font-weight: bold; font-size: 10px; }"
        "QPushButton:hover { background: #FFA726; }"
        "QPushButton:pressed { background: #F57C00; }"
    );
    connect(saveAviButton_, &QPushButton::clicked, this, &AnalysisView::onExportMp4Clicked);
    toolbarLayout->addWidget(saveAviButton_);'''

# Find and replace
pattern = r'(    // === PLAYBACK CONTROL TOOLBAR.*?toolbarLayout->addWidget\(saveAviButton_\);)'
content_new = re.sub(pattern, new_toolbar, content, flags=re.DOTALL)

# Write back
with open('src/gui/AnalysisView.cpp', 'w') as f:
    f.write(content_new)

print("✓ Playback buttons reverted to text labels!")
