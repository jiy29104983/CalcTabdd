#include "expression_engine.h"
#include "calculation_history.h"
#include "calculation_catalog.h"
#include <QSet>
#include <QtTest>
#include <cmath>

class EngineTests : public QObject
{
    Q_OBJECT
private slots:
    void historyStartsEmpty()
    {
        const CalculationHistory history;
        QCOMPARE(history.count(), 0);
        QVERIFY(history.records().isEmpty());
        QCOMPARE(history.answer(), 0.0);
        QVERIFY(!history.record(0));
        QVERIFY(!history.record(1));
    }
    void historyPreservesSuccessFailureAndAnswer()
    {
        CalculationHistory history;
        const auto first = history.calculate(QStringLiteral("6*7"));
        const auto error = history.calculate(QStringLiteral("12/(3-3)"));
        const auto next = history.calculate(QStringLiteral("ans+1"));
        QCOMPARE(history.count(), 3);
        QCOMPARE(first.id, quint64(1));
        QCOMPARE(first.expression, QStringLiteral("6*7"));
        QVERIFY(first.result.ok);
        QCOMPARE(first.result.value, 42.0);
        QCOMPARE(first.result.text, QStringLiteral("42"));
        QCOMPARE(first.result.errorPosition, -1);
        QCOMPARE(first.answerBefore, 0.0);
        QCOMPARE(error.id, quint64(2));
        QCOMPARE(error.expression, QStringLiteral("12/(3-3)"));
        QVERIFY(!error.result.ok);
        QCOMPARE(error.result.text, QStringLiteral("除数不能为 0"));
        QCOMPARE(error.result.errorPosition, 8);
        QCOMPARE(error.answerBefore, 42.0);
        QCOMPARE(next.id, quint64(3));
        QCOMPARE(next.answerBefore, 42.0);
        QVERIFY(next.result.ok);
        QCOMPARE(next.result.value, 43.0);
        QCOMPARE(history.answer(), 43.0);
        for (int index = 0; index < history.count(); ++index)
        {
            const auto &entry = history.records().at(index);
            QCOMPARE(entry.id, quint64(index + 1));
            const auto *byId = history.record(entry.id);
            QVERIFY(byId);
            QCOMPARE(byId->expression, entry.expression);
            QCOMPARE(byId->result.ok, entry.result.ok);
            QCOMPARE(byId->result.text, entry.result.text);
            QCOMPARE(byId->result.errorPosition, entry.result.errorPosition);
            QCOMPARE(byId->answerBefore, entry.answerBefore);
        }
        QVERIFY(!history.record(0));
        QVERIFY(!history.record(4));
        QVERIFY(!history.record(quint64(-1)));
    }
    void historyKeepsUnroundedValues()
    {
        CalculationHistory history;
        const auto first = history.calculate(QStringLiteral("0.1+0.2"));
        QVERIFY(first.result.ok);
        QCOMPARE(first.result.text, QStringLiteral("0.3"));
        // 精确比较，避免模糊浮点断言掩盖错误的“显示文本转回 double”。
        QVERIFY(first.result.value == 0.1 + 0.2);
        QVERIFY(first.result.value != first.result.text.toDouble());
        history.calculate(QStringLiteral("1/0"));
        const auto next = history.calculate(QStringLiteral("ans-0.3"));
        QVERIFY(next.answerBefore == first.result.value);
        QVERIFY(next.result.value == (0.1 + 0.2) - 0.3);
        QVERIFY(next.result.value > 0);
        QVERIFY(history.answer() == next.result.value);
        QVERIFY(history.record(first.id)->result.value == first.result.value);
    }
    void historyPreservesSubmittedFormulaAndErrors()
    {
        CalculationHistory history;
        const QString expression = QStringLiteral("  2 × 3 ÷ 2 − 1\n");
        const auto first = history.calculate(expression);
        QCOMPARE(first.expression, expression);
        QCOMPARE(first.result.value, 2.0);
        for (const auto &formula : {QStringLiteral("1+"), QStringLiteral("sqrt(-1)"),
                                   QStringLiteral("1+中"), QStringLiteral("1e309")})
        {
            const auto entry = history.calculate(formula);
            const auto expected = ExpressionEngine::evaluate(formula, 2);
            QVERIFY(!entry.result.ok);
            QCOMPARE(entry.expression, formula);
            QCOMPARE(entry.result.text, expected.text);
            QCOMPARE(entry.result.errorPosition, expected.errorPosition);
            QCOMPARE(entry.answerBefore, 2.0);
            QCOMPARE(history.answer(), 2.0);
        }
    }
    void historyIdsAndSnapshotsSurviveGrowth()
    {
        CalculationHistory history;
        const auto first = history.calculate(QStringLiteral("ans+1"));
        const auto failed = history.calculate(QStringLiteral("1+"));
        const auto snapshot = history.records();
        for (int index = 0; index < 512; ++index)
        {
            const auto entry = history.calculate(QStringLiteral("ans+1"));
            QCOMPARE(entry.id, quint64(index + 3));
            QCOMPARE(entry.answerBefore, double(index + 1));
            QCOMPARE(entry.result.value, double(index + 2));
        }
        QCOMPARE(history.count(), 514);
        QCOMPARE(snapshot.size(), 2);
        QCOMPARE(first.result.value, 1.0);
        QCOMPARE(history.record(first.id)->result.value, 1.0);
        QCOMPARE(history.record(failed.id)->result.text, failed.result.text);
        QCOMPARE(history.record(failed.id)->answerBefore, 1.0);
        for (int index = 0; index < history.count(); ++index)
            QCOMPARE(history.records().at(index).id, quint64(index + 1));
    }
    void historiesAreIndependent()
    {
        CalculationHistory second;
        {
            CalculationHistory first;
            first.calculate(QStringLiteral("123"));
            first.calculate(QStringLiteral("1/0"));
            const auto entry = second.calculate(QStringLiteral("ans+1"));
            QCOMPARE(entry.id, quint64(1));
            QCOMPARE(entry.answerBefore, 0.0);
            QCOMPARE(entry.result.value, 1.0);
            QCOMPARE(first.count(), 2);
            QCOMPARE(first.answer(), 123.0);
        }
        QCOMPARE(second.count(), 1);
        QCOMPARE(second.answer(), 1.0);
        CalculationHistory reopened;
        const auto entry = reopened.calculate(QStringLiteral("ans"));
        QCOMPARE(entry.id, quint64(1));
        QCOMPARE(entry.result.value, 0.0);
        QCOMPARE(second.calculate(QStringLiteral("ans+1")).result.value, 2.0);
    }
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
    void advertisedCapabilitiesAreExecutable()
    {
        QSet<QString> functions, constants;
        for (const auto &entry : CalculationCatalog::entries())
        {
            if (!entry.isCompletion()) continue;
            QString formula = entry.name;
            if (entry.kind == CalculationCatalog::Kind::Function)
            {
                QVERIFY(!functions.contains(entry.name));
                functions.insert(entry.name);
                formula += entry.insertion.contains(QLatin1Char(',')) ? QStringLiteral("(2,3)") : QStringLiteral("(1)");
            }
            else constants.insert(entry.name);
            const auto result = ExpressionEngine::evaluate(formula, 42);
            QVERIFY2(result.ok, qPrintable(formula + QStringLiteral(": ") + result.text));
        }
        QCOMPARE(functions.size(), 14);
        QCOMPARE(constants, QSet<QString>({QStringLiteral("pi"), QStringLiteral("e"), QStringLiteral("ans")}));
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
