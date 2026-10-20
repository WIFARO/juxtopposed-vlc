// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest>
#include <limits>
#include "util/preference_values.hpp"

class TestPreferenceValues : public QObject
{
    Q_OBJECT
private slots:
    void decimalEndpoints()
    {
        for (const QVariant &input : {QVariant(0.1), QVariant(0.9), QVariant(QStringLiteral("0.1"))}) {
            const auto result = vlc::preferences::floatValue(input, 0.1f, 0.9f);
            QVERIFY(result.has_value());
            QCOMPARE(*result, input.toFloat());
        }
    }
    void invalidValues_data()
    {
        QTest::addColumn<QVariant>("input");
        QTest::newRow("below-minimum") << QVariant(0.09);
        QTest::newRow("above-maximum") << QVariant(0.91);
        QTest::newRow("empty") << QVariant(QString());
        QTest::newRow("text") << QVariant(QStringLiteral("invalid"));
        QTest::newRow("nan") << QVariant(std::numeric_limits<double>::quiet_NaN());
        QTest::newRow("infinity") << QVariant(std::numeric_limits<double>::infinity());
        QTest::newRow("negative-infinity") << QVariant(-std::numeric_limits<double>::infinity());
        QTest::newRow("float-overflow") << QVariant(std::numeric_limits<double>::max());
    }
    void invalidValues()
    {
        QFETCH(QVariant, input);
        QVERIFY(!vlc::preferences::floatValue(input, 0.1f, 0.9f));
    }
    void equalBoundsMeanUnbounded()
    {
        for (const double input : {-200., 0., 500.})
            QCOMPARE(vlc::preferences::floatValue(QVariant(input), 0.f, 0.f).value(), float(input));
        QVERIFY(!vlc::preferences::floatValue(QVariant(std::numeric_limits<double>::infinity()), 0.f, 0.f));
    }
};

QTEST_APPLESS_MAIN(TestPreferenceValues)
#include "test_preference_values.moc"
