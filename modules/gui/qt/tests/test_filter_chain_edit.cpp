// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <QTest>
#include "util/filter_chain_edit.hpp"

class TestFilterChainEdit : public QObject
{
    Q_OBJECT
private slots:
    void preserveOptions_data()
    {
        QTest::addColumn<QString>("chain");
        QTest::addColumn<bool>("enable");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("present");
        QTest::newRow("empty-enable") << "" << true << "subsdelay" << false;
        QTest::newRow("empty-disable") << "" << false << "" << false;
        QTest::newRow("keep-enabled") << "subsdelay{factor=3}" << true << "subsdelay{factor=3}" << true;
        QTest::newRow("disable") << "subsdelay{factor=3}" << false << "" << true;
        QTest::newRow("middle") << "first:subsdelay:last" << false << "first:last" << true;
        QTest::newRow("duplicate") << "subsdelay{factor=3}:other:subsdelay" << false << "other" << true;
        QTest::newRow("exact-name") << "subsdelay2:subsdelay" << false << "subsdelay2" << true;
        QTest::newRow("quoted-name") << "other{text='subsdelay:keep'}" << true << "other{text='subsdelay:keep'}:subsdelay" << false;
        const QString other = "other{file='file://server/a:b',text=\"keep:me\"}";
        QTest::newRow("quoted-colons-before") << other + ":subsdelay{factor=2}" << false << other << true;
        QTest::newRow("quoted-colons-after") << "subsdelay:" + other << false << other << true;
        const QString unicode = QString::fromUtf8("other{text='字幕:值'}");
        QTest::newRow("unicode-options") << unicode + ":subsdelay" << false << unicode << true;
        QTest::newRow("spaces") << " first : subsdelay {factor=2} : last" << false << " first : last" << true;
    }
    void preserveOptions()
    {
        QFETCH(QString, chain); QFETCH(bool, enable); QFETCH(QString, expected); QFETCH(bool, present);
        const auto result = FilterChainEdit::withFilter(chain, QStringLiteral("subsdelay"), enable);
        QCOMPARE(result.first, expected); QCOMPARE(result.second, present);
    }
};
QTEST_APPLESS_MAIN(TestFilterChainEdit)
#include "test_filter_chain_edit.moc"
