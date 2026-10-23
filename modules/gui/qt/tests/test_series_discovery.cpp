// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "util/series_discovery.hpp"

class TestSeriesDiscovery : public QObject
{
    Q_OBJECT
    static QUrl file(const QTemporaryDir& base, const QString& path) {
        const auto full = QDir(base.path()).filePath(path);
        QDir().mkpath(QFileInfo(full).absolutePath());
        QFile output(full);
        if (!output.open(QIODevice::WriteOnly)) return {};
        return QUrl::fromLocalFile(full);
    }
private slots:
    void nestedSeasonsAndEpisodeWrappers() {
        QTemporaryDir base;
        const auto current = file(base, "Show/Season 123/Show S123E01/Show S123E01.mkv");
        file(base, "Show/Season 123/Show S123E02.mkv");
        file(base, "Show/Season 124/Show S124E01/video.mkv");
        file(base, "Other/Season 123/Other S123E01.mkv");
        const auto identity = SeriesDiscovery::identify(current);
        QCOMPARE(identity.title, "Show"); QCOMPARE(identity.season, 123);
        const auto result = SeriesDiscovery::scan(current, identity);
        QCOMPARE(result.root.toLocalFile(), base.path() + "/Show");
        QCOMPARE(result.episodes.size(), 3);
        QCOMPARE(result.episodes[2].identity.season, 124);
        QCOMPARE(result.episodes[2].identity.episodes, QList<int>{1});
        QVERIFY(!result.limited);
    }
    void flatMixedDirectoryAndVersions() {
        QTemporaryDir base;
        const auto current = file(base, "Show.S01E02.1080p.mkv");
        file(base, "Show.S01E02.2160p.mkv");
        file(base, "Show.S01E10.mkv");
        file(base, "Show.S02E01E02.mkv");
        file(base, "Other.S01E01.mkv");
        file(base, "Show.S01E02.srt");
        file(base, "Show.S01E02-thumb.jpg");
        const auto result = SeriesDiscovery::scan(current, SeriesDiscovery::identify(current));
        QCOMPARE(result.root.toLocalFile(), base.path());
        QCOMPARE(result.episodes.size(), 3);
        QCOMPARE(result.episodes[0].urls.size(), 2);
        QCOMPARE(result.episodes[0].identity.episodes, QList<int>{2});
        QCOMPARE(result.episodes[1].identity.episodes, QList<int>{10});
        QCOMPARE(result.episodes[2].identity.episodes, (QList<int>{1, 2}));
    }
    void folderNumberingSpecialsAndRemakes() {
        QTemporaryDir base;
        const auto current = file(base, "Show (2024)/Season 1/01.mkv");
        file(base, "Show (2024)/Season 1/02 - Pilot.mkv");
        file(base, "Show (2024)/Specials/03.mkv");
        file(base, "Show (2024)/Season 1/Show.1999.S01E01.mkv");
        const auto target = SeriesDiscovery::identify(current);
        QCOMPARE(target.title, "Show"); QCOMPARE(target.year, 2024);
        const auto result = SeriesDiscovery::scan(current, target);
        QCOMPARE(result.episodes.size(), 3);
        QCOMPARE(result.episodes[0].identity.season, 0);
        QCOMPARE(result.episodes[0].identity.episodes, QList<int>{3});
        QCOMPARE(result.episodes[2].identity.episodes, QList<int>{2});
        auto other = target; other.ids["seriesImdb"] = "tt0000001";
        auto conflicting = other; conflicting.ids["seriesImdb"] = "tt0000002";
        QVERIFY(!SeriesDiscovery::sameSeries(other, conflicting));
    }
    void datedAndAbsoluteEpisodesStaySeparate() {
        QTemporaryDir base;
        const auto current = file(base, "Show.2025.01.01.mkv");
        file(base, "Show.2025.01.02.mkv");
        const auto result = SeriesDiscovery::scan(current, SeriesDiscovery::identify(current));
        QCOMPARE(result.episodes.size(), 2);
        QCOMPARE(result.episodes[0].identity.season, -1);
        const auto absolute = SeriesDiscovery::identify(file(base, "Anime - 123.mkv"));
        QCOMPARE(absolute.absoluteEpisode, "123"); QCOMPARE(absolute.season, -1);
    }
    void boundsCancellationAndSymlinks() {
        QTemporaryDir base, outside;
        const auto current = file(base, "Show.S01E01.mkv");
        const auto foreign = file(outside, "Show.S01E02.mkv");
        QFile::link(outside.path(), base.path() + "/linked");
        auto result = SeriesDiscovery::scan(current, SeriesDiscovery::identify(current));
        QCOMPARE(result.episodes.size(), 1);
        std::atomic_bool cancelled {true};
        result = SeriesDiscovery::scan(current, SeriesDiscovery::identify(current), {}, {}, &cancelled);
        QVERIFY(result.episodes.isEmpty());
        SeriesDiscovery::Limits limits; limits.entries = 1;
        file(base, "Show.S01E03.mkv");
        result = SeriesDiscovery::scan(current, SeriesDiscovery::identify(current), {}, limits);
        QVERIFY(result.limited); QVERIFY(!result.episodes.isEmpty());
        limits = {}; limits.depth = 0;
        file(base, "nested/Show.S01E04.mkv");
        result = SeriesDiscovery::scan(current, SeriesDiscovery::identify(current), {}, limits);
        QVERIFY(result.limited);
        for (const auto& episode : result.episodes) QVERIFY(!episode.urls.contains(foreign));
    }
    void moviesAndRemoteUrlsDoNotTriggerDiscovery() {
        QTemporaryDir base;
        const auto movie = file(base, "Movie (2024).mkv");
        QVERIFY(SeriesDiscovery::scan(movie, SeriesDiscovery::identify(movie)).root.isEmpty());
        const QUrl remote("https://example.invalid/Show.S01E01.mkv");
        QVERIFY(SeriesDiscovery::scan(remote, SeriesDiscovery::identify(remote)).root.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestSeriesDiscovery)
#include "test_series_discovery.moc"
