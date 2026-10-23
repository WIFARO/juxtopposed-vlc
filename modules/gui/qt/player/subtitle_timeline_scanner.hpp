// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_SUBTITLE_TIMELINE_SCANNER_HPP
#define VLC_QT_SUBTITLE_TIMELINE_SCANNER_HPP

#include <vlc_common.h>
#include <vlc_interrupt.h>
#include <QByteArray>
#include <QVector>
#include <QVariantMap>
#include <QString>

struct SubtitleTimelineCue
{
    vlc_tick_t start;
    vlc_tick_t stop;
    QString text;
};

struct SubtitleTimelineSource
{
    QByteArray uri;
    QByteArray demux = "any";
    // Already validated input options. Never pass untrusted options here.
    QVariantMap options;
    // Index among subtitle tracks in this source, excluding other slaves.
    int subtitleIndex = 0;
    vlc_tick_t duration = 0;
    double originalFPS = 0;
    float subtitleRate = 1;
    unsigned videoWidth = 1280;
    unsigned videoHeight = 720;
    vlc_tick_t deadline = VLC_TICK_INVALID;
    qsizetype maximumCues = 100000;
};

struct SubtitleTimelineScan
{
    enum Status { Complete, Cancelled, Failed, TrackMissing, LimitReached };
    Status status = Failed;
    QVector<SubtitleTimelineCue> cues;
};

// Run on a worker thread. Kill interrupt to cancel an active read.
SubtitleTimelineScan scanSubtitleTimeline(vlc_object_t *parent,
                                         const SubtitleTimelineSource& source,
                                         vlc_interrupt_t *interrupt);

#endif
