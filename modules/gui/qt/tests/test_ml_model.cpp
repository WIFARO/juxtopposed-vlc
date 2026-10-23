/*****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "../medialibrary/mlbasemodel.hpp"
#include "../medialibrary/mlmedia.hpp"
#include "../medialibrary/mlmediastore.hpp"
#include "../medialibrary/mlfileartwork.hpp"
#include "../util/artwork_store.hpp"
#include "../medialibrary/mlfoldersmodel.hpp"
#include "vlc_stub_modules.hpp"

#include <memory>
#include <vector>

#include <QCoreApplication>
#include <QTest>
#include <QTemporaryDir>
#include "../../../misc/medialibrary/track_rules.hpp"
#include "../../../misc/medialibrary/history_filter.hpp"
#include <QFile>
#include <QQmlEngine>
#include <QQmlContext>
#include <QImage>
#include <QDir>
#include <QSettings>
#include <QScopeGuard>
#include <QSemaphore>
#include <atomic>
#include <QSignalSpy>
#include "../util/playlist_cover_store.hpp"
#include "../util/media_identity_vlc.hpp"
#include "../util/shared_input_item.hpp"

struct vlc_medialibrary_t;

class MLTestModel : public MLBaseModel
{
    Q_OBJECT

public:
    virtual QVariant itemRoleData(const MLItem *item, int role) const override
    {
        VLC_UNUSED(role);
        if (!item)
            return {};

        return QVariant::fromValue(item->getId());
    }

    void appendRange(int64_t min, int64_t max)
    {
        for (int64_t i = min; i <= max; ++i)
            m_items.emplace_back(i, VLC_ML_PARENT_UNKNOWN);
        emit resetRequested();
    }

    void updateItem(size_t position, int64_t id)
    {
        MLItemId itemId{id, VLC_ML_PARENT_UNKNOWN};
        m_items[position] = itemId;
        updateItemInCache(itemId);
    }

    std::unique_ptr<MLListCacheLoader> selectionLoader() const { return createMLLoader(); }

    MLListCacheLoader::MLOp::SortKey nativeSortKey(const MLItem& item) const
    {
        Loader loader(*this, *this);
        return loader.MLListCacheLoader::MLOp::sortKey(nullptr, item);
    }


protected:
    std::unique_ptr<MLListCacheLoader> createMLLoader() const override
    {
        return std::make_unique<MLListCacheLoader>(m_mediaLib, std::make_shared<MLTestModel::Loader>(*this, *this));
    }

    struct Loader : public MLListCacheLoader::MLOp
    {
        Loader(const MLBaseModel& model, const MLTestModel& parent)
            : MLOp(model)
            , m_mlTestModel(parent)
        {}

        size_t count(vlc_medialibrary_t* ml, const vlc_ml_query_params_t* queryParams) const override
        {
            VLC_UNUSED(ml);
            VLC_UNUSED(queryParams);
            return m_mlTestModel.m_items.size();
        }

        std::vector<std::unique_ptr<MLItem>> load(vlc_medialibrary_t* ml, const vlc_ml_query_params_t* queryParams) const override
        {
            VLC_UNUSED(ml);

            if (m_mlTestModel.m_items.empty())
                return {};

            uint32_t offset = queryParams->i_offset;
            uint32_t count = queryParams->i_nbResults;
            size_t maxIndex = count ? std::min(
                static_cast<size_t>(offset + count),
                m_mlTestModel.m_items.size()) : m_mlTestModel.m_items.size();
            std::vector<std::unique_ptr<MLItem>> ret;
            for (size_t i = offset; i < maxIndex; ++i) {
                ret.emplace_back(std::make_unique<MLItem>(m_mlTestModel.m_items[i]));
            }
            return ret;
        }

        std::unique_ptr<MLItem> loadItemById(vlc_medialibrary_t* ml, MLItemId itemId) const override
        {
            VLC_UNUSED(ml);
            return std::make_unique<MLItem>(itemId);
        }

        SortKey sortKey(vlc_medialibrary_t*, const MLItem& item) const override
        {
            const auto id = item.getId().id;
            SortKey key;
            key.title = QString("Title %1").arg(id, 3, 10, QChar('0'));
            key.uri = QString("file://server/%1").arg(1000 - id);
            key.track = id % 3 == 1 ? 10 : id % 3 == 2 ? 2 : 1;
            key.disc = id <= 6 ? 2 : 1;
            key.played = id % 2;
            key.artist = id % 2 ? "Z artist" : "A artist";
            key.album = id <= 6 ? "Z album" : "A album";
            return key;
        }

        const MLTestModel& m_mlTestModel;
    };


    std::vector<MLItemId> m_items;
};

// These folder tests inject MediaLib directly and never construct a MainCtx.
const QMetaObject MainCtx::staticMetaObject = QObject::staticMetaObject;

class TestFoldersModel : public MLFoldersModel
{
public:
    explicit TestFoldersModel(MediaLib* library) { m_mediaLib = library; }
    void refresh() { m_refreshTimer.start(0); }
    bool pending() const { return m_updatePending || m_refreshTimer.isActive(); }
};

class TestVLCMLModel : public QObject
{
    Q_OBJECT

private slots:
    void testHistoryExclusions_data()
    {
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<bool>("excluded");
        QTest::newRow("empty") << "" << "file:///Private/a.mp4" << "/Private/a.mp4" << true << false;
        QTest::newRow("filename") << "private" << "file:///Private/a.mp4" << "/Private/a.mp4" << true << true;
        QTest::newRow("nonmatching") << "private" << "file:///Films/a.mp4" << "/Films/a.mp4" << true << false;
        QTest::newRow("decoded spaces") << "Secret Show" << "file:///Secret%20Show/a.mp4" << "/Secret Show/a.mp4" << true << true;
        QTest::newRow("encoded URL") << "%20" << "https://example.test/Secret%20Show" << "https://example.test/Secret Show" << true << true;
        QTest::newRow("network") << "example[.]test" << "https://example.test/film" << "https://example.test/film" << true << true;
        QTest::newRow("alternation") << "private|secret" << "file:///Secret/a.mp4" << "/Secret/a.mp4" << true << true;
        QTest::newRow("extension") << "[.]mkv$" << "file:///Films/a.MKV" << "/Films/a.MKV" << true << true;
        QTest::newRow("windows path") << R"(^C:\\Private)" << "file:///C:/Private/a.mp4" << R"(C:\Private\a.mp4)" << true << true;
        QTest::newRow("unicode") << "猫" << "file:///%E7%8C%AB/a.mp4" << "/猫/a.mp4" << true << true;
        QTest::newRow("invalid regex") << "[" << "file:///Films/a.mp4" << "/Films/a.mp4" << false << true;
    }

    void testHistoryExclusions()
    {
        QFETCH(QString, pattern); QFETCH(QString, uri); QFETCH(QString, path);
        QFETCH(bool, valid); QFETCH(bool, excluded);
        const vlc::medialibrary::HistoryFilter filter(pattern.toStdString());
        QCOMPARE(filter.valid(), valid);
        QCOMPARE(filter.excludes(uri.toStdString(), path.toStdString()), excluded);
    }

    void testSourceFolderCategories_data()
    {
        QTest::addColumn<bool>("folderQueryFails");
        QTest::newRow("indexed descendants") << false;
        QTest::newRow("folder query unavailable") << true;
    }

    void testSourceFolderCategories()
    {
        QFETCH(bool, folderQueryFails);
        struct Folder { const char* url; unsigned audio = 0; unsigned video = 0; };
        const std::vector<Folder> roots {
            {"file:///library/Music/"}, {"file:///library/Shows/"},
            {"file:///library/Mixed/"}, {"file:///library/Empty/"},
            {"file:///library/Direct/", 1, 1},
            {"smb://offline/share/"}, {"file:///library/Music%20Extras/"},
            {"file:///library/DirectAudio/", 1}
        };
        bool mixedHasVideo = true;
        std::atomic_bool workerOnly {true};
        auto list = [](const std::vector<Folder>& folders) {
            auto* result = static_cast<vlc_ml_folder_list_t*>(calloc(1,
                sizeof(vlc_ml_folder_list_t) + folders.size() * sizeof(vlc_ml_folder_t)));
            result->i_nb_items = folders.size();
            for (size_t i = 0; i < folders.size(); ++i) {
                result->p_items[i].psz_mrl = strdup(folders[i].url);
                result->p_items[i].i_nb_audio = folders[i].audio;
                result->p_items[i].i_nb_video = folders[i].video;
            }
            return result;
        };
        m_env->mediaList = [&](int query, const vlc_ml_query_params_t*, va_list args) {
            workerOnly = workerOnly && QThread::currentThread() != QCoreApplication::instance()->thread();
            if (query == VLC_ML_LIST_ENTRY_POINTS) {
                va_arg(args, int); // Banned entry points are requested separately.
                *va_arg(args, vlc_ml_folder_list_t**) = list(roots);
                return VLC_SUCCESS;
            }
            if (query != VLC_ML_LIST_FOLDERS) return VLC_EGENERIC;
            if (folderQueryFails) return VLC_EGENERIC;
            std::vector<Folder> indexed {
                {"file:///library/Music/Album/", 1}, {"file:///library/Mixed/Album/", 1},
                {"file:///library/Music%20Extras/Album/", 1}, {"smb://other/share/Album/", 1},
                {"file:///library/Shows/Season/", 0, 1}, {"file:///library/Music%20Extras2/Film/", 0, 1}
            };
            if (mixedHasVideo) indexed.push_back({"file:///library/Mixed/Film/", 0, 1});
            *va_arg(args, vlc_ml_folder_list_t**) = list(indexed);
            return VLC_SUCCESS;
        };
        TestFoldersModel model(m_medialib.get());
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        model.refresh();
        QTRY_COMPARE(reset.count(), 1);
        const auto roles = model.roleNames();
        QVERIFY(roles.values().contains("contains_video"));
        const auto videoRole = roles.key("contains_video");
        QCOMPARE(model.rowCount(), int(roots.size()));
        for (int i = 0; i < model.rowCount(); ++i) {
            const bool audio = i == 4 || i == 7 || (!folderQueryFails && (i == 0 || i == 2 || i == 6));
            const bool video = folderQueryFails || i == 1 || i == 2 || i == 4;
            QCOMPARE(model.data(model.index(i, 0), MLFoldersBaseModel::ContainsAudio).toBool(), audio);
            QCOMPARE(model.data(model.index(i, 0), videoRole).toBool(), video);
        }
        QVERIFY(workerOnly.load());
        QCOMPARE(m_medialib->folderRoots().size(), int(roots.size()));
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(reset.count(), 1); // Unchanged source data must keep focused delegates.
        mixedHasVideo = false;
        model.refresh();
        if (!folderQueryFails) {
            QTRY_COMPARE(reset.count(), 2);
            QVERIFY(!model.data(model.index(2, 0), videoRole).toBool());
        } else {
            QTRY_VERIFY(!model.pending());
            QCOMPARE(reset.count(), 1);
        }
    }

    void testAudioMetadataReadsStayOnTheWorker_data()
    {
        QTest::addColumn<bool>("enabled");
        QTest::newRow("audio metadata") << true;
        QTest::newRow("ordinary lookup") << false;
    }
    void testAudioMetadataReadsStayOnTheWorker()
    {
        QFETCH(bool, enabled);
        std::atomic<int> artistReads {0}, writes {0};
        std::atomic_bool workerOnly {true};
        m_env->mediaGet = [&](int query, va_list args) -> void* {
            workerOnly = workerOnly && QThread::currentThread() != QCoreApplication::instance()->thread();
            if (query == VLC_ML_GET_ARTIST) {
                if (va_arg(args, int64_t) != 44) return nullptr;
                ++artistReads;
                auto* artist = static_cast<vlc_ml_artist_t*>(calloc(1, sizeof(vlc_ml_artist_t)));
                artist->i_id = 44; artist->psz_name = strdup("Artist");
                return artist;
            }
            if (query != VLC_ML_GET_MEDIA_BY_MRL) return nullptr;
            const auto mrl = va_arg(args, const char*);
            auto* media = static_cast<vlc_ml_media_t*>(calloc(1, sizeof(vlc_ml_media_t)));
            media->i_id = 42; media->i_type = VLC_ML_MEDIA_TYPE_AUDIO;
            media->i_subtype = VLC_ML_MEDIA_SUBTYPE_ALBUMTRACK;
            media->album_track.i_artist_id = 44;
            media->psz_title = strdup("Track title"); media->psz_filename = strdup("01.mp3");
            media->p_files = static_cast<vlc_ml_file_list_t*>(calloc(1, sizeof(vlc_ml_file_list_t) + sizeof(vlc_ml_file_t)));
            media->p_files->i_nb_items = 1;
            media->p_files->p_items[0].i_type = VLC_ML_FILE_TYPE_MAIN;
            media->p_files->p_items[0].psz_mrl = strdup(mrl);
            return media;
        };
        m_env->mediaControl = [&](int, va_list) { ++writes; return VLC_EGENERIC; };
        MLMediaStore store(m_medialib.get());
        store.setReadAudioMetadata(enabled);
        QSignalSpy metadata(&store, &MLMediaStore::audioMetadataUpdated);
        QSignalSpy updated(&store, &MLMediaStore::updated);
        const QString mrl = QStringLiteral("file://server/music/01.mp3");
        bool finished = false;
        store.insertMany({mrl}, [&] { finished = true; });
        QTRY_VERIFY(finished);
        QCOMPARE(updated.count(), 1);
        QCOMPARE(metadata.count(), enabled ? 1 : 0);
        if (enabled) QCOMPARE(metadata[0], (QVariantList{mrl, "Track title", "Artist"}));
        QStringList files;
        for (int i = 0; i < 80; ++i) files.append(QStringLiteral("file://server/music/%1.mp3").arg(i));
        finished = false;
        store.insertMany(files, [&] { finished = true; });
        QTRY_VERIFY(finished);
        QCOMPARE(updated.count(), 81);
        QCOMPARE(artistReads.load(), enabled ? 2 : 0); // One artist read across both 64-item chunks.
        store.insert(mrl);
        QTRY_COMPARE(updated.count(), 82);
        QCOMPARE(artistReads.load(), enabled ? 3 : 0);
        QVERIFY(workerOnly.load());
        QCOMPARE(writes.load(), 0);
    }
    void initTestCase() {
        m_env = std::make_unique<VLCTestingEnv>();
        QVERIFY(m_env->init());
    }

    void cleanupTestCase() {
        m_env.reset();
    }


    void init() {
        m_medialib = std::make_unique<MediaLib>(m_env->intf, nullptr);
        m_model = std::make_unique<MLTestModel>();
        m_model->classBegin();
        m_model->setMl(m_medialib.get());
    }

    void cleanup() {
        m_model.reset();
        m_medialib.reset();
        m_env->mediaGet = {};
        m_env->mediaControl = {};
        m_env->mediaList = {};
    }

    void testArtworkPriorityCollageAndCancellation()
    {
        QStandardPaths::setTestModeEnabled(true);
        QTemporaryDir directory;
        const auto makeImage = [&](const QString& name, QColor color) {
            QImage image(64, 64, QImage::Format_RGB32); image.fill(color);
            return image.save(directory.filePath(name), "PNG");
        };
        QVERIFY(makeImage("a.png", Qt::red));
        QVERIFY(makeImage("b.png", Qt::red)); // same picture in a different file
        QVERIFY(makeImage("c.png", Qt::green));
        QVERIFY(makeImage("d.png", Qt::blue));
        QVERIFY(makeImage("e.png", Qt::yellow));
        for (const auto& name : {"a", "b", "c", "d", "e"}) {
            QFile file(directory.filePath(QString(name) + ".mkv")); QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const auto folder = QUrl::fromLocalFile(directory.path());
        const auto media = QUrl::fromLocalFile(directory.filePath("a.mkv"));
        MLFileArtwork resolver;
        resolver.setMediaLibrary(m_medialib.get());
        QHash<QUrl, QString> results;
        connect(&resolver, &MLFileArtwork::artworkChanged, this, [&](const QUrl& url, const QString& image) { results.insert(url, image); });
        resolver.request(folder, true);
        QTRY_VERIFY(results.contains(folder));
        auto collage = ArtworkStore::read(QUrl(results.value(folder)).toLocalFile());
        QVERIFY(!collage.isNull()); QCOMPARE(collage.size(), QSize(512,320));
        QCOMPARE(collage.pixelColor(20, 20), QColor(Qt::red));
        QCOMPARE(collage.pixelColor(500, 20), QColor(Qt::green));
        QCOMPARE(collage.pixelColor(20, 300), QColor(Qt::blue));
        QCOMPARE(collage.pixelColor(500, 300), QColor(Qt::yellow));
        QSignalSpy saved(&resolver, &MLFileArtwork::selectionFinished);
        resolver.setSelectedArtwork(folder, QUrl::fromLocalFile(directory.filePath("c.png")));
        QTRY_COMPARE(saved.count(), 1); QVERIFY(saved[0][1].toBool());
        results.clear(); resolver.request(folder, true);
        QTRY_VERIFY(results.contains(folder));
        QCOMPARE(ArtworkStore::read(QUrl(results.value(folder)).toLocalFile()).pixelColor(20, 20), QColor(Qt::green));
        resolver.setSelectedArtwork(folder, {});
        QTRY_COMPARE(saved.count(), 2); QVERIFY(saved[1][1].toBool());
        const auto cleanup = qScopeGuard([folder] { ArtworkStore::save(folder, {}); });
        resolver.clear(); results.clear();
        resolver.request(media, true, "file:///missing/old-thumbnail.png");
        QTRY_VERIFY(results.contains(media));
        QCOMPARE(QUrl(results.value(media)).fileName(), "a.png");
        resolver.clear(); results.clear();
        resolver.request(folder, true); resolver.clear();
        QTest::qWait(100); QVERIFY(results.isEmpty());
    }

    void testArtworkMissingChildAndFolderOverride()
    {
        QTemporaryDir directory;
        QFile missing(directory.filePath("a.mkv")); QVERIFY(missing.open(QIODevice::WriteOnly)); missing.close();
        QFile valid(directory.filePath("b.mkv")); QVERIFY(valid.open(QIODevice::WriteOnly)); valid.close();
        QImage image(40, 40, QImage::Format_RGB32); image.fill(Qt::blue);
        QVERIFY(image.save(directory.filePath("b.png")));
        MLFileArtwork resolver; resolver.setMediaLibrary(m_medialib.get());
        const auto folder = QUrl::fromLocalFile(directory.path());
        QString result; bool finished = false;
        connect(&resolver, &MLFileArtwork::artworkChanged, this, [&](const QUrl& url, const QString& image) {
            if (url == folder) { result = image; finished = true; }
        });
        resolver.request(folder, true);
        QTRY_VERIFY(finished); QVERIFY(!result.isEmpty());
        QCOMPARE(ArtworkStore::read(QUrl(result).toLocalFile()).pixelColor(20, 20), QColor(Qt::blue));
        resolver.clear(); finished = false;
        image.fill(Qt::red); QVERIFY(image.save(directory.filePath("folder.png")));
        resolver.request(folder, true);
        QTRY_VERIFY(finished); QCOMPARE(QUrl(result).fileName(), "folder.png");
    }

    void testMetadataBatchesReplaceQueuedRequestsAndKeepTheUiResponsive()
    {
        QSemaphore started, release;
        std::atomic<int> lookups {0}, unexpected {0};
        m_env->mediaGet = [&](int query, va_list) -> void* {
            if (query != VLC_ML_GET_MEDIA_BY_MRL) { ++unexpected; return nullptr; }
            if (++lookups == 1) { started.release(); release.acquire(); }
            return nullptr;
        };
        m_env->mediaControl = [&](int, va_list) { ++unexpected; return VLC_EGENERIC; };
        MLMediaStore store(m_medialib.get());
        auto unblock = qScopeGuard([&] { release.release(); });
        QStringList files;
        for (int i = 0; i < 500; ++i) files.append(QStringLiteral("file:///library/%1.mkv").arg(i));
        bool oldFinished = false, latestFinished = false;
        store.insertMany(files, [&] { oldFinished = true; });
        QTRY_VERIFY(started.available() > 0);
        for (int i = 0; i < 100; ++i)
            store.insertMany({QStringLiteral("file:///latest/%1.mkv").arg(i)}, [&, i] { latestFinished = i == 99; });
        QCOMPARE(lookups.load(), 1); // The worker is blocked; the GUI still accepts every replacement.
        QSignalSpy updated(&store, &MLMediaStore::updated);
        release.release();
        unblock.dismiss();
        QTRY_VERIFY(latestFinished);
        QVERIFY(!oldFinished);
        QCOMPARE(lookups.load(), 2); // Finish the in-flight lookup, then use the final replacement only.
        QCOMPARE(updated.count(), 1);
        QCOMPARE(updated[0][0].toString(), QStringLiteral("file:///latest/99.mkv"));
        QCOMPARE(unexpected.load(), 0); // No import, parsing or decoder API.
    }

    void testClearingMetadataBatchesRejectsInFlightResults_data()
    {
        QTest::addColumn<bool>("clearStore");
        QTest::newRow("clear store") << true;
        QTest::newRow("cancel batch") << false;
    }
    void testClearingMetadataBatchesRejectsInFlightResults()
    {
        QFETCH(bool, clearStore);
        QSemaphore started, release;
        std::atomic<int> lookups {0};
        m_env->mediaGet = [&](int query, va_list) -> void* {
            if (query == VLC_ML_GET_MEDIA_BY_MRL && ++lookups == 1) {
                started.release(); release.acquire();
            }
            return nullptr;
        };
        MLMediaStore store(m_medialib.get());
        auto unblock = qScopeGuard([&] { release.release(); });
        bool oldFinished = false, nextFinished = false;
        store.insertMany({QStringLiteral("file:///old.mkv")}, [&] { oldFinished = true; });
        QTRY_VERIFY(started.available() > 0);
        if (clearStore) store.clear();
        else store.cancelBatch();
        store.insertMany({QStringLiteral("file:///next.mkv"), QStringLiteral("file:///next.mkv")}, [&] { nextFinished = true; });
        QSignalSpy updated(&store, &MLMediaStore::updated);
        release.release();
        unblock.dismiss();
        QTRY_VERIFY(nextFinished);
        QVERIFY(!oldFinished);
        QCOMPARE(lookups.load(), 2);
        QCOMPARE(updated.count(), 1);
        QCOMPARE(updated[0][0].toString(), QStringLiteral("file:///next.mkv"));
    }

    void testFolderResyncQueuesOnlyTheChosenUrl()
    {
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(),QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(),engine.rootContext());engine.newQObject(m_medialib.get());
        QList<QByteArray> requests;bool reject=false;
        m_env->mediaControl=[&](int query,va_list args) {
            if(query!=VLC_ML_RELOAD_FOLDER) return VLC_EGENERIC;
            const char* uri=va_arg(args,const char*);
            if(!uri) return VLC_EGENERIC; // A folder action must not request a full-library scan.
            requests.push_back(uri);return reject ? VLC_EGENERIC : VLC_SUCCESS;
        };
        auto request=[&](const QUrl& url) {
            engine.globalObject().setProperty("answer",QJSValue(QJSValue::NullValue));
            m_medialib->resyncFolder(url,engine.evaluate("(function(value){answer=value;})"));
            if(!QTest::qWaitFor([&]{return !engine.globalObject().property("answer").isNull();}))qFatal("Folder resync callback did not finish");
            return engine.globalObject().property("answer");
        };
        const QUrl folder(QStringLiteral("file:///library/Movies%20%26%20fran%C3%A7ais"));
        QVERIFY(request(folder).property("success").toBool());
        QCOMPARE(requests,QList<QByteArray>{folder.toEncoded()});
        reject=true;QVERIFY(!request(folder).property("success").toBool());
        QVERIFY(!request(QUrl("relative")).property("success").toBool());
        QCOMPARE(requests.size(),2);
    }

    void testLibraryFileImportKeepsFilesAndDeduplicatesSourceFolders()
    {
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkdir("other"));
        const auto a=directory.filePath("one & accents.mp4"),b=directory.filePath("two.mkv"),c=directory.filePath("other/three.wav");
        for (const auto& path : {a,b,c}) { QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write("original"),qint64(8)); }
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(),QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(),engine.rootContext());engine.newQObject(m_medialib.get());
        QList<QByteArray> requests;int failAt=-1;
        m_env->mediaControl=[&](int query,va_list args) {
            if(query!=VLC_ML_ADD_FOLDER) return VLC_EGENERIC;
            requests.append(va_arg(args,const char*));return requests.size()==failAt ? VLC_EGENERIC : VLC_SUCCESS;
        };
        auto request=[&](const QList<QUrl>& urls) {
            engine.globalObject().setProperty("answer",QJSValue(QJSValue::NullValue));
            m_medialib->addLibraryFiles(urls,engine.evaluate("(function(value){answer=value;})"));
            if(!QTest::qWaitFor([&]{return !engine.globalObject().property("answer").isNull();}))qFatal("Library import callback did not finish");
            return engine.globalObject().property("answer");
        };
        const QList<QUrl> files{QUrl::fromLocalFile(a),QUrl::fromLocalFile(b),QUrl::fromLocalFile(a),QUrl::fromLocalFile(c)};
        auto result=request(files);QVERIFY(result.property("success").toBool());
        QCOMPARE(requests,(QList<QByteArray>{QUrl::fromLocalFile(directory.path()).toEncoded(),QUrl::fromLocalFile(directory.filePath("other")).toEncoded()}));
        requests.clear();QVERIFY(!request({files[0],QUrl("https://example.test/video.mp4")}).property("success").toBool());QVERIFY(requests.empty());
        QVERIFY(!request({files[0],QUrl::fromLocalFile(directory.filePath("missing.mp4"))}).property("success").toBool());QVERIFY(requests.empty());
        failAt=2;result=request(files);QVERIFY(!result.property("success").toBool());QCOMPARE(result.property("folders").property("length").toInt(),1);
        for(const auto& path:{a,b,c}) { QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("original")); }
    }

    void testPlaylistFavoriteAndState()
    {
        vlc_ml_event_t event{};
        event.i_type = VLC_ML_EVENT_FAVORITES_CHANGED;
        event.favorites_changed = {42, VLC_ML_PARENT_PLAYLIST, true};
        const MLEvent copied(&event);
        QCOMPARE(copied.favorites_changed.i_entity_id, 42);
        QCOMPARE(copied.favorites_changed.i_entity_type, VLC_ML_PARENT_PLAYLIST);
        QVERIFY(copied.favorites_changed.b_favorite);
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(), QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(), engine.rootContext());
        engine.newQObject(m_medialib.get());
        QVERIFY(qjsEngine(m_medialib.get()) == &engine);
        bool favorite = false, reject = false;
        int writes = 0;
        m_env->mediaGet = [&](int query, va_list args) -> void* {
            if (query != VLC_ML_GET_PLAYLIST || va_arg(args, int64_t) != 42) return nullptr;
            auto* playlist = static_cast<vlc_ml_playlist_t*>(calloc(1, sizeof(vlc_ml_playlist_t)));
            playlist->i_id = 42;
            playlist->psz_name = strdup("Saved playlist");
            playlist->b_is_favorite = favorite;
            playlist->i_nb_media = 3;
            return playlist;
        };
        m_env->mediaControl = [&](int query, va_list args) {
            if (query != VLC_ML_PLAYLIST_SET_FAVORITE || va_arg(args, int64_t) != 42) return VLC_EGENERIC;
            const bool requested = va_arg(args, int);
            ++writes;
            if (reject) return VLC_EGENERIC;
            favorite = requested;
            return VLC_SUCCESS;
        };
        auto request = [&](const MLItemId& id, bool update, bool value) {
            engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
            auto callback = engine.evaluate("(function(value) { answer = value; })");
            if (update) m_medialib->setPlaylistFavorite(id, value, callback);
            else m_medialib->playlistState(id, callback);
            if (!QTest::qWaitFor([&] { return !engine.globalObject().property("answer").isNull(); }))
                qFatal("Playlist state callback did not finish");
            return engine.globalObject().property("answer");
        };
        const MLItemId id(42, VLC_ML_PARENT_PLAYLIST);
        auto result = request(id, false, false);
        QVERIFY(result.property("success").toBool());
        QCOMPARE(result.property("count").toInt(), 3);
        QVERIFY(!result.property("favorite").toBool());
        result = request(id, true, true);
        QVERIFY(result.property("success").toBool() && result.property("favorite").toBool());
        QVERIFY(favorite);
        QVERIFY(request(id, false, false).property("favorite").toBool());
        reject = true;
        result = request(id, true, false);
        QVERIFY(!result.property("success").toBool());
        QVERIFY(!result.property("error").toString().isEmpty());
        QVERIFY(favorite);
        QCOMPARE(writes, 2);
        QVERIFY(!request(MLItemId(42, VLC_ML_PARENT_UNKNOWN), false, false).property("success").toBool());
        QVERIFY(!request(MLItemId(99, VLC_ML_PARENT_PLAYLIST), false, false).property("success").toBool());
    }

    void testPlaylistPropertiesSaveAndFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = qgetenv("VLC_USERDATA_PATH");
        qputenv("VLC_USERDATA_PATH", directory.path().toUtf8());
        const auto restore = qScopeGuard([&] {
            if (previous.isNull()) qunsetenv("VLC_USERDATA_PATH");
            else qputenv("VLC_USERDATA_PATH", previous);
        });
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(), QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(), engine.rootContext());
        engine.newQObject(m_medialib.get());
        QString savedName = QStringLiteral("Original");
        bool rejectRename = false, readOnly = false;
        int renames = 0;
        m_env->mediaGet = [&](int query, va_list args) -> void* {
            if (query != VLC_ML_GET_PLAYLIST || va_arg(args, int64_t) != 42) return nullptr;
            auto* playlist = static_cast<vlc_ml_playlist_t*>(calloc(1, sizeof(vlc_ml_playlist_t)));
            playlist->i_id = 42;
            playlist->psz_name = strdup(qtu(savedName));
            playlist->i_creation_date = 1234;
            playlist->i_nb_media = 4;
            playlist->i_nb_present_media = 3;
            playlist->i_nb_video = 2;
            playlist->i_nb_audio = 1;
            playlist->i_nb_unknown = 1;
            playlist->i_nb_duration_unknown = 1;
            playlist->i_duration = 3723000;
            playlist->b_is_read_only = readOnly;
            playlist->psz_mrl = strdup("file:///saved%20list.m3u8");
            return playlist;
        };
        m_env->mediaControl = [&](int query, va_list args) {
            if (query != VLC_ML_PLAYLIST_RENAME || va_arg(args, int64_t) != 42) return VLC_EGENERIC;
            const auto name = QString::fromUtf8(va_arg(args, const char*));
            ++renames;
            if (rejectRename) return VLC_EGENERIC;
            savedName = name;
            return VLC_SUCCESS;
        };
        auto request = [&](const QString& name, const QUrl& cover, bool save = true) {
            engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
            auto callback = engine.evaluate("(function(value) { answer = value; })");
            const MLItemId id(42, VLC_ML_PARENT_PLAYLIST);
            if (save) m_medialib->savePlaylistDetails(id, name, cover, callback);
            else m_medialib->playlistState(id, callback);
            if (!QTest::qWaitFor([&] { return !engine.globalObject().property("answer").isNull(); }))
                qFatal("Playlist properties callback did not finish");
            return engine.globalObject().property("answer");
        };
        QSignalSpy changes(m_medialib.get(), &MediaLib::playlistDetailsChanged);
        auto state = request({}, {}, false);
        QCOMPARE(state.property("missingCount").toInt(), 1);
        QCOMPARE(state.property("videoCount").toInt(), 2);
        QCOMPARE(state.property("duration").toInt(), 3723000);
        QCOMPARE(state.property("created").toInt(), 1234);
        QCOMPARE(state.property("source").toString(), QStringLiteral("file:///saved%20list.m3u8"));
        const auto source = directory.filePath(QStringLiteral("source & cover.png"));
        QImage image(2048, 1024, QImage::Format_RGB32);
        image.fill(Qt::red); QVERIFY(image.save(source));
        QVERIFY(request(QStringLiteral("  New name  "), QUrl::fromLocalFile(source)).property("success").toBool());
        QCOMPARE(savedName, QStringLiteral("New name"));
        QCOMPARE(changes.count(), 1);
        QCOMPARE(changes.at(0).at(1).toString(), savedName);
        const auto managed = PlaylistCoverStore::cover(42, 1234);
        QVERIFY(managed.isLocalFile() && managed.toLocalFile() != source);
        QCOMPARE(QImage(managed.toLocalFile()).size(), QSize(1024, 512));
        QVERIFY(QFile::remove(source));
        state = request({}, {}, false);
        QCOMPARE(state.property("cover").toString(), managed.toString(QUrl::FullyEncoded));
        QVERIFY(QFile::exists(managed.toLocalFile()));
        // An unreadable image must not rename the playlist or replace its cover.
        const auto count = renames;
        QVERIFY(!request(QStringLiteral("Lost edit"), QUrl::fromLocalFile(source)).property("success").toBool());
        QCOMPARE(renames, count);
        QCOMPARE(PlaylistCoverStore::cover(42, 1234), managed);
        image.fill(Qt::blue); QVERIFY(image.save(source));
        const auto covers = PlaylistCoverStore::directory();
        const auto imagesBefore = QDir(covers).entryList({QStringLiteral("*.png")}, QDir::Files);
        rejectRename = true;
        QVERIFY(!request(QStringLiteral("Rejected"), QUrl::fromLocalFile(source)).property("success").toBool());
        QCOMPARE(savedName, QStringLiteral("New name"));
        QCOMPARE(QDir(covers).entryList({QStringLiteral("*.png")}, QDir::Files), imagesBefore);
        QCOMPARE(PlaylistCoverStore::cover(42, 1234), managed);
        rejectRename = false;
        // Failure to prepare metadata must leave the native name untouched.
        const auto ini = QDir(covers).filePath(QStringLiteral("covers.ini"));
        const auto permissions = QFile::permissions(ini);
        QVERIFY(QFile::setPermissions(ini, QFile::ReadOwner));
        const auto beforeStorageFailure = renames;
        QVERIFY(!request(QStringLiteral("Storage failure"), QUrl::fromLocalFile(source)).property("success").toBool());
        QCOMPARE(renames, beforeStorageFailure);
        QVERIFY(QFile::setPermissions(ini, permissions));
        QCOMPARE(PlaylistCoverStore::cover(42, 1234), managed);
        readOnly = true;
        QVERIFY(!request(QStringLiteral("Read only"), {}).property("success").toBool());
        QCOMPARE(savedName, QStringLiteral("New name"));
        QVERIFY(QFile::exists(managed.toLocalFile()));
        readOnly = false;
        // Editing this playlist must preserve another playlist's cover entry.
        {
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue(QStringLiteral("99-1234"), QStringLiteral("other.png"));
            settings.sync();
        }
        QVERIFY(request(QStringLiteral("Retry"), {}).property("success").toBool());
        QCOMPARE(savedName, QStringLiteral("Retry"));
        QVERIFY(!QFile::exists(managed.toLocalFile()));
        QVERIFY(PlaylistCoverStore::cover(42, 1234).isEmpty());
        QCOMPARE(PlaylistCoverStore::cover(99, 1234).fileName(), QStringLiteral("other.png"));
        QCOMPARE(changes.count(), 2);
    }

    void testPlaylistTrackTargets()
    {
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(), QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(), engine.rootContext());
        engine.newQObject(m_medialib.get());
        m_env->mediaGet = [](int query, va_list args) -> void* {
            if (query != VLC_ML_GET_PLAYLIST || va_arg(args, int64_t) != 42) return nullptr;
            auto* item = static_cast<vlc_ml_playlist_t*>(calloc(1, sizeof(vlc_ml_playlist_t)));
            item->i_id = 42; item->psz_name = strdup("Saved playlist");
            return item;
        };
        bool empty = false, reject = false;
        m_env->mediaList = [&](int query, const vlc_ml_query_params_t*, va_list args) {
            if (query != VLC_ML_LIST_PLAYLIST_MEDIA || va_arg(args, int64_t) != 42 || reject) return VLC_EGENERIC;
            auto** output = va_arg(args, vlc_ml_media_list_t**);
            auto* list = static_cast<vlc_ml_media_list_t*>(calloc(1, sizeof(vlc_ml_media_list_t) + 4 * sizeof(vlc_ml_media_t)));
            list->i_nb_items = empty ? 0 : 4;
            for (size_t i = 0; i < list->i_nb_items; ++i) {
                auto& item = list->p_items[i];
                item.i_id = i == 2 ? 11 : 11 + i;
                item.psz_filename = strdup("Same name.mkv");
                if (i == 3) continue; // A media record with no usable main file.
                item.p_files = static_cast<vlc_ml_file_list_t*>(calloc(1, sizeof(vlc_ml_file_list_t) + 2 * sizeof(vlc_ml_file_t)));
                item.p_files->i_nb_items = 2;
                item.p_files->p_items[0].i_type = VLC_ML_FILE_TYPE_SUBTITLE;
                item.p_files->p_items[0].psz_mrl = strdup("file:///captions.srt");
                auto& file = item.p_files->p_items[1];
                file.i_type = VLC_ML_FILE_TYPE_MAIN;
                file.psz_mrl = strdup(i == 1 ? "file:///missing/video.mkv" : "file:///one/video%20%26%20fran%C3%A7ais.mkv");
                file.b_present = i != 1;
            }
            *output = list;
            return VLC_SUCCESS;
        };
        auto request = [&](const MLItemId& id) {
            engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
            m_medialib->playlistTrackTargets(id, engine.evaluate("(function(value) { answer = value; })"));
            if (!QTest::qWaitFor([&] { return !engine.globalObject().property("answer").isNull(); }))
                qFatal("Playlist track targets did not finish");
            return engine.globalObject().property("answer");
        };
        const MLItemId id(42, VLC_ML_PARENT_PLAYLIST);
        auto result = request(id);
        QVERIFY(result.property("success").toBool());
        QCOMPARE(result.property("name").toString(), QStringLiteral("Saved playlist"));
        auto items = result.property("items");
        QCOMPARE(items.property("length").toInt(), 2);
        QCOMPARE(items.property(0).property("url").toString(), QStringLiteral("file:///one/video%20%26%20fran%C3%A7ais.mkv"));
        QVERIFY(items.property(0).property("enabled").toBool());
        QVERIFY(!items.property(1).property("enabled").toBool());
        QVERIFY(items.property(0).property("location").toString().contains(QStringLiteral("français")));
        empty = true;
        result = request(id);
        QVERIFY(result.property("success").toBool());
        QCOMPARE(result.property("items").property("length").toInt(), 0);
        reject = true;
        QVERIFY(!request(id).property("success").toBool());
        QVERIFY(!request(MLItemId(99, VLC_ML_PARENT_PLAYLIST)).property("success").toBool());
        QVERIFY(!request(MLItemId(42, VLC_ML_PARENT_UNKNOWN)).property("success").toBool());
    }

    void testSavedPlaylistExport()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        m_env->mediaGet = [&](int query, va_list args) -> void* {
            const auto id = va_arg(args, int64_t);
            if (query == VLC_ML_GET_PLAYLIST && id == 42) {
                auto* playlist = static_cast<vlc_ml_playlist_t*>(calloc(1, sizeof(vlc_ml_playlist_t)));
                playlist->i_id = 42;
                return playlist;
            }
            if (query == VLC_ML_GET_INPUT_ITEM && (id == 11 || id == 12))
                return input_item_New(id == 11 ? "file:///media/first%20%26%20last.mkv" : "https://example.test/video.mp4", "Video");
            return nullptr;
        };
        bool empty = false, reject = false;
        m_env->mediaList = [&](int query, const vlc_ml_query_params_t*, va_list args) {
            if (query != VLC_ML_LIST_PLAYLIST_MEDIA || va_arg(args, int64_t) != 42 || reject) return VLC_EGENERIC;
            auto** output = va_arg(args, vlc_ml_media_list_t**);
            auto* list = static_cast<vlc_ml_media_list_t*>(calloc(1, sizeof(vlc_ml_media_list_t) + 3 * sizeof(vlc_ml_media_t)));
            list->i_nb_items = empty ? 0 : 3;
            list->p_items[0].i_id = list->p_items[2].i_id = 11;
            list->p_items[1].i_id = 12;
            *output = list;
            return VLC_SUCCESS;
        };
        auto exportFile = [&](const MLItemId& id, const QString& path, const QByteArray& format) {
            bool done = false;
            QString error;
            m_medialib->exportPlaylist(id, QUrl::fromLocalFile(path), format, this,
                [&](const QString& result) { error = result; done = true; });
            if (!QTest::qWaitFor([&] { return done; })) qFatal("Playlist export did not finish");
            return error;
        };
        const MLItemId id(42, VLC_ML_PARENT_PLAYLIST);
        const QString file = directory.filePath(QStringLiteral("saved & français.m3u8"));
        QVERIFY(exportFile(id, file, "export-m3u8").isEmpty());
        QFile output(file); QVERIFY(output.open(QIODevice::ReadOnly));
        const auto content = output.readAll(); output.close();
        QVERIFY(content.startsWith("#EXTM3U"));
        QCOMPARE(content.count("first%20%26%20last.mkv"), 2);
        QVERIFY(content.indexOf("first%20%26%20last.mkv") < content.indexOf("https://example.test/video.mp4"));
        QVERIFY(content.lastIndexOf("first%20%26%20last.mkv") > content.indexOf("https://example.test/video.mp4"));
        reject = true;
        QVERIFY(!exportFile(id, file, "export-m3u8").isEmpty());
        QVERIFY(output.open(QIODevice::ReadOnly)); QCOMPARE(output.readAll(), content); output.close();
        reject = false;
        QVERIFY(!exportFile(MLItemId(99, VLC_ML_PARENT_PLAYLIST), file, "export-m3u8").isEmpty());
        QVERIFY(!exportFile(id, file, "unsupported").isEmpty());
        QVERIFY(!exportFile(id, directory.path(), "export-m3u8").isEmpty());
        empty = true;
        QVERIFY(exportFile(id, file, "export-m3u8").isEmpty());
        QVERIFY(output.open(QIODevice::ReadOnly)); QCOMPARE(output.readAll().trimmed(), QByteArray("#EXTM3U"));
    }

    void testAssociatedTracks()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto create = [&](const QString& name) {
            const QString path = directory.filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) return QUrl();
            file.write("test track");
            return QUrl::fromLocalFile(path);
        };
        const auto media = create("video.mkv");
        const auto first = create(QStringLiteral("captions & français.srt"));
        const auto second = create("other.srt");
        QVERIFY(!media.isEmpty() && !first.isEmpty() && !second.isEmpty());
        QList<QPair<QUrl, int>> links;
        int lookups = 0, writes = 0;
        bool rejectSecond = true;
        m_env->mediaGet = [&](int query, va_list args) -> void* {
            if (query != VLC_ML_GET_MEDIA_BY_MRL) return nullptr;
            ++lookups;
            if (QUrl(QString::fromUtf8(va_arg(args, const char*))) != media) return nullptr;
            auto* item = static_cast<vlc_ml_media_t*>(calloc(1, sizeof(vlc_ml_media_t)));
            item->i_id = 42;
            item->p_files = static_cast<vlc_ml_file_list_t*>(calloc(1,
                sizeof(vlc_ml_file_list_t) + (links.size() + 1) * sizeof(vlc_ml_file_t)));
            item->p_files->i_nb_items = links.size() + 1;
            item->p_files->p_items[0].psz_mrl = strdup(media.toEncoded().constData());
            item->p_files->p_items[0].i_type = VLC_ML_FILE_TYPE_MAIN;
            for (int i = 0; i < links.size(); ++i) {
                item->p_files->p_items[i + 1].psz_mrl = strdup(links[i].first.toEncoded().constData());
                item->p_files->p_items[i + 1].i_type = static_cast<vlc_ml_file_type_t>(links[i].second);
            }
            return item;
        };
        m_env->mediaControl = [&](int query, va_list args) {
            if (query != VLC_ML_MEDIA_ADD_EXTERNAL_MRL) return VLC_EGENERIC;
            const auto id = va_arg(args, int64_t);
            const auto uri = QUrl(QString::fromUtf8(va_arg(args, const char*)));
            const auto type = va_arg(args, int);
            if (id != 42) return VLC_EGENERIC;
            ++writes;
            if (uri == second && rejectSecond) return VLC_EGENERIC;
            links.push_back({uri, type});
            return VLC_SUCCESS;
        };
        auto add = [&](const QList<QUrl>& tracks, bool subtitles) {
            bool done = false;
            MediaLib::TrackAddition result;
            m_medialib->addMediaTracks(media, tracks, subtitles, this,
                [&](const MediaLib::TrackAddition& value) { result = value; done = true; });
            if (!QTest::qWaitFor([&] { return done; }))
                qFatal("Track request did not finish");
            return result;
        };
        auto result = add({first, first, second}, true);
        QCOMPARE(result.added, QList<QUrl>{first});
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(links.size(), 1);
        rejectSecond = false;
        result = add({first, second}, true);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.added, QList<QUrl>{second});
        QCOMPARE(links.size(), 2);
        const int previousWrites = writes;
        result = add({first, second}, true);
        QVERIFY(result.error.isEmpty());
        QVERIFY(result.added.isEmpty());
        QCOMPARE(writes, previousWrites);
        result = add({first}, false);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(writes, previousWrites);
        const int previousLookups = lookups;
        result = add({QUrl("missing.srt")}, true);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(lookups, previousLookups);
        result = add({media}, false);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(lookups, previousLookups);
        const auto sound = create("sound.wav");
        result = add({sound}, false);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.added, QList<QUrl>{sound});
        QCOMPARE(links.last().second, int(VLC_ML_FILE_TYPE_SOUNDTRACK));
    }

    void testTrackMatching()
    {
        const auto url = [](const char* name) { return QUrl(QString::fromUtf8(name)); };
        const QList<QUrl> media = {url("file:///show/The_Wand_S01E01.mp4"),
            url("file:///show/The_Wand_S01E02.mp4"), url("file:///show/The_Wand_S01E10.mp4")};
        const auto english = url("file:///tracks/The_Wand_S01E01_English.srt");
        const auto spanish = url("file:///tracks/The.Wand.1x01.Spanish.srt");
        const auto second = url("file:///tracks/S01E02.srt");
        const auto otherShow = url("file:///tracks/Other_Show_S01E01.srt");
        auto matches = m_medialib->matchTrackAssignments(media, {english, spanish, second, otherShow, english});
        QCOMPARE(matches.value(media[0].toString(QUrl::FullyEncoded)).toStringList(),
            QStringList({english.toString(QUrl::FullyEncoded), spanish.toString(QUrl::FullyEncoded)}));
        QCOMPARE(matches.value(media[1].toString(QUrl::FullyEncoded)).toStringList(),
            QStringList({second.toString(QUrl::FullyEncoded)}));
        QVERIFY(!matches.contains(media[2].toString(QUrl::FullyEncoded)));
        // No arbitrary assignment between two locations with the same filename.
        QVERIFY(m_medialib->matchTrackAssignments({media[0], url("file:///other/The_Wand_S01E01.mp4")}, {english}).isEmpty());
        QCOMPARE(m_medialib->matchTrackAssignments({media[0], media[0]}, {english}).size(), 1);
        QVERIFY(m_medialib->matchTrackAssignments({media[1], url("file:///other/Other_Show_S01E02.mp4")}, {second}).isEmpty());
        QVERIFY(m_medialib->matchTrackAssignments({url("file:///movie/Up.mkv")}, {url("file:///tracks/Up_in_the_Air.srt")}).isEmpty());
        QCOMPARE(m_medialib->matchTrackAssignments({url("file:///movie/Up.mkv")}, {url("file:///tracks/Up.English.srt")}).size(), 1);
        QCOMPARE(m_medialib->matchTrackAssignments({url("file:///movie/FRAN%C3%87AIS.mkv")}, {url("file:///tracks/fran%C3%A7ais.srt")}).size(), 1);
        QCOMPARE(m_medialib->matchTrackAssignments({url("file:///show/The_Wand_S01E04.mp4")},
            {url("file:///tracks/thewand_s01e04.srt")}).size(), 1);
        QCOMPARE(m_medialib->matchTrackAssignments({url("file:///show/The_Wand_S01E04.mp4")},
            {url("file:///tracks/thewand_s01e04_English.srt")}).size(), 1);
        QCOMPARE(m_medialib->matchTrackAssignments({url("file:///movie/The_Wand.mkv")},
            {url("file:///tracks/thewand.English.srt")}).size(), 1);
        QVERIFY(m_medialib->matchTrackAssignments({url("file:///movie/The_Wand.mkv")},
            {url("file:///tracks/thewand_returns.srt")}).isEmpty());
        QVERIFY(m_medialib->matchTrackAssignments({url("file:///show/The_Wand_S01E04.mp4"),
            url("file:///other/thewand_S01E04.mkv")}, {url("file:///tracks/thewand_s01e04_English.srt")}).isEmpty());
    }

    void testTrackMatchingEpisodeFolders()
    {
        const QUrl file("file:///show/The_Wand_S01E04.mkv");
        const QUrl track("file:///show/Subs/The.Wand.Season1Episode4/English.srt");
        const auto matches = m_medialib->matchTrackAssignments({file}, {track});
        QCOMPARE(matches.value(file.toString()).toStringList(), QStringList{track.toString()});
        QVERIFY(m_medialib->matchTrackAssignments({file},
            {QUrl("file:///show/Subs/Other_Show_S01E04/English.srt")}).isEmpty());
        QVERIFY(m_medialib->matchTrackAssignments({file},
            {QUrl("file:///show/Subs/The_Wand_S02E04/English.srt")}).isEmpty());
        QVERIFY(m_medialib->matchTrackAssignments({file},
            {QUrl("file:///show/Subs/The_Wand_S01E04/Different_Film.srt")}).isEmpty());
        QCOMPARE(m_medialib->matchTrackAssignments({QUrl("file:///movies/English.mkv")},
            {QUrl("file:///tracks/English.srt")}).size(), 1);
        const QUrl film("file:///movies/The_Wand.mkv"), captions("file:///movies/Subtitles/The.Wand/French.forced.srt");
        QCOMPARE(m_medialib->matchTrackAssignments({film}, {captions}).size(), 1);
        QVERIFY(m_medialib->matchTrackAssignments({file, QUrl("file:///other/The_Wand_S01E04.mkv")}, {track}).isEmpty());
    }

    void testNativeMediaIdentityEvidence()
    {
        SharedInputItem item(input_item_New("file:///Show.S01E03.mkv", "Show.S01E03.mkv"), false);
        QVERIFY(item);
        input_item_SetMeta(item.get(), vlc_meta_ShowName, "Show");
        input_item_SetMeta(item.get(), vlc_meta_Season, "1");
        input_item_SetMeta(item.get(), vlc_meta_Episode, "3");
        input_item_SetMeta(item.get(), vlc_meta_Date, "2024-03-15");
        const auto episode = MediaIdentity::inputMetadata(item.get());
        QCOMPARE(episode.value("showName").toString(), "Show");
        QCOMPARE(episode.value("episode").toString(), "3");
        QVERIFY(!episode.contains("year")); // An episode air year is not a series start year.
        input_item_SetMeta(item.get(), vlc_meta_ShowName, nullptr);
        input_item_SetMeta(item.get(), vlc_meta_Episode, nullptr);
        QCOMPARE(MediaIdentity::inputMetadata(item.get()).value("year").toInt(), 2024);
        input_item_SetMeta(item.get(), vlc_meta_Artist, "Artist");
        input_item_SetMeta(item.get(), vlc_meta_Album, "Album");
        input_item_SetMeta(item.get(), vlc_meta_AlbumArtist, "Album artist");
        input_item_SetMeta(item.get(), vlc_meta_TrackNumber, "3/12");
        input_item_SetMeta(item.get(), vlc_meta_DiscNumber, "2");
        const auto audio = MediaIdentity::inputMetadata(item.get());
        QCOMPARE(audio.value("artist").toString(), "Artist");
        QCOMPARE(audio.value("album").toString(), "Album");
        QCOMPARE(audio.value("track").toString(), "3/12");
        input_item_SetMetaExtra(item.get(), "podcast", "1");
        input_item_SetMetaExtra(item.get(), "purl", "https://example.com/feed");
        input_item_SetMetaExtra(item.get(), "egid", "episode-3");
        const auto podcast = MediaIdentity::inputMetadata(item.get());
        QCOMPARE(podcast.value("type").toString(), "podcastEpisode");
        QCOMPARE(podcast.value("series").toString(), "Album");
        QCOMPARE(podcast.value("guid").toString(), "episode-3");
        QVERIFY(!podcast.contains("year"));
        QCOMPARE(podcast.value("airDate").toString(), "2024-03-15");
        SharedInputItem feedItem(input_item_New("https://example.test/episode.mp3", "Feed episode"), false);
        input_item_SetShowName(feedItem.get(), "Feed show");
        input_item_SetMetaExtra(feedItem.get(), "podcast:feed", "https://example.test/feed.xml");
        input_item_SetMetaExtra(feedItem.get(), "podcast:guid", "feed-episode-8");
        input_item_SetDate(feedItem.get(), "Fri, 23 Oct 2026 12:50:14 GMT");
        const auto feedMetadata = MediaIdentity::inputMetadata(feedItem.get());
        QCOMPARE(feedMetadata.value("type").toString(), "podcastEpisode");
        QCOMPARE(feedMetadata.value("guid").toString(), "feed-episode-8");
        QCOMPARE(feedMetadata.value("airDate").toString(), "2026-10-23");
        vlc_ml_media_t feedMedia{};
        feedMedia.p_podcast = feedItem.get();
        QCOMPARE(MediaIdentity::libraryMetadata(&feedMedia, QUrl("https://example.test/episode.mp3")).value("type").toString(), "podcastEpisode");
    }

    void testSubtitleMatchingUsesProviderFacts()
    {
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(), QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(), engine.rootContext());
        engine.newQObject(m_medialib.get());
        engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
        const QString url = "file:///Show.S01E03.mkv", subtitle = "opensubtitles:/42/unhelpful.srt";
        const QVariantList media{QVariantMap{{"url", url}}};
        const QVariantList sources{QVariantMap{{"url", subtitle}, {"name", "unhelpful.srt"}, {"language", "en"},
            {"feature", QVariantMap{{"feature_type", "Episode"}, {"parent_title", "Show"}, {"season_number", 1}, {"episode_number", 3}}}}};
        m_medialib->requestSubtitleMatches(media, sources, "en", engine.evaluate("(function(value) { answer = value; })"));
        QVERIFY(QTest::qWaitFor([&] { return !engine.globalObject().property("answer").isNull(); }));
        QCOMPARE(engine.globalObject().property("answer").toVariant().toMap().value(url).toStringList(), QStringList{subtitle});
    }

    void testTrackMatchingAsync()
    {
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(), QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(), engine.rootContext());
        engine.newQObject(m_medialib.get());
        engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
        engine.globalObject().setProperty("staleCalls", 0);
        QList<QUrl> media, tracks;
        for (int i = 0; i < 2000; ++i) {
            media.append(QUrl(QString("file:///show/Show_S01E%1.mkv").arg(i)));
            tracks.append(QUrl(QString("file:///tracks/Show_S01E%1.en.srt").arg(i)));
        }
        m_medialib->requestTrackMatches(media, tracks, engine.evaluate("(function(value) { ++staleCalls; })"));
        QVERIFY(engine.globalObject().property("answer").isNull());
        const QUrl file("file:///show/Show_S01E04.mkv"), track("file:///tracks/show_s01e04.en.srt");
        m_medialib->requestTrackMatches({file}, {track}, engine.evaluate("(function(value) { answer = value; })"));
        QVERIFY(engine.globalObject().property("answer").isNull());
        QVERIFY(QTest::qWaitFor([&] { return !engine.globalObject().property("answer").isNull(); }));
        const auto answer = engine.globalObject().property("answer").toVariant().toMap();
        QCOMPARE(answer.value(file.toString()).toList(), QVariantList{track.toString()});
        QCOMPARE(engine.globalObject().property("staleCalls").toInt(), 0);
        m_medialib->requestTrackMatches(media, tracks, engine.evaluate("(function(value) { ++staleCalls; })"));
        m_medialib->cancelTrackMatching();
        QTest::qWait(50);
        QCOMPARE(engine.globalObject().property("staleCalls").toInt(), 0);
    }

    void testTrackFolderRuleDraftKeepsInheritedChoices()
    {
        namespace Rules = vlc::medialibrary::trackrules;
        QTemporaryDir directory;
        const auto folder = QUrl::fromLocalFile(directory.path());
        const auto source = QUrl::fromLocalFile(directory.filePath("captions"));
        const auto media = QUrl::fromLocalFile(directory.filePath("Show_S01E02.mp4"));
        const auto english = QUrl::fromLocalFile(directory.filePath("captions/Show_S01E02.en.srt")).toString(QUrl::FullyEncoded);
        const auto french = QUrl::fromLocalFile(directory.filePath("captions/Show_S01E02.fr.srt")).toString(QUrl::FullyEncoded);
        const auto old = QString::fromStdString(Rules::encode({{"Show_S01E01.mp4",directory.filePath("captions/Show_S01E01.en.srt").toStdString(),source.toLocalFile().toStdString()}}));
        QVariantMap row{{"url",media.toString(QUrl::FullyEncoded)}, {"tracks",QStringList{}},
            {"initialTracks",QStringList{}}, {"inheritedRule",true}, {"manual",false}};
        QCOMPARE(m_medialib->trackFolderRuleDefinition({row},folder,source,old),old);
        row["manual"] = true;
        QVERIFY(m_medialib->trackFolderRuleDefinition({row},folder,source,old).isEmpty());
        row["tracks"] = QStringList{english,french};
        std::vector<Rules::Pair> pairs;
        QVERIFY(Rules::decode(m_medialib->trackFolderRuleDefinition({row},folder,source,old).toStdString(),pairs));
        QCOMPARE(pairs.size(),size_t(2));
    }

    void testInheritedChoicePinAndFolderRuleRetry()
    {
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(),QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(),engine.rootContext());engine.newQObject(m_medialib.get());
        QTemporaryDir directory;
        const auto media=QUrl::fromLocalFile(directory.filePath("Show_S01E01.mp4"));
        const auto captions=QUrl::fromLocalFile(directory.filePath("Show_S01E01.en.srt"));
        const auto text=captions.toString(QUrl::FullyEncoded);
        for(const auto& url:{media,captions}) {QFile file(url.toLocalFile());QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");}
        bool pinned=false,rejectRule=true;int trackWrites=0,ruleWrites=0;
        m_env->mediaGet=[&](int query,va_list args)->void* {
            if(query!=VLC_ML_GET_MEDIA_BY_MRL)return nullptr;
            if(QUrl(QString::fromUtf8(va_arg(args,const char*)))!=media)return nullptr;
            auto* result=static_cast<vlc_ml_media_t*>(calloc(1,sizeof(vlc_ml_media_t)));result->i_id=90;result->b_subtitle_track_selection=pinned;
            result->p_files=static_cast<vlc_ml_file_list_t*>(calloc(1,sizeof(vlc_ml_file_list_t)+2*sizeof(vlc_ml_file_t)));result->p_files->i_nb_items=2;
            result->p_files->p_items[0].i_type=VLC_ML_FILE_TYPE_MAIN;result->p_files->p_items[0].psz_mrl=strdup(media.toEncoded().constData());
            result->p_files->p_items[1].i_type=VLC_ML_FILE_TYPE_SUBTITLE;result->p_files->p_items[1].psz_mrl=strdup(captions.toEncoded().constData());
            return result;
        };
        m_env->mediaControl=[&](int query,va_list args) {
            if(query==VLC_ML_MEDIA_SET_EXTERNAL_MRLS) {++trackWrites;pinned=true;return VLC_SUCCESS;}
            if(query==VLC_ML_FOLDER_SET_TRACK_RULE) {++ruleWrites;return rejectRule?VLC_EGENERIC:VLC_SUCCESS;}
            return VLC_EGENERIC;
        };
        const auto definition=m_medialib->trackFolderRuleDefinition({QVariantMap{{"url",media},{"tracks",QStringList{text}}}},QUrl::fromLocalFile(directory.path()),QUrl::fromLocalFile(directory.path()));
        QVERIFY(!definition.isEmpty());
        const QVariantMap rule{{"folder",QUrl::fromLocalFile(directory.path())},{"definition",definition}};
        const QVariantMap row{{"url",media},{"baseline",QStringList{text}},{"initialTracks",QStringList{text}},{"tracks",QStringList{text}},{"inheritedRule",true},{"manual",true}};
        auto save=[&](const QVariantList& rows) {
            engine.globalObject().setProperty("answer",QJSValue(QJSValue::NullValue));
            m_medialib->saveTrackAssignmentsWithRule(rows,true,rule,engine.evaluate("(function(value) { answer=value; })"));
            if(!QTest::qWaitFor([&]{return !engine.globalObject().property("answer").isNull();}))qFatal("Folder save did not finish");
            return engine.globalObject().property("answer");
        };
        auto result=save({row});QVERIFY(!result.property("success").toBool());QVERIFY(pinned);
        QCOMPARE(trackWrites,1);QCOMPARE(ruleWrites,1);QCOMPARE(result.property("saved").property("length").toInt(),1);
        rejectRule=false;result=save({});QVERIFY(result.property("success").toBool());QVERIFY(result.property("folderRuleSaved").toBool());
        QCOMPARE(trackWrites,1);QCOMPARE(ruleWrites,2);QCOMPARE(result.property("folderRule").toString(),definition);
    }

    void testTrackAssignmentRead()
    {
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(), QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(), engine.rootContext());
        engine.newQObject(m_medialib.get());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (const auto& name : {"Show_S01E10.mkv", "Show_S01E02.mkv", "Show_S01E02.en.srt", "captions.IDX", "captions.sub"}) {
            QFile file(directory.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture");
        }
        m_env->mediaGet = [](int, va_list) -> void* { return nullptr; };
        int writes = 0;
        m_env->mediaControl = [&](int query, va_list args) {
            if (query == VLC_ML_FOLDER_GET_TRACK_RULE) {
                va_arg(args, const char*); va_arg(args, int); *va_arg(args, char**) = nullptr; return VLC_SUCCESS;
            }
            if (query == VLC_ML_MEDIA_GET_FOLDER_TRACKS) {
                va_arg(args, const char*); va_arg(args, int); *va_arg(args, bool*) = false;
                *va_arg(args, vlc_ml_file_list_t**) = nullptr; return VLC_SUCCESS;
            }
            ++writes; return VLC_EGENERIC;
        };
        QVariantMap runtime;
        auto request = [&](bool sources, const QUrl& folder) {
            engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
            auto callback = engine.evaluate("(function(value) { answer = value; })");
            if (sources) m_medialib->trackAssignmentSources(folder, true, callback);
            else {
                auto target = runtime;
                target.insert("scope", "folder"); target.insert("folder", folder);
                m_medialib->trackAssignmentRows(target, true, callback);
            }
            if (!QTest::qWaitFor([&] { return !engine.globalObject().property("answer").isNull(); }))
                qFatal("Track assignment read did not finish");
            return engine.globalObject().property("answer");
        };
        const auto folder = QUrl::fromLocalFile(directory.path());
        auto result = request(false, folder);
        QVERIFY(result.property("success").toBool());
        const auto rows = result.property("rows");
        QCOMPARE(rows.property("length").toInt(), 2);
        QCOMPARE(rows.property(0).property("name").toString(), QStringLiteral("Show_S01E02.mkv"));
        QCOMPARE(rows.property(1).property("name").toString(), QStringLiteral("Show_S01E10.mkv"));
        QCOMPARE(rows.property(0).property("baseline").property("length").toInt(), 0);
        result = request(true, folder);
        QVERIFY(result.property("success").toBool());
        QCOMPARE(result.property("sources").property("length").toInt(), 2);
        QCOMPARE(result.property("sources").property(0).property("name").toString(), QStringLiteral("captions.IDX"));
        QVERIFY(QDir(directory.path()).mkpath("Subs/Show_S01E02"));
        QVERIFY(QDir(directory.path()).mkpath("Subtitles/Show_S01E10"));
        QVERIFY(QDir(directory.path()).mkpath("Other"));
        for (const auto& name : {"Subs/Show_S01E02/English.srt", "Subs/captions.sub", "Subtitles/Show_S01E10/French.srt", "Other/unrelated.srt"}) {
            QFile file(directory.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture");
        }
        result = request(true, folder);
        const auto found = result.property("sources");
        QCOMPARE(found.property("length").toInt(), 5);
        QStringList names;
        for (int i = 0; i < found.property("length").toInt(); ++i)
            names.append(found.property(i).property("name").toString());
        QVERIFY(names.contains("Subs/Show_S01E02/English.srt"));
        QVERIFY(names.contains("Subs/captions.sub")); // The index in another folder is not its VobSub pair.
        QVERIFY(names.contains("Subtitles/Show_S01E10/French.srt"));
        QVERIFY(!names.contains("Other/unrelated.srt"));
        const auto loaded = QUrl::fromLocalFile(directory.filePath("loaded & captions.srt"));
        runtime = {{"runtimeUrl", QUrl::fromLocalFile(directory.filePath("Show_S01E02.mkv"))},
            {"runtimeTracks", QVariantList{loaded, loaded}}};
        result = request(false, folder);
        QCOMPARE(result.property("rows").property(0).property("baseline").property("length").toInt(), 0);
        QCOMPARE(result.property("rows").property(0).property("initialTracks").property("length").toInt(), 1);
        QCOMPARE(result.property("rows").property(0).property("initialTracks").property(0).toString(), loaded.toString(QUrl::FullyEncoded));
        QCOMPARE(result.property("rows").property(1).property("initialTracks").property("length").toInt(), 0);
        m_env->mediaGet = [&](int query, va_list args) -> void* {
            if (query != VLC_ML_GET_MEDIA_BY_MRL) return nullptr;
            const auto uri = QUrl(QString::fromUtf8(va_arg(args, const char*)));
            if (uri.fileName() != "Show_S01E02.mkv") return nullptr;
            auto* media = static_cast<vlc_ml_media_t*>(calloc(1, sizeof(vlc_ml_media_t)));
            media->b_subtitle_track_selection = true;
            media->p_files = static_cast<vlc_ml_file_list_t*>(calloc(1,
                sizeof(vlc_ml_file_list_t) + 2 * sizeof(vlc_ml_file_t)));
            media->p_files->i_nb_items = 2;
            for (int i = 0; i < 2; ++i) {
                auto& file = media->p_files->p_items[i];
                file.i_type = VLC_ML_FILE_TYPE_SUBTITLE; file.b_external = false;
                file.b_unselected = i == 1;
                file.psz_mrl = strdup(QUrl::fromLocalFile(directory.filePath(
                    i ? "captions.IDX" : "Show_S01E02.en.srt")).toEncoded().constData());
            }
            return media;
        };
        result = request(false, folder);
        QVERIFY(result.property("rows").property(0).property("explicitSelection").toBool());
        QCOMPARE(result.property("rows").property(0).property("initialTracks").property("length").toInt(), 1);
        QVERIFY(!result.property("rows").property(0).property("initialTracks").property(0).toString().contains("loaded"));
        QCOMPARE(result.property("rows").property(0).property("baseline").property("length").toInt(), 1);
        QVERIFY(result.property("rows").property(0).property("baseline").property(0).toString().endsWith("Show_S01E02.en.srt"));
        QVERIFY(!request(true, QUrl("https://example.test/folder")).property("success").toBool());
        QVERIFY(!request(false, QUrl::fromLocalFile(directory.filePath("missing"))).property("success").toBool());
        QCOMPARE(writes, 0); // Opening, scanning and cancelling do not create media records.
    }

    void testTrackAssignmentUrlIdentity_data()
    {
        QTest::addColumn<bool>("subtitles");
        QTest::addColumn<QString>("origin");
        for (const auto subtitles : {true, false})
            for (const auto& origin : {"runtime", "saved", "folder"})
                QTest::newRow(qPrintable(QString(subtitles ? "subtitle-" : "audio-") + origin)) << subtitles << QString(origin);
    }

    void testTrackAssignmentUrlIdentity()
    {
        QFETCH(bool, subtitles);
        QFETCH(QString, origin);
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(), QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(), engine.rootContext());
        engine.newQObject(m_medialib.get());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto mediaUrl = QUrl::fromLocalFile(directory.filePath("show.mkv"));
        const auto track = QUrl::fromLocalFile(directory.filePath(QStringLiteral("captions & +;= français 100%.") + (subtitles ? "srt" : "wav")));
        QFile trackFile(track.toLocalFile()); QVERIFY(trackFile.open(QIODevice::WriteOnly)); trackFile.write("track"); trackFile.close();
        const auto encoded = track.toString(QUrl::FullyEncoded);
        auto escaped = encoded;
        escaped.replace("&", "%26").replace("+", "%2B").replace(";", "%3B").replace("=", "%3D");
        QVERIFY(escaped != encoded);
        QCOMPARE(QUrl(escaped).toLocalFile(), track.toLocalFile());
        const auto type = subtitles ? VLC_ML_FILE_TYPE_SUBTITLE : VLC_ML_FILE_TYPE_SOUNDTRACK;
        auto files = [&] {
            auto* list = static_cast<vlc_ml_file_list_t*>(calloc(1, sizeof(vlc_ml_file_list_t) + sizeof(vlc_ml_file_t)));
            list->i_nb_items = 1;
            list->p_items[0].i_type = type;
            list->p_items[0].psz_mrl = strdup(qtu(escaped));
            return list;
        };
        m_env->mediaGet = [&](int query, va_list) -> void* {
            if (query != VLC_ML_GET_MEDIA_BY_MRL) return nullptr;
            auto* media = static_cast<vlc_ml_media_t*>(calloc(1, sizeof(vlc_ml_media_t)));
            media->i_id = 1;
            if (origin == "saved") {
                media->b_subtitle_track_selection = subtitles;
                media->b_soundtrack_track_selection = !subtitles;
                media->p_files = files();
            }
            return media;
        };
        int writes = 0;
        m_env->mediaControl = [&](int query, va_list args) {
            if (query == VLC_ML_MEDIA_GET_FOLDER_TRACKS) {
                va_arg(args, const char*); va_arg(args, int);
                *va_arg(args, bool*) = origin == "folder";
                *va_arg(args, vlc_ml_file_list_t**) = origin == "folder" ? files() : nullptr;
                return VLC_SUCCESS;
            }
            ++writes; return VLC_EGENERIC;
        };
        const auto callback = engine.evaluate("(function(value) { answer=value; })");
        auto waitForAnswer = [&] {
            if (!QTest::qWaitFor([&] { return !engine.globalObject().property("answer").isNull(); }))
                qFatal("Track URL request did not finish");
            return engine.globalObject().property("answer");
        };
        engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
        m_medialib->trackAssignmentSources(QUrl::fromLocalFile(directory.path()), subtitles, callback);
        auto result = waitForAnswer(); QVERIFY(result.property("success").toBool());
        QCOMPARE(result.property("sources").property("length").toInt(), 1);
        const auto discovered = result.property("sources").property(0).property("url").toString();
        engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
        QVariantMap target{{"scope", "file"}, {"url", mediaUrl}};
        if (origin == "runtime") {
            target.insert("runtimeUrl", mediaUrl);
            target.insert("runtimeTracks", QVariantList{encoded, escaped});
        }
        m_medialib->trackAssignmentRows(target, subtitles, callback);
        result = waitForAnswer(); QVERIFY(result.property("success").toBool());
        const auto row = result.property("rows").property(0);
        QCOMPARE(row.property("initialTracks").property("length").toInt(), 1);
        QCOMPARE(row.property("initialTracks").property(0).toString(), discovered);
        QCOMPARE(row.property("baseline").property("length").toInt(), origin == "saved" ? 1 : 0);
        if (origin == "saved") {
            auto unchanged = row.toVariant().toMap(); unchanged.insert("tracks", QStringList{encoded, escaped});
            engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
            m_medialib->saveTrackAssignments({unchanged}, subtitles, callback);
            result = waitForAnswer(); QVERIFY(result.property("success").toBool());
            QCOMPARE(result.property("saved").property("length").toInt(), 0);
        }
        if (origin == "runtime") {
            // Decode local filenames once. Preserve query separators and stream selectors.
            const QStringList distinct{
                QUrl::fromLocalFile(directory.filePath("captions %26.srt")).toString(QUrl::FullyEncoded),
                QStringLiteral("https://example.test/captions?name=A%26B&language=en"),
                QStringLiteral("https://example.test/captions?name=A&B&language=en"),
                encoded + QStringLiteral("?track=A%26B"),
                encoded + QStringLiteral("#track=A%26B")};
            QVariantList loaded{encoded, escaped};
            for (const auto& url : distinct) loaded.append(url);
            target.insert("runtimeTracks", loaded);
            engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
            m_medialib->trackAssignmentRows(target, subtitles, callback);
            result = waitForAnswer(); QVERIFY(result.property("success").toBool());
            const auto initial = result.property("rows").property(0).property("initialTracks");
            QCOMPARE(initial.property("length").toInt(), distinct.size() + 1);
            QCOMPARE(initial.property(0).toString(), discovered);
            for (int i = 0; i < distinct.size(); ++i) QCOMPARE(initial.property(i + 1).toString(), distinct[i]);
        }
        QCOMPARE(writes, 0);
    }

    void testTrackFolderCaptionContext_data()
    {
        QTest::addColumn<int>("mediaCategory"); QTest::addColumn<int>("runtimeCategory");
        QTest::addColumn<int>("libraryCategory"); QTest::addColumn<bool>("mixed"); QTest::addColumn<int>("expected");
        QTest::newRow("stored-video") << 1 << 0 << 0 << false << 1;
        QTest::newRow("stored-audio") << 2 << 0 << 0 << false << 2;
        QTest::newRow("playing-video") << 0 << 1 << 0 << false << 1;
        QTest::newRow("playing-audio") << 0 << 2 << 0 << false << 2;
        QTest::newRow("library-context") << 1 << 1 << 2 << false << 2;
        QTest::newRow("video-playlist") << 1 << 0 << 4 << false << 1;
        QTest::newRow("mixed-playlist") << 1 << 1 << 4 << true << 0;
        QTest::newRow("unknown") << 0 << 0 << 0 << false << 0;
    }

    void testTrackFolderCaptionContext()
    {
        QFETCH(int, mediaCategory); QFETCH(int, runtimeCategory); QFETCH(int, libraryCategory);
        QFETCH(bool, mixed); QFETCH(int, expected);
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(), QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(), engine.rootContext()); engine.newQObject(m_medialib.get());
        const QUrl first("file:///media/Shows/Episode.mkv"), second("file:///media/Album/Song.flac");
        m_env->mediaGet = [&](int query, va_list args) -> void* {
            if (query != VLC_ML_GET_MEDIA_BY_MRL) return nullptr;
            const auto url = QUrl(QString::fromUtf8(va_arg(args, const char*)));
            auto* media = static_cast<vlc_ml_media_t*>(calloc(1, sizeof(vlc_ml_media_t)));
            const auto category = url == second ? 2 : mediaCategory;
            media->i_type = category == 1 ? VLC_ML_MEDIA_TYPE_VIDEO : category == 2 ? VLC_ML_MEDIA_TYPE_AUDIO : VLC_ML_MEDIA_TYPE_UNKNOWN;
            media->b_subtitle_track_selection = true;
            return media;
        };
        int lists = 0;
        m_env->mediaList = [&](int, const vlc_ml_query_params_t*, va_list) { ++lists; return VLC_EGENERIC; };
        QSignalSpy changed(m_medialib.get(), &MediaLib::folderRootsChanged);
        const QStringList roots{"file:///media/Shows", "file://nas/Shows%20%26%20Films"};
        auto duplicateRoots = roots; duplicateRoots.append(roots.first()); duplicateRoots.append(QString());
        m_medialib->setFolderRoots(duplicateRoots); m_medialib->setFolderRoots(roots);
        QCOMPARE(changed.count(), 1); QCOMPARE(m_medialib->folderRoots(), roots);
        int writes = 0;
        m_env->mediaControl = [&](int, va_list) { ++writes; return VLC_EGENERIC; };
        QVariantList urls{first}; if (mixed) urls.append(second);
        const QVariantMap target{{"scope","queue"},{"urls",urls},{"runtimeUrl",first},
            {"runtimeCategory",runtimeCategory},{"libraryCategory",libraryCategory}};
        engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
        m_medialib->trackAssignmentRows(target, true, engine.evaluate("(function(value) { answer=value; })"));
        QVERIFY(QTest::qWaitFor([&] { return !engine.globalObject().property("answer").isNull(); }));
        const auto result = engine.globalObject().property("answer");
        QVERIFY(result.property("success").toBool());
        QCOMPARE(result.property("folderCategory").toInt(), expected);
        QCOMPARE(result.property("rows").property("length").toInt(), mixed ? 2 : 1);
        QCOMPARE(writes, 0); QCOMPARE(lists, 0);
    }

    void testTrackDropSources()
    {
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(), QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(), engine.rootContext());
        engine.newQObject(m_medialib.get());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir(directory.path()).mkpath("Subs/Show_S01E02"));
        for (const auto& name : {"Show_S01E10.en.srt", "Subs/Show_S01E02/English.srt", "captions.IDX", "captions.sub", "sound.wav", "video.mkv"}) {
            QFile file(directory.filePath(name)); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("Keep this file");
        }
        const auto url = [&](const QString& name) { return QUrl::fromLocalFile(directory.filePath(name)); };
        auto prepare = [&](const QList<QUrl>& urls, bool subtitles = true) {
            engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
            m_medialib->prepareTrackDrop(urls, subtitles, engine.evaluate("(function(value) { answer = value; })"));
            if (!QTest::qWaitFor([&] { return !engine.globalObject().property("answer").isNull(); }))
                qFatal("Dropped track preparation did not finish");
            return engine.globalObject().property("answer");
        };
        int writes = 0;
        m_env->mediaControl = [&](int, va_list) { ++writes; return VLC_EGENERIC; };
        auto result = prepare({QUrl::fromLocalFile(directory.path()), url("captions.sub"), url("Show_S01E10.en.srt"),
            url("missing.srt"), url("video.mkv"), QUrl("https://example.test/subtitle.srt")});
        QVERIFY(result.property("success").toBool());
        QCOMPARE(result.property("skipped").toInt(), 3);
        QCOMPARE(result.property("sources").property("length").toInt(), 3);
        QCOMPARE(result.property("folder").toString(), QUrl::fromLocalFile(directory.path()).toString());
        QVERIFY(!result.property("error").toString().isEmpty());
        result = prepare({url("captions.sub")});
        QVERIFY(result.property("success").toBool());
        QVERIFY(result.property("sources").property(0).property("url").toString().endsWith("captions.IDX"));
        result = prepare({url("sound.wav"), url("captions.IDX")}, false);
        QVERIFY(result.property("success").toBool()); QCOMPARE(result.property("sources").property("length").toInt(), 1);
        result = prepare({url("video.mkv")}); QVERIFY(!result.property("success").toBool());
        QCOMPARE(result.property("sources").property("length").toInt(), 0);
        QFile disk(directory.filePath("captions.sub")); QVERIFY(disk.open(QIODevice::ReadOnly));
        QCOMPARE(disk.readAll(), QByteArray("Keep this file")); QCOMPARE(writes, 0);
    }

    void testTrackAssignmentSave()
    {
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(m_medialib.get(), QQmlEngine::CppOwnership);
        QQmlEngine::setContextForObject(m_medialib.get(), engine.rootContext());
        engine.newQObject(m_medialib.get());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto create = [&](const QString& name) {
            const auto path = directory.filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) return QUrl();
            file.write("Keep this file");
            return QUrl::fromLocalFile(path);
        };
        const auto first = create("first.mkv"), second = create("second.mkv");
        const auto subtitle = create(QStringLiteral("captions & français.srt"));
        const auto audio = create("sound.wav");
        const QString offline = QStringLiteral("file:///offline/keep.srt");
        const auto encoded = [](const QUrl& value) { return value.toString(QUrl::FullyEncoded); };
        QMap<int64_t, QStringList> assignments{{11, {offline}}, {12, {}}};
        int writes = 0;
        bool rejectSecond = false;
        m_env->mediaGet = [&](int query, va_list args) -> void* {
            if (query != VLC_ML_GET_MEDIA_BY_MRL) return nullptr;
            const QUrl url(QString::fromUtf8(va_arg(args, const char*)));
            const int64_t id = url == first ? 11 : url == second ? 12 : 0;
            if (!id) return nullptr;
            auto* item = static_cast<vlc_ml_media_t*>(calloc(1, sizeof(vlc_ml_media_t)));
            item->i_id = id;
            item->p_files = static_cast<vlc_ml_file_list_t*>(calloc(1,
                sizeof(vlc_ml_file_list_t) + (assignments[id].size() + 2) * sizeof(vlc_ml_file_t)));
            item->p_files->i_nb_items = assignments[id].size() + 2;
            auto* files = item->p_files->p_items;
            files[0].i_type = VLC_ML_FILE_TYPE_MAIN;
            files[0].psz_mrl = strdup(url.toEncoded().constData());
            files[1].i_type = VLC_ML_FILE_TYPE_SOUNDTRACK; files[1].b_external = true;
            files[1].psz_mrl = strdup(audio.toEncoded().constData());
            for (int i = 0; i < assignments[id].size(); ++i) {
                files[i + 2].i_type = VLC_ML_FILE_TYPE_SUBTITLE; files[i + 2].b_external = false;
                files[i + 2].psz_mrl = strdup(assignments[id][i].toUtf8().constData());
            }
            return item;
        };
        m_env->mediaControl = [&](int query, va_list args) {
            if (query != VLC_ML_MEDIA_SET_EXTERNAL_MRLS) return VLC_EGENERIC;
            const auto id = va_arg(args, int64_t);
            const auto type = va_arg(args, int);
            const auto tracks = va_arg(args, const char* const*);
            const auto count = va_arg(args, size_t);
            if (type != VLC_ML_FILE_TYPE_SUBTITLE) return VLC_EGENERIC;
            ++writes;
            if (rejectSecond && id == 12) return VLC_EGENERIC;
            assignments[id].clear();
            for (size_t i = 0; i < count; ++i) assignments[id].append(QString::fromUtf8(tracks[i]));
            return VLC_SUCCESS;
        };
        auto row = [&](const QUrl& url, const QStringList& before, const QStringList& after) {
            return QVariantMap{{"url", encoded(url)}, {"baseline", before}, {"tracks", after}};
        };
        auto save = [&](const QVariantList& rows) {
            engine.globalObject().setProperty("answer", QJSValue(QJSValue::NullValue));
            m_medialib->saveTrackAssignments(rows, true, engine.evaluate("(function(value) { answer = value; })"));
            if (!QTest::qWaitFor([&] { return !engine.globalObject().property("answer").isNull(); }))
                qFatal("Track assignment save did not finish");
            return engine.globalObject().property("answer");
        };
        const auto selected = QStringList{offline, encoded(subtitle)};
        auto result = save({row(first, {offline}, selected), row(second, {}, {"file:///missing.srt"})});
        QVERIFY(!result.property("success").toBool()); QCOMPARE(writes, 0);
        result = save({row(first, {offline}, {encoded(audio)})});
        QVERIFY(!result.property("success").toBool()); QCOMPARE(writes, 0);
        rejectSecond = true;
        result = save({row(first, {offline}, selected), row(second, {}, {encoded(subtitle)})});
        QVERIFY(!result.property("success").toBool());
        QCOMPARE(result.property("saved").property("length").toInt(), 1);
        QCOMPARE(assignments[11], selected); QVERIFY(assignments[12].isEmpty());
        rejectSecond = false;
        result = save({row(second, {}, {encoded(subtitle), encoded(subtitle)})});
        QVERIFY(result.property("success").toBool());
        QCOMPARE(assignments[12], QStringList{encoded(subtitle)});
        const int previous = writes;
        result = save({row(first, {offline}, {})}); // A stale draft must not remove another edit.
        QVERIFY(!result.property("success").toBool()); QCOMPARE(writes, previous);
        result = save({row(first, selected, {})});
        QVERIFY(result.property("success").toBool()); QVERIFY(assignments[11].isEmpty());
        QCOMPARE(assignments[12], QStringList{encoded(subtitle)});
        auto loadedOnly = row(first, {}, {});
        loadedOnly.insert("initialTracks", QStringList{encoded(subtitle)});
        const int beforeClear = writes;
        result = save({loadedOnly});
        QVERIFY(result.property("success").toBool()); QCOMPARE(writes, beforeClear + 1);
        QCOMPARE(result.property("saved").property("length").toInt(), 1);
        QCOMPARE(result.property("saved").property(0).property("initialTracks").property(0).toString(), encoded(subtitle));
        // A loaded source can be retained even if it is now offline.
        loadedOnly = row(first, {}, {offline, encoded(subtitle)});
        loadedOnly.insert("initialTracks", QStringList{offline});
        result = save({loadedOnly}); QVERIFY(result.property("success").toBool());
        QCOMPARE(assignments[11], QStringList({offline, encoded(subtitle)}));
        QFile disk(subtitle.toLocalFile()); QVERIFY(disk.open(QIODevice::ReadOnly));
        QCOMPARE(disk.readAll(), QByteArray("Keep this file"));
    }

    void testCustomSort_data()
    {
        QTest::addColumn<QString>("criterion");
        QTest::addColumn<Qt::SortOrder>("order");
        QTest::addColumn<QList<int>>("expected");
        QTest::newRow("track-ascending") << QString("track_number") << Qt::AscendingOrder << QList<int>{3,6,9,12,2,5,8,11,1,4,7,10};
        QTest::newRow("track-descending") << QString("track_number") << Qt::DescendingOrder << QList<int>{1,4,7,10,2,5,8,11,3,6,9,12};
        QTest::newRow("disc-ascending") << QString("disc_number") << Qt::AscendingOrder << QList<int>{7,8,9,10,11,12,1,2,3,4,5,6};
        QTest::newRow("disc-descending") << QString("disc_number") << Qt::DescendingOrder << QList<int>{1,2,3,4,5,6,7,8,9,10,11,12};
        QTest::newRow("uri-ascending") << QString("uri") << Qt::AscendingOrder << QList<int>{12,11,10,9,8,7,6,5,4,3,2,1};
        QTest::newRow("uri-descending") << QString("uri") << Qt::DescendingOrder << QList<int>{1,2,3,4,5,6,7,8,9,10,11,12};
        QTest::newRow("unwatched-first") << QString("played") << Qt::AscendingOrder << QList<int>{2,4,6,8,10,12,1,3,5,7,9,11};
        QTest::newRow("watched-first") << QString("played") << Qt::DescendingOrder << QList<int>{1,3,5,7,9,11,2,4,6,8,10,12};
        QTest::newRow("artist-ascending") << QString("main_artist") << Qt::AscendingOrder << QList<int>{2,4,6,8,10,12,1,3,5,7,9,11};
        QTest::newRow("artist-descending") << QString("main_artist") << Qt::DescendingOrder << QList<int>{1,3,5,7,9,11,2,4,6,8,10,12};
        QTest::newRow("album-ascending") << QString("album_title") << Qt::AscendingOrder << QList<int>{7,8,9,10,11,12,1,2,3,4,5,6};
        QTest::newRow("album-descending") << QString("album_title") << Qt::DescendingOrder << QList<int>{1,2,3,4,5,6,7,8,9,10,11,12};
    }

    void testSortKeysUseLoadedMediaMetadata_data()
    {
        QTest::addColumn<bool>("audio");
        QTest::addColumn<int>("track");
        QTest::addColumn<unsigned>("expectedTrack");
        QTest::newRow("audio-track") << true << 10 << 10u;
        QTest::newRow("unknown-track") << true << -1 << 0u;
        QTest::newRow("video-has-no-track-number") << false << 10 << 0u;
    }

    void testSortKeysUseLoadedMediaMetadata()
    {
        QFETCH(bool, audio);
        QFETCH(int, track);
        QFETCH(unsigned, expectedTrack);
        auto files = std::unique_ptr<vlc_ml_file_list_t, decltype(&free)>(
            static_cast<vlc_ml_file_list_t*>(calloc(1, sizeof(vlc_ml_file_list_t) + sizeof(vlc_ml_file_t))), free);
        QVERIFY(files);
        char title[] = "A title";
        char filename[] = "A B.mkv";
        char uri[] = "file://server/Media/A%20B.mkv";
        files->i_nb_items = 1;
        files->p_items[0].i_type = VLC_ML_FILE_TYPE_MAIN;
        files->p_items[0].psz_mrl = uri;
        vlc_ml_media_t media{};
        media.i_id = 42;
        media.psz_title = title;
        media.psz_filename = filename;
        media.p_files = files.get();
        media.i_type = audio ? VLC_ML_MEDIA_TYPE_AUDIO : VLC_ML_MEDIA_TYPE_VIDEO;
        media.i_subtype = audio ? VLC_ML_MEDIA_SUBTYPE_ALBUMTRACK : VLC_ML_MEDIA_SUBTYPE_UNKNOWN;
        media.album_track.i_track_nb = track;
        media.album_track.i_disc_nb = 2;
        media.i_playcount = 3;
        MLMedia item(&media);
        // A null library proves this path uses the metadata already loaded.
        const auto key = m_model->nativeSortKey(item);
        QCOMPARE(key.title, QString(title));
        QCOMPARE(key.uri, QString(uri));
        QCOMPARE(key.track, expectedTrack);
        QCOMPARE(key.disc, audio ? 2u : 0u);
        QVERIFY(key.played);
    }

    void testCustomSort()
    {
        QFETCH(QString, criterion);
        QFETCH(Qt::SortOrder, order);
        QFETCH(QList<int>, expected);
        m_model->setSortCriteria(criterion);
        m_model->setSortOrder(order);
        m_model->appendRange(1, 12);
        m_model->componentComplete();
        QVERIFY(QTest::qWaitFor([this] { return !m_model->loading(); }));
        QCOMPARE(m_model->getCount(), 12u);
        for (int row = 0; row < expected.size(); ++row) {
            const auto value = m_model->data(m_model->index(row), Qt::DisplayRole);
            QCOMPARE(value.value<MLItemId>().id, expected[row]);
        }
        QList<int64_t> selected;
        auto loader = m_model->selectionLoader();
        loader->loadItemsTask(0, {0, 11}, [&selected](quint64, std::vector<std::unique_ptr<MLItem>>& items) {
            for (const auto& item : items) selected.push_back(item->getId().id);
        });
        QVERIFY(QTest::qWaitFor([&selected] { return selected.size() == 2; }));
        QCOMPARE(selected, (QList<int64_t>{expected.first(), expected.last()}));
    }

    void testCustomSortAcrossCachePages()
    {
        m_model->setSortCriteria("track_number");
        m_model->appendRange(1, 300);
        m_model->componentComplete();
        QVERIFY(QTest::qWaitFor([this] { return !m_model->loading(); }));
        for (const auto& pair : QList<QPair<int, int>>{{0,3},{99,300},{100,2},{199,299},{200,1},{299,298}}) {
            QVERIFY(QTest::qWaitFor([&] {
                return m_model->data(m_model->index(pair.first), Qt::DisplayRole).isValid();
            }));
            QCOMPARE(m_model->data(m_model->index(pair.first), Qt::DisplayRole).value<MLItemId>().id, pair.second);
        }
    }

    /**
     * @brief testModelUpdatedFromCallback
     * test for !6537 scenario
     *
     * - Count&Load task issued (task n°1)
     * - model need reset (thumbnail is updated for instance) => `needReload = true`
     * - Count&Load task (n°1) is resolved
     *    * diff util insert/remove rows
     *    - Callback from model change ask to refer an item beyond the cache
     *    - refer issue a new 'fetch more' request (n°2) as m_countTask is 0
     *    - as `needReload await true`, cache is moved to oldCache, new count&load request
     *       is issued (n° 3)
     * - task n°2 resolve, cache is null, assertion fails
     *
     */
    void testModelRecuseUpdateFromSlot()
    {
        connect(m_model.get(), &BaseModel::countChanged, this, [this](){
            m_model->updateItem(13, 37);
            //model will initially only load 100 elements fetching beyond the loaded range
            // will trigger "fetch more" request
            m_model->data(m_model->index(500), Qt::DisplayRole);
        }, Qt::SingleShotConnection);

        m_model->appendRange(1, 1000);
        m_model->componentComplete();
        QTRY_COMPARE_WITH_TIMEOUT(m_model->getCount(), 1000u, 100);
    }


    void testGetIndexFromID()
    {
        const int low = 1;
        const int high = 300;
        m_model->appendRange(low, high);
        m_model->componentComplete();

        // let loading complete, getIndexFromID won't work for 'loading' model
        QVERIFY(QTest::qWaitFor([this] () { return !m_model->loading(); }));

        std::optional<int> row = -1;
        auto cb = [&row](std::optional<int> index)
        {
            row = index;
        };

        // bug of MR6926, here model tries to get index for data outside the initial chunk size
        m_model->getIndexFromId2(MLItemId{high, VLC_ML_PARENT_UNKNOWN}, cb);

        // NOTE: CI fails with QTRY_COMPARE_WITH_TIMEOUT
        QVERIFY(QTest::qWaitFor([&row]() { return row != -1; }));
        QCOMPARE(row, high - 1);

        // above we must have loaded all the data, following this we should not need to wait for results
        m_model->getIndexFromId2(MLItemId{low - 1, VLC_ML_PARENT_UNKNOWN}, cb);
        QCOMPARE(row, std::nullopt);

        m_model->getIndexFromId2(MLItemId{low, VLC_ML_PARENT_UNKNOWN}, cb);
        QCOMPARE(row, low - 1);

        m_model->getIndexFromId2(MLItemId{high + 1, VLC_ML_PARENT_UNKNOWN}, cb);
        QCOMPARE(row, std::nullopt);

        const int mid = (low + high) / 2;
        m_model->getIndexFromId2(MLItemId{mid, VLC_ML_PARENT_UNKNOWN}, cb);
        QCOMPARE(row, mid - 1);
    }

private:
    std::unique_ptr<VLCTestingEnv> m_env;
    std::unique_ptr<MediaLib> m_medialib;
    std::unique_ptr<MLTestModel> m_model;
};


QTEST_GUILESS_MAIN(TestVLCMLModel)
#include "test_ml_model.moc"
