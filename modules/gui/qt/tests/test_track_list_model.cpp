// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QAbstractItemModelTester>
#include <QUrl>
#include "player/input_models.hpp"
#include "util/vlctick.hpp"

struct TestTrackId { QByteArray uri; int references = 0; };
static int references;
static vlc_es_id_t* selected;
static TestTrackId& trackId(vlc_es_id_t* id) { return *reinterpret_cast<TestTrackId*>(id); }
extern "C" {
vlc_es_id_t* vlc_es_id_Hold(vlc_es_id_t* id) { ++trackId(id).references; ++references; return id; }
void vlc_es_id_Release(vlc_es_id_t* id) { --trackId(id).references; --references; }
const char* vlc_es_id_GetSourceUri(vlc_es_id_t* id) { return trackId(id).uri.isEmpty() ? nullptr : trackId(id).uri.constData(); }
enum es_format_category_e vlc_es_id_GetCat(vlc_es_id_t*) { return SPU_ES; }
void vlc_player_Lock(vlc_player_t*) {}
void vlc_player_Unlock(vlc_player_t*) {}
unsigned vlc_player_SelectEsId(vlc_player_t*, vlc_es_id_t* id, enum vlc_player_select_policy) { selected = id; return 1; }
void vlc_player_UnselectEsId(vlc_player_t*, vlc_es_id_t* id) { if (selected == id) selected = nullptr; }
}

