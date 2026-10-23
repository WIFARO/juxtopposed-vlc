// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include "util/artwork_discovery.hpp"
#include "util/artwork_store.hpp"
using namespace ArtworkDiscovery;

class TestArtworkDiscovery : public QObject {
    Q_OBJECT
    static void touch(const QString& path) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); }
    static void image(const QString& path, QColor color = Qt::red) {
        QImage value(40, 60, QImage::Format_RGB32); value.fill(color); QVERIFY(value.save(path, "PNG"));
    }
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); QCoreApplication::setApplicationName("vlc-artwork-tests-" + QUuid::createUuid().toString(QUuid::WithoutBraces)); }
    void cleanupTestCase() { QDir(ArtworkStore::directory()).removeRecursively(); }
    void siblingOwnershipAndRoles() {
        QTemporaryDir dir;
        const auto media = QUrl::fromLocalFile(dir.filePath("Alien.1979.1080p.mkv"));
        touch(media.toLocalFile());
        image(dir.filePath("folder.png")); image(dir.filePath("Alien (1979)-poster.png"));
        image(dir.filePath("Alien (1979)-fanart.png")); image(dir.filePath("Alien.1979.1080p-thumb.png"));
        auto candidates = local(media, false);
        QCOMPARE(candidates.size(), 3);
        QCOMPARE(candidates.first().url.fileName(), "Alien.1979.1080p-thumb.png");
        for (const auto& candidate : candidates) QVERIFY(candidate.role != Role::Backdrop);
        const auto background = local(media, false, {}, Role::Backdrop);
        QCOMPARE(background.size(), 1); QCOMPARE(background.first().url.fileName(), "Alien (1979)-fanart.png");
        touch(dir.filePath("Alien.2025.mkv"));
        candidates = local(media, false);
        for (const auto& candidate : candidates) QVERIFY(candidate.url.fileName() != "folder.png");
    }
    void remakesEpisodesAndAmbiguity() {
        QTemporaryDir dir;
        image(dir.filePath("Dune (1984)-poster.png"));
        const auto dune = QUrl::fromLocalFile(dir.filePath("Dune.2021.mkv"));
        QVERIFY(local(dune, false).isEmpty());
        image(dir.filePath("Show.S01E02-thumb.png")); image(dir.filePath("poster.png"));
        touch(dir.filePath("Show.S01E01.mkv")); touch(dir.filePath("Show.S01E02.mkv"));
        QVERIFY(local(QUrl::fromLocalFile(dir.filePath("Show.S01E01.mkv")), false).isEmpty());
        image(dir.filePath("Dune.2021.720p-poster.png")); image(dir.filePath("Dune.2021.1080p-poster.png"));
        QVERIFY(local(dune, false).isEmpty()); // two equally plausible identity matches
        image(dir.filePath("Dune.2021.png"));
        QCOMPARE(local(dune, false).first().url.fileName(), "Dune.2021.png");
    }
    void folderArtAndBoundedChildren() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("Album/Disc 1"));
        touch(dir.filePath("z.mkv")); touch(dir.filePath("a.mkv"));
        touch(dir.filePath("Album/Disc 1/01 - Song.flac"));
        image(dir.filePath("Album/cover.png")); image(dir.filePath("fanart.png"));
        const auto url = QUrl::fromLocalFile(dir.path());
        QVERIFY(local(url, true).isEmpty());
        image(dir.filePath("folder.png"));
        QCOMPARE(local(url, true).first().url.fileName(), "folder.png");
        const auto children = ArtworkDiscovery::children(url);
        QVERIFY(children.urls.contains(QUrl::fromLocalFile(dir.filePath("Album"))));
        QVERIFY(!children.urls.contains(QUrl::fromLocalFile(dir.filePath("Album/Disc 1/01 - Song.flac")))); // represented by its album cover
        const auto a = children.urls.indexOf(QUrl::fromLocalFile(dir.filePath("a.mkv")));
        const auto z = children.urls.indexOf(QUrl::fromLocalFile(dir.filePath("z.mkv")));
        QVERIFY(a >= 0 && a < z);
        QCOMPARE(children.urls, ArtworkDiscovery::children(url).urls);
        for (int i = 0; i < 50; ++i) touch(dir.filePath(QString("video%1.mp4").arg(i)));
        QVERIFY(ArtworkDiscovery::children(url).urls.size() <= 32);
        QVERIFY(ArtworkDiscovery::children(url, [] { return true; }).urls.isEmpty());
        QVERIFY(ArtworkDiscovery::children(QUrl("https://example.com/feed")).urls.isEmpty());
    }
    void persistentOverridesAndReset() {
        QTemporaryDir dir;
        const auto target = QUrl::fromLocalFile(dir.filePath("media.mkv"));
        const auto source = QUrl::fromLocalFile(dir.filePath("chosen.png"));
        image(source.toLocalFile());
        QVERIFY(ArtworkStore::save(target, source));
        const auto chosen = ArtworkStore::selected(target);
        QVERIFY(!chosen.isEmpty()); QVERIFY(chosen != source.toString());
        QVERIFY(QFile::remove(source.toLocalFile()));
        QVERIFY(!ArtworkStore::read(QUrl(chosen).toLocalFile()).isNull());
        touch(dir.filePath("broken.jpg"));
        QVERIFY(!ArtworkStore::save(target, QUrl::fromLocalFile(dir.filePath("broken.jpg"))));
        QCOMPARE(ArtworkStore::selected(target), chosen);
        QVERIFY(ArtworkStore::save(target, {}));
        QVERIFY(ArtworkStore::selected(target).isEmpty());
        QVERIFY(!QFile::exists(QUrl(chosen).toLocalFile()));
    }
    void reservedMediaBasenamesKeepExactOwnership() {
        QTemporaryDir dir;
        touch(dir.filePath("movie.mp4")); touch(dir.filePath("Other (2024).mkv"));
        image(dir.filePath("movie.png"));
        const auto candidates = local(QUrl::fromLocalFile(dir.filePath("movie.mp4")), false);
        QCOMPARE(candidates.size(), 1); QCOMPARE(candidates.first().score / 100, 300);
    }
    void albumOwnershipAndDiscInheritance() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("Album/Disc 1"));
        image(dir.filePath("Album/cover.png"));
        const auto url = QUrl::fromLocalFile(dir.filePath("Album/Disc 1/01 - Song.flac"));
        const auto candidates = local(url, false);
        QVERIFY(!candidates.isEmpty()); QCOMPARE(candidates.first().url.fileName(), "cover.png");
        auto other = MediaIdentity::identify(url, {{"album", "Different record"}});
        QVERIFY(local(url, false, other).isEmpty());
        other.title.clear(); // Missing track titles must not discard known album evidence.
        QVERIFY(local(url, false, other).isEmpty());
        QCOMPARE(normalizeUrl(QUrl("file:///")), QUrl("file:///"));
        QVERIFY(ArtworkStore::key(QUrl("https://example.com/feed/")) != ArtworkStore::key(QUrl("https://example.com/feed")));
        QCOMPARE(workKey(QUrl("file:///Dune.2021.720p.mkv")), workKey(QUrl("file:///Dune.2021.1080p.mkv")));
        QVERIFY(workKey(QUrl("file:///Dune.2021.mkv")) != workKey(QUrl("file:///Dune.1984.mkv")));
    }
    void collageLayouts() {
        QList<QImage> images;
        for (const auto color : {Qt::red, Qt::green, Qt::blue, Qt::yellow, Qt::magenta}) {
            QImage value(20, 20, QImage::Format_RGB32); value.fill(color); images.append(value);
        }
        QVERIFY(ArtworkStore::collage({}).isNull());
        auto result = ArtworkStore::collage(images.mid(0, 1));
        QCOMPARE(result.pixelColor(500, 300), QColor(Qt::red));
        result = ArtworkStore::collage(images.mid(0, 2));
        QCOMPARE(result.pixelColor(20, 300), QColor(Qt::red)); QCOMPARE(result.pixelColor(500, 300), QColor(Qt::green));
        result = ArtworkStore::collage(images.mid(0, 3));
        QCOMPARE(result.pixelColor(20, 300), QColor(Qt::red)); QCOMPARE(result.pixelColor(500, 20), QColor(Qt::green));
        QCOMPARE(result.pixelColor(500, 300), QColor(Qt::blue));
        result = ArtworkStore::collage(images);
        QCOMPARE(result.pixelColor(20, 300), QColor(Qt::blue)); QCOMPARE(result.pixelColor(500, 300), QColor(Qt::yellow));
    }
};
QTEST_GUILESS_MAIN(TestArtworkDiscovery)
#include "test_artwork_discovery.moc"
