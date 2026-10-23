// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include "util/media_fingerprint.hpp"
#include "util/media_identity.hpp"
#include "util/track_matching.hpp"
using namespace MediaIdentity;

class TestMediaIdentity : public QObject {
    Q_OBJECT
private slots:
    void filenames_data() {
        QTest::addColumn<QString>("filename"); QTest::addColumn<QString>("title");
        QTest::addColumn<int>("year"); QTest::addColumn<int>("season");
        QTest::addColumn<QList<int>>("episodes");
        QTest::newRow("movie") << "Dune.2021.1080p.BluRay.x264-GROUP.mkv" << "Dune" << 2021 << -1 << QList<int>{};
        QTest::newRow("remake") << "Dune (1984).mkv" << "Dune" << 1984 << -1 << QList<int>{};
        QTest::newRow("numeric-title") << "1917.2019.1080p.WEB-DL.mkv" << "1917" << 2019 << -1 << QList<int>{};
        QTest::newRow("numeric-prefix") << "2001.A.Space.Odyssey.1968.mkv" << "2001 A Space Odyssey" << 1968 << -1 << QList<int>{};
        QTest::newRow("year-as-title") << "1984.mkv" << "1984" << -1 << -1 << QList<int>{};
        QTest::newRow("punctuation") << "Spider-Man.No.Way.Home.2021.mkv" << "Spider-Man No Way Home" << 2021 << -1 << QList<int>{};
        QTest::newRow("part-of-title") << "Dune.Part.Two.2024.mkv" << "Dune Part Two" << 2024 << -1 << QList<int>{};
        QTest::newRow("part-one") << "Movie.Part.1.2024.mkv" << "Movie Part 1" << 2024 << -1 << QList<int>{};
        QTest::newRow("bracket-title") << "[REC].2007.mkv" << "[REC]" << 2007 << -1 << QList<int>{};
        QTest::newRow("unicode") << QString::fromUtf8("Le.Fabuleux.Destin.d’Amélie.Poulain.2001.mkv") << QString::fromUtf8("Le Fabuleux Destin d’Amélie Poulain") << 2001 << -1 << QList<int>{};
        QTest::newRow("episode") << "The.Show.S02E03.1080p.WEB-DL-GROUP.mkv" << "The Show" << -1 << 2 << QList<int>{3};
        QTest::newRow("series-year") << "The.Show.2005.S02E03.mkv" << "The Show" << 2005 << 2 << QList<int>{3};
        QTest::newRow("x-episode") << "The_Show.2x03.en.srt" << "The Show" << -1 << 2 << QList<int>{3};
        QTest::newRow("long-episode") << "The Show Season 2 Episode 3.mkv" << "The Show" << -1 << 2 << QList<int>{3};
        QTest::newRow("multi") << "The.Show.S02E03E04.mkv" << "The Show" << -1 << 2 << QList<int>{3,4};
        QTest::newRow("range") << "The.Show.S02E03-E05.mkv" << "The Show" << -1 << 2 << QList<int>{3,4,5};
        QTest::newRow("multi-x") << "The.Show.2x03x04.mkv" << "The Show" << -1 << 2 << QList<int>{3,4};
        QTest::newRow("special") << "The.Show.S00E01.mkv" << "The Show" << -1 << 0 << QList<int>{1};
        QTest::newRow("season-pack") << "The.Show.S02.1080p.mkv" << "The Show" << -1 << 2 << QList<int>{};
        QTest::newRow("episode-only") << "S02E03.mkv" << "" << -1 << 2 << QList<int>{3};
        QTest::newRow("unknown") << "video.mp4" << "" << -1 << -1 << QList<int>{};
        QTest::newRow("release-name-without-extension") << "The.Matrix.1999" << "The Matrix" << 1999 << -1 << QList<int>{};
    }
    void filenames() {
        QFETCH(QString,filename); QFETCH(QString,title); QFETCH(int,year); QFETCH(int,season); QFETCH(QList<int>,episodes);
        const auto value = parse(filename);
        QCOMPARE(value.title,title); QCOMPARE(value.year,year); QCOMPARE(value.season,season); QCOMPARE(value.episodes,episodes);
        const auto roundtrip = fromMap(toMap(value));
        QCOMPARE(roundtrip.title,value.title); QCOMPARE(roundtrip.episodes,value.episodes); QCOMPARE(roundtrip.kind,value.kind);
    }
    void releaseEvidence() {
        const auto value = parse("Dune.2021.Extended.1080p.WEB-DL.x265.FLAC-GROUP.mkv");
        QCOMPARE(value.title,"Dune"); QCOMPARE(value.release.value("source"),"webdl");
        QCOMPARE(value.release.value("resolution"),"1080p"); QCOMPARE(value.release.value("videoCodec"),"x265");
        QCOMPARE(value.release.value("audioCodec"),"flac"); QCOMPARE(value.release.value("group"),"group");
        QCOMPARE(value.release.value("edition"),"extended");
    }
    void datesAndAbsoluteNumbers() {
        const auto daily = parse("The.Daily.Show.2024.03.15.1080p.mkv");
        QCOMPARE(daily.title,"The Daily Show"); QCOMPARE(daily.airDate,QDate(2024,3,15)); QCOMPARE(daily.year,-1);
        const auto anime = parse("[Fansub] Anime Title - 452v2 [1080p][ABC12345].mkv");
        QCOMPARE(anime.title,"Anime Title"); QCOMPARE(anime.absoluteEpisode,"452"); QCOMPARE(anime.season,-1);
        QCOMPARE(anime.release.value("group"),"Fansub"); QVERIFY(anime.episodes.isEmpty());
        const auto fractional = parse("[Fansub] Anime Title - 12.5 [720p].mkv");
        QCOMPARE(fractional.absoluteEpisode,"12.5");
        auto other = anime; other.absoluteEpisode="453"; QVERIFY(compare(anime,other).conflict);
        auto seasonal = parse("Anime Title S01E452.mkv"); QVERIFY(!compare(anime,seasonal).strong);
    }
    void explicitIds() {
        const auto movie = parse("Dune (2021) [imdbid-tt1160419] [tmdbid-438631].mkv");
        QCOMPARE(movie.ids.value("imdb"),"tt1160419"); QCOMPARE(movie.ids.value("tmdbMovie"),"438631");
        const auto series = parse("Show.S01E01.[tmdb-438631].mkv");
        QCOMPARE(series.ids.value("seriesTmdb"),"438631"); QVERIFY(!series.ids.contains("tmdbMovie"));
    }
    void foldersAndMetadata() {
        const auto movie = identify(QUrl("file:///movies/Dune%20(2021)/video.mp4"));
        QCOMPARE(movie.title,"Dune"); QCOMPARE(movie.year,2021); QCOMPARE(movie.provenance.value("title"),"folder");
        const auto episode = identify(QUrl("file:///shows/Show/Season%202/S02E03.mkv"));
        QCOMPARE(episode.title,"Show"); QCOMPARE(episode.season,2); QCOMPARE(episode.episodes,QList<int>{3});
        const auto corrected = identify(QUrl("file:///Dune.1984.mkv"),{{"title","Dune"},{"year",2021},{"imdbId","tt1160419"}});
        QCOMPARE(corrected.year,2021); QCOMPARE(corrected.provenance.value("year"),"metadata");
        QVERIFY(corrected.conflicts.contains("year")); QCOMPARE(corrected.ids.value("imdb"),"tt1160419");
    }
    void comparisonProtectsIdentity() {
        QVERIFY(compare(parse("Dune.2021.mkv"),parse("Dune.2021.720p.srt")).strong);
        QVERIFY(compare(parse("Dune.2021.mkv"),parse("Dune.1984.srt")).conflict);
        QVERIFY(!compare(parse("Up.mkv"),parse("Up.in.the.Air.srt")).strong);
        QVERIFY(!compare(parse("Movie.Part.1.2024.mkv"),parse("Movie.Part.2.2024.srt")).strong);
        QVERIFY(compare(parse("Show.S01E03.mkv"),parse("Show.S01E04.srt")).conflict);
        QVERIFY(compare(parse("Show.S01E03E04.mkv"),parse("Show.S01E03.srt")).conflict);
        QVERIFY(!compare(parse("Show.S01E03.mkv"),parse("Show.S01.srt")).strong);
        QVERIFY(!compare(parse("Show.S01E03.mkv"),parse("Other.Show.S01E03.srt")).strong);
        QVERIFY(similarity("The Matrx","The Matrix")>.86);
        QVERIFY(!compare(parse("The.Matrx.1999.mkv"),parse("The.Matrix.1999.srt")).strong);
    }
    void fileFingerprint() {
        QTemporaryDir directory;
        QFile file(directory.filePath("video.mkv")); QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray bytes(3 * 65536, '\0'); bytes[0] = 1; bytes[bytes.size() - 1] = char(0x80);
        QCOMPARE(file.write(bytes),qint64(bytes.size())); file.close();
        const auto url = QUrl::fromLocalFile(file.fileName());
        const auto hash = MediaFingerprint::openSubtitles(url);
        QCOMPARE(hash.hash,"8000000000030001"); QCOMPARE(hash.size,qint64(bytes.size()));
        QVERIFY(MediaFingerprint::openSubtitles(url,[] { return true; }).hash.isEmpty());
        QVERIFY(MediaFingerprint::openSubtitles(QUrl("https://example.com/video.mkv")).hash.isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(1024)); file.close();
        QVERIFY(MediaFingerprint::openSubtitles(url).hash.isEmpty());
    }
    void localMatchingKeepsAmbiguityGuards() {
        const QUrl film("file:///Dune.2021.1080p.WEB-DL.mkv"), subtitle("file:///Dune.2021.720p.BluRay.srt");
        QCOMPARE(TrackMatching::match({film},{subtitle}).value(film),QList<QUrl>{subtitle});
        QVERIFY(TrackMatching::match({film,QUrl("file:///other/Dune.2021.mkv")},{subtitle}).isEmpty());
        QVERIFY(TrackMatching::match({film},{QUrl("file:///Dune.1984.srt")}).isEmpty());
        const QUrl show("file:///Show.S01E01E02.mkv");
        QVERIFY(TrackMatching::match({show},{QUrl("file:///Show.S01E01.srt")}).isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestMediaIdentity)
#include "test_media_identity.moc"