class TestTrackListModel : public QObject {
    Q_OBJECT
    static vlc_player_track track(TestTrackId& id, const char* title, bool checked = false) {
        vlc_player_track value{};
        value.es_id = reinterpret_cast<vlc_es_id_t*>(&id); value.name = title; value.selected = checked;
        value.fmt.i_cat = SPU_ES;
        return value;
    }
private slots:
    void init() { references = 0; selected = nullptr; }
    void cleanup() { QCOMPARE(references, 0); }
    void chapterOffsets_data() {
        QTest::addColumn<qint64>("offset");
        QTest::newRow("beginning") << qint64(0);
        QTest::newRow("subsecond") << qint64(999999);
        QTest::newRow("one-second") << qint64(1000000);
        QTest::newRow("six-seconds") << qint64(6000000);
        QTest::newRow("over-one-hour") << qint64(3601250000);
    }
    void chapterOffsets() {
        QFETCH(qint64, offset);
        vlc_player_chapter chapter{};
        chapter.name = "Chapter";
        chapter.time = offset;
        vlc_player_title title{};
        title.length = VLC_TICK_FROM_SEC(7200);
        title.chapter_count = 1;
        title.chapters = &chapter;
        ChapterListModel model(nullptr);
        model.resetTitle(&title);
        const auto index = model.index(0);
        const auto value = model.data(index, ChapterListModel::TimeRole);
        QVERIFY(value.canConvert<VLCTime>());
        const auto time = value.value<VLCTime>();
        QVERIFY(time.valid());
        QCOMPARE(time.toSeconds(), int(offset / 1000000));
        QCOMPARE(time.toMilliseconds(), int(offset / 1000));
        QCOMPARE(time.toVLCTick(), VLC_TICK_0 + offset);
        QCOMPARE(model.data(index, ChapterListModel::StartPositionRole).toFloat(),
                 float(offset) / float(title.length));
    }
    void sourcesAndSelection() {
        TestTrackId embedded{}, external{"file:///C:/Movies/caps%20%26%20captions.srt"};
        TrackListModel model(nullptr);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto first = track(embedded, "English", true), second = track(external, "Track 2");
        model.updateTracks(VLC_PLAYER_LIST_ADDED, &first);
        model.updateTracks(VLC_PLAYER_LIST_ADDED, &second);
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(!model.data(model.index(0), TrackListModel::ExternalRole).toBool());
        QVERIFY(model.data(model.index(1), TrackListModel::ExternalRole).toBool());
        QCOMPARE(model.data(model.index(1), TrackListModel::SourceNameRole).toString(), "caps & captions.srt");
        QCOMPARE(model.data(model.index(1), TrackListModel::SourceUriRole).toUrl(), QUrl(QString::fromUtf8(external.uri)));
        QVERIFY(model.setData(model.index(1), true, Qt::CheckStateRole));
        QCOMPARE(selected, second.es_id);
        model.updateTrackSelection(first.es_id, false); model.updateTrackSelection(second.es_id, true);
        QVERIFY(!model.data(model.index(0), Qt::CheckStateRole).toBool());
        QVERIFY(model.data(model.index(1), Qt::CheckStateRole).toBool());
        model.clear(); model.clear(); QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.data(QModelIndex()).isValid());
        QVERIFY(!model.setData(QModelIndex(), true, Qt::CheckStateRole));
    }
    void sharedSourcesKeepDistinctLabels() {
        TestTrackId one{"file:///audio/alternate.mka"}, two{"file:///audio/alternate.mka"};
        TrackListModel model(nullptr);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto first = track(one, "English"), second = track(two, "Japanese");
        model.updateTracks(VLC_PLAYER_LIST_ADDED, &first);
        QCOMPARE(model.data(model.index(0), TrackListModel::SourceNameRole).toString(), "alternate.mka");
        QSignalSpy updates(&model, &QAbstractItemModel::dataChanged);
        model.updateTracks(VLC_PLAYER_LIST_ADDED, &second);
        QVERIFY(!updates.isEmpty());
        QCOMPARE(model.data(model.index(0), TrackListModel::SourceNameRole).toString(), "alternate.mka (English)");
        QCOMPARE(model.data(model.index(1), TrackListModel::SourceNameRole).toString(), "alternate.mka (Japanese)");
        second.name = "French"; model.updateTracks(VLC_PLAYER_LIST_UPDATED, &second);
        QCOMPARE(model.data(model.index(1), TrackListModel::SourceNameRole).toString(), "alternate.mka (French)");
        model.updateTracks(VLC_PLAYER_LIST_REMOVED, &second);
        QCOMPARE(model.data(model.index(0), TrackListModel::SourceNameRole).toString(), "alternate.mka");
    }
    void remoteSourcesHideCredentials() {
        TestTrackId id{"https://account:secret@example.test/subtitles/captions%20en.srt"};
        TrackListModel model(nullptr); auto value = track(id, "English");
        model.updateTracks(VLC_PLAYER_LIST_ADDED, &value);
        QCOMPARE(model.data(model.index(0), TrackListModel::SourceNameRole).toString(), "captions en.srt");
        const auto uri = model.data(model.index(0), TrackListModel::SourceUriRole).toUrl();
        QVERIFY(uri.userInfo().isEmpty()); QCOMPARE(uri.host(), "example.test");
    }
    void conciseLabels_data() {
        QTest::addColumn<QByteArray>("language");QTest::addColumn<QByteArray>("description");
        QTest::addColumn<bool>("forced");QTest::addColumn<QString>("expected");
        QTest::newRow("same-language") << QByteArray("eng") << QByteArray("English") << false << QString("English");
        QTest::newRow("japanese") << QByteArray("jpn") << QByteArray("Japanese") << false << QString("Japanese");
        QTest::newRow("language-only") << QByteArray("deu") << QByteArray() << false << QString("German");
        QTest::newRow("commentary") << QByteArray("en") << QByteArray("Director commentary") << false << QString::fromUtf8("English — Director commentary");
        QTest::newRow("descriptive-language") << QByteArray("en") << QByteArray("English commentary") << false << QString("English commentary");
        QTest::newRow("sdh") << QByteArray("en") << QByteArray("English (SDH)") << false << QString("English (SDH)");
        QTest::newRow("forced") << QByteArray("en") << QByteArray("English") << true << QString("English (Forced)");
        QTest::newRow("named-forced") << QByteArray("en") << QByteArray("English (Forced)") << true << QString("English (Forced)");
        QTest::newRow("no-language") << QByteArray() << QByteArray("Custom track") << false << QString("Custom track");
        QTest::newRow("unknown-language") << QByteArray("xx") << QByteArray("Custom track") << false << QString("Custom track");
        QTest::newRow("unnamed") << QByteArray() << QByteArray() << false << QString("Track 1 - [English]");
    }
    void conciseLabels() {
        QFETCH(QByteArray,language);QFETCH(QByteArray,description);QFETCH(bool,forced);QFETCH(QString,expected);
        TestTrackId id{};TrackListModel model(nullptr);auto value=track(id,"Track 1 - [English]");
        value.fmt.psz_language=language.isEmpty()?nullptr:language.data();
        value.fmt.psz_description=description.isEmpty()?nullptr:description.data();value.fmt.subs.b_forced=forced;
        model.updateTracks(VLC_PLAYER_LIST_ADDED,&value);
        QCOMPARE(model.data(model.index(0),TrackListModel::LabelRole).toString(),expected);
        QCOMPARE(model.data(model.index(0),Qt::DisplayRole).toString(),QString("Track 1 - [English]"));
    }
    void duplicateLabelsStayDistinctAfterUpdates() {
        TestTrackId one{},two{};TrackListModel model(nullptr);
        auto first=track(one,"Track 1 - [English]"),second=track(two,"Track 2 - [English]");
        first.fmt.psz_language=second.fmt.psz_language=const_cast<char*>("eng");
        model.updateTracks(VLC_PLAYER_LIST_ADDED,&first);model.updateTracks(VLC_PLAYER_LIST_ADDED,&second);
        QCOMPARE(model.data(model.index(0),TrackListModel::LabelRole).toString(),"English (1)");
        QCOMPARE(model.data(model.index(1),TrackListModel::LabelRole).toString(),"English (2)");
        QSignalSpy changed(&model,&QAbstractItemModel::dataChanged);
        second.fmt.psz_description=const_cast<char*>("Commentary");model.updateTracks(VLC_PLAYER_LIST_UPDATED,&second);
        QCOMPARE(model.data(model.index(0),TrackListModel::LabelRole).toString(),"English");
        QCOMPARE(model.data(model.index(1),TrackListModel::LabelRole).toString(),QString::fromUtf8("English — Commentary"));
        QVERIFY(std::any_of(changed.begin(),changed.end(),[](const auto& args){return args[2].template value<QList<int>>().contains(TrackListModel::LabelRole);}));
    }
    void externalFileDoesNotNumberTheEmbeddedLanguage() {
        TestTrackId one{},two{"file:///subtitles/english.srt"};TrackListModel model(nullptr);
        auto embedded=track(one,"English - [English]"),external=track(two,"Track 2 - [English]");
        embedded.fmt.psz_language=external.fmt.psz_language=const_cast<char*>("eng");
        model.updateTracks(VLC_PLAYER_LIST_ADDED,&embedded);model.updateTracks(VLC_PLAYER_LIST_ADDED,&external);
        QCOMPARE(model.data(model.index(0),TrackListModel::LabelRole).toString(),"English");
        QCOMPARE(model.data(model.index(1),TrackListModel::SourceNameRole).toString(),"english.srt");
    }
};
QTEST_GUILESS_MAIN(TestTrackListModel)
#include "test_track_list_model.moc"
