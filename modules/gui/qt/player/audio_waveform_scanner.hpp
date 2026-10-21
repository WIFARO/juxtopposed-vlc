// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_AUDIO_WAVEFORM_SCANNER_HPP
#define VLC_QT_AUDIO_WAVEFORM_SCANNER_HPP

#include <vlc_common.h>
#include <vlc_interrupt.h>
#include <QByteArray>
#include <QVector>
#include <QUrl>

bool canScanAudioWaveform(const QUrl& url, vlc_tick_t duration);

struct AudioWaveformSource
{
    QByteArray uri;
    int audioIndex = 0;
    vlc_tick_t duration = 0;
    vlc_tick_t deadline = VLC_TICK_INVALID;
    int bins = 2048;
};

struct AudioWaveformScan
{
    enum Status { Complete, Cancelled, Failed, TrackMissing, LimitReached };
    Status status = Failed;
    QVector<float> peaks;
};

// Decode on a worker thread without an audio output. Kill interrupt to cancel.
AudioWaveformScan scanAudioWaveform(vlc_object_t* parent, const AudioWaveformSource& source,
                                    vlc_interrupt_t* interrupt);
#endif
