// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <limits>
#include "util/redesign_folder_model.hpp"

class TestRedesignFolderModel : public QObject
{
    Q_OBJECT
    static void touch(const QString& path) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
private slots:
    void playedRoleUpdatesCaptionsWithoutChangingTitleOrder() {
        QTemporaryDir directory, other;
        touch(QDir(directory.path()).filePath("a.mp4"));
        touch(QDir(directory.path()).filePath("b.mp4"));
        touch(QDir(other.path()).filePath("a.mp4"));
        RedesignFolderModel model;
        QSignalSpy requested(&model, &RedesignFolderModel::artworkRequested);
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 2);
        const auto url = model.get(0, "fileUrl").toUrl();
        model.get(0, "thumbnail");
        QTRY_COMPARE(requested.count(), 1);
        QSignalSpy changed(&model, &RedesignFolderModel::dataChanged);
        QSignalSpy reset(&model, &RedesignFolderModel::modelReset);
        model.setPlayed(url, true);
        QVERIFY(model.get(0, "played").toBool());
        QCOMPARE(model.get(0, "fileUrl").toUrl(), url);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed[0][2].value<QList<int>>(), QList<int>{model.roleNames().key("played")});
        model.setPlayed(url, false);
        QVERIFY(!model.get(0, "played").toBool());
        QCOMPARE(changed.count(), 2);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(requested.count(), 1);
        model.setFolder(QUrl::fromLocalFile(other.path()));
        QTRY_COMPARE(model.count(), 1);
        model.setPlayed(url, true);
        QVERIFY(!model.get(0, "played").toBool());
    }
    void audioMetadataSortAndSearchKeepFileIdentity() {
        QTemporaryDir directory;
        QDir dir(directory.path());
        QVERIFY(dir.mkdir("folder"));
        for (const auto* name : {"01.mp3", "02.mp3", "03.mp3"}) touch(dir.filePath(name));
        RedesignFolderModel model;
        model.setAudioMetadataEnabled(true);
        QSignalSpy requested(&model, &RedesignFolderModel::audioMetadataRequested);
        QSignalSpy artwork(&model, &RedesignFolderModel::artworkRequested);
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 4);
        QTRY_COMPARE(requested.count(), 1);
        QCOMPARE(requested[0][0].value<QList<QUrl>>().size(), 3);
        const auto a = model.get(1, "fileUrl").toUrl();
        const auto b = model.get(2, "fileUrl").toUrl();
        const auto c = model.get(3, "fileUrl").toUrl();
        QPersistentModelIndex selected(model.index(1));
        QSignalSpy reset(&model, &RedesignFolderModel::modelReset);
        model.setMetadata(a, "Zulu", "Alpha artist");
        model.setMetadata(b, "Alpha", "Zulu artist");
        model.setMetadata(c, "", "Beta artist");
        model.finishAudioMetadata(requested[0][1].toULongLong());
        QTRY_COMPARE(model.get(1, "fileUrl").toUrl(), c);
        QCOMPARE(model.get(2, "fileUrl").toUrl(), b);
        QCOMPARE(selected.row(), 3);
        QCOMPARE(artwork.count(), 0); // Cached metadata does not decode unseen media.
        QCOMPARE(model.get(0, "title").toString(), "folder");
        QCOMPARE(model.get(1, "title").toString(), "03.mp3"); // Untagged title fallback.
        QCOMPARE(model.get(2, "title").toString(), "Alpha");
        QCOMPARE(model.get(2, "main_artist").toString(), "Zulu artist");
        model.setSortCriteria("main_artist");
        QTRY_COMPARE(model.get(1, "fileUrl").toUrl(), a);
        QCOMPARE(selected.row(), 1);
        model.setSortReversed(true);
        QTRY_COMPARE(model.get(1, "fileUrl").toUrl(), b);
        QCOMPARE(model.get(3, "fileUrl").toUrl(), a);
        QCOMPARE(reset.count(), 0);
        model.setSearchPattern("BETA ARTIST");
        QTRY_COMPARE(model.count(), 1);
        QCOMPARE(model.get(0, "fileUrl").toUrl(), c);
        model.setSearchPattern("Zulu");
        QTRY_COMPARE(model.count(), 2); // Title and artist matches both work.
        model.setSearchPattern("02.mp3");
        QTRY_COMPARE(model.count(), 1);
        QCOMPARE(model.get(0, "fileUrl").toUrl(), b);
        model.setSearchPattern("");
        QTRY_COMPARE(model.count(), 4);
        QCOMPARE(requested.count(), 1); // Sort and search use the cached folder snapshot.
    }
    void audioMetadataCanRevealAHiddenSearchResult() {
        QTemporaryDir directory;
        touch(QDir(directory.path()).filePath("01.mp3"));
        RedesignFolderModel model;
        model.setAudioMetadataEnabled(true);
        model.setSearchPattern("Artist");
        QSignalSpy requested(&model, &RedesignFolderModel::audioMetadataRequested);
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(requested.count(), 1);
        QCOMPARE(model.count(), 0);
        const auto url = requested[0][0].value<QList<QUrl>>()[0];
        model.setMetadata(url, "A title", "An Artist");
        model.finishAudioMetadata(requested[0][1].toULongLong());
        QTRY_COMPARE(model.count(), 1);
        QCOMPARE(model.get(0, "fileUrl").toUrl(), url);
        QVERIFY(!model.audioMetadataPending());
    }
    void oldAudioMetadataCannotChangeAnotherFolderOrVideoView() {
        QTemporaryDir first, second;
        touch(QDir(first.path()).filePath("01.mp3"));
        touch(QDir(second.path()).filePath("01.mp3"));
        RedesignFolderModel model;
        model.setAudioMetadataEnabled(true);
        QSignalSpy requested(&model, &RedesignFolderModel::audioMetadataRequested);
        model.setFolder(QUrl::fromLocalFile(first.path()));
        QTRY_COMPARE(requested.count(), 1);
        QTRY_COMPARE(model.count(), 1);
        const auto url = model.get(0, "fileUrl").toUrl();
        model.setFolder(QUrl::fromLocalFile(second.path()));
        QTRY_COMPARE(requested.count(), 2);
        model.setMetadata(url, "Old title", "Old artist");
        model.finishAudioMetadata(requested[0][1].toULongLong());
        QVERIFY(model.audioMetadataPending());
        QTRY_COMPARE(model.count(), 1);
        QCOMPARE(model.get(0, "title").toString(), "01.mp3");
        const auto current = model.get(0, "fileUrl").toUrl();
        model.setMetadata(current, "Current", "Artist");
        QCOMPARE(model.get(0, "title").toString(), "Current");
        model.setAudioMetadataEnabled(false);
        model.setMetadata(current, "Late", "Late");
        model.finishAudioMetadata(requested[1][1].toULongLong());
        QCOMPARE(model.get(0, "title").toString(), "01.mp3");
        QVERIFY(!model.audioMetadataPending());
    }
    void watchSortingKeepsDirectoriesFirstAndPreservesFileIdentity() {
        QTemporaryDir directory;
        QDir dir(directory.path());
        QVERIFY(dir.mkdir("folder"));
        for (const auto* name : {"a.mp4", "b.mp4", "c.mp4", "d.mp4"}) touch(dir.filePath(name));
        RedesignFolderModel model;
        QSignalSpy requested(&model, &RedesignFolderModel::watchStatesRequested);
        QSignalSpy artwork(&model, &RedesignFolderModel::artworkRequested);
        QSignalSpy reset(&model, &RedesignFolderModel::modelReset);
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 5);
        const auto a = model.get(1, "fileUrl").toUrl();
        const auto c = model.get(3, "fileUrl").toUrl();
        QPersistentModelIndex selected(model.index(1));
        reset.clear();
        model.setSortCriteria("played");
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested[0][0].value<QList<QUrl>>().size(), 4);
        QVERIFY(model.watchStatesPending());
        model.setPlayed(a, true); model.setPlayed(c, true);
        model.finishWatchStates(requested[0][1].toULongLong());
        QTRY_COMPARE(model.get(1, "fileName").toString(), "b.mp4");
        QCOMPARE(model.get(2, "fileName").toString(), "d.mp4"); // No library record means unwatched.
        QCOMPARE(model.get(3, "fileName").toString(), "a.mp4");
        QCOMPARE(model.get(4, "fileName").toString(), "c.mp4");
        QCOMPARE(model.get(0, "fileName").toString(), "folder");
        QCOMPARE(selected.row(), 3);
        QCOMPARE(selected.data(RedesignFolderModel::FileUrl).toUrl(), a);
        QCOMPARE(artwork.count(), 0); // Sorting does not request or decode thumbnails.
        model.setSortReversed(true);
        QTRY_COMPARE(model.get(1, "fileName").toString(), "a.mp4");
        QCOMPARE(model.get(2, "fileName").toString(), "c.mp4");
        QCOMPARE(model.get(3, "fileName").toString(), "b.mp4");
        QCOMPARE(selected.row(), 1);
        QCOMPARE(requested.count(), 1); // Reversing groups reuses the metadata.
        model.setPlayed(a, false);
        QTRY_COMPARE(model.get(1, "fileName").toString(), "c.mp4");
        QCOMPARE(selected.row(), 2);
        QCOMPARE(selected.data(RedesignFolderModel::FileUrl).toUrl(), a);
        QCOMPARE(model.get(0, "fileName").toString(), "folder");
        QCOMPARE(reset.count(), 0);
    }
    void delayedWatchStatesCannotFinishAnotherSearchOrFolder() {
        QTemporaryDir first, second;
        touch(QDir(first.path()).filePath("a.mp4"));
        touch(QDir(first.path()).filePath("b.mp4"));
        touch(QDir(second.path()).filePath("b.mp4"));
        RedesignFolderModel model;
        QSignalSpy requested(&model, &RedesignFolderModel::watchStatesRequested);
        model.setFolder(QUrl::fromLocalFile(first.path()));
        QTRY_COMPARE(model.count(), 2);
        const auto oldUrl = model.get(1, "fileUrl").toUrl();
        model.setSortCriteria("played");
        QCOMPARE(requested.count(), 1);
        const auto old = requested[0][1].toULongLong();
        model.setSearchPattern("b");
        QTRY_COMPARE(requested.count(), 2);
        model.finishWatchStates(old);
        QVERIFY(model.watchStatesPending());
        QCOMPARE(requested[1][0].value<QList<QUrl>>(), QList<QUrl>{oldUrl});
        const auto filtered = requested[1][1].toULongLong();
        model.finishWatchStates(filtered);
        QVERIFY(!model.watchStatesPending());
        model.setFolder(QUrl::fromLocalFile(second.path()));
        QTRY_COMPARE(requested.count(), 3);
        model.finishWatchStates(filtered);
        QVERIFY(model.watchStatesPending());
        model.setPlayed(oldUrl, true);
        model.finishWatchStates(requested[2][1].toULongLong());
        QVERIFY(!model.watchStatesPending());
        QCOMPARE(model.get(0, "fileUrl").toUrl(), QUrl::fromLocalFile(QDir(second.path()).filePath("b.mp4")));
        model.setSortCriteria("title");
        model.finishWatchStates(requested[2][1].toULongLong());
        QCOMPARE(model.sortCriteria(), "title");
        QVERIFY(!model.watchStatesPending());
    }
    void watchDirectionCanChangeBeforeMetadataArrives() {
        QTemporaryDir directory;
        touch(QDir(directory.path()).filePath("a.mp4"));
        touch(QDir(directory.path()).filePath("z.mp4"));
        RedesignFolderModel model;
        QSignalSpy requested(&model, &RedesignFolderModel::watchStatesRequested);
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 2);
        const auto z = model.get(1, "fileUrl").toUrl();
        model.setSortCriteria("played");
        model.setSortReversed(true);
        model.setPlayed(z, true);
        model.finishWatchStates(requested[0][1].toULongLong());
        QTRY_COMPARE(model.get(0, "fileName").toString(), "z.mp4");
        QCOMPARE(requested.count(), 1);
    }
    void progressSharesTheLazyArtworkRequestAndUpdatesOnlyItsRole() {
        QTemporaryDir directory;
        QDir dir(directory.path());
        QVERIFY(dir.mkdir("folder"));
        touch(dir.filePath("movie.mp4"));
        RedesignFolderModel model;
        QSignalSpy requested(&model, &RedesignFolderModel::artworkRequested);
        QSignalSpy changed(&model, &RedesignFolderModel::dataChanged);
        QSignalSpy reset(&model, &RedesignFolderModel::modelReset);
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 2);
        reset.clear();
        QCOMPARE(model.get(0, "progress").toDouble(), 0.0);
        QCOMPARE(requested.count(), 0);
        QCOMPARE(model.get(1, "progress").toDouble(), 0.0);
        model.get(1, "thumbnail");
        QTRY_COMPARE(requested.count(), 1);
        const auto url = model.get(1, "fileUrl").toUrl();
        model.setProgress(url, .45);
        QCOMPARE(model.get(1, "progress").toDouble(), .45);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed[0][2].value<QList<int>>(), QList<int>{RedesignFolderModel::Progress});
        model.setProgress(url, .45);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(requested.count(), 1);
    }
    void progressIsBoundedAndRefreshClearsIt() {
        QTemporaryDir directory;
        touch(QDir(directory.path()).filePath("movie.mp4"));
        RedesignFolderModel model;
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 1);
        const auto url = model.get(0, "fileUrl").toUrl();
        model.setProgress(url, .8);
        QCOMPARE(model.get(0, "progress").toDouble(), 0.0);
        model.setProgress(url, 2);
        QCOMPARE(model.get(0, "progress").toDouble(), 1.0);
        model.setProgress(url, -1);
        QCOMPARE(model.get(0, "progress").toDouble(), 0.0);
        model.setProgress(url, .6);
        model.setProgress(url, std::numeric_limits<qreal>::quiet_NaN());
        QCOMPARE(model.get(0, "progress").toDouble(), 0.0);
        model.setProgress(url, .6);
        model.setProgress(url, std::numeric_limits<qreal>::infinity());
        QCOMPARE(model.get(0, "progress").toDouble(), 0.0);
        model.setProgress(url, .6);
        model.refresh();
        QCOMPARE(model.get(0, "progress").toDouble(), 0.0);
    }
    void artworkIsLazyAndDirectoriesDoNotRequestIt() {
        QTemporaryDir directory;
        QDir dir(directory.path());
        QVERIFY(dir.mkdir("folder"));
        touch(dir.filePath("movie.mp4"));
        RedesignFolderModel model;
        QSignalSpy requested(&model, &RedesignFolderModel::artworkRequested);
        QSignalSpy changed(&model, &RedesignFolderModel::dataChanged);
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 2);
        QCOMPARE(requested.count(), 0);
        QVERIFY(model.get(0, "thumbnail").toString().isEmpty());
        QVERIFY(model.get(1, "thumbnail").toString().isEmpty());
        QVERIFY(model.get(1, "thumbnail").toString().isEmpty());
        QTRY_COMPARE(requested.count(), 1);
        const auto url = model.get(1, "fileUrl").toUrl();
        QCOMPARE(requested[0][0].toUrl(), url);
        model.setArtwork(url, QStringLiteral("file:///cached/movie.jpg"));
        QCOMPARE(model.get(1, "thumbnail").toString(), QStringLiteral("file:///cached/movie.jpg"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed[0][2].value<QList<int>>(), QList<int>{RedesignFolderModel::Thumbnail});
    }
    void artworkFollowsFilesAcrossSortAndSearch() {
        QTemporaryDir directory;
        QDir dir(directory.path());
        touch(dir.filePath("a.mp4")); touch(dir.filePath("b.mp4"));
        RedesignFolderModel model;
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 2);
        const auto a = model.get(0, "fileUrl").toUrl();
        const auto b = model.get(1, "fileUrl").toUrl();
        model.get(0, "thumbnail"); model.get(1, "thumbnail");
        model.setProgress(a, .3); model.setProgress(b, .8);
        model.setSearchPattern("b");
        QTRY_COMPARE(model.count(), 1);
        model.setArtwork(a, QStringLiteral("a.jpg"));
        model.setArtwork(b, QStringLiteral("b.jpg"));
        model.setProgress(a, .4);
        QCOMPARE(model.get(0, "progress").toDouble(), .8);
        QCOMPARE(model.get(0, "thumbnail").toString(), QStringLiteral("b.jpg"));
        model.setSearchPattern(""); model.setSortReversed(true);
        QTRY_COMPARE(model.count(), 2);
        QCOMPARE(model.get(0, "fileName").toString(), QStringLiteral("b.mp4"));
        QCOMPARE(model.get(0, "thumbnail").toString(), QStringLiteral("b.jpg"));
        QCOMPARE(model.get(1, "thumbnail").toString(), QStringLiteral("a.jpg"));
        QCOMPARE(model.get(0, "progress").toDouble(), .8);
        QCOMPARE(model.get(1, "progress").toDouble(), .4);
    }
    void oldFolderArtworkCannotReplaceCurrentFiles() {
        QTemporaryDir first, second;
        touch(QDir(first.path()).filePath("movie.mp4"));
        touch(QDir(second.path()).filePath("movie.mp4"));
        RedesignFolderModel model;
        model.setFolder(QUrl::fromLocalFile(first.path()));
        QTRY_COMPARE(model.count(), 1);
        const auto old = model.get(0, "fileUrl").toUrl();
        model.get(0, "thumbnail");
        model.setFolder(QUrl::fromLocalFile(second.path()));
        QTRY_COMPARE(model.count(), 1);
        model.get(0, "thumbnail");
        model.setArtwork(old, QStringLiteral("old.jpg"));
        model.setProgress(old, .7);
        QVERIFY(model.get(0, "thumbnail").toString().isEmpty());
        QCOMPARE(model.get(0, "progress").toDouble(), 0.0);
        model.setArtwork(model.get(0, "fileUrl").toUrl(), QStringLiteral("current.jpg"));
        QCOMPARE(model.get(0, "thumbnail").toString(), QStringLiteral("current.jpg"));
    }
    void sortDatesAndEncodedUrisKeepDirectoriesFirst() {
        QTemporaryDir directory;
        QDir dir(directory.path());
        QVERIFY(dir.mkdir("folder"));
        touch(dir.filePath("a.mp4"));
        touch(dir.filePath("z.mp4"));
        touch(dir.filePath("é.mp4"));
        for (const auto& pair : QList<QPair<QString, int>>{{"a.mp4",300},{"z.mp4",100},{"é.mp4",200}}) {
            QFile file(dir.filePath(pair.first));
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::fromSecsSinceEpoch(1600000000 + pair.second), QFileDevice::FileModificationTime));
        }
        RedesignFolderModel model;
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 4);
        model.setSortCriteria("insertion");
        model.setSortReversed(true);
        QTRY_COMPARE(model.get(2, "fileName").toString(), "é.mp4");
        QCOMPARE(model.get(1, "fileName").toString(), "a.mp4");
        QCOMPARE(model.get(0, "fileName").toString(), "folder");
        model.setSortReversed(false);
        QTRY_COMPARE(model.get(1, "fileName").toString(), "z.mp4");
        model.setSortCriteria("uri");
        QTRY_COMPARE(model.get(1, "fileName").toString(), "é.mp4");
        QCOMPARE(model.get(0, "fileName").toString(), "folder");
        model.setSortReversed(true);
        QTRY_COMPARE(model.get(1, "fileName").toString(), "z.mp4");
        QCOMPARE(model.get(3, "fileName").toString(), "é.mp4");
    }
    void absoluteLibraryPathIsLocal() {
        QTemporaryDir directory;
        touch(QDir(directory.path()).filePath("movie.mp4"));
        RedesignFolderModel model;
        model.setFolder(QUrl(directory.path()));
        QTRY_COMPARE(model.count(), 1);
        QVERIFY(model.folder().isLocalFile());
        QCOMPARE(QFileInfo(model.folder().toLocalFile()), QFileInfo(directory.path()));
        QCOMPARE(QFileInfo(model.get(0, "fileUrl").toUrl().toLocalFile()), QFileInfo(QDir(directory.path()).filePath("movie.mp4")));
    }
    void networkUrlsAreNotLocalPaths() {
        RedesignFolderModel model;
        const QUrl url("https://example.invalid/media");
        model.setFolder(url);
        QCOMPARE(model.folder(), url);
        QVERIFY(!model.folder().isLocalFile());
    }
    void searchKeepsMediaFilters() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QDir dir(directory.path());
        QVERIFY(dir.mkdir("Alpha Folder"));
        touch(dir.filePath("ALPHA.mp4"));
        touch(dir.filePath("Alpha.txt"));
        touch(dir.filePath("Beta.mp4"));
        RedesignFolderModel model;
        model.setNameFilters({"*.mp4"});
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 3);
        QCOMPARE(model.directoryCount(), 1);
        model.setSearchPattern("alpha");
        QTRY_COMPARE(model.count(), 2);
        QCOMPARE(model.get(0, "fileName").toString(), "Alpha Folder");
        QCOMPARE(model.get(1, "fileName").toString(), "ALPHA.mp4");
        model.setSearchPattern("missing");
        QTRY_COMPARE(model.count(), 0);
        QCOMPARE(model.directoryCount(), 0);
        model.setSearchPattern("");
        QTRY_COMPARE(model.count(), 3);
    }
    void unicodeNames() {
        QTemporaryDir directory;
        touch(QDir(directory.path()).filePath(QString::fromUtf8("Épisode 日本語.mp4")));
        RedesignFolderModel model;
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 1);
        model.setSearchPattern(QString::fromUtf8("éPISODE"));
        QTest::qWait(100);
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.get(0, "fileUrl").toUrl().fileName(), QString::fromUtf8("Épisode 日本語.mp4"));
        model.setSearchPattern(QString::fromUtf8("日本語"));
        QTest::qWait(100);
        QCOMPARE(model.count(), 1);
    }
    void latestFolderAndSearchWin() {
        QTemporaryDir first, second;
        touch(QDir(first.path()).filePath("old.mp4"));
        touch(QDir(second.path()).filePath("current.mp4"));
        RedesignFolderModel model;
        model.setFolder(QUrl::fromLocalFile(first.path()));
        model.setSearchPattern("old");
        model.setFolder(QUrl::fromLocalFile(second.path()));
        model.setSearchPattern("current");
        QTRY_COMPARE(model.count(), 1);
        QCOMPARE(model.get(0, "fileName").toString(), "current.mp4");
    }
    void watcherAppliesActiveSearch() {
        QTemporaryDir directory;
        QDir dir(directory.path());
        RedesignFolderModel model;
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        model.setSearchPattern("match");
        QTest::qWait(100);
        touch(dir.filePath("match.mp4"));
        touch(dir.filePath("other.mp4"));
        QTRY_COMPARE(model.count(), 1);
        QCOMPARE(model.get(0, "fileName").toString(), "match.mp4");
        QVERIFY(QFile::remove(dir.filePath("match.mp4")));
        QTRY_COMPARE(model.count(), 0);
    }
    void infoCountsUnfilteredDirectContentsAndDoesNotNavigate() {
        QTemporaryDir temporary;
        QDir base(temporary.path());
        QVERIFY(base.mkdir(QStringLiteral("Folder & français")));
        const auto path = base.filePath(QStringLiteral("Folder & français"));
        QDir directory(path);
        QVERIFY(directory.mkdir("nested"));
        for (const auto& pair : QList<QPair<QString,QByteArray>>{{"visible.txt","abc"},{".hidden","xy"},{"nested/inside.mp4","not counted"}}) {
            QFile file(directory.filePath(pair.first));
            QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(pair.second), pair.second.size());
        }
        RedesignFolderModel model;
        model.setNameFilters({"*.mp4"}); model.setSearchPattern("unmatched");
        const auto original = model.folder();
        QSignalSpy ready(&model, &RedesignFolderModel::infoReady);
        const auto request = model.requestInfo(QUrl::fromLocalFile(path));
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.at(0).at(0).toULongLong(), request);
        const auto info = ready.at(0).at(1).toMap();
        QVERIFY(info.value("success").toBool());
        QCOMPARE(info.value("files").toInt(), 2);
        QCOMPARE(info.value("folders").toInt(), 1);
        QCOMPARE(info.value("size").toULongLong(), quint64(5));
        QCOMPARE(info.value("name").toString(), QStringLiteral("Folder & français"));
        QCOMPARE(info.value("url").toString(), QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
        QCOMPARE(info.value("location").toString(), QDir::toNativeSeparators(path));
        QVERIFY(info.value("modified").toLongLong() > 0);
        QCOMPARE(model.folder(), original);
        QCOMPARE(model.count(), 0);
        ready.clear();
        model.requestInfo(QUrl(path)); // Raw absolute library paths use the same normalization.
        QTRY_COMPARE(ready.count(), 1);
        QVERIFY(ready.at(0).at(1).toMap().value("success").toBool());
    }
    void infoRejectsMissingOrNonFolderLocations() {
        QTemporaryDir temporary;
        touch(QDir(temporary.path()).filePath("file.mp4"));
        RedesignFolderModel model;
        QSignalSpy ready(&model, &RedesignFolderModel::infoReady);
        for (const auto& url : QList<QUrl>{QUrl::fromLocalFile(temporary.path()+"/missing"),
            QUrl::fromLocalFile(temporary.path()+"/file.mp4"), QUrl("https://example.invalid/media"), QUrl("relative")}) {
            ready.clear(); model.requestInfo(url); QTRY_COMPARE(ready.count(),1);
            const auto info=ready.at(0).at(1).toMap();
            QVERIFY(!info.value("success").toBool());
            QVERIFY(!info.value("location").toString().isEmpty());
        }
    }
    void refreshPreservesFiltersAndReleasesOldArtworkRequests() {
        QTemporaryDir temporary;
        QDir directory(temporary.path());
        touch(directory.filePath("match.mp4")); touch(directory.filePath("hidden.txt"));
        RedesignFolderModel model;
        model.setNameFilters({"*.mp4"}); model.setSearchPattern("match");
        model.setSortCriteria("uri"); model.setSortReversed(true);
        model.setFolder(QUrl::fromLocalFile(temporary.path()));
        QTRY_COMPARE(model.count(),1);
        QSignalSpy requested(&model,&RedesignFolderModel::artworkRequested);
        model.get(0,"thumbnail"); // This queued request must be invalidated by refresh.
        QSignalSpy reset(&model,&QAbstractItemModel::modelReset);
        model.refresh(); QTRY_VERIFY(reset.count()>0);
        QCOMPARE(requested.count(),0);
        QCOMPARE(model.nameFilters(),QStringList{"*.mp4"});
        QCOMPARE(model.searchPattern(),QStringLiteral("match"));
        QCOMPARE(model.sortCriteria(),QStringLiteral("uri")); QVERIFY(model.sortReversed());
        model.get(0,"thumbnail"); QTRY_COMPARE(requested.count(),1);
        const auto url=model.get(0,"fileUrl").toUrl();model.setArtwork(url,"old.png");
        QCOMPARE(model.get(0,"thumbnail").toString(),QStringLiteral("old.png"));
        reset.clear();model.refresh();QTRY_VERIFY(reset.count()>0);
        QCOMPARE(model.get(0,"thumbnail").toString(),QString());
        QTRY_COMPARE(requested.count(),2);
    }
    void directoryCountChangesAtConstantRowCount() {
        QTemporaryDir directory;
        QDir dir(directory.path());
        touch(dir.filePath("entry"));
        RedesignFolderModel model;
        model.setFolder(QUrl::fromLocalFile(directory.path()));
        QTRY_COMPARE(model.count(), 1);
        QCOMPARE(model.directoryCount(), 0);
        QSignalSpy changed(&model, &RedesignFolderModel::countChanged);
        QVERIFY(QFile::remove(dir.filePath("entry")));
        QVERIFY(dir.mkdir("entry"));
        QTRY_COMPARE(model.directoryCount(), 1);
        QCOMPARE(model.count(), 1);
        QVERIFY(changed.count() > 0);
    }
};
QTEST_GUILESS_MAIN(TestRedesignFolderModel)
#include "test_redesign_folder_model.moc"
