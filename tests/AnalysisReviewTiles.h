#pragma once

#include <QObject>

// Regression tests for the review grid's per-camera tiles.
//
// Every configured camera gets a tile, and the ones the event has no file for
// must say NOT RECORDED instead of showing an unnamed empty slot. The marking
// pass used to run before the view switched into the per-camera review mode and
// looked cameras up with a 1-based id while the rest of the view indexes them
// from 0 - so the FIRST camera silently lost its label.
class AnalysisReviewTiles : public QObject {
    Q_OBJECT

private slots:
    void camerasLeftOutOfTheEventAreMarked();
    void hostClockMapsWhenCameraClocksDisagree();
};
