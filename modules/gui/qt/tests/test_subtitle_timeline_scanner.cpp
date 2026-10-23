// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QUrl>
#include <QTcpServer>
#include <QTcpSocket>
#include <QScopeGuard>
#include <future>
#include <cmath>
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_modules.h>
#include <vlc_configuration.h>
#include "../../../../lib/libvlc_internal.h"
#include "../player/subtitle_timeline_scanner.hpp"

class SubtitleTimelineScannerTest : public QObject
{
    Q_OBJECT
    libvlc_instance_t *m_vlc = nullptr;
    QTemporaryDir m_dir;

    SubtitleTimelineSource source(const QString& name = QStringLiteral("cues.srt")) {
        SubtitleTimelineSource source;
        source.uri = QUrl::fromLocalFile(m_dir.filePath(name)).toEncoded();
        source.demux = "subtitle";
        source.duration = VLC_TICK_FROM_SEC(10);
        source.deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(10);
        return source;
    }
    SubtitleTimelineScan scan(const SubtitleTimelineSource& source, bool killed = false) {
        auto *interrupt = vlc_interrupt_create();
        if (killed) vlc_interrupt_kill(interrupt);
        auto result = scanSubtitleTimeline(VLC_OBJECT(m_vlc->p_libvlc_int), source, interrupt);
        vlc_interrupt_destroy(interrupt);
        return result;
    }
    void check(const SubtitleTimelineCue& cue, double start, double stop) {
        QVERIFY(std::abs(cue.start / double(CLOCK_FREQ) - start) < .001);
        QVERIFY(std::abs(cue.stop / double(CLOCK_FREQ) - stop) < .001);
    }
private slots:
    void initTestCase() {
        const char *args[] = {"--ignore-config", "--no-media-library", "--quiet"};
        m_vlc = libvlc_new(3, args);
        QVERIFY(m_vlc != nullptr);
        QVERIFY(m_dir.isValid());
        QFile file(m_dir.filePath(QStringLiteral("cues.srt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray data = "1\n00:00:01,000 --> 00:00:02,500\nFirst\n\n"
            "2\n00:00:02,000 --> 00:00:04,000\nOverlap\n\n"
            "3\n00:00:07,000 --> 00:00:08,250\nLater\n";
        QCOMPARE(file.write(data), data.size());
        file.close();
        file.setFileName(m_dir.filePath(QStringLiteral("frames.sub")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("{25}{50}First\n{75}{100}Second\n") > 0);
    }
    void cleanupTestCase() { libvlc_release(m_vlc); }
    void readsRealCueIntervalsIncludingOverlaps() {
        auto result = scan(source());
        QCOMPARE(result.status, SubtitleTimelineScan::Complete);
        QCOMPARE(result.cues.size(), 3);
        check(result.cues[0], 1, 2.5);
        check(result.cues[1], 2, 4);
        check(result.cues[2], 7, 8.25);
        QCOMPARE(result.cues[0].text, QStringLiteral("First"));
        QCOMPARE(result.cues[1].text, QStringLiteral("Overlap"));
        QCOMPARE(result.cues[2].text, QStringLiteral("Later"));
    }
    void readsEmbeddedSubRipThroughAvformat() {
        module_t **demuxers = nullptr;
        size_t matches = 0;
        const auto count = vlc_module_match("demux", "avformat", true, &demuxers, &matches);
        free(demuxers);
        if (count <= 0) QSKIP("avformat demux unavailable");
        // Two Matroska S_TEXT/UTF8 tracks, without audio or video. Track zero
        // has three overlapping cues; track one has one Unicode cue.
        const auto bytes = QByteArray::fromBase64(
            "GkXfo6NChoEBQveBAULygQRC84EIQoKIbWF0cm9za2FCh4EEQoWBAhhTgGcBAAAAAAACkxFNm3TAv4QDdI8mTbuLU6uEFUmpZlOs"
            "gaFNu4tTq4QWVK5rU6yBzE27jFOrhBJUw2dTrIIBPE27jFOrhBxTu2tTrIICMOwBAAAAAAAAUwAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAFUmpZqa/hOOUNzcq"
            "17GDD0JATYCETGF2ZldBhExhdmZEiYhAwB0AAAAAABZUrmvrv4SMgm0orgEAAAAAAAAo14EBc8WIAAAAAAAAAAGcgQAitZyDdW5k"
            "hotTX1RFWFQvVVRGOIOBEa4BAAAAAAAAK9eBAnPFiAAAAAAAAAACnIEAIrWcg3VuZIiBAIaLU19URVhUL1VURjiDgRESVMNnQIC/"
            "hPT7Hd1zcwEAAAAAAAAzY8CLY8WIAAAAAAAAAAFnyKJFo4hEVVJBVElPTkSHlDAwOjAwOjA4LjI1MDAwMDAwMAAAc3MBAAAAAAAA"
            "M2PAi2PFiAAAAAAAAAACZ8iiRaOIRFVSQVRJT05Eh5QwMDowMDowMS41MDAwMDAwMDAAAB9DtnXJv4Q2QcAA54IB9KCZoZOCAAAA"
            "w4l0w6kg5pel5pys6Kqem4ID6KCPoYmBAfQARmlyc3SbggXcoJGhi4EF3ABPdmVybGFwm4IH0B9DtnWbv4TZTqfO54IbWKCPoYmB"
            "AAAATGF0ZXKbggTiHFO7a96/hHQ4KiO7lLOCAfS3jveBAvGCAcLwgQqyggPou5SzggPot473gQHxggHC8IElsoIF3LuUs4IH0LeO"
            "94EB8YIBwvCBNrKCB9C7lLOCG1i3jveBAfGCAhDwgQqyggTi");
        QFile file(m_dir.filePath(QStringLiteral("embedded.mks")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        auto request = source(QStringLiteral("embedded.mks"));
        request.demux = "avformat";
        auto result = scan(request);
        QCOMPARE(result.status, SubtitleTimelineScan::Complete);
        QCOMPARE(result.cues.size(), 3);
        check(result.cues[0], 1, 2.5);
        check(result.cues[1], 2, 4);
        check(result.cues[2], 7, 8.25);
        request.subtitleIndex = 1;
        result = scan(request);
        QCOMPARE(result.status, SubtitleTimelineScan::Complete);
        QCOMPARE(result.cues.size(), 1);
        check(result.cues[0], .5, 1.5);
        QCOMPARE(result.cues[0].text, QString::fromUtf8("Été 日本語"));
    }
    void collectsFormattedMultilineTextAsPlainDialogue() {
        QFile file(m_dir.filePath(QStringLiteral("styled.srt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("1\n00:00:01,000 --> 00:00:03,000\n<b>Café</b> 日本語\nSecond line &amp; more\n\n") > 0);
        file.close();
        const auto result = scan(source(QStringLiteral("styled.srt")));
        QCOMPARE(result.status, SubtitleTimelineScan::Complete);
        QCOMPARE(result.cues.size(), 1);
        QCOMPARE(result.cues[0].text, QString::fromUtf8("Café 日本語 Second line &amp; more"));
    }
    void collectsAssTextWithoutChangingPlayerAppearance() {
        if (!module_exists("libass")) QSKIP("ASS decoder unavailable");
        QFile file(m_dir.filePath(QStringLiteral("styled.ass")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 360\n"
            "[V4+ Styles]\nFormat: Name, Fontname, Fontsize\nStyle: Default,Arial,24\n"
            "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\i1}ASS{\\i0}\\NSecond line\n"
            "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,{\\i1}ASS{\\i0}\\NSecond line\n"
            "Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,Other speaker\n"
            "Dialogue: 0,0:00:04.00,0:00:06.00,Default,,0,0,0,,{\\p1}m 0 0 l 40 0 40 40 0 40{\\p0}\n") > 0);
        file.close();
        const auto embedded = config_GetInt("sub-text-use-embedded-style");
        const auto result = scan(source(QStringLiteral("styled.ass")));
        QCOMPARE(config_GetInt("sub-text-use-embedded-style"), embedded);
        QCOMPARE(result.status, SubtitleTimelineScan::Complete);
        QCOMPARE(result.cues.size(), 2);
        QCOMPARE(result.cues[0].text, QStringLiteral("ASS Second line · Other speaker"));
        QVERIFY(result.cues[1].text.isEmpty());
        check(result.cues[1], 4, 6);
    }
    void boundsLongTextWithoutBreakingUnicode() {
        QFile file(m_dir.filePath(QStringLiteral("long.srt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto text = QStringLiteral("x") + QString::fromUtf8("😀").repeated(2000);
        const auto data = (QStringLiteral("1\n00:00:01,000 --> 00:00:03,000\n") + text + QStringLiteral("\n\n")).toUtf8();
        QCOMPARE(file.write(data), data.size());
        file.close();
        const auto result = scan(source(QStringLiteral("long.srt")));
        QCOMPARE(result.status, SubtitleTimelineScan::Complete);
        QCOMPARE(result.cues.size(), 1);
        const auto caption = result.cues[0].text;
        QVERIFY(caption.size() <= 2048);
        QVERIFY(caption.startsWith(QString::fromUtf8("x😀")));
        QVERIFY(!caption.back().isHighSurrogate());
        QVERIFY(!caption.contains(QChar::ReplacementCharacter));
    }
    void appliesNativeSubtitleRate() {
        auto request = source(); request.subtitleRate = .5f;
        auto result = scan(request);
        QCOMPARE(result.status, SubtitleTimelineScan::Complete);
        QCOMPARE(result.cues.size(), 3);
        check(result.cues[0], .5, 1.25);
        check(result.cues[2], 3.5, 4.125);
    }
    void usesOriginalFrameRateForFrameBasedSubtitles() {
        auto request = source(QStringLiteral("frames.sub")); request.originalFPS = 25;
        auto result = scan(request);
        QCOMPARE(result.status, SubtitleTimelineScan::Complete);
        QCOMPARE(result.cues.size(), 2);
        check(result.cues[0], 1, 2);
        check(result.cues[1], 3, 4);
    }
    void missingTrackDoesNotInventCues() {
        auto request = source(); request.subtitleIndex = 1;
        auto result = scan(request);
        QCOMPARE(result.status, SubtitleTimelineScan::TrackMissing);
        QVERIFY(result.cues.isEmpty());
    }
    void missingSourceFailsCleanly() {
        auto result = scan(source(QStringLiteral("missing.srt")));
        QCOMPARE(result.status, SubtitleTimelineScan::Failed);
        QVERIFY(result.cues.isEmpty());
    }
    void boundedCueCountIsReportedAsIncomplete() {
        auto request = source(); request.maximumCues = 1;
        auto result = scan(request);
        QCOMPARE(result.status, SubtitleTimelineScan::LimitReached);
        QCOMPARE(result.cues.size(), 1);
    }
    void expiredDeadlineDoesNotReadTheSource() {
        auto request = source(); request.deadline = vlc_tick_now() - 1;
        auto result = scan(request);
        QCOMPARE(result.status, SubtitleTimelineScan::LimitReached);
        QVERIFY(result.cues.isEmpty());
    }
    void alreadyCancelledDoesNotReadTheSource() {
        auto result = scan(source(), true);
        QCOMPARE(result.status, SubtitleTimelineScan::Cancelled);
        QVERIFY(result.cues.isEmpty());
    }
    void cancelsAnActiveBlockedRead() {
        if (!module_exists("https") && !module_exists("http")) QSKIP("HTTP access unavailable");
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto request = source();
        request.uri = QStringLiteral("http://127.0.0.1:%1/cues.srt").arg(server.serverPort()).toUtf8();
        auto *interrupt = vlc_interrupt_create();
        auto future = std::async(std::launch::async, [this, request, interrupt] {
            return scanSubtitleTimeline(VLC_OBJECT(m_vlc->p_libvlc_int), request, interrupt);
        });
        QTcpSocket *socket = nullptr;
        const auto cleanup = qScopeGuard([&] {
            vlc_interrupt_kill(interrupt);
            if (socket) socket->abort();
            server.close();
            if (future.valid()) future.wait();
            vlc_interrupt_destroy(interrupt);
        });
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
        socket = server.nextPendingConnection();
        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 1000000\r\nContent-Type: application/x-subrip\r\n\r\n");
        socket->flush();
        QTest::qWait(100);
        QVERIFY(future.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
        vlc_interrupt_kill(interrupt);
        QVERIFY(future.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        QCOMPARE(future.get().status, SubtitleTimelineScan::Cancelled);
    }
};

QTEST_GUILESS_MAIN(SubtitleTimelineScannerTest)
#include "test_subtitle_timeline_scanner.moc"
