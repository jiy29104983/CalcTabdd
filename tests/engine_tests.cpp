#include "expression_engine.h"
#include "decimal_value.h"
#include "custom_formula.h"
#include "calculation_history.h"
#include "calculation_export.h"
#include "calculation_session.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <cstdio>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include "calculation_catalog.h"
#include <QSet>
#include <QtTest>
#include <cmath>
#include <cfenv>
#include <cerrno>
#include <QMap>
#include <cstring>
#include <limits>
#include <random>

namespace {
quint64 bits(double value)
{
    quint64 result;
    static_assert(sizeof(result) == sizeof(value), "Expected 64-bit double");
    std::memcpy(&result, &value, sizeof(value));
    return result;
}

struct RestoreLocale
{
    QLocale previous;
    ~RestoreLocale() { QLocale::setDefault(previous); }
};
}

class EngineTests : public QObject
{
    Q_OBJECT
private slots:
    void numericReferences_data()
    {
        QTest::addColumn<QByteArray>("data");
        QFile file(QFINDTESTDATA("data/numeric_vectors.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(document.isArray());
        QVERIFY(document.array().size() > 800);
        for (const auto &entry : document.array())
            QTest::newRow(qPrintable(entry.toObject().value(QStringLiteral("name")).toString()))
                << QJsonDocument(entry.toObject()).toJson(QJsonDocument::Compact);
    }
    void numericReferences()
    {
        QFETCH(QByteArray, data);
        const auto row = QJsonDocument::fromJson(data).object();
        const QString operation = row.value(QStringLiteral("operation")).toString();
        const QString left = row.value(QStringLiteral("left")).toString();
        const QString right = row.value(QStringLiteral("right")).toString();
        const QString error = row.value(QStringLiteral("error")).toString();
        RestoreLocale locale;
        QLocale::setDefault(QLocale(QLocale::German));
        DecimalValue a, b, sentinel;
        QCOMPARE(DecimalValue::parse(left, a), DecimalError::None);
        QCOMPARE(DecimalValue::parse(right, b), DecimalError::None);
        QCOMPARE(DecimalValue::parse(QStringLiteral("-42.5"), sentinel), DecimalError::None);
        const auto equal = [](const DecimalValue &x, const DecimalValue &y) {
            return x.coefficient() == y.coefficient() && x.exponent() == y.exponent() && x.isNegative() == y.isNegative();
        };
        if (operation == QStringLiteral("convert"))
        {
            const NumericError expectedError = error == QStringLiteral("conversion-overflow") ? NumericError::ConversionOverflow
                : error == QStringLiteral("conversion-underflow") ? NumericError::ConversionUnderflow : NumericError::None;
            for (bool initial : {false, true})
            {
                double result = -42.5;
                bool changed = initial;
                const auto status = NumericValue(a).toBinary(result, changed);
                QCOMPARE(status.error, expectedError);
                if (!status.ok()) { QCOMPARE(result, -42.5); QCOMPARE(changed, initial); }
                else
                {
                    bool ok = false;
                    const quint64 expected = row.value(QStringLiteral("bits")).toString().toULongLong(&ok, 16);
                    QVERIFY(ok);
                    QCOMPARE(bits(result), expected);
                    QCOMPARE(changed, row.value(QStringLiteral("changed")).toBool());
                }
            }
            return;
        }
        if (operation == QStringLiteral("compare"))
        {
            const int expected = row.value(QStringLiteral("comparison")).toInt();
            QCOMPARE(DecimalValue::compare(a, b), expected);
            QCOMPARE(DecimalValue::compare(b, a), -expected);
            return;
        }
        const QMap<QString, DecimalError> errors = {{QStringLiteral("none"), DecimalError::None},
            {QStringLiteral("overflow"), DecimalError::Overflow}, {QStringLiteral("underflow"), DecimalError::Underflow},
            {QStringLiteral("precision"), DecimalError::PrecisionLimit}, {QStringLiteral("domain"), DecimalError::Domain},
            {QStringLiteral("division-by-zero"), DecimalError::DivisionByZero}, {QStringLiteral("resource"), DecimalError::ResourceLimit}};
        QVERIFY(errors.contains(error));
        // 分别验证独立输出、左别名、右别名，以及布尔输出的两种初始值。
        for (int alias = 0; alias < 3; ++alias)
            for (bool initial : {false, true})
            {
                DecimalValue x = a, y = b, separate = sentinel;
                DecimalValue &output = alias == 0 ? separate : alias == 1 ? x : y;
                const DecimalValue before = output;
                bool flag = initial;
                DecimalError actual;
                if (operation == QStringLiteral("remainder")) actual = DecimalValue::remainder(x, y, output);
                else if (operation == QStringLiteral("power")) actual = DecimalValue::integerPower(x, y, output, flag);
                else if (operation == QStringLiteral("sqrt")) actual = DecimalValue::squareRootExact(x, output, flag);
                else actual = DecimalValue::integral(x, operation == QStringLiteral("floor") ? DecimalValue::IntegralRounding::Floor
                    : operation == QStringLiteral("ceil") ? DecimalValue::IntegralRounding::Ceiling : DecimalValue::IntegralRounding::HalfAwayFromZero, output);
                QCOMPARE(actual, errors.value(error));
                if (actual != DecimalError::None) { QVERIFY(equal(output, before)); QCOMPARE(flag, initial); continue; }
                if (operation == QStringLiteral("sqrt"))
                {
                    QCOMPARE(flag, row.value(QStringLiteral("exact")).toBool());
                    if (!flag) { QVERIFY(equal(output, before)); continue; }
                }
                if (operation == QStringLiteral("power")) QCOMPARE(flag, row.value(QStringLiteral("inexact")).toBool());
                QCOMPARE(output.coefficient(), row.value(QStringLiteral("coefficient")).toString().toLatin1());
                QCOMPARE(output.exponent(), row.value(QStringLiteral("exponent")).toInt());
                QCOMPARE(output.isNegative(), row.value(QStringLiteral("negative")).toBool());
            }
    }
    void numericExpressions_data()
    {
        QTest::addColumn<QString>("formula");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("binary");
        QTest::addColumn<uint>("sources");
        const auto row = [](const char *formula, const QString &value, bool binary = false, unsigned sources = 0) {
            QTest::newRow(formula) << QString::fromLatin1(formula) << value << binary << sources;
        };
        const unsigned R = NumericValue::Rounded, A = NumericValue::Approximate, C = NumericValue::ConversionLoss;
        row("0.1+0.2", "0.3"); row("(0.1+0.2)-0.3", "0"); row("0.1*0.2", "0.02");
        row("19.9*3", "59.7"); row("0.3%0.1", "0"); row("-5.5%2", "-1.5"); row("5.5%-2", "1.5");
        row("1e999%3", "1"); row("abs(-0.3)", "0.3"); row("floor(-1.2)", "-2"); row("ceil(-1.2)", "-1");
        row("round(2.5)", "3"); row("round(-2.5)", "-3"); row("round(1.005*100)/100", "1.01");
        row("min(9007199254740993,9007199254740992)", "9007199254740992");
        row("max(9007199254740993,9007199254740992)", "9007199254740993");
        row("0.1^2", "0.01"); row("2^-3", "0.125"); row("pow(2,-3)", "0.125");
        row("3^-1", QStringLiteral("0.") + QString(50, '3'), false, R);
        row("2^3^2", "512"); row("-2^2", "-4"); row("(-2)^2", "4"); row("0^0", "1");
        row("pow(0,0)", "1"); row("1^10000", "1"); row("(-1)^9999", "-1");
        row("1e999^1", "1e999"); row("1e-999^1", "1e-999"); row("1e999^0", "1");
        row("sqrt(0.09)", "0.3"); row("sqrt(1e400)", "1e200"); row("sqrt(1e-998)", "1e-499");
        row("sqrt(0.04)", "0.2"); row("MAX(sqrt(9),pow(2,3))", "8");
        row("sqrt(2)", "1.4142135623730951", true, A); row("4^0.5", "2", true, A);
        row("sin(0)", "0", true, A); row("cos(0)", "1", true, A); row("tan(0)", "0", true, A);
        row("ln(1)", "0", true, A); row("log(1000)", "3", true, A); row("exp(0)", "1", true, A);
        row("pi", "3.141592653589793", true, A); row("e", "2.718281828459045", true, A);
        row("sin(pi)", "1.2246467991473532e-16", true, A); row("sin(pi/2)", "1", true, A);
        row("cos(pi)", "-1", true, A); row("tan(1)", "1.5574077246549023", true, A);
        row("ln(e)", "1", true, A); row("exp(1)", "2.718281828459045", true, A);
        row("abs(-pi)", "3.141592653589793", true, A); row("floor(pi)", "3", true, A);
        row("ceil(pi)", "4", true, A); row("round(pi)", "3", true, A);
        row("sqrt(0.02)", "0.1414213562373095", true, A | C);
        row("0.5+sin(0)", "0.5", true, A); row("0.1+sin(0)", "0.1", true, A | C);
        row("9007199254740993+sin(0)", "9007199254740992", true, A | C);
        row("floor(1/3)", "0", false, R); row("sqrt(floor(1/3))", "0", false, R);
        row("max(1,1/3)", "1", false, R); row("min(1,pi)", "1", true, A);
        row("max(4,pi)", "4", true, A); row("pi-pi", "0", true, A); row("sin(0)*0", "0", true, A);
        row("sin(1/3)", "0.3271946967961522", true, R | A | C);
        row("(1/3)-(1/3)", "0", false, R); row("(1/3)*3", QStringLiteral("0.") + QString(50, '9'), false, R);
        row("(0.1+0.2)-0.3+sin(0)", "0", true, A);
        row("(1e-300+sin(0))*1e-20", "1e-320", true, A | C);
        row("pow(-8,cos(0))", "-8", true, A); row("(5.5+sin(0))%2", "1.5", true, A);
        row("min(0.1,sin(0))", "0", true, A | C); row("(1/3)^0", "1", false, R);
    }
    void numericExpressions()
    {
        QFETCH(QString, formula);
        QFETCH(QString, expected);
        QFETCH(bool, binary);
        QFETCH(uint, sources);
        const auto result = ExpressionEngine::evaluateNumeric(formula);
        QVERIFY2(result.ok, qPrintable(result.text));
        QVERIFY(result.text.isEmpty()); // 尚未定义用户显示格式。
        QCOMPARE(result.value.isBinary(), binary);
        QCOMPARE(result.value.sources(), sources);
        if (binary)
        {
            const double value = expected.toDouble();
            if (value == 0 || std::abs(value) < 1e-300) QCOMPARE(result.value.binary(), value);
            else QVERIFY(std::abs(result.value.binary() - value) <= std::abs(value) * 2e-14);
        }
        else
        {
            DecimalValue value;
            QCOMPARE(DecimalValue::parse(expected, value), DecimalError::None);
            QCOMPARE(DecimalValue::compare(result.value.decimal(), value), 0);
        }
    }
    void numericErrors_data()
    {
        QTest::addColumn<QString>("formula");
        QTest::addColumn<int>("error");
        QTest::addColumn<int>("position");
        QTest::addColumn<int>("length");
        const auto row = [](const char *input, CalculationError error, const char *target) {
            const QString formula = QString::fromLatin1(input), text = QString::fromLatin1(target);
            QTest::newRow(input) << formula << int(error) << formula.lastIndexOf(text) << text.size();
        };
        row("2^167", CalculationError::Limit, "^"); row("2^-200", CalculationError::Limit, "^");
        row("1^10001", CalculationError::Limit, "10001"); row("(-1)^10001", CalculationError::Limit, "10001");
        row("0^10001", CalculationError::Limit, "10001"); row("10^-1000", CalculationError::Overflow, "^");
        row("1%0", CalculationError::DivisionByZero, "0"); row("1/(-0)", CalculationError::DivisionByZero, "(-0)");
        row("0^-1", CalculationError::Domain, "-1"); row("(-8)^(1/3)", CalculationError::Domain, "(1/3)");
        row("pow(-8,1/3)", CalculationError::Domain, "1/3"); row("sqrt(-1)", CalculationError::Domain, "-1");
        row("ln(-1e400)", CalculationError::Domain, "-1e400"); row("log(0)", CalculationError::Domain, "0");
        row("ln(1e400)", CalculationError::Overflow, "1e400"); row("sin(1e-400)", CalculationError::Underflow, "1e-400");
        row("exp(-1000)", CalculationError::Underflow, "exp"); row("exp(1000)", CalculationError::Overflow, "exp");
        row("(1/6)*6", CalculationError::Limit, "*"); QTest::newRow("decimal-node-before-scientific") << QStringLiteral("(1e50+1)-1e50+sin(0)")
            << int(CalculationError::Limit) << 5 << 1;
        row("1e400+sin(0)", CalculationError::Overflow, "1e400");
        row("max(1,1e400+sin(0))", CalculationError::Overflow, "1e400");
        row("(1e-200+sin(0))*1e-200", CalculationError::Underflow, "*");
        row("(1e-200+sin(0))/1e200", CalculationError::Underflow, "/");
        row("pow(1e-200+sin(0),2)", CalculationError::Underflow, "pow");
        row("1e1000", CalculationError::Overflow, "1e1000"); row("1e-1000", CalculationError::Underflow, "1e-1000");
        row("1.0000000000000000000000000000000000000000000000001e-999%1e-999", CalculationError::Underflow, "%");
        row("min(1e999,pi)", CalculationError::Overflow, "1e999");
        row("min(pi,1e999)", CalculationError::Overflow, "1e999");
        row("sin(0)+1e400", CalculationError::Overflow, "1e400");
        row("pow(-2,1.00000000000000000001)", CalculationError::Domain, "1.00000000000000000001");
        row("pow(-2+sin(0),1.00000000000000000001)", CalculationError::Domain, "1.00000000000000000001");
        row("sqrt(-1e-400)", CalculationError::Domain, "-1e-400");
        row("pow(sin(0),-1)", CalculationError::Domain, "-1");
        row("1^(-10001)", CalculationError::Limit, "(-10001)");
    }
    void numericErrors()
    {
        QFETCH(QString, formula);
        QFETCH(int, error);
        QFETCH(int, position);
        QFETCH(int, length);
        const auto answer = ExpressionEngine::evaluateNumeric(QStringLiteral("sin(1/3)"));
        QVERIFY(answer.ok);
        const auto result = ExpressionEngine::evaluateNumeric(formula, answer.value);
        QVERIFY(!result.ok);
        QCOMPARE(int(result.error), error);
        QCOMPARE(result.errorPosition, position);
        QCOMPARE(result.errorLength, length);
        QCOMPARE(answer.value.sources(), unsigned(NumericValue::Rounded | NumericValue::Approximate | NumericValue::ConversionLoss));
        const auto reuse = ExpressionEngine::evaluateNumeric(QStringLiteral("ans"), answer.value);
        QVERIFY(reuse.ok);
        QCOMPARE(bits(reuse.value.binary()), bits(answer.value.binary()));
        QCOMPARE(reuse.value.sources(), answer.value.sources());
    }
    void numericContextAndCustom()
    {
        NumericValue answer;
        auto first = ExpressionEngine::evaluateNumeric(QStringLiteral("0.1+0.2"), answer);
        QVERIFY(first.ok);
        answer = first.value;
        const auto zero = ExpressionEngine::evaluateNumeric(QStringLiteral("ans-0.3"), answer);
        QVERIFY(zero.ok && zero.value.isZero() && zero.value.sources() == 0);
        QVERIFY(ExpressionEngine::evaluateNumeric(QStringLiteral("ans")).value.isZero());
        for (const auto &entry : QVector<QPair<QString,QString>>{
             {QStringLiteral("A=x+y"), QStringLiteral("x=0.1\ny=0.2")},
             {QStringLiteral("A=sqrt(x)"), QStringLiteral("x=1e400")},
             {QStringLiteral("A=sin(x)+ans"), QStringLiteral("x=0.1")},
             {QStringLiteral("A=pow(x,y)"), QStringLiteral("x=2\ny=-3")},
             {QStringLiteral("A=ln(x)"), QStringLiteral("x=1e400")},
             {QStringLiteral("A=x^y"), QStringLiteral("x=2\ny=-200")}})
        {
            CustomFormula custom;
            QVERIFY(CustomFormula::parse(entry.first, custom).isEmpty());
            QString expression, parameterError;
            QVERIFY(custom.substituteNumeric(entry.second, expression).isEmpty());
            const auto ordinary = ExpressionEngine::evaluateNumeric(expression, answer);
            const auto calculated = custom.evaluateNumeric(entry.second, answer, parameterError);
            QVERIFY(parameterError.isEmpty());
            QCOMPARE(calculated.ok, ordinary.ok);
            QCOMPARE(calculated.error, ordinary.error);
            QCOMPARE(calculated.errorPosition, ordinary.errorPosition);
            QCOMPARE(calculated.errorLength, ordinary.errorLength);
            if (!calculated.ok) continue;
            QCOMPARE(calculated.value.isBinary(), ordinary.value.isBinary());
            QCOMPARE(calculated.value.sources(), ordinary.value.sources());
            if (ordinary.value.isBinary()) QCOMPARE(bits(calculated.value.binary()), bits(ordinary.value.binary()));
            else QCOMPARE(DecimalValue::compare(calculated.value.decimal(), ordinary.value.decimal()), 0);
        }
        CustomFormula custom;
        QVERIFY(CustomFormula::parse(QStringLiteral("A=sqrt(x)"), custom).isEmpty());
        QString expression = QStringLiteral("sentinel");
        QVERIFY(!custom.substitute(QStringLiteral("x=1e400"), expression).isEmpty());
        QCOMPARE(expression, QStringLiteral("sentinel"));
        QVERIFY(!custom.substituteNumeric(QStringLiteral("x=1e1000"), expression).isEmpty());
        QCOMPARE(expression, QStringLiteral("sentinel"));
        const auto approximate = ExpressionEngine::evaluateNumeric(QStringLiteral("sin(1/3)"));
        QVERIFY(approximate.ok);
        const auto copied = approximate.value;
        const auto canceled = ExpressionEngine::evaluateNumeric(QStringLiteral("ans-ans"), copied);
        QVERIFY(canceled.ok && canceled.value.isZero());
        QCOMPARE(canceled.value.sources(), copied.sources());
    }
    void numericSyntaxAndOutputProtection()
    {
        for (const auto &text : {QStringLiteral("1+"), QStringLiteral("sqrt()"), QStringLiteral("min(1)"),
             QStringLiteral("max(1,2,3)"), QStringLiteral("unknown(2)"), QStringLiteral("(1+2"),
             QStringLiteral("2pi"), QStringLiteral("1e+"), QStringLiteral("1+😀"), QString(4097, '1'),
             QString(129, '-') + QLatin1Char('1')})
        {
            const auto ordinary = ExpressionEngine::evaluate(text);
            const auto numeric = ExpressionEngine::evaluateNumeric(text);
            QVERIFY(!ordinary.ok && !numeric.ok);
            QCOMPARE(numeric.error, ordinary.error);
            QCOMPARE(numeric.errorPosition, ordinary.errorPosition);
            QCOMPARE(numeric.errorLength, ordinary.errorLength);
        }
        const auto sentinel = ExpressionEngine::evaluateNumeric(QStringLiteral("sin(1/3)")).value;
        NumericValue zero, one;
        QVERIFY(NumericValue::parse(QStringLiteral("1"), one).ok());
        for (const auto &bad : {QStringLiteral("1e1000"), QStringLiteral("no"), QString(51, '1')})
        {
            NumericValue output = sentinel;
            QVERIFY(!NumericValue::parse(bad, output).ok());
            QCOMPARE(bits(output.binary()), bits(sentinel.binary()));
            QCOMPARE(output.sources(), sentinel.sources());
        }
        for (double bad : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        {
            NumericValue output = sentinel;
            QVERIFY(!NumericValue::fromBinary(bad, output).ok());
            QCOMPARE(bits(output.binary()), bits(sentinel.binary()));
            QCOMPARE(output.sources(), sentinel.sources());
        }
        NumericValue output = sentinel;
        QVERIFY(!NumericValue::operate('/', output, zero, output).ok());
        QCOMPARE(bits(output.binary()), bits(sentinel.binary()));
        QCOMPARE(output.sources(), sentinel.sources());
        QVERIFY(!NumericValue::function(QStringLiteral("pow"), {one}, output).ok());
        QVERIFY(!NumericValue::constant(QStringLiteral("unknown"), output).ok());
        QCOMPARE(output.sources(), sentinel.sources());
        QVERIFY(NumericValue::operate('-', output, output, output).ok());
        QVERIFY(output.isZero());
        QCOMPARE(output.sources(), sentinel.sources());
        QVERIFY(NumericValue::function(QStringLiteral("abs"), {output}, output).ok());
        QCOMPARE(output.sources(), sentinel.sources());
        // 所有输入／输出同对象，以及精确开方和取整的原位路径。
        DecimalValue same;
        QVERIFY(DecimalValue::parse(QStringLiteral("2"), same) == DecimalError::None);
        bool inexact = true;
        QCOMPARE(DecimalValue::integerPower(same, same, same, inexact), DecimalError::None);
        QCOMPARE(same.coefficient(), QByteArray("4"));
        QVERIFY(!inexact);
        QCOMPARE(DecimalValue::remainder(same, same, same), DecimalError::None);
        QVERIFY(same.isZero());
    }
    void numericSignedZeroAndAliases()
    {
        const auto evaluate = [](const QString &text) { return ExpressionEngine::evaluateNumeric(text); };
        for (const QString &text : {QStringLiteral("-4%2"), QStringLiteral("sqrt(-0)"), QStringLiteral("round(-0.1)"),
             QStringLiteral("min(-0,0)"), QStringLiteral("max(-0,0)")})
        {
            const auto result = evaluate(text);
            QVERIFY2(result.ok && result.value.isZero() && result.value.isNegative(), qPrintable(text));
            QVERIFY(!result.value.isBinary());
        }
        for (const QString &text : {QStringLiteral("abs(-0)"), QStringLiteral("min(0,-0)"), QStringLiteral("max(0,-0)")})
        {
            const auto result = evaluate(text);
            QVERIFY(result.ok && result.value.isZero() && !result.value.isNegative());
        }
        for (const QString &text : {QStringLiteral("min(-0,sin(0))"), QStringLiteral("max(-0,sin(0))"), QStringLiteral("sin(-0)")})
        {
            const auto result = evaluate(text);
            QVERIFY2(result.ok && result.value.isZero() && result.value.isNegative(), qPrintable(text));
            QVERIFY(result.value.isBinary());
            QCOMPARE(result.value.sources(), unsigned(NumericValue::Approximate));
        }
        struct Case { const char *left; char operation; const char *right; const char *expected; };
        for (const auto &entry : {Case{"0.1", '+', "0.2", "0.3"}, Case{"0.3", '%', "0.1", "0"},
             Case{"3", '^', "-1", "3^-1"}, Case{"1/3", '-', "1/3", "(1/3)-(1/3)"},
             Case{"1/3", '+', "pi", "1/3+pi"}, Case{"pi", '*', "0", "pi*0"}})
        {
            const auto a = evaluate(QString::fromLatin1(entry.left)).value;
            const auto b = evaluate(QString::fromLatin1(entry.right)).value;
            const auto expected = evaluate(QString::fromLatin1(entry.expected));
            QVERIFY(expected.ok);
            for (int alias = 0; alias < 3; ++alias)
            {
                NumericValue x = a, y = b, separate;
                NumericValue &output = alias == 0 ? separate : alias == 1 ? x : y;
                QVERIFY(NumericValue::operate(entry.operation, x, y, output).ok());
                QCOMPARE(output.sources(), expected.value.sources());
                QCOMPARE(output.isBinary(), expected.value.isBinary());
                if (output.isBinary()) QCOMPARE(bits(output.binary()), bits(expected.value.binary()));
                else QCOMPARE(DecimalValue::compare(output.decimal(), expected.value.decimal()), 0);
            }
        }
        const auto limit = evaluate(QStringLiteral("1e999")).value;
        auto approximate = evaluate(QStringLiteral("pi")).value;
        const auto before = approximate;
        QCOMPARE(NumericValue::operate('+', approximate, limit, approximate).error, NumericError::ConversionOverflow);
        QCOMPARE(bits(approximate.binary()), bits(before.binary()));
        QCOMPARE(approximate.sources(), before.sources());
        // 函数输出可以直接复用 QVector 中的参数，但两参数来源仍合并。
        QVector<NumericValue> parameters{evaluate(QStringLiteral("1")).value, evaluate(QStringLiteral("1/3")).value};
        QVERIFY(NumericValue::function(QStringLiteral("max"), parameters, parameters[0]).ok());
        QCOMPARE(parameters[0].sources(), unsigned(NumericValue::Rounded));
        QCOMPARE(DecimalValue::compare(parameters[0].decimal(), evaluate(QStringLiteral("1")).value.decimal()), 0);
    }
    void numericFloatingEnvironment()
    {
        std::fenv_t environment;
        QCOMPARE(std::fegetenv(&environment), 0);
        const int oldErrno = errno;
        std::feclearexcept(FE_ALL_EXCEPT);
        std::feraiseexcept(FE_DIVBYZERO);
        errno = EDOM;
        const auto result = ExpressionEngine::evaluateNumeric(QStringLiteral("exp(-1000)"));
        const int flags = std::fetestexcept(FE_ALL_EXCEPT);
        const int errorNumber = errno;
        std::fesetenv(&environment);
        errno = oldErrno;
        QVERIFY(!result.ok);
        QCOMPARE(result.error, CalculationError::Underflow);
        QCOMPARE(flags, int(FE_DIVBYZERO));
        QCOMPARE(errorNumber, EDOM);
    }
    void decimalParsing_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QByteArray>("coefficient");
        QTest::addColumn<int>("exponent");
        QTest::addColumn<bool>("negative");
        QTest::newRow("default-zero") << QStringLiteral("0") << QByteArray("0") << 0 << false;
        QTest::newRow("signed-zero") << QStringLiteral("-0.000") << QByteArray("0") << 0 << true;
        QTest::newRow("padding") << QStringLiteral("+00012.3400") << QByteArray("1234") << -2 << false;
        QTest::newRow("leading-fraction") << QStringLiteral("-.000123") << QByteArray("123") << -6 << true;
        QTest::newRow("integer-zeros") << QStringLiteral("1000.") << QByteArray("1") << 3 << false;
        QTest::newRow("internal-zeros") << QStringLiteral("100.001000") << QByteArray("100001") << -3 << false;
        QTest::newRow("upper-inclusive") << QStringLiteral("1e999") << QByteArray("1") << 999 << false;
        QTest::newRow("lower-inclusive") << QStringLiteral("-1E-999") << QByteArray("1") << -999 << true;
        QTest::newRow("normalized-exponent") << QStringLiteral("0.1e1000") << QByteArray("1") << 999 << false;
        QTest::newRow("normalized-exponent-negative") << QStringLiteral("10e-1000") << QByteArray("1") << -999 << false;
        QTest::newRow("maximum-value") << QString(50, QLatin1Char('9')) + QStringLiteral("e950") << QByteArray(50, '9') << 950 << false;
        QTest::newRow("minimum-quantum") << QStringLiteral("1.") + QString(48, QLatin1Char('0')) + QStringLiteral("1e-999")
            << QByteArray("1") + QByteArray(48, '0') + "1" << -1048 << false;
        QTest::newRow("long-leading-zeros") << QString(4095, QLatin1Char('0')) + QLatin1Char('1') << QByteArray("1") << 0 << false;
        QTest::newRow("long-fractional-padding") << QStringLiteral("1.") + QString(4094, QLatin1Char('0')) << QByteArray("1") << 0 << false;
        QTest::newRow("long-exponent-padding") << QStringLiteral("1e") + QString(4091, QLatin1Char('0')) + QStringLiteral("999") << QByteArray("1") << 999 << false;
        QTest::newRow("compensated-large-exponent") << QStringLiteral("0.") + QString(3900, QLatin1Char('0')) + QStringLiteral("1e4900") << QByteArray("1") << 999 << false;
        QTest::newRow("zero-large-exponent") << QStringLiteral("0e") + QString(4094, QLatin1Char('9')) << QByteArray("0") << 0 << false;
    }
    void decimalParsing()
    {
        QFETCH(QString, input);
        QFETCH(QByteArray, coefficient);
        QFETCH(int, exponent);
        QFETCH(bool, negative);
        RestoreLocale locale;
        QLocale::setDefault(QLocale(QLocale::German));
        DecimalValue value;
        QCOMPARE(DecimalValue::parse(input, value), DecimalError::None);
        QCOMPARE(value.coefficient(), coefficient);
        QCOMPARE(value.exponent(), exponent);
        QCOMPARE(value.isNegative(), negative);
    }
    void decimalInvalidLiterals_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<int>("error");
        for (const QString &text : {QString(), QStringLiteral("+"), QStringLiteral("-"), QStringLiteral("."),
             QStringLiteral("1e"), QStringLiteral("1e+"), QStringLiteral("1e-"), QStringLiteral("e1"),
             QStringLiteral(" 1"), QStringLiteral("1 "), QStringLiteral("1,2"), QStringLiteral("１"),
             QStringLiteral("١"), QStringLiteral("NaN"), QStringLiteral("Infinity"), QStringLiteral("1..2"),
             QStringLiteral("1e2.0"), QStringLiteral("--1"), QStringLiteral("1_000"), QStringLiteral("1+2")})
            QTest::newRow(qPrintable(QStringLiteral("syntax-") + text)) << text << int(DecimalError::Syntax);
        QTest::newRow("too-many-digits") << QString(51, QLatin1Char('1')) << int(DecimalError::PrecisionLimit);
        QTest::newRow("too-many-fractional-digits") << QStringLiteral("0.") + QString(51, QLatin1Char('1')) << int(DecimalError::PrecisionLimit);
        QTest::newRow("internal-zero-precision") << QStringLiteral("1") + QString(49, QLatin1Char('0')) + QLatin1Char('1') << int(DecimalError::PrecisionLimit);
        QTest::newRow("overflow") << QStringLiteral("1e1000") << int(DecimalError::Overflow);
        QTest::newRow("underflow") << QStringLiteral("9.9e-1000") << int(DecimalError::Underflow);
        QTest::newRow("exponent-integer-overflow") << QStringLiteral("1e99999999999999999999999999999") << int(DecimalError::Overflow);
        QTest::newRow("exponent-integer-underflow") << QStringLiteral("1e-99999999999999999999999999999") << int(DecimalError::Underflow);
        QTest::newRow("giant-exponent-still-validates-syntax") << QStringLiteral("0e") + QString(4000, QLatin1Char('9')) + QLatin1Char('x') << int(DecimalError::Syntax);
        QTest::newRow("literal-resource-limit") << QString(4097, QLatin1Char('0')) << int(DecimalError::ResourceLimit);
        QTest::newRow("compensated-still-overflow") << QStringLiteral("0.") + QString(3900, QLatin1Char('0')) + QStringLiteral("1e4901") << int(DecimalError::Overflow);
        QTest::newRow("maximum-mantissa-overflow") << QStringLiteral("1") + QString(4095, QLatin1Char('0')) << int(DecimalError::Overflow);
    }
    void decimalInvalidLiterals()
    {
        QFETCH(QString, input);
        QFETCH(int, error);
        DecimalValue value;
        QCOMPARE(DecimalValue::parse(QStringLiteral("-42.5"), value), DecimalError::None);
        QCOMPARE(int(DecimalValue::parse(input, value)), error);
        QCOMPARE(value.coefficient(), QByteArray("425"));
        QCOMPARE(value.exponent(), -1);
        QVERIFY(value.isNegative());
    }
    void decimalArithmetic_data()
    {
        QTest::addColumn<QString>("left");
        QTest::addColumn<QString>("right");
        QTest::addColumn<char>("operation");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("error");
        const auto row = [](const char *name, const QString &a, char op, const QString &b, const QString &result,
                            DecimalError error = DecimalError::None) {
            QTest::newRow(name) << a << b << op << result << int(error);
        };
        row("decimal-sum", "0.1", '+', "0.2", "0.3");
        row("decimal-difference", "0.3", '-', "0.2", "0.1");
        row("decimal-product", "0.1", '*', "0.2", "0.02");
        row("price-product", "19.9", '*', "3", "59.7");
        row("negative-sum", "-0.1", '-', "0.2", "-0.3");
        row("different-signs", "-2.5", '+', "4.05", "1.55");
        row("reverse-magnitudes", "2.5", '+', "-4.05", "-1.55");
        row("negative-product", "-19.9", '*', "3", "-59.7");
        row("both-negative-product", "-19.9", '*', "-3", "59.7");
        row("carry-boundary", "1e49", '+', "1", "1" + QString(48, '0') + "1");
        row("precision-boundary", "1e50", '+', "1", {}, DecimalError::PrecisionLimit);
        row("normalized-carry", QString(50, '9'), '+', "1", "1e50");
        row("borrow-chain", "1e50", '-', "1", QString(50, '9'));
        row("cancellation", "1e999", '-', "1e999", "0");
        row("overflow-product", "1e999", '*', "10", {}, DecimalError::Overflow);
        row("overflow-sum", "9e999", '+', "1e999", {}, DecimalError::Overflow);
        row("underflow-product", "1e-999", '*', "0.1", {}, DecimalError::Underflow);
        row("underflow-difference", "1.00001e-999", '-', "1e-999", {}, DecimalError::Underflow);
        row("precision-product", QString(50, '9'), '*', QString(50, '9'), {}, DecimalError::PrecisionLimit);
        row("normalized-product", "5e49", '*', "2", "1e50");
        row("full-product-normalizes", "2361183241434822606848", '*', "42351647362715016953416125033982098102569580078125", "1e71");
        row("wide-exponent-product", "1e999", '*', "1e-999", "1");
        row("widest-alignment", "1e999", '+', "1." + QString(48, '0') + "1e-999", {}, DecimalError::PrecisionLimit);
        row("widest-subtraction", "1e999", '-', "1." + QString(48, '0') + "1e-999", {}, DecimalError::PrecisionLimit);
        row("zero-add-large", "0", '+', "1e999", "1e999");
        row("large-add-zero", "1e999", '+', "0", "1e999");
        row("zero-product", "0", '*', "1e999", "0");
        row("negative-zero-product", "-0", '*', "1e999", "-0");
        row("negative-zeros-sum", "-0", '+', "-0", "-0");
        row("mixed-zeros-sum", "-0", '+', "0", "0");
    }
    void decimalArithmetic()
    {
        QFETCH(QString, left);
        QFETCH(QString, right);
        QFETCH(char, operation);
        QFETCH(QString, expected);
        QFETCH(int, error);
        DecimalValue a, b, output, result;
        QCOMPARE(DecimalValue::parse(left, a), DecimalError::None);
        QCOMPARE(DecimalValue::parse(right, b), DecimalError::None);
        QCOMPARE(DecimalValue::parse(QStringLiteral("-42.5"), output), DecimalError::None);
        const auto apply = [operation](const DecimalValue &a, const DecimalValue &b, DecimalValue &value) {
            if (operation == '+') return DecimalValue::add(a, b, value);
            if (operation == '-') return DecimalValue::subtract(a, b, value);
            return DecimalValue::multiply(a, b, value);
        };
        QCOMPARE(int(apply(a, b, output)), error);
        QCOMPARE(DecimalValue::parse(error == int(DecimalError::None) ? expected : QStringLiteral("-42.5"), result), DecimalError::None);
        QCOMPARE(output.coefficient(), result.coefficient());
        QCOMPARE(output.exponent(), result.exponent());
        QCOMPARE(output.isNegative(), result.isNegative());
        // 输出别名不能改变输入的读取；失败保留原数值，成功与独立输出一致。
        const DecimalValue originalA = a, originalB = b;
        QCOMPARE(int(apply(a, b, a)), error);
        const DecimalValue expectedA = error ? originalA : result;
        QCOMPARE(a.coefficient(), expectedA.coefficient());
        QCOMPARE(a.exponent(), expectedA.exponent());
        QCOMPARE(a.isNegative(), expectedA.isNegative());
        QCOMPARE(int(apply(originalA, b, b)), error);
        const DecimalValue expectedB = error ? originalB : result;
        QCOMPARE(b.coefficient(), expectedB.coefficient());
        QCOMPARE(b.exponent(), expectedB.exponent());
        QCOMPARE(b.isNegative(), expectedB.isNegative());
    }
    void decimalDivision_data()
    {
        QTest::addColumn<QString>("left");
        QTest::addColumn<QString>("right");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("inexact");
        QTest::addColumn<int>("error");
        const auto row = [](const char *name, const QString &a, const QString &b, const QString &expected,
                            bool inexact = false, DecimalError error = DecimalError::None) {
            QTest::newRow(name) << a << b << expected << inexact << int(error);
        };
        row("eighth-exact", QStringLiteral("1"), QStringLiteral("8"), QStringLiteral("0.125"));
        row("third-round-down", QStringLiteral("1"), QStringLiteral("3"), QStringLiteral("0.") + QString(50, '3'), true);
        row("two-thirds-round-up", QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("0.") + QString(49, '6') + '7', true);
        row("sixth-round-up", QStringLiteral("1"), QStringLiteral("6"), QStringLiteral("0.1") + QString(48, '6') + '7', true);
        row("sticky-five-even", QStringLiteral("2"), QStringLiteral("7"),
            QStringLiteral("0.28571428571428571428571428571428571428571428571429"), true);
        row("carry-and-normalize", QStringLiteral("6"), QStringLiteral("23"),
            QStringLiteral("0.2608695652173913043478260869565217391304347826087"), true);
        row("exact-padding", QStringLiteral("12.3400"), QStringLiteral("0.010"), QStringLiteral("1234"));
        row("coefficient-shorter", QStringLiteral("1"), QStringLiteral("125"), QStringLiteral("0.008"));
        row("coefficient-longer", QStringLiteral("125"), QStringLiteral("2"), QStringLiteral("62.5"));
        row("normalized-equal-length", QStringLiteral("12"), QStringLiteral("24"), QStringLiteral("0.5"));
        row("negative-numerator", QStringLiteral("-1"), QStringLiteral("8"), QStringLiteral("-0.125"));
        row("negative-denominator", QStringLiteral("1"), QStringLiteral("-8"), QStringLiteral("-0.125"));
        row("both-negative", QStringLiteral("-1"), QStringLiteral("-8"), QStringLiteral("0.125"));
        row("signed-zero", QStringLiteral("0"), QStringLiteral("-8"), QStringLiteral("-0"));
        row("negative-zero", QStringLiteral("-0"), QStringLiteral("8"), QStringLiteral("-0"));
        row("negative-zero-negative-denominator", QStringLiteral("-0"), QStringLiteral("-8"), QStringLiteral("0"));
        row("zero-positive-denominator", QStringLiteral("0"), QStringLiteral("8"), QStringLiteral("0"));
        row("upper-inclusive", QStringLiteral("1e999"), QStringLiteral("1"), QStringLiteral("1e999"));
        row("lower-inclusive", QStringLiteral("1e-999"), QStringLiteral("1"), QStringLiteral("1e-999"));
        row("largest-exact", QString(50, '9') + QStringLiteral("e950"), QStringLiteral("1"), QString(50, '9') + QStringLiteral("e950"));
        row("smallest-quantum", QStringLiteral("1e-999"), QStringLiteral("3"), {}, false, DecimalError::Underflow);
        row("low-rounded-in-range", QStringLiteral("1e-998"), QStringLiteral("3"), QString(50, '3') + QStringLiteral("e-1048"), true);
        row("high-rounded-in-range", QStringLiteral("1e999"), QStringLiteral("3"), QString(50, '3') + QStringLiteral("e949"), true);
        row("far-exponents-overflow", QStringLiteral("1e999"), QStringLiteral("1e-999"), {}, false, DecimalError::Overflow);
        row("far-exponents-underflow", QStringLiteral("1e-999"), QStringLiteral("1e999"), {}, false, DecimalError::Underflow);
        row("divide-by-positive-zero", QStringLiteral("1"), QStringLiteral("0"), {}, false, DecimalError::DivisionByZero);
        row("divide-by-negative-zero", QStringLiteral("-1"), QStringLiteral("-0"), {}, false, DecimalError::DivisionByZero);
        row("zero-over-zero", QStringLiteral("0"), QStringLiteral("0"), {}, false, DecimalError::DivisionByZero);
        row("signed-zero-over-zero", QStringLiteral("-0"), QStringLiteral("-0"), {}, false, DecimalError::DivisionByZero);
        row("long-exact-50", QString(50, '1'), QStringLiteral("1"), QString(50, '1'));
        row("finite-over-50", QStringLiteral("1"), QStringLiteral("1267650600228229401496703205376"),
            QStringLiteral("7.8886090522101180541172856528278622967320643510902e-31"), true);
        // 50 位整数的 .5 半值：偶数不进位，奇数进位；同一规则用于负数。
        for (const QString &sign : {QString(), QStringLiteral("-")})
        {
            const QByteArray name = sign.isEmpty() ? "positive-" : "negative-";
            row((name + "tie-even").constData(), sign + QString(49, '9') + '3', QStringLiteral("2"),
                sign + '4' + QString(48, '9') + '6', true);
            row((name + "tie-odd").constData(), sign + QString(49, '9') + '5', QStringLiteral("2"),
                sign + '4' + QString(48, '9') + '8', true);
            row((name + "below-half").constData(), sign + QString(49, '9') + '3', QStringLiteral("4"),
                sign + "24" + QString(47, '9') + '8', true);
            row((name + "above-half").constData(), sign + QString(49, '9') + '5', QStringLiteral("4"),
                sign + "24" + QString(47, '9') + '9', true);
        }

        QFile file(QFINDTESTDATA("data/decimal_division_vectors.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QVERIFY(document.isArray());
        const auto rows = document.array();
        QCOMPARE(rows.size(), 475);
        for (const auto &item : rows)
        {
            const auto entry = item.toObject();
            const auto errorName = entry.value(QStringLiteral("error")).toString();
            QVERIFY(errorName == QStringLiteral("none") || errorName == QStringLiteral("overflow")
                || errorName == QStringLiteral("underflow") || errorName == QStringLiteral("division-by-zero"));
            const auto error = errorName == QStringLiteral("overflow") ? DecimalError::Overflow
                : errorName == QStringLiteral("underflow") ? DecimalError::Underflow
                : errorName == QStringLiteral("division-by-zero") ? DecimalError::DivisionByZero : DecimalError::None;
            const QString expected = (entry.value(QStringLiteral("negative")).toBool() ? QStringLiteral("-") : QString())
                + entry.value(QStringLiteral("coefficient")).toString() + QLatin1Char('e')
                + QString::number(entry.value(QStringLiteral("exponent")).toInt());
            row(qPrintable(entry.value(QStringLiteral("name")).toString()), entry.value(QStringLiteral("left")).toString(),
                entry.value(QStringLiteral("right")).toString(), expected, entry.value(QStringLiteral("inexact")).toBool(), error);
        }
    }
    void decimalDivision()
    {
        QFETCH(QString, left);
        QFETCH(QString, right);
        QFETCH(QString, expected);
        QFETCH(bool, inexact);
        QFETCH(int, error);
        RestoreLocale locale;
        QLocale::setDefault(QLocale(QLocale::German));
        DecimalValue originalA, originalB, sentinel, result;
        QCOMPARE(DecimalValue::parse(left, originalA), DecimalError::None);
        QCOMPARE(DecimalValue::parse(right, originalB), DecimalError::None);
        QCOMPARE(DecimalValue::parse(QStringLiteral("-42.5"), sentinel), DecimalError::None);
        if (!error) QCOMPARE(DecimalValue::parse(expected, result), DecimalError::None);
        const auto equal = [](const DecimalValue &a, const DecimalValue &b) {
            return a.coefficient() == b.coefficient() && a.exponent() == b.exponent() && a.isNegative() == b.isNegative();
        };
        for (bool initialFlag : {false, true})
        {
            for (int alias = 0; alias < 3; ++alias)
            {
                DecimalValue a = originalA, b = originalB, output = sentinel;
                DecimalValue &target = alias == 1 ? a : alias == 2 ? b : output;
                const DecimalValue before = target;
                bool changed = initialFlag;
                QCOMPARE(int(DecimalValue::divide(a, b, target, changed)), error);
                QVERIFY(equal(target, error ? before : result));
                QCOMPARE(changed, error ? initialFlag : inexact);
                if (alias != 1) QVERIFY(equal(a, originalA));
                if (alias != 2) QVERIFY(equal(b, originalB));
            }
        }
    }
    void decimalDivisionSelfAlias()
    {
        for (const QString &text : {QStringLiteral("0"), QStringLiteral("-0"), QStringLiteral("2"),
             QStringLiteral("-2"), QStringLiteral("1e999"), QStringLiteral("-1e-999"), QString(50, '9')})
        {
            DecimalValue value;
            QCOMPARE(DecimalValue::parse(text, value), DecimalError::None);
            const DecimalValue before = value;
            bool changed = true;
            const bool zero = value.isZero();
            QCOMPARE(DecimalValue::divide(value, value, value, changed), zero ? DecimalError::DivisionByZero : DecimalError::None);
            QCOMPARE(value.coefficient(), zero ? before.coefficient() : QByteArray("1"));
            QCOMPARE(value.exponent(), 0);
            QCOMPARE(value.isNegative(), zero ? before.isNegative() : false);
            QCOMPARE(changed, zero);
        }
    }
    void decimalDivisionIntermediateRounding()
    {
        DecimalValue one, three, six, accumulator;
        QCOMPARE(DecimalValue::parse(QStringLiteral("1"), one), DecimalError::None);
        QCOMPARE(DecimalValue::parse(QStringLiteral("3"), three), DecimalError::None);
        QCOMPARE(DecimalValue::parse(QStringLiteral("6"), six), DecimalError::None);
        bool changed = false;
        QCOMPARE(DecimalValue::divide(one, three, accumulator, changed), DecimalError::None);
        QVERIFY(changed);
        QCOMPARE(DecimalValue::multiply(accumulator, three, accumulator), DecimalError::None);
        QCOMPARE(accumulator.coefficient(), QByteArray(50, '9'));
        QCOMPARE(accumulator.exponent(), -50);
        QCOMPARE(DecimalValue::subtract(accumulator, one, accumulator), DecimalError::None);
        QCOMPARE(accumulator.coefficient(), QByteArray("1"));
        QCOMPARE(accumulator.exponent(), -50);
        QVERIFY(accumulator.isNegative());
        QCOMPARE(DecimalValue::divide(one, six, accumulator, changed), DecimalError::None);
        QVERIFY(changed);
        const DecimalValue sixth = accumulator;
        QCOMPARE(DecimalValue::multiply(accumulator, six, accumulator), DecimalError::PrecisionLimit);
        QCOMPARE(accumulator.coefficient(), sixth.coefficient());
        QCOMPARE(accumulator.exponent(), sixth.exponent());
        QCOMPARE(accumulator.isNegative(), sixth.isNegative());
        // 本次相除精确，不把此前舍入来源混进 primitive 的 inexact 输出。
        QCOMPARE(DecimalValue::divide(sixth, sixth, accumulator, changed), DecimalError::None);
        QVERIFY(!changed);
        QCOMPARE(accumulator.coefficient(), QByteArray("1"));
        QCOMPARE(accumulator.exponent(), 0);
        // 既有 round(x) 的整数半值规则不受除法舍入方式影响。
        const auto positive = ExpressionEngine().evaluate(QStringLiteral("round(2.5)"));
        const auto negative = ExpressionEngine().evaluate(QStringLiteral("round(-2.5)"));
        QVERIFY(positive.ok && negative.ok);
        QCOMPARE(positive.value, 3.0);
        QCOMPARE(negative.value, -3.0);
    }
    void decimalScaledIntegerOracle()
    {
        // 独立 qint64 精确参考，覆盖进位、借位、符号和小数位；所有整数运算远离溢出。
        std::mt19937 generator(20261007);
        std::uniform_int_distribution<int> distribution(-999999999, 999999999);
        for (int i = 0; i < 1000; ++i)
        {
            const qint64 a = distribution(generator), b = distribution(generator);
            DecimalValue left, right;
            QCOMPARE(DecimalValue::parse(QString::number(a) + QStringLiteral("e-9"), left), DecimalError::None);
            QCOMPARE(DecimalValue::parse(QString::number(b) + QStringLiteral("e-9"), right), DecimalError::None);
            for (int operation = 0; operation < 3; ++operation)
            {
                DecimalValue actual, expected;
                const qint64 integer = operation == 0 ? a + b : operation == 1 ? a - b : a * b;
                QCOMPARE(DecimalValue::parse(QString::number(integer) + (operation == 2 ? QStringLiteral("e-18") : QStringLiteral("e-9")), expected), DecimalError::None);
                const auto error = operation == 0 ? DecimalValue::add(left, right, actual)
                    : operation == 1 ? DecimalValue::subtract(left, right, actual) : DecimalValue::multiply(left, right, actual);
                QCOMPARE(error, DecimalError::None);
                QCOMPARE(actual.coefficient(), expected.coefficient());
                QCOMPARE(actual.exponent(), expected.exponent());
                if (!actual.isZero()) QCOMPARE(actual.isNegative(), expected.isNegative());
            }
        }
    }
    void decimalIndependentGoldenVectors()
    {
        QFile file(QFINDTESTDATA("data/decimal_vectors.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QVERIFY(document.isArray());
        const auto rows = document.array();
        QCOMPARE(rows.size(), 360);
        for (const auto &item : rows)
        {
            const auto row = item.toObject();
            DecimalValue a, b, actual;
            QCOMPARE(DecimalValue::parse(row.value(QStringLiteral("left")).toString(), a), DecimalError::None);
            QCOMPARE(DecimalValue::parse(row.value(QStringLiteral("right")).toString(), b), DecimalError::None);
            const auto operation = row.value(QStringLiteral("operation")).toString();
            const auto error = operation == QStringLiteral("+") ? DecimalValue::add(a, b, actual)
                : operation == QStringLiteral("-") ? DecimalValue::subtract(a, b, actual) : DecimalValue::multiply(a, b, actual);
            const auto errorName = row.value(QStringLiteral("error")).toString();
            const auto expectedError = errorName == QStringLiteral("overflow") ? DecimalError::Overflow
                : errorName == QStringLiteral("underflow") ? DecimalError::Underflow
                : errorName == QStringLiteral("precision") ? DecimalError::PrecisionLimit : DecimalError::None;
            const auto description = row.value(QStringLiteral("left")).toString() + operation + row.value(QStringLiteral("right")).toString();
            QVERIFY2(error == expectedError, qPrintable(description));
            if (error != DecimalError::None) continue;
            QCOMPARE(actual.coefficient(), row.value(QStringLiteral("coefficient")).toString().toLatin1());
            QCOMPARE(actual.exponent(), row.value(QStringLiteral("exponent")).toInt());
            QCOMPARE(actual.isNegative(), row.value(QStringLiteral("negative")).toBool());
        }
    }
    void decimalIntermediateFailurePreservesAccumulator()
    {
        DecimalValue accumulator, one, big;
        QCOMPARE(DecimalValue::parse(QStringLiteral("1e50"), accumulator), DecimalError::None);
        QCOMPARE(DecimalValue::parse(QStringLiteral("1"), one), DecimalError::None);
        big = accumulator;
        // (1e50+1)-1e50 必须在第一步报告失败；调用方不能继续算第二步来掩盖超限。
        QCOMPARE(DecimalValue::add(accumulator, one, accumulator), DecimalError::PrecisionLimit);
        QCOMPARE(accumulator.coefficient(), big.coefficient());
        QCOMPARE(accumulator.exponent(), big.exponent());
        QCOMPARE(DecimalValue::parse(QStringLiteral("0.1"), accumulator), DecimalError::None);
        DecimalValue second, third;
        QCOMPARE(DecimalValue::parse(QStringLiteral("0.2"), second), DecimalError::None);
        QCOMPARE(DecimalValue::parse(QStringLiteral("0.3"), third), DecimalError::None);
        QCOMPARE(DecimalValue::add(accumulator, second, accumulator), DecimalError::None);
        QCOMPARE(DecimalValue::subtract(accumulator, third, accumulator), DecimalError::None);
        QVERIFY(accumulator.isZero());
    }
    void customExpandedLengthLimit()
    {
        CustomFormula formula;
        QCOMPARE(CustomFormula::parse(QStringLiteral("A=x+x"), formula), QString());
        QString output = QStringLiteral("unchanged");
        const QString largeValue = QString(2047, QLatin1Char('0')) + QLatin1Char('1');
        QVERIFY(formula.substitute(QStringLiteral("x=") + largeValue, output).contains(QStringLiteral("4096")));
        QCOMPARE(output, QStringLiteral("unchanged"));
        const QString fittingValue = QString(2046, QLatin1Char('0')) + QLatin1Char('1');
        QCOMPARE(formula.substitute(QStringLiteral("x=") + fittingValue, output), QString());
        QCOMPARE(output.size(), 4095);
        QCOMPARE(CustomFormula::parse(QStringLiteral("A=x+x "), formula), QString());
        // 恰好 4096：保留内部空格；定义两端空白会被解析器去掉。
        QCOMPARE(CustomFormula::parse(QStringLiteral("A=x +x"), formula), QString());
        QCOMPARE(formula.substitute(QStringLiteral("x=") + fittingValue, output), QString());
        QCOMPARE(output.size(), 4096);
        // 有符号参数代入时的括号也计入展开上限。
        output = QStringLiteral("unchanged");
        QVERIFY(formula.substitute(QStringLiteral("x=-") + fittingValue, output).contains(QStringLiteral("4096")));
        QCOMPARE(output, QStringLiteral("unchanged"));
        CalculationHistory history;
        history.calculate(QStringLiteral("42"));
        QString error;
        const auto record = history.calculateCustom(QStringLiteral("A=x+x"), QStringLiteral("x=") + largeValue, error);
        QCOMPARE(record.id, quint64(0));
        QVERIFY(error.contains(QStringLiteral("4096")));
        QCOMPARE(history.count(), 1);
        QCOMPARE(history.answer(), 42.0);
    }
    void customFunctions_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("substituted");
        QTest::addColumn<double>("expected");
        QTest::newRow("add-lines") << QStringLiteral("x+y") << QStringLiteral("x=1\ny=2") << QStringLiteral("1+2") << 3.0;
        QTest::newRow("crlf-with-padding") << QStringLiteral("x+y") << QStringLiteral("  x = 1 \r\n\r\n\ty = 2\r\n") << QStringLiteral("1+2") << 3.0;
        QTest::newRow("blank-lines") << QStringLiteral("x+y") << QStringLiteral("\n x=1\n  \n y=2\n") << QStringLiteral("1+2") << 3.0;
        QTest::newRow("signed-power") << QStringLiteral("x^2+y") << QStringLiteral("x=-2\ny=3") << QStringLiteral("(-2)^2+3") << 7.0;
        QTest::newRow("unary-power") << QStringLiteral("-x^2") << QStringLiteral("x=-2") << QStringLiteral("-(-2)^2") << -4.0;
        QTest::newRow("scientific") << QStringLiteral("x+x1+_x+X+e2") << QStringLiteral("x=1e2\nx1=.5\n_x=2.\nX=+3\ne2=4") << QStringLiteral("1e2+.5+2.+(+3)+4") << 109.5;
        QTest::newRow("sqrt") << QStringLiteral("sqrt(x)") << QStringLiteral("x=9") << QStringLiteral("sqrt(9)") << 3.0;
        QTest::newRow("abs") << QStringLiteral("abs(x)") << QStringLiteral("x=-2") << QStringLiteral("abs((-2))") << 2.0;
        QTest::newRow("sin") << QStringLiteral("sin(x)") << QStringLiteral("x=0") << QStringLiteral("sin(0)") << 0.0;
        QTest::newRow("cos") << QStringLiteral("cos(x)") << QStringLiteral("x=0") << QStringLiteral("cos(0)") << 1.0;
        QTest::newRow("tan") << QStringLiteral("tan(x)") << QStringLiteral("x=0") << QStringLiteral("tan(0)") << 0.0;
        QTest::newRow("ln") << QStringLiteral("ln(x)") << QStringLiteral("x=1") << QStringLiteral("ln(1)") << 0.0;
        QTest::newRow("log") << QStringLiteral("log(x)") << QStringLiteral("x=100") << QStringLiteral("log(100)") << 2.0;
        QTest::newRow("exp") << QStringLiteral("exp(x)") << QStringLiteral("x=0") << QStringLiteral("exp(0)") << 1.0;
        QTest::newRow("floor") << QStringLiteral("floor(x)") << QStringLiteral("x=1.8") << QStringLiteral("floor(1.8)") << 1.0;
        QTest::newRow("ceil") << QStringLiteral("ceil(x)") << QStringLiteral("x=1.2") << QStringLiteral("ceil(1.2)") << 2.0;
        QTest::newRow("round") << QStringLiteral("round(x)") << QStringLiteral("x=1.5") << QStringLiteral("round(1.5)") << 2.0;
        QTest::newRow("min") << QStringLiteral("min(x,y)") << QStringLiteral("x=4\ny=2") << QStringLiteral("min(4,2)") << 2.0;
        QTest::newRow("max") << QStringLiteral("max(x,y)") << QStringLiteral("x=4\ny=2") << QStringLiteral("max(4,2)") << 4.0;
        QTest::newRow("pow") << QStringLiteral("pow(x,y)") << QStringLiteral("x=-2\ny=3") << QStringLiteral("pow((-2),3)") << -8.0;
        QTest::newRow("nested") << QStringLiteral("sqrt(x^2+y^2)+max(z,0)") << QStringLiteral("x=3\ny=4\nz=2") << QStringLiteral("sqrt(3^2+4^2)+max(2,0)") << 7.0;
        QTest::newRow("unicode-operators") << QStringLiteral("x×y−x÷y+x%y") << QStringLiteral("x=6\ny=3") << QStringLiteral("6×3−6÷3+6%3") << 16.0;
        QTest::newRow("constants-no-parameters") << QStringLiteral("PI+e+ans") << QStringLiteral("") << QStringLiteral("PI+e+ans") << std::acos(-1.0)+std::exp(1.0)+10;
        QTest::newRow("token-boundaries") << QStringLiteral("exp(x)+max(x1,x)+1e3") << QStringLiteral("x=0\nx1=2") << QStringLiteral("exp(0)+max(2,0)+1e3") << 1003.0;
    }
    void customFunctions()
    {
        RestoreLocale locale;
        QLocale::setDefault(QLocale(QLocale::German));
        QFETCH(QString, body);
        QFETCH(QString, input);
        QFETCH(QString, substituted);
        QFETCH(double, expected);
        CalculationHistory history;
        history.calculate(QStringLiteral("10"));
        QString error;
        const auto record = history.calculateCustom(QStringLiteral("A=")+body, input, error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(record.result.ok, qPrintable(record.result.text));
        QCOMPARE(record.expression, QStringLiteral("A=")+substituted);
        QCOMPARE(record.customDefinition, QStringLiteral("A=")+body);
        QCOMPARE(record.parameterInput, input);
        QCOMPARE(record.answerBefore, 10.0);
        QCOMPARE(record.result.value, expected);
        QCOMPARE(history.answer(), expected);
        QCOMPARE(record.id, quint64(2));
    }
    void customInvalidDefinition_data()
    {
        QTest::addColumn<QString>("definition");
        for (const auto *text : {"x+y", "1A=x", "sin=x", "ANS=x", "A=A+1", "A=x+", "A=foo(x)",
                                "A=pow(x)", "A=sin(x,y)", "A=sin", "A=x y", "A=x=1", "A=2x", "A=1e", "A="})
            QTest::newRow(text) << QString::fromLatin1(text);
        QTest::newRow("chinese-name") << QStringLiteral("面积=x");
        QTest::newRow("chinese-parameter") << QStringLiteral("A=宽+1");
        QTest::newRow("length") << QStringLiteral("A=x+") + QString(4096, QLatin1Char('1'));
        QTest::newRow("depth") << QStringLiteral("A=") + QString(130, '(') + QStringLiteral("x") + QString(130, ')');
    }
    void customInvalidDefinition()
    {
        QFETCH(QString, definition);
        CustomFormula formula;
        QVERIFY(CustomFormula::parse(QStringLiteral("Original=x+y"), formula).isEmpty());
        QVERIFY(!CustomFormula::parse(definition, formula).isEmpty());
        QCOMPARE(formula.definition(), QStringLiteral("Original=x+y"));
    }
    void customInvalidParameters_data()
    {
        QTest::addColumn<QString>("input");
        for (const auto *text : {"", "x=1", "x=\ny=2", "x=1\ny=", "x=1\nx=2\ny=3", "x=1\nz=2", "X=1\ny=2",
                                "x=1+2\ny=3", "x=nan\ny=2", "x=inf\ny=2", "x=1e400\ny=2", "x=1e-400\ny=2",
                                "x=1y=2", "x=1,\ny=2", "x=(1)\ny=2", "oops\nx=1\ny=2", "x=1\ny=2 extra", "x=ans\ny=2"})
            QTest::newRow(text[0] ? qPrintable(QString::fromLatin1(text).replace(QLatin1Char('\n'), QStringLiteral(" / "))) : "empty") << QString::fromLatin1(text);
        QTest::newRow("same-line-space") << QStringLiteral("x=1 y=2");
        QTest::newRow("same-line-tab") << QStringLiteral("x=1\ty=2");
        QTest::newRow("value-on-next-line") << QStringLiteral("x=\n1\ny=2");
        QTest::newRow("extra-equals") << QStringLiteral("x==1\ny=2");
        QTest::newRow("fullwidth") << QStringLiteral("x=１\ny=2");
    }
    void customInvalidParameters()
    {
        QFETCH(QString, input);
        CalculationHistory history;
        history.calculate(QStringLiteral("42"));
        QString error;
        QCOMPARE(history.calculateCustom(QStringLiteral("A=x+y"), input, error).id, quint64(0));
        QVERIFY(!error.isEmpty());
        QCOMPARE(history.count(), 1);
        QCOMPARE(history.answer(), 42.0);
    }
    void customManyParametersAndSignedZero()
    {
        QStringList names, assignments;
        for (int i = 0; i < 40; ++i)
        {
            const QString name = QStringLiteral("x_%1").arg(i);
            names.append(name);
            assignments.append(name + QStringLiteral("=1"));
        }
        CustomFormula formula;
        QVERIFY(CustomFormula::parse(QStringLiteral("Total=") + names.join(QLatin1Char('+')), formula).isEmpty());
        QCOMPARE(formula.parameters().size(), 40);
        QCOMPARE(formula.parameterTemplate().split(QLatin1Char('\n')).size(), 40);
        CalculationHistory history;
        QString error;
        QCOMPARE(history.calculateCustom(formula.definition(), assignments.join(QLatin1Char('\n')), error).result.value, 40.0);
        const auto zero = history.calculateCustom(QStringLiteral("Zero=x"), QStringLiteral("x=-0"), error);
        QCOMPARE(zero.valueText(), QStringLiteral("-0"));
        QCOMPARE(zero.expression, QStringLiteral("Zero=(-0)"));
    }
    void customAnalysisDoesNotEvaluateAndKeepsParameterOrder()
    {
        CustomFormula formula;
        QVERIFY(CustomFormula::parse(QStringLiteral(" A_1 = ln(x-1)+1/(y-y)+sqrt(-2)+1e400+X+x "), formula).isEmpty());
        QCOMPARE(formula.parameters(), QStringList({"x", "y", "X"}));
        QCOMPARE(formula.parameterTemplate(), QStringLiteral("x=\ny=\nX="));
        QCOMPARE(formula.parameterTemplate(QStringLiteral("y=\nx=1e-\nold=2\nX=4")), QStringLiteral("x=1e-\ny=\nX=4"));
        // 不把普通模式扩展成变量／赋值系统。
        QVERIFY(!ExpressionEngine::evaluate(QStringLiteral("x+1")).ok);
        QVERIFY(!ExpressionEngine::evaluate(QStringLiteral("A=1+2")).ok);
    }
    void customMathFailuresAndSnapshots()
    {
        CalculationHistory history;
        QString error;
        const auto first = history.calculateCustom(QStringLiteral("A=x+y"), QStringLiteral("x=1\ny=2"), error);
        const auto again = history.calculateCustom(QStringLiteral("A=x+y"), QStringLiteral("x=1\ny=2"), error);
        QCOMPARE(again.id, quint64(2));
        const auto failed = history.calculateCustom(QStringLiteral("LongName=x/y"), QStringLiteral("x=1\ny=0"), error);
        QVERIFY(error.isEmpty());
        QVERIFY(!failed.result.ok);
        QCOMPARE(failed.result.error, CalculationError::DivisionByZero);
        QCOMPARE(failed.expression.mid(failed.result.errorPosition, failed.result.errorLength), QStringLiteral("0"));
        QCOMPARE(history.answer(), 3.0);
        const auto domain = history.calculateCustom(QStringLiteral("A=sqrt(x)"), QStringLiteral("x=-1"), error);
        QCOMPARE(domain.result.error, CalculationError::Domain);
        QCOMPARE(history.calculate(QStringLiteral("ans+1")).result.value, 4.0);
        QCOMPARE(history.records().first().calculationText(), QStringLiteral("A=1+2\n= 3"));
        QCOMPARE(first.expression, history.records().first().expression);
        QVERIFY(CalculationExport::serialize(history.records(), CalculationExport::Format::Text).contains("A=1+2\n= 3"));
        QVERIFY(CalculationExport::serialize(history.records(), CalculationExport::Format::Markdown).contains("A=1+2"));
    }
    void customSessionSnapshots()
    {
        CalculationSession original;
        QString error;
        original.history.calculateCustom(QStringLiteral("A=x+y"), QStringLiteral("x=1\ny=2"), error);
        auto records = original.history.records();
        records[0].result.value = 17; // 已保存的结果快照必须保留，不能用当前求值替换成 3。
        records[0].result.text = QStringLiteral("历史结果 17");
        QVERIFY(original.history.restoreRecords(records));
        original.input = {QStringLiteral("x=5\ny=6"), 3, 3, QStringLiteral("A=x+y")};
        original.customInput = original.input;
        original.normalInput = {QStringLiteral("123+"), 4, 2};
        CalculationSession restored;
        const auto bytes = SessionFormat::encode(original);
        QVERIFY(SessionFormat::decode(bytes, restored).isEmpty());
        QCOMPARE(SessionFormat::encode(restored), bytes);
        QCOMPARE(restored.history.answer(), 17.0);
        QCOMPARE(restored.history.records().first().result.text, QStringLiteral("历史结果 17"));
        QCOMPARE(restored.normalInput.text, QStringLiteral("123+"));
    }
    void corruptCustomSessionsAreAtomic_data()
    {
        QTest::addColumn<QString>("field");
        for (const auto *field : {"definition", "values", "display", "normal-mode", "missing-buffer", "active-definition"})
            QTest::newRow(field) << QString::fromLatin1(field);
    }
    void corruptCustomSessionsAreAtomic()
    {
        QFETCH(QString, field);
        CalculationSession original;
        QString error;
        original.history.calculateCustom(QStringLiteral("A=x"), QStringLiteral("x=2"), error);
        auto root = QJsonDocument::fromJson(SessionFormat::encode(original)).object();
        auto records = root.value("records").toArray();
        auto record = records.first().toObject();
        if (field == "definition") record.insert("customDefinition", "A=foo(x)");
        if (field == "values") record.insert("parameterInput", "x=3");
        if (field == "display") record.insert("expression", "A=3");
        records[0] = record;
        root.insert("records", records);
        if (field == "normal-mode" || field == "active-definition")
        {
            const QString key = field == "normal-mode" ? QStringLiteral("normalInput") : QStringLiteral("input");
            auto input = root.value(key).toObject();
            input.insert("customDefinition", field == "normal-mode" ? "A=x" : "A=x+");
            root.insert(key, input);
        }
        if (field == "missing-buffer") root.remove("customInput");
        CalculationSession destination;
        destination.history.calculate(QStringLiteral("42"));
        const auto before = SessionFormat::encode(destination);
        QVERIFY(!SessionFormat::decode(QJsonDocument(root).toJson(), destination).isEmpty());
        QCOMPARE(SessionFormat::encode(destination), before);
    }
    void unsupportedVersionFileStaysUnchanged_data()
    {
        QTest::addColumn<int>("version");
        QTest::newRow("unsupported-1") << 1;
        QTest::newRow("unsupported-3") << 3;
    }
    void unsupportedVersionFileStaysUnchanged()
    {
        QFETCH(int, version);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("future.calctabdd"));
        CalculationSession original;
        original.history.calculate(QStringLiteral("42"));
        auto root = QJsonDocument::fromJson(SessionFormat::encode(original)).object();
        root.insert(QStringLiteral("version"), version);
        root.insert(QStringLiteral("futureState"), QStringLiteral("preserve unknown data"));
        const QByteArray bytes = QJsonDocument(root).toJson();
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        CalculationSessionFile store;
        CalculationSession restored;
        QVERIFY(store.load(path, restored).contains(QStringLiteral("格式版本")));
        QCOMPARE(restored.history.count(), 0);
        QVERIFY(store.path().isEmpty());
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".lock")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
    }
    void sessionFilesSurviveProcessExit_data()
    {
        QTest::addColumn<bool>("crash");
        QTest::newRow("normal-exit") << false;
        QTest::newRow("abrupt-exit") << true;
    }
    void sessionFilesSurviveProcessExit()
    {
        QFETCH(bool, crash);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("process.calctabdd"));
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--hold-session"), path});
        QVERIFY(process.waitForStarted(5000));
        QVERIFY(process.waitForReadyRead(5000));
        QCOMPARE(process.readAllStandardOutput().trimmed(), QByteArray("LOCKED"));
        CalculationSessionFile file;
        CalculationSession session;
        QVERIFY(!file.load(path, session).isEmpty());
        if (crash) process.kill();
        else process.write("close\n");
        QVERIFY(process.waitForFinished(5000));
        if (!crash) QCOMPARE(process.exitCode(), 0);
        QVERIFY2(file.load(path, session).isEmpty(), "Session should be available after its process exits");
        QCOMPARE(session.history.count(), 2);
        QCOMPARE(session.history.answer(), 42.0);
        QCOMPARE(session.input.text, QStringLiteral("跨进程草稿😀"));
        QCOMPARE(session.history.records().last().result.error, CalculationError::DivisionByZero);
    }
    void sessionRoundTripKeepsSnapshotsAndNavigation()
    {
        CalculationSession original;
        original.history.calculate(QStringLiteral("0.1+0.2"));
        original.history.calculate(QStringLiteral("ans+1"));
        original.history.calculate(QStringLiteral("ln(0)"));
        original.input = {QStringLiteral("临时😀\nans + 7"), 5, 1};
        original.draft = {QStringLiteral("  草稿😀\n99 + "), 3, 8};
        original.historyPosition = 1;
        original.recalledInputs.insert(2, original.input);
        original.recalledInputs.insert(3, {QStringLiteral("ln(9)"), 4, 4});
        // 独立构造历史快照，确保恢复不会再次调用求值。
        auto records = original.history.records();
        records[0].expression = QStringLiteral("old_function(中文😀)");
        records[0].result.text = QStringLiteral("历史显示文本");
        QVERIFY(original.history.restoreRecords(records));
        RestoreLocale locale;
        QLocale::setDefault(QLocale(QLocale::German));
        const QByteArray bytes = SessionFormat::encode(original);
        CalculationSession restored;
        const QString error = SessionFormat::decode(bytes, restored);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(SessionFormat::encode(restored), bytes);
        QCOMPARE(restored.id, original.id);
        QCOMPARE(bits(restored.history.answer()), bits(original.history.answer()));
        QCOMPARE(restored.history.records().first().expression, records[0].expression);
        QCOMPARE(restored.history.records().first().result.text, records[0].result.text);
        QCOMPARE(restored.history.records().last().result.error, CalculationError::Domain);
        QCOMPARE(restored.input.text, original.input.text);
        QCOMPARE(restored.input.anchor, 1);
        QCOMPARE(restored.draft.position, 3);
        QCOMPARE(restored.recalledInputs.value(3).text, QStringLiteral("ln(9)"));
        QCOMPARE(restored.history.calculate(QStringLiteral("ans+1")).id, quint64(4));
        QCOMPARE(bits(restored.history.answer()), bits(original.history.answer() + 1));
    }
    void sessionNumbersRoundTripExactly()
    {
        QVector<double> values{0, -0.0, std::numeric_limits<double>::max(),
            std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min(),
            -std::numeric_limits<double>::denorm_min(), 0.1 + 0.2, 9007199254740992.0};
        std::mt19937_64 random(803);
        while (values.size() < 500)
        {
            const quint64 raw = random();
            double value;
            std::memcpy(&value, &raw, sizeof(value));
            if (std::isfinite(value)) values.append(value);
        }
        CalculationSession original;
        QVector<CalculationRecord> records;
        double before = 0;
        for (const double value : values)
        {
            CalculationRecord entry;
            entry.id = static_cast<quint64>(records.size()) + 1;
            entry.expression = QStringLiteral("historical result");
            entry.result.ok = true;
            entry.result.value = value;
            entry.result.text = QStringLiteral("saved display");
            entry.answerBefore = before;
            records.append(entry);
            before = value;
        }
        QVERIFY(original.history.restoreRecords(records));
        CalculationSession restored;
        const QString error = SessionFormat::decode(SessionFormat::encode(original), restored);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        for (int i = 0; i < values.size(); ++i)
        {
            QCOMPARE(bits(restored.history.records().at(i).result.value), bits(values[i]));
            QCOMPARE(bits(restored.history.records().at(i).answerBefore), bits(records[i].answerBefore));
        }
    }
    void invalidSessionsLeaveDestinationUntouched_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QJsonValue>("replacement");
        QTest::newRow("future-version") << QStringLiteral("version") << QJsonValue(3);
        QTest::newRow("other-version") << QStringLiteral("version") << QJsonValue(1);
        QTest::newRow("zero-version") << QStringLiteral("version") << QJsonValue(0);
        QTest::newRow("missing-version") << QStringLiteral("version") << QJsonValue();
        QTest::newRow("wrong-format") << QStringLiteral("format") << QJsonValue("Other.Session");
        QTest::newRow("invalid-uuid") << QStringLiteral("sessionId") << QJsonValue("none");
        QTest::newRow("numeric-answer") << QStringLiteral("answer") << QJsonValue(42);
        QTest::newRow("answer-mismatch") << QStringLiteral("answer") << QJsonValue("41");
        QTest::newRow("nan") << QStringLiteral("answer") << QJsonValue("nan");
        QTest::newRow("infinity") << QStringLiteral("answer") << QJsonValue("inf");
        QTest::newRow("missing-records") << QStringLiteral("records") << QJsonValue();
        QTest::newRow("invalid-navigation") << QStringLiteral("historyPosition") << QJsonValue(1);
        QTest::newRow("fractional-navigation") << QStringLiteral("historyPosition") << QJsonValue(0.5);
        QTest::newRow("invalid-input") << QStringLiteral("input") << QJsonValue(QJsonObject{{"text", "x"}, {"position", 2}, {"anchor", 0}});
        QTest::newRow("invalid-draft") << QStringLiteral("draft") << QJsonValue(QJsonObject{{"text", "x"}, {"position", 0}, {"anchor", 0}});
        QTest::newRow("missing-recalled") << QStringLiteral("recalledInputs") << QJsonValue();
        QTest::newRow("wrong-id") << QStringLiteral("record.id") << QJsonValue("2");
        QTest::newRow("unknown-error") << QStringLiteral("record.error") << QJsonValue(99);
        QTest::newRow("success-with-error") << QStringLiteral("record.error") << QJsonValue(1);
        QTest::newRow("negative-length") << QStringLiteral("record.errorLength") << QJsonValue(-1);
        QTest::newRow("invalid-position") << QStringLiteral("record.errorPosition") << QJsonValue(999);
        QTest::newRow("missing-ok") << QStringLiteral("record.ok") << QJsonValue();
        QTest::newRow("before-mismatch") << QStringLiteral("record.answerBefore") << QJsonValue("9");
        QTest::newRow("missing-value") << QStringLiteral("record.value") << QJsonValue();
        QTest::newRow("value-overflow") << QStringLiteral("record.value") << QJsonValue("1e500");
        QTest::newRow("missing-text") << QStringLiteral("record.text") << QJsonValue();
        QTest::newRow("missing-record-definition") << QStringLiteral("record.customDefinition") << QJsonValue();
        QTest::newRow("missing-record-parameters") << QStringLiteral("record.parameterInput") << QJsonValue();
        QTest::newRow("missing-mode-draft") << QStringLiteral("normalInput") << QJsonValue();
        QTest::newRow("missing-expression") << QStringLiteral("record.expression") << QJsonValue();
    }
    void invalidSessionsLeaveDestinationUntouched()
    {
        QFETCH(QString, field);
        QFETCH(QJsonValue, replacement);
        CalculationSession original;
        original.history.calculate(QStringLiteral("42"));
        auto root = QJsonDocument::fromJson(SessionFormat::encode(original)).object();
        if (field.startsWith(QStringLiteral("record.")))
        {
            auto array = root.value(QStringLiteral("records")).toArray();
            auto record = array.first().toObject();
            record.insert(field.mid(7), replacement);
            array[0] = record;
            root.insert(QStringLiteral("records"), array);
        }
        else root.insert(field, replacement);
        CalculationSession destination;
        destination.history.calculate(QStringLiteral("7"));
        destination.input = {QStringLiteral("草稿"), 1, 0};
        const QByteArray before = SessionFormat::encode(destination);
        QVERIFY(!SessionFormat::decode(QJsonDocument(root).toJson(), destination).isEmpty());
        QCOMPARE(SessionFormat::encode(destination), before);
    }
    void truncatedAndOversizedSessionsAreRejected()
    {
        CalculationSession session;
        session.history.calculate(QStringLiteral("5"));
        const auto before = SessionFormat::encode(session);
        for (const QByteArray &invalid : {before.left(before.size() / 2), QByteArray("[]"), QByteArray(),
                                        QByteArray(SessionFormat::maximumBytes + 1, ' ')})
        {
            QVERIFY(!SessionFormat::decode(invalid, session).isEmpty());
            QCOMPARE(SessionFormat::encode(session), before);
        }
        auto root = QJsonDocument::fromJson(before).object();
        QJsonArray records;
        for (int i = 0; i <= SessionFormat::maximumRecords; ++i) records.append(QJsonObject());
        root.insert(QStringLiteral("records"), records);
        QVERIFY(!SessionFormat::decode(QJsonDocument(root).toJson(), session).isEmpty());
        QCOMPARE(SessionFormat::encode(session), before);
    }
    void sessionFilesLockAndResumeWithoutOverwriting()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("本地😀.calctabdd"));
        CalculationSession session;
        session.history.calculate(QStringLiteral("42"));
        {
            CalculationSessionFile first;
            QVERIFY(first.create(path, session).isEmpty());
            CalculationSessionFile other;
            CalculationSession destination;
            QVERIFY(!other.load(path, destination).isEmpty());
            QVERIFY(!other.create(path, destination).isEmpty());
            const QString alias = directory.path() + QStringLiteral("/./本地😀.calctabdd");
            QVERIFY(!other.load(alias, destination).isEmpty());
            session.history.calculate(QStringLiteral("ans+1"));
            QVERIFY(first.save(session).isEmpty());
        }
        CalculationSessionFile reopened;
        CalculationSession restored;
        QVERIFY(reopened.load(path, restored).isEmpty());
        QCOMPARE(SessionFormat::encode(restored), SessionFormat::encode(session));
        QVERIFY(reopened.save(restored).isEmpty());
        QVERIFY(QFileInfo::exists(path + QStringLiteral(".lock")));
    }
    void sessionFailureKeepsOldBytesAndCanRetryOrSaveElsewhere()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("session.calctabdd"));
        CalculationSession session;
        session.history.calculate(QStringLiteral("9"));
        CalculationSessionFile file;
        QVERIFY(file.create(path, session).isEmpty());
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto bytes = saved.readAll();
        saved.close();
        // 移走文件并用同名目录阻止提交，不依赖 root 权限或 Windows ACL。
        QVERIFY(QFile::rename(path, path + QStringLiteral(".bak")));
        QVERIFY(QDir().mkdir(path));
        session.history.calculate(QStringLiteral("ans+1"));
        QVERIFY(!file.save(session).isEmpty());
        QVERIFY(QDir().rmdir(path));
        QVERIFY(QFile::rename(path + QStringLiteral(".bak"), path));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), bytes);
        saved.close();
        QVERIFY(file.save(session).isEmpty());
        QVERIFY(saved.open(QIODevice::WriteOnly));
        saved.write("externally modified");
        saved.close();
        QVERIFY(!file.save(session).isEmpty());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArray("externally modified"));
        saved.close();
        CalculationSessionFile rescue;
        QVERIFY(rescue.create(directory.filePath(QStringLiteral("rescue.calctabdd")), session).isEmpty());
        QCOMPARE(session.history.count(), 2);
    }
    void existingOrDamagedSessionIsNeverTruncated()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("existing.calctabdd"));
        QFile original(path);
        QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("broken snapshot");
        original.close();
        CalculationSession session;
        session.history.calculate(QStringLiteral("42"));
        const auto before = SessionFormat::encode(session);
        CalculationSessionFile file;
        QVERIFY(!file.create(path, session).isEmpty());
        QVERIFY(!file.load(path, session).isEmpty());
        QCOMPARE(SessionFormat::encode(session), before);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("broken snapshot"));
        original.close();
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".lock")));
        QVERIFY(!file.create(directory.filePath(QStringLiteral("missing/file.calctabdd")), session).isEmpty());
        QVERIFY(file.create(directory.filePath(QStringLiteral("new.calctabdd")), session).isEmpty());
    }
    void emptyAndClearedSessionsResetAnswerAndNumbering()
    {
        CalculationSession original;
        original.history.calculate(QStringLiteral("-0"));
        original.history.calculate(QStringLiteral("1+"));
        CalculationSession restored;
        QVERIFY(SessionFormat::decode(SessionFormat::encode(original), restored).isEmpty());
        QCOMPARE(bits(restored.history.answer()), bits(-0.0));
        original.history.clear();
        original.input = {QStringLiteral("草稿😀"), 2, 0};
        QVERIFY(SessionFormat::decode(SessionFormat::encode(original), restored).isEmpty());
        QCOMPARE(restored.history.count(), 0);
        QCOMPARE(bits(restored.history.answer()), bits(0.0));
        QCOMPARE(restored.history.calculate(QStringLiteral("ans+1")).id, quint64(1));
        QCOMPARE(restored.history.answer(), 1.0);
    }
    void exportKeepsOrderAndHistoricalResults_data()
    {
        QTest::addColumn<bool>("markdown");
        QTest::newRow("txt") << false;
        QTest::newRow("markdown") << true;
    }
    void exportKeepsOrderAndHistoricalResults()
    {
        QFETCH(bool, markdown);
        CalculationHistory history;
        history.calculate(QStringLiteral("6*7"));
        history.calculate(QStringLiteral("ans+1"));
        history.calculate(QStringLiteral("1/0"));
        history.calculate(QStringLiteral("100"));
        const auto snapshot = history.records();
        const QByteArray bytes = CalculationExport::serialize(snapshot, markdown
            ? CalculationExport::Format::Markdown : CalculationExport::Format::Text);
        const QString text = QString::fromUtf8(bytes);
        QVERIFY(text.contains(QStringLiteral("6*7\n= 42")));
        QVERIFY(text.contains(QStringLiteral("ans+1\n= 43")));
        QVERIFY(text.contains(QStringLiteral("1/0\n无法计算：")));
        QVERIFY(text.contains(QStringLiteral("100\n= 100")));
        QVERIFY(text.indexOf(QStringLiteral("6*7\n= 42")) < text.indexOf(QStringLiteral("ans+1\n= 43")));
        QVERIFY(text.indexOf(QStringLiteral("ans+1\n= 43")) < text.indexOf(QStringLiteral("1/0\n无法计算：")));
        QVERIFY(text.indexOf(QStringLiteral("1/0\n无法计算：")) < text.indexOf(QStringLiteral("100\n= 100")));
        QVERIFY(text.contains(snapshot.at(2).result.text));
        QVERIFY(!text.contains(QStringLiteral("= 101"))); // 不按当前 ans 重算历史。
        QVERIFY(text.contains(markdown ? QStringLiteral("## 记录 04") : QStringLiteral("[04]")));
        QCOMPARE(history.count(), 4);
        QCOMPARE(history.answer(), 100.0);
        QCOMPARE(history.record(2)->result.value, 43.0);
        QCOMPARE(history.calculate(QStringLiteral("ans+1")).id, quint64(5));
        QCOMPARE(CalculationExport::serialize(snapshot, markdown
            ? CalculationExport::Format::Markdown : CalculationExport::Format::Text), bytes);
    }
    void exportKeepsUnicodeAndMarkdownLiteral_data()
    {
        QTest::addColumn<bool>("markdown");
        QTest::newRow("txt") << false;
        QTest::newRow("markdown") << true;
    }
    void exportKeepsUnicodeAndMarkdownLiteral()
    {
        QFETCH(bool, markdown);
        CalculationHistory history;
        const QString expression = QStringLiteral("2×3 ÷ 4 − 1\n中文😀\n```\n# 标题 | [链接](x) <b>& \\\n````");
        auto entry = history.calculate(expression);
        entry.result.text = QStringLiteral("参数错误：中文😀\n`````\n<img src=x> & | * _ `");
        const QByteArray bytes = CalculationExport::serialize({entry}, markdown
            ? CalculationExport::Format::Markdown : CalculationExport::Format::Text);
        const QString text = QString::fromUtf8(bytes);
        QVERIFY(text.contains(expression));
        QVERIFY(text.contains(entry.result.text));
        QVERIFY(!text.contains(QChar::ReplacementCharacter));
        QCOMPARE(text.toUtf8(), bytes);
        QVERIFY(!bytes.startsWith(QByteArray::fromHex("efbbbf")));
        QVERIFY(!bytes.contains('\r'));
        if (markdown)
        {
            QVERIFY(text.contains(QStringLiteral("\n``````text\n") + expression));
            QVERIFY(text.endsWith(QStringLiteral("\n``````\n\n")));
            QCOMPARE(text.count(QStringLiteral("``````")), 2);
        }
        else QVERIFY(text.contains(QStringLiteral("[01]\n") + expression + QStringLiteral("\n无法计算：")));
    }
    void exportedValuesRoundTrip_data()
    {
        QTest::addColumn<QString>("formula");
        QTest::addColumn<bool>("markdown");
        for (const bool markdown : {false, true})
        {
            for (const char *formula : {"0.1+0.2", "-0", "-2", "4.9406564584124654e-324", "1.7976931348623157e308"})
                QTest::newRow((QByteArray(markdown ? "md-" : "txt-") + formula).constData())
                    << QString::fromLatin1(formula) << markdown;
        }
    }
    void exportedValuesRoundTrip()
    {
        QFETCH(QString, formula);
        QFETCH(bool, markdown);
        RestoreLocale restore;
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        CalculationHistory history;
        const auto entry = history.calculate(formula);
        QVERIFY(entry.result.ok);
        const QString text = QString::fromUtf8(CalculationExport::serialize(history.records(), markdown
            ? CalculationExport::Format::Markdown : CalculationExport::Format::Text));
        const int start = text.indexOf(QStringLiteral("\n= "));
        QVERIFY(start >= 0);
        const QString number = text.mid(start + 3).section(QLatin1Char('\n'), 0, 0);
        const auto parsed = ExpressionEngine::evaluate(number);
        QVERIFY2(parsed.ok, qPrintable(number));
        QCOMPARE(bits(parsed.value), bits(entry.result.value));
        QCOMPARE(bits(history.answer()), bits(entry.result.value));
        if (formula == QStringLiteral("0.1+0.2")) QCOMPARE(number, QStringLiteral("0.30000000000000004"));
        if (formula == QStringLiteral("-0")) QCOMPARE(number, QStringLiteral("-0"));
    }
    void exportWritesUtf8AndReplacesCompleteFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("计算记录 测试.txt"));
        QFile original(path);
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write(QByteArray(2048, 'x')), qint64(2048));
        original.close();
        CalculationHistory history;
        history.calculate(QStringLiteral("1+中"));
        const QByteArray bytes = CalculationExport::serialize(history.records(), CalculationExport::Format::Text);
        const QString error = CalculationExport::writeFile(path, bytes);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), bytes);
        original.close();
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden), QStringList{QFileInfo(path).fileName()});
    }
    void exportFailurePreservesFilesAndCanRetry()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString oldPath = directory.filePath(QStringLiteral("原记录.txt"));
        QFile old(oldPath);
        QVERIFY(old.open(QIODevice::WriteOnly));
        const QByteArray original("previous saved calculation");
        QCOMPARE(old.write(original), qint64(original.size()));
        old.close();
        const QByteArray bytes("new export\n");
        QVERIFY(!CalculationExport::writeFile(oldPath + QStringLiteral("/cannot-create.txt"), bytes).isEmpty());
        QVERIFY(!CalculationExport::writeFile(directory.filePath(QStringLiteral("missing/result.txt")), bytes).isEmpty());
        QVERIFY(!CalculationExport::writeFile(directory.path(), bytes).isEmpty());
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), original);
        old.close();
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden), QStringList{QFileInfo(oldPath).fileName()});
        const QString error = CalculationExport::writeFile(oldPath, bytes);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(old.open(QIODevice::ReadOnly));
        QCOMPARE(old.readAll(), bytes);
    }
    void clearingHistoryResetsSessionAndPreservesOtherHistories()
    {
        CalculationHistory history;
        CalculationHistory other;
        other.calculate(QStringLiteral("100"));
        const auto first = history.calculate(QStringLiteral("6*7"));
        history.calculate(QStringLiteral("1/0"));
        const auto snapshot = history.records();
        history.clear();
        QCOMPARE(history.count(), 0);
        QVERIFY(history.records().isEmpty());
        QVERIFY(!history.record(first.id));
        QVERIFY(!history.record(2));
        QCOMPARE(bits(history.answer()), bits(0.0));
        QCOMPARE(snapshot.size(), 2);
        QCOMPARE(snapshot.first().result.value, 42.0);
        QCOMPARE(other.count(), 1);
        QCOMPARE(other.answer(), 100.0);
        const auto next = history.calculate(QStringLiteral("ans+1"));
        QCOMPARE(next.id, quint64(1));
        QCOMPARE(next.answerBefore, 0.0);
        QCOMPARE(next.result.value, 1.0);
        QVERIFY(!history.record(2));
        history.clear();
        history.clear();
        const auto error = history.calculate(QStringLiteral("1+"));
        QCOMPARE(error.id, quint64(1));
        QVERIFY(!error.result.ok);
        QCOMPARE(history.answer(), 0.0);
        history.clear();
        QCOMPARE(history.calculate(QStringLiteral("ans")).result.value, 0.0);
        history.calculate(QStringLiteral("-0"));
        QVERIFY(std::signbit(history.answer()));
        history.clear();
        QCOMPARE(bits(history.answer()), bits(0.0));
        QCOMPARE(other.calculate(QStringLiteral("ans+1")).result.value, 101.0);
    }
    void clearingEmptyHistoryIsSafe()
    {
        CalculationHistory history;
        history.clear();
        history.clear();
        QCOMPARE(history.count(), 0);
        QVERIFY(!history.record(1));
        QCOMPARE(history.answer(), 0.0);
        QCOMPARE(history.calculate(QStringLiteral("ans")).id, quint64(1));
    }
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
        QCOMPARE(error.result.errorPosition, 3);
        QCOMPARE(error.result.errorLength, 5);
        QCOMPARE(error.result.error, CalculationError::DivisionByZero);
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
            QCOMPARE(byId->result.errorLength, entry.result.errorLength);
            QCOMPARE(byId->result.error, entry.result.error);
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
    void reusableValueBoundaries_data()
    {
        QTest::addColumn<double>("value");
        QTest::newRow("positive-zero") << 0.0;
        QTest::newRow("negative-zero") << -0.0;
        QTest::newRow("decimal-residue") << (0.1 + 0.2);
        QTest::newRow("third") << (1.0 / 3.0);
        QTest::newRow("negative-root") << -std::sqrt(2.0);
        QTest::newRow("integer-boundary") << 9007199254740992.0;
        QTest::newRow("next-large-integer") << 9007199254740994.0;
        QTest::newRow("minimum-normal") << std::numeric_limits<double>::min();
        QTest::newRow("maximum-subnormal") << std::nextafter(std::numeric_limits<double>::min(), 0.0);
        QTest::newRow("minimum-subnormal") << std::numeric_limits<double>::denorm_min();
        QTest::newRow("negative-subnormal") << -std::numeric_limits<double>::denorm_min();
        QTest::newRow("maximum-finite") << std::numeric_limits<double>::max();
        QTest::newRow("negative-maximum") << -std::numeric_limits<double>::max();
        QTest::newRow("next-after-one") << std::nextafter(1.0, 2.0);
        QTest::newRow("next-before-one") << std::nextafter(1.0, 0.0);
    }
    void reusableValueBoundaries()
    {
        QFETCH(double, value);
        RestoreLocale restore;
        QLocale::setDefault(QLocale(QLocale::German));
        CalculationRecord entry;
        entry.result.ok = true;
        entry.result.value = value;
        entry.result.text = QStringLiteral("显示文本不能用于数值复用");
        const QString text = entry.valueText();
        QVERIFY(!text.isEmpty());
        QVERIFY(!text.contains(QLatin1Char('=')));
        QVERIFY(!text.contains(QLatin1Char(',')));
        const auto parsed = ExpressionEngine::evaluate(text);
        QVERIFY2(parsed.ok, qPrintable(text + QStringLiteral(": ") + parsed.text));
        QCOMPARE(bits(parsed.value), bits(value));
        const auto inserted = ExpressionEngine::evaluate(entry.insertionText());
        QVERIFY(inserted.ok);
        QCOMPARE(bits(inserted.value), bits(value));
    }
    void reusableValuesAcrossBinaryExponents()
    {
        // 遍历指数边界，再覆盖确定性随机位模式；逐位相等包括负零。
        std::mt19937_64 random(0xCA1C7ABD);
        for (int index = 0; index < 6144; ++index)
        {
            const quint64 raw = index < 4096
                ? (quint64(index / 2) << 52) | (index % 2 ? Q_UINT64_C(0x000fffffffffffff) : 0)
                : random();
            double value;
            std::memcpy(&value, &raw, sizeof(value));
            if (!std::isfinite(value)) continue;
            CalculationRecord entry;
            entry.result.ok = true;
            entry.result.value = value;
            const auto parsed = ExpressionEngine::evaluate(entry.valueText());
            QVERIFY2(parsed.ok, qPrintable(entry.valueText()));
            QCOMPARE(bits(parsed.value), raw);
        }
    }
    void recordCopyFormatsAndFixedValues()
    {
        CalculationHistory history;
        const auto first = history.calculate(QStringLiteral("0.1+0.2"));
        QCOMPARE(first.result.text, QStringLiteral("0.3"));
        QCOMPARE(first.valueText(), QStringLiteral("0.30000000000000004"));
        QCOMPARE(first.calculationText(), QStringLiteral("0.1+0.2\n= 0.30000000000000004"));
        const auto second = history.calculate(QStringLiteral("ans*2"));
        history.calculate(QStringLiteral("100"));
        QCOMPARE(history.record(second.id)->calculationText(), second.calculationText());
        const auto fixed = ExpressionEngine::evaluate(second.insertionText(), history.answer());
        QVERIFY(fixed.ok);
        QCOMPARE(bits(fixed.value), bits(second.result.value));
        QCOMPARE(history.answer(), 100.0);
        const auto error = history.calculate(QStringLiteral("1+中"));
        QVERIFY(!error.result.ok);
        QVERIFY(error.valueText().isEmpty());
        QVERIFY(error.insertionText().isEmpty());
        QCOMPARE(error.calculationText(), QStringLiteral("1+中\n无法计算：") + error.result.text);
        QCOMPARE(history.answer(), 100.0);
        const auto negative = history.calculate(QStringLiteral("-2"));
        QCOMPARE(negative.valueText(), QStringLiteral("-2"));
        QCOMPARE(negative.insertionText(), QStringLiteral("(-2)"));
        QCOMPARE(ExpressionEngine::evaluate(negative.insertionText() + QStringLiteral("^2")).value, 4.0);
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
        QVERIFY(result.error != CalculationError::None);
        QVERIFY(result.errorPosition >= 0);
        QVERIFY(result.errorLength >= 0);
        QVERIFY(result.errorPosition + result.errorLength <= formula.size());
    }
    void diagnosticRanges_data()
    {
        QTest::addColumn<QString>("formula");
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("position");
        QTest::addColumn<int>("length");
        QTest::addColumn<QString>("message");
        const auto row = [](const char *name, const QString &text, CalculationError kind,
                            int position, int length, const QString &message) {
            QTest::newRow(name) << text << int(kind) << position << length << message;
        };
        row("missing-parenthesis", QStringLiteral("(2+3"), CalculationError::Syntax, 4, 0, QStringLiteral("缺少右括号"));
        row("missing-function-parenthesis", QStringLiteral("sqrt(9"), CalculationError::Syntax, 6, 0, QStringLiteral("缺少右括号"));
        row("nested-parenthesis", QStringLiteral("max(1, sqrt(4)"), CalculationError::Syntax, 14, 0, QStringLiteral("缺少右括号"));
        row("extra-parenthesis", QStringLiteral("2+3)"), CalculationError::Syntax, 3, 1, QStringLiteral("多余字符"));
        row("missing-left-parenthesis", QStringLiteral("sqrt 9"), CalculationError::Syntax, 5, 1, QStringLiteral("缺少左括号"));
        row("missing-value", QStringLiteral("1+"), CalculationError::Syntax, 2, 0, QStringLiteral("需要数字"));
        row("illegal-character", QStringLiteral("2+$3"), CalculationError::Syntax, 2, 1, QStringLiteral("需要数字"));
        row("surrogate-pair", QStringLiteral("2+😀"), CalculationError::Syntax, 2, 2, QStringLiteral("需要数字"));
        row("whitespace-and-unicode-operator", QStringLiteral(" \n 2 × $3  "), CalculationError::Syntax, 7, 1, QStringLiteral("需要数字"));
        row("unknown-constant", QStringLiteral("1+未知"), CalculationError::UnknownName, 2, 2, QStringLiteral("未知函数或常量"));
        row("unknown-empty-function", QStringLiteral("what()"), CalculationError::UnknownName, 0, 4, QStringLiteral("未知函数或常量"));
        row("unknown-multiple-arguments", QStringLiteral("what(1,2,3)"), CalculationError::UnknownName, 0, 4, QStringLiteral("未知函数或常量"));
        row("decimal-without-digits", QStringLiteral("1+."), CalculationError::Syntax, 2, 1, QStringLiteral("需要数字"));
        row("missing-exponent", QStringLiteral("1e+"), CalculationError::Syntax, 1, 2, QStringLiteral("缺少指数"));
        row("empty-arguments", QStringLiteral("sqrt()"), CalculationError::ArgumentCount, 5, 1, QStringLiteral("需要 1 个参数"));
        row("too-few-arguments", QStringLiteral("min(1)"), CalculationError::ArgumentCount, 5, 1, QStringLiteral("需要 2 个参数"));
        row("too-many-arguments", QStringLiteral("sqrt(1,2)"), CalculationError::ArgumentCount, 7, 1, QStringLiteral("参数过多"));
        row("third-argument", QStringLiteral("max(1,2,3)"), CalculationError::ArgumentCount, 8, 1, QStringLiteral("参数过多"));
        row("missing-first-argument", QStringLiteral("pow(,2)"), CalculationError::ArgumentCount, 4, 1, QStringLiteral("缺少第 1 个参数"));
        row("missing-last-argument", QStringLiteral("pow(2, )"), CalculationError::ArgumentCount, 7, 1, QStringLiteral("缺少第 2 个参数"));
        row("incomplete-argument", QStringLiteral("max(2, "), CalculationError::ArgumentCount, 7, 0, QStringLiteral("缺少第 2 个参数"));
        row("division", QStringLiteral("12/(3-3)"), CalculationError::DivisionByZero, 3, 5, QStringLiteral("除数不能为 0"));
        row("spaced-division", QStringLiteral("1 ÷ (2 − 2)   + 4"), CalculationError::DivisionByZero, 4, 7, QStringLiteral("除数不能为 0"));
        row("remainder-zero", QStringLiteral("2 % -0"), CalculationError::DivisionByZero, 4, 2, QStringLiteral("取余的除数"));
        row("ln-zero", QStringLiteral("ln(0)"), CalculationError::Domain, 3, 1, QStringLiteral("参数必须大于 0"));
        row("ln-negative-zero", QStringLiteral("LN(-0)"), CalculationError::Domain, 3, 2, QStringLiteral("参数必须大于 0"));
        row("log-negative", QStringLiteral("log(-2)"), CalculationError::Domain, 4, 2, QStringLiteral("参数必须大于 0"));
        row("nested-domain", QStringLiteral("1+ln( 2-2 )"), CalculationError::Domain, 6, 3, QStringLiteral("没有实数对数"));
        row("sqrt-negative", QStringLiteral("sqrt(-1)"), CalculationError::Domain, 5, 2, QStringLiteral("大于或等于 0"));
        row("power-domain", QStringLiteral("(-8)^(1/3)"), CalculationError::Domain, 5, 5, QStringLiteral("指数必须为整数"));
        row("pow-domain", QStringLiteral("pow(-8,1/3)"), CalculationError::Domain, 7, 3, QStringLiteral("指数必须为整数"));
        row("zero-negative-power", QStringLiteral("0^-1"), CalculationError::Domain, 2, 2, QStringLiteral("指数不能为负数"));
        row("zero-negative-pow", QStringLiteral("pow(0,-2)"), CalculationError::Domain, 6, 2, QStringLiteral("指数不能为负数"));
        row("literal-overflow", QStringLiteral("1e309"), CalculationError::Overflow, 0, 5, QStringLiteral("字面量过大"));
        row("literal-underflow", QStringLiteral("1e-400"), CalculationError::Underflow, 0, 6, QStringLiteral("字面量过小"));
        row("literal-near-zero", QStringLiteral("1e-324"), CalculationError::Underflow, 0, 6, QStringLiteral("下溢"));
        row("arithmetic-overflow", QStringLiteral("1e308 * 2"), CalculationError::Overflow, 6, 1, QStringLiteral("数值溢出"));
        row("addition-overflow", QStringLiteral("1e308+1e308"), CalculationError::Overflow, 5, 1, QStringLiteral("数值溢出"));
        row("power-overflow", QStringLiteral("2^1024"), CalculationError::Overflow, 1, 1, QStringLiteral("数值溢出"));
        row("function-overflow", QStringLiteral("1+exp(1000)"), CalculationError::Overflow, 2, 3, QStringLiteral("数值溢出"));
        row("input-limit", QString(4097, QLatin1Char('1')), CalculationError::Limit, 4096, 1, QStringLiteral("公式过长"));
        row("surrogate-at-limit", QString(4095, QLatin1Char('1')) + QStringLiteral("😀"), CalculationError::Limit, 4095, 2, QStringLiteral("公式过长"));
        row("nesting-limit", QString(129, QLatin1Char('-')) + QLatin1Char('1'), CalculationError::Limit, 128, 1, QStringLiteral("嵌套过深"));
    }
    void diagnosticRanges()
    {
        QFETCH(QString, formula);
        QFETCH(int, kind);
        QFETCH(int, position);
        QFETCH(int, length);
        QFETCH(QString, message);
        CalculationHistory history;
        history.calculate(QStringLiteral("42"));
        const auto entry = history.calculate(formula);
        QVERIFY(!entry.result.ok);
        QCOMPARE(int(entry.result.error), kind);
        QCOMPARE(entry.result.errorPosition, position);
        QCOMPARE(entry.result.errorLength, length);
        QVERIFY2(entry.result.text.contains(message), qPrintable(entry.result.text));
        QCOMPARE(entry.expression, formula);
        QCOMPARE(history.answer(), 42.0);
        QCOMPARE(entry.answerBefore, 42.0);
        const auto next = history.calculate(QStringLiteral("ans+1"));
        QCOMPARE(next.result.value, 43.0);
        QCOMPARE(next.result.error, CalculationError::None);
        QCOMPARE(next.result.errorPosition, -1);
        QCOMPARE(next.result.errorLength, 0);
        QCOMPARE(history.record(entry.id)->result.errorLength, length);
        // 失败不能把负零 ans 的符号改成正零。
        history.calculate(QStringLiteral("-0"));
        history.calculate(formula);
        QCOMPARE(bits(history.answer()), bits(-0.0));
    }
    void numericBoundaries_data()
    {
        QTest::addColumn<QString>("formula");
        QTest::addColumn<double>("expected");
        const QVector<QPair<QString, double>> cases = {
            {QStringLiteral("9007199254740991"), 9007199254740991.0},
            {QStringLiteral("9007199254740993"), 9007199254740992.0},
            {QStringLiteral("9007199254740994-9007199254740992"), 2.0},
            {QStringLiteral("(2^53+1)-2^53"), 0.0},
            {QStringLiteral("-9007199254740993"), -9007199254740992.0},
            {QStringLiteral("(1e16+1)-1e16"), 0.0},
            {QStringLiteral("(1e16-1e16)+1"), 1.0},
            {QStringLiteral("(0.1+0.2)-0.3"), 5.5511151231257827021181583404541015625e-17},
            {QStringLiteral("5.5%2"), 1.5},
            {QStringLiteral("-5.5%2"), -1.5},
            {QStringLiteral("5.5%-2"), 1.5},
            {QStringLiteral("-5.5%-2"), -1.5},
            {QStringLiteral("-4%2"), -0.0},
            {QStringLiteral("0.3%0.1"), 0.09999999999999998},
            {QStringLiteral("round(1.005*100)/100"), 1.0},
            {QStringLiteral("1e-200*1e-200"), 0.0},
            {QStringLiteral("-1e-200*1e-200"), -0.0},
            {QStringLiteral("2.2250738585072014e-308/2"), std::numeric_limits<double>::min() / 2},
            {QStringLiteral("4.9406564584124654e-324"), std::numeric_limits<double>::denorm_min()},
            {QStringLiteral("4.9406564584124654e-324/2"), 0.0},
            {QStringLiteral("-4.9406564584124654e-324/2"), -0.0},
            {QStringLiteral("exp(-1000)"), 0.0},
            {QStringLiteral("0^0"), 1.0},
            {QStringLiteral("pow(0,0)"), 1.0},
            {QStringLiteral("(-2)^-3"), -0.125},
            {QStringLiteral("sqrt(-0)"), -0.0}
        };
        for (const auto &entry : cases) QTest::newRow(qPrintable(entry.first)) << entry.first << entry.second;
    }
    void numericBoundaries()
    {
        QFETCH(QString, formula);
        QFETCH(double, expected);
        CalculationHistory history;
        const auto entry = history.calculate(formula);
        QVERIFY2(entry.result.ok, qPrintable(entry.result.text));
        // 精确逐位比较；微小值和有符号零不能被统一绝对误差容限掩盖。
        QCOMPARE(bits(entry.result.value), bits(expected));
        QCOMPARE(bits(history.answer()), bits(expected));
        const auto parsed = ExpressionEngine::evaluate(entry.valueText());
        const auto inserted = ExpressionEngine::evaluate(entry.insertionText());
        QVERIFY(parsed.ok && inserted.ok);
        QCOMPARE(bits(parsed.value), bits(expected));
        QCOMPARE(bits(inserted.value), bits(expected));
    }
    void nonFiniteAnswerIsRejectedAtItsToken()
    {
        for (double value : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        {
            const auto result = ExpressionEngine::evaluate(QStringLiteral("1+ans"), value);
            QVERIFY(!result.ok);
            QCOMPARE(result.errorPosition, 2);
            QCOMPARE(result.errorLength, 3);
        }
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
int main(int argc, char **argv)
{
    // 独立子进程持有真实文件锁；同时验证普通退出和进程崩溃后的快照恢复。
    if (argc == 3 && QByteArray(argv[1]) == QByteArray("--hold-session"))
    {
        QCoreApplication app(argc, argv);
        CalculationSession session;
        session.history.calculate(QStringLiteral("42"));
        session.history.calculate(QStringLiteral("1/0"));
        session.input = {QStringLiteral("跨进程草稿😀"), 2, 0};
        CalculationSessionFile file;
        if (!file.create(QString::fromLocal8Bit(argv[2]), session).isEmpty()) return 2;
        std::fputs("LOCKED\n", stdout);
        std::fflush(stdout);
        char buffer[16];
        return std::fgets(buffer, sizeof(buffer), stdin) ? 0 : 3;
    }
    QApplication app(argc, argv);
    EngineTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "engine_tests.moc"
