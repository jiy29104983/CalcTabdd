#include "calculator_page.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QInputMethodEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QtTest>

class PageTests : public QObject
{
    Q_OBJECT
private slots:
    void initialState()
    {
        CalculatorPage page;
        QCOMPARE(page.recordCount(), 0);
        QVERIFY(page.input()->toPlainText().isEmpty());
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("emptyHistory")));
    }
    void keyboardAndReuse()
    {
        CalculatorPage page;
        page.resize(900, 620);
        page.show();
        page.focusInput();
        QTest::keyClicks(page.input(), "1+2*3");
        QTest::keyClick(page.input(), Qt::Key_Return);
        QCOMPARE(page.recordCount(), 0);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+2*3"));
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.recordCount(), 1);
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("recordResult"))->text(), QStringLiteral("= 7"));
        QVERIFY(page.input()->toPlainText().isEmpty());
        QTest::mouseClick(page.findChild<QPushButton *>(QStringLiteral("reuseFormula")), Qt::LeftButton);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+2*3"));
        QTest::keyClick(page.input(), Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier);
        QCOMPARE(page.recordCount(), 2);
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.recordCount(), 2);
    }
    void errorsPreserveInputAndAnswer()
    {
        CalculatorPage page;
        page.input()->setPlainText(QStringLiteral("6*7"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("12/(3-3)"));
        page.submit();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("12/(3-3)"));
        const auto results = page.findChildren<QLabel *>(QStringLiteral("recordResult"));
        QVERIFY(results.last()->text().contains(QStringLiteral("除数不能为 0")));
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.submit();
        QCOMPARE(page.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 43"));
    }
    void stackedLayoutAndScrolling()
    {
        CalculatorPage page;
        page.resize(860, 620);
        page.show();
        for (int index = 0; index < 25; ++index)
        {
            page.input()->setPlainText(QStringLiteral("%1*2").arg(index));
            page.submit();
        }
        QTest::qWait(50);
        const auto *formula = page.findChild<QLabel *>(QStringLiteral("recordFormula"));
        const auto *result = page.findChild<QLabel *>(QStringLiteral("recordResult"));
        QVERIFY(result->mapTo(&page, QPoint()).y() > formula->mapTo(&page, QPoint()).y());
        auto *scroll = page.findChild<QScrollArea *>();
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QTRY_COMPARE(scroll->verticalScrollBar()->value(), scroll->verticalScrollBar()->maximum());
        page.resize(480, 520);
        QTest::qWait(30);
        QVERIFY(page.input()->isVisible());
        QVERIFY(page.input()->width() > 100);
    }
    void clipboardAndUndo()
    {
        CalculatorPage page;
        page.input()->setPlainText(QStringLiteral("1234"));
        page.routeEdit(QStringLiteral("actionselect_All"));
        page.routeEdit(QStringLiteral("actioncopy"));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("1234"));
        page.routeEdit(QStringLiteral("actioncut"));
        QVERIFY(page.input()->toPlainText().isEmpty());
        page.routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1234"));
        page.input()->clear();
        page.routeEdit(QStringLiteral("actionpaste"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1234"));
    }
    void copyUsesFocusedSelection()
    {
        CalculatorPage page;
        page.resize(800, 600);
        page.show();
        page.activateWindow();
        page.input()->setPlainText(QStringLiteral("6*7"));
        page.submit();
        auto *label = page.findChild<QLabel *>(QStringLiteral("recordResult"));
        label->setFocus();
        QTRY_VERIFY(label->hasFocus());
        label->setSelection(2, 2);
        page.routeEdit(QStringLiteral("actioncopy"));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("42"));
        page.focusInput();
        page.input()->setPlainText(QStringLiteral("99"));
        page.input()->selectAll();
        page.routeEdit(QStringLiteral("actioncopy"));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("99"));
    }
    void compositionDoesNotSubmit()
    {
        CalculatorPage page;
        page.input()->setPlainText(QStringLiteral("1+2"));
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(page.input(), &preedit);
        page.submit();
        QCOMPARE(page.recordCount(), 0);
        QInputMethodEvent finish;
        QApplication::sendEvent(page.input(), &finish);
        page.submit();
        QCOMPARE(page.recordCount(), 1);
    }
    void textIsNotHtml()
    {
        CalculatorPage page;
        page.input()->setPlainText(QStringLiteral("<b>1</b>"));
        page.submit();
        auto *formula = page.findChild<QLabel *>(QStringLiteral("recordFormula"));
        QCOMPARE(formula->textFormat(), Qt::PlainText);
        QCOMPARE(formula->text(), QStringLiteral("<b>1</b>"));
    }
    void paletteAndScreenshot()
    {
        CalculatorPage page;
        page.resize(960, 660);
        page.show();
        for (const auto &formula : {QStringLiteral("128+256"), QStringLiteral("(1299+899)*0.85"), QStringLiteral("(384-128)/2"), QStringLiteral("12/(3-3)")})
        {
            page.input()->setPlainText(formula);
            page.submit();
        }
        page.input()->setPlainText(QStringLiteral("(128+256)/3"));
        QTest::qWait(30);
        const QString directory = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        if (!directory.isEmpty())
        {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(page.grab().save(directory + QStringLiteral("/calculator-light.png")));
        }
        QPalette dark = page.palette();
        dark.setColor(QPalette::Window, QColor("#202329"));
        dark.setColor(QPalette::Base, QColor("#202329"));
        dark.setColor(QPalette::Text, QColor("#e4e8ef"));
        dark.setColor(QPalette::WindowText, QColor("#e4e8ef"));
        page.setPalette(dark);
        QTest::qWait(30);
        QVERIFY(page.styleSheet().contains(QStringLiteral("#f2a49a")));
        if (!directory.isEmpty()) QVERIFY(page.grab().save(directory + QStringLiteral("/calculator-dark.png")));
    }
};
QTEST_MAIN(PageTests)
#include "page_tests.moc"
