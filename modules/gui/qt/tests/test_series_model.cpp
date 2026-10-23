// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "player/series_model.hpp"

class TestSeriesModel : public QObject
{
    Q_OBJECT
    static QVariantMap episode(int season, int number, const QString& guid, const QString& url, int sourceIndex) {
        return {{"title", "A title"}, {"mrl", url}, {"sourceIndex", sourceIndex}, {"progress", 0.25},
            {"podcast", QVariantMap{{"feed", "https://example.invalid/feed"}, {"guid", guid},
                {"season", season < 0 ? "" : QString::number(season)}, {"episode", number < 0 ? "" : QString::number(number)},
                {"description", "Episode notes"}, {"date", "2025-01-01T00:00:00Z"}}}};
    }
    static QVariantMap podcast() {
        return {{"type", "podcastEpisode"}, {"feedUrl", "https://example.invalid/feed"},
            {"series", "Podcast"}, {"guid", "playing"}, {"season", 3}, {"episode", 2}};
    }
private slots:
    void feedSeasonsGuidIdentityAndOrdering() {
        SeriesModel model;
        model.setCurrent(QUrl("https://example.invalid/old-token.mp3"), podcast());
        QVERIFY(model.available()); QCOMPARE(model.showTitle(), "Podcast");
        QVariantList episodes{episode(3, 10, "last", "https://example.invalid/10.mp3", 0),
            episode(3, 2, "playing", "https://example.invalid/new-token.mp3", 1),
            episode(0, 1, "special", "https://example.invalid/special.mp3", 2),
            episode(-1, -1, "unnumbered", "https://example.invalid/extra.mp3", 3)};
        model.setFeedEpisodes(model.feed(), episodes);
        QCOMPARE(model.seasons(), (QVariantList{-1, 0, 3})); QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.get(0).value("number").toString(), "2");
        QVERIFY(model.get(0).value("current").toBool());
        QCOMPARE(model.get(0).value("feedIndex").toInt(), 1);
        model.setProgress(QUrl("https://example.invalid/old-token.mp3"), .6);
        QCOMPARE(model.get(0).value("progress").toDouble(), .6);
        model.setSelectedSeason(-1); QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.get(0).value("number").toString(), "");
        auto meta = podcast(); meta["description"] = "New notes";
        model.setCurrent(QUrl("https://example.invalid/old-token.mp3"), meta);
        QCOMPARE(model.selectedSeason(), -1); // Metadata refresh does not move the browser.
    }
    void unrelatedFeedAndOldScanCannotReplaceCurrentContext() {
        QTemporaryDir base;
        const auto local = QUrl::fromLocalFile(base.path() + "/Show.S01E01.mkv");
        QFile file(local.toLocalFile()); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        SeriesModel model; model.setCurrent(local, {});
        model.setCurrent(QUrl("https://example.invalid/current.mp3"), podcast());
        const QVariantList episodes{episode(3, 2, "playing", "https://example.invalid/current.mp3", 0)};
        model.setFeedEpisodes(QUrl("https://example.invalid/other-feed"), episodes);
        QCOMPARE(model.rowCount(), 0);
        model.setFeedEpisodes(model.feed(), episodes);
        QTest::qWait(100);
        QCOMPARE(model.rowCount(), 1); QCOMPARE(model.showTitle(), "Podcast");
        model.setCurrent(QUrl("https://example.invalid/movie.mp4"), {});
        QVERIFY(!model.available()); QCOMPARE(model.rowCount(), 0);
    }
    void localProgressVersionsAndSnapshotReuse() {
        QTemporaryDir base;
        QList<QUrl> urls;
        for (const auto* name : {"Show.S01E01.mkv", "Show.S01E01.1080p.mkv", "Show.S02E01.mkv"}) {
            const auto path = base.path() + '/' + name;
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
            urls.append(QUrl::fromLocalFile(path));
        }
        SeriesModel model;
        QAbstractItemModelTester checker(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setCurrent(urls[0], {});
        QTRY_VERIFY(!model.loading()); QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.get(0).value("urls").toList().size(), 2);
        model.setProgress(urls[0], .4); QCOMPARE(model.get(0).value("progress").toDouble(), .4);
        model.setPlayed(urls[0], true); QVERIFY(model.get(0).value("played").toBool());
        model.setProgress(QUrl("file:///other.mkv"), .8);
        QCOMPARE(model.get(0).value("progress").toDouble(), .4);
        model.setCurrent(urls[2], {});
        QVERIFY(!model.loading()); QCOMPARE(model.currentSeason(), 2); QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.get(0).value("current").toBool());
        model.setSelectedSeason(1); QVERIFY(!model.get(0).value("current").toBool());
    }
    void destructionDuringScanIsSafe() {
        QTemporaryDir base;
        const auto url = QUrl::fromLocalFile(base.path() + "/Show.S01E01.mkv");
        auto* model = new SeriesModel; model->setCurrent(url, {}); delete model;
        QTest::qWait(50);
    }
};
QTEST_GUILESS_MAIN(TestSeriesModel)
#include "test_series_model.moc"
