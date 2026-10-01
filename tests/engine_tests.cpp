#include "expression_engine.h"
#include <QtTest>
#include <cmath>

class EngineTests : public QObject
{
    Q_OBJECT
private slots:
    void arithmetic_data()
    {
        QTest::addColumn<QString>("formula");
        QTest::addColumn<double>("expected");
        const QVector<QPair<QString, double>> cases = {
            {QStringLiteral("128 + 256"), 384}, {QStringLiteral("(1299+899)*0.85"), 1868.3},
            {QStringLiteral("(384-128)/2"), 128}, {QStringLiteral("1+2*3"), 7},
            {QStringLiteral("(1+2)*3"), 9}, {QStringLiteral("8/4/2"), 1},
            {QStringLiteral("-2^2"), -4}, {QStringLiteral("(-2)^2"), 4},
            {QStringLiteral("2^-3"), .125}, {QStringLiteral("2^3^2"), 512},
            {QStringLiteral("--2"), 2}, {QStringLiteral("1+-2"), -1},
            {QStringLiteral(".5+1."), 1.5}, {QStringLiteral("1e3 + 2.5e-2"), 1000.025},
            {QStringLiteral("1E+2"), 100}, {QStringLiteral("2 × 3 ÷ 2 − 1"), 2},
            {QStringLiteral("\n 1 +\t2 "), 3}, {QStringLiteral("10%3"), 1},
            {QStringLiteral("0.1+0.2"), .3}, {QStringLiteral("sqrt(9)"), 3},
            {QStringLiteral("abs(-2)"), 2}, {QStringLiteral("sin(pi/2)"), 1},
            {QStringLiteral("cos(0)"), 1}, {QStringLiteral("tan(0)"), 0},
            {QStringLiteral("ln(e)"), 1}, {QStringLiteral("log(1000)"), 3},
            {QStringLiteral("exp(0)"), 1}, {QStringLiteral("floor(1.9)"), 1},
            {QStringLiteral("ceil(1.1)"), 2}, {QStringLiteral("round(-1.5)"), -2},
            {QStringLiteral("min(2,3)"), 2}, {QStringLiteral("max(-2,-3)"), -2},
            {QStringLiteral("pow(2,10)"), 1024}, {QStringLiteral("max(sqrt(9), pow(2,3))"), 8},
            {QStringLiteral("ans * 2"), 84}, {QStringLiteral("SQRT(16)"), 4},
            {QStringLiteral("-0"), 0}
        };
        for (const auto &entry : cases)
            QTest::newRow(entry.first.toUtf8().constData()) << entry.first << entry.second;
    }
    void arithmetic()
    {
        QFETCH(QString, formula);
        QFETCH(double, expected);
        const auto result = ExpressionEngine::evaluate(formula, 42);
        QVERIFY2(result.ok, qPrintable(result.text));
        QVERIFY(std::abs(result.value - expected) <= 1e-12 * qMax(1.0, std::abs(expected)));
    }
    void errors_data()
    {
        QTest::addColumn<QString>("formula");
        const QStringList cases = {QString(), QStringLiteral("1/0"), QStringLiteral("1%0"),
            QStringLiteral("1+"), QStringLiteral("(2+3"), QStringLiteral("2+3)"),
            QStringLiteral("2(3)"), QStringLiteral("1..2"), QStringLiteral("1e"),
            QStringLiteral("nan"), QStringLiteral("inf"), QStringLiteral("sqrt(-1)"),
            QStringLiteral("ln(0)"), QStringLiteral("1e309"), QStringLiteral("1e308*2"),
            QStringLiteral("2^1024"), QStringLiteral("unknown(2)"), QStringLiteral("sqrt(1,2)"),
            QStringLiteral("min(1)"), QStringLiteral("max(1,2,3)"), QStringLiteral("sqrt()"),
            QStringLiteral("<b>1</b>"), QStringLiteral("1,234"), QStringLiteral("１２+１"),
            QString(200, QLatin1Char('(')) + QStringLiteral("1") + QString(200, QLatin1Char(')')),
            QString(5000, QLatin1Char('1'))};
        int index = 0;
        for (const auto &text : cases) QTest::newRow(qPrintable(QString::number(index++))) << text;
    }
    void errors()
    {
        QFETCH(QString, formula);
        const auto result = ExpressionEngine::evaluate(formula);
        QVERIFY(!result.ok);
        QVERIFY(!result.text.isEmpty());
        QVERIFY(result.errorPosition >= 0);
    }
    void displayPrecision()
    {
        QCOMPARE(ExpressionEngine::evaluate(QStringLiteral("0.1+0.2")).text, QStringLiteral("0.3"));
        QCOMPARE(ExpressionEngine::evaluate(QStringLiteral("-0")).text, QStringLiteral("0"));
        QLocale::setDefault(QLocale(QLocale::German));
        QCOMPARE(ExpressionEngine::evaluate(QStringLiteral("1.5+2.5")).text, QStringLiteral("4"));
        QLocale::setDefault(QLocale::c());
    }
};
QTEST_MAIN(EngineTests)
#include "engine_tests.moc"
