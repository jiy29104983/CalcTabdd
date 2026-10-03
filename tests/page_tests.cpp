#include "calculator_page.h"
#include "calculator_help.h"
#include "calculation_catalog.h"
#include <QAbstractItemView>
#include <QCompleter>
#include <QDialog>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>
#include <QTimer>
#include <QTabWidget>
#include <QTextBrowser>
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
    QAbstractItemView *completion(CalculatorPage &page)
    {
        return page.findChild<QCompleter *>()->popup();
    }
    bool chooseRecordAction(QToolButton *button, QAction *action)
    {
        bool opened = false;
        QTimer::singleShot(20, button, [&]() {
            opened = button->menu()->isVisible();
            QTest::mouseClick(button->menu(), Qt::LeftButton, Qt::NoModifier,
                             button->menu()->actionGeometry(action).center());
            button->menu()->close();
        });
        QTest::mouseClick(button, Qt::LeftButton);
        return opened;
    }
    void prepare(CalculatorPage &page, const QString &text)
    {
        page.resize(960, 660);
        page.show();
        page.activateWindow();
        page.focusInput();
        QCoreApplication::processEvents();
        page.input()->setPlainText(text);
        page.input()->moveCursor(QTextCursor::End);
    }
private slots:
    void historyRecallRestoresDraft_data()
    {
        QTest::addColumn<QString>("draft");
        QTest::addColumn<int>("anchor");
        QTest::addColumn<int>("position");
        QTest::newRow("empty") << QString() << 0 << 0;
        QTest::newRow("cursor-in-middle") << QStringLiteral("12+34") << 2 << 2;
        QTest::newRow("reverse-selection") << QStringLiteral("  草稿😀\n 12 + 34\t ") << 12 << 2;
        QTest::newRow("long-multiline") << QString(4200, QLatin1Char('1')) + QStringLiteral("\n尾部 ") << 1 << 4204;
    }
    void historyRecallRestoresDraft()
    {
        QFETCH(QString, draft);
        QFETCH(int, anchor);
        QFETCH(int, position);
        CalculatorPage page;
        prepare(page, QString());
        for (const auto &formula : {QStringLiteral("11"), QStringLiteral("1+"), QStringLiteral("ans+1")})
        {
            page.input()->setPlainText(formula);
            page.submit();
        }
        page.input()->setPlainText(draft);
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(anchor);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), draft);
        QCOMPARE(page.input()->textCursor().position(), position);
        QCOMPARE(page.input()->textCursor().anchor(), anchor);
        for (const auto &formula : {QStringLiteral("ans+1"), QStringLiteral("1+"), QStringLiteral("11"), QStringLiteral("11")})
        {
            QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
            QCOMPARE(page.input()->toPlainText(), formula);
        }
        for (const auto &formula : {QStringLiteral("1+"), QStringLiteral("ans+1"), draft, draft})
        {
            QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
            QCOMPARE(page.input()->toPlainText(), formula);
        }
        QCOMPARE(page.input()->textCursor().position(), position);
        QCOMPARE(page.input()->textCursor().anchor(), anchor);
        QCOMPARE(page.recordCount(), 3);
        QCOMPARE(page.history().answer(), 12.0);
        // 返回草稿后重新开始浏览，应保存此时的新草稿，而非上一轮旧快照。
        page.input()->insertPlainText(QStringLiteral("更新"));
        const QString edited = page.input()->toPlainText();
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), edited);
    }
    void historyEditsSurviveBrowsing()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("1"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("2"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("8+"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClicks(page.input(), "+3");
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(3);
        cursor.setPosition(1, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClicks(page.input(), "+7");
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+7"));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("2+3"));
        QCOMPARE(page.input()->textCursor().position(), 1);
        QCOMPARE(page.input()->textCursor().anchor(), 3);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("8+"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("2"));
        QCOMPARE(page.history().record(1)->expression, QStringLiteral("1"));
        QCOMPARE(page.history().record(2)->expression, QStringLiteral("2"));
        QCOMPARE(page.recordCount(), 2);
        QCOMPARE(page.history().answer(), 2.0);
    }
    void recalledSubmissionUsesCurrentAnswerAndReturnsDraft()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("6*7"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("未提交\n 100+ "));
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(5);
        cursor.setPosition(2, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClick(page.input(), Qt::Key_Return);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ans+1"));
        QCOMPARE(page.recordCount(), 2);
        QTest::keyClick(page.input(), Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier);
        QCOMPARE(page.history().records().last().answerBefore, 43.0);
        QCOMPARE(page.history().answer(), 44.0);
        QCOMPARE(page.history().record(2)->result.value, 43.0);
        QCOMPARE(page.recordCount(), 3);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("未提交\n 100+ "));
        QCOMPARE(page.input()->textCursor().position(), 2);
        QCOMPARE(page.input()->textCursor().anchor(), 5);
        page.input()->setPlainText(QStringLiteral("2+3"));
        page.submit();
        QVERIFY(page.input()->toPlainText().isEmpty());
    }
    void failedRecallPreservesDraftUntilCorrection()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("1+"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("7*8"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.recordCount(), 2);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+"));
        QCOMPARE(page.input()->textCursor().position(), 2);
        QCOMPARE(page.history().answer(), 0.0);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+"));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("7*8"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClicks(page.input(), "2");
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.history().answer(), 3.0);
        QCOMPARE(page.recordCount(), 3);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("7*8"));
        QVERIFY(!page.history().record(1)->result.ok);
        QVERIFY(!page.history().record(2)->result.ok);
    }
    void rejectedRecallSubmissionsKeepSavedDraft()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("draft"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        page.input()->setPlainText(QStringLiteral(" \n "));
        page.submit();
        QCOMPARE(page.recordCount(), 1);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("draft"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        page.input()->setPlainText(QStringLiteral("@sq"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.recordCount(), 1);
        QTest::keyClick(page.input(), Qt::Key_Escape);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("draft"));
    }
    void historyKeysYieldToCompletionAndComposition()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("@p"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(completion(page), Qt::Key_Down);
        QCOMPARE(completion(page)->currentIndex().row(), 1);
        for (QWidget *target : {static_cast<QWidget *>(page.input()), static_cast<QWidget *>(completion(page))})
        {
            QTest::keyClick(target, Qt::Key_Up, Qt::AltModifier);
            QTest::keyClick(target, Qt::Key_Down, Qt::AltModifier);
            QCOMPARE(page.input()->toPlainText(), QStringLiteral("@p"));
            QVERIFY(completion(page)->isVisible());
            QCOMPARE(completion(page)->currentIndex().row(), 1);
        }
        QTest::keyClick(page.input(), Qt::Key_Escape);
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("42"));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("@p"));
        page.input()->setPlainText(QStringLiteral("9+"));
        page.input()->moveCursor(QTextCursor::End);
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(page.input(), &preedit);
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("9+"));
        QCOMPARE(page.recordCount(), 1);
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("1"));
        QApplication::sendEvent(page.input(), &commit);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("9+1"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("42"));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("9+1"));
    }
    void ordinaryArrowsAndEmptyHistoryKeepEditing()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("123\n456"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("123\n456"));
        QCOMPARE(page.input()->textCursor().position(), 7);
        page.input()->setPlainText(QStringLiteral("42"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("123\n456"));
        page.input()->moveCursor(QTextCursor::End);
        QTest::keyClick(page.input(), Qt::Key_Up);
        QCOMPARE(page.input()->textCursor().position(), 3);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::ShiftModifier);
        QVERIFY(page.input()->textCursor().hasSelection());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("123\n456"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier);
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier | Qt::ControlModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("123\n456"));
        QCOMPARE(page.recordCount(), 1);
    }
    void reuseAndResultInsertionPreserveNavigationDraft()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("4"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("-2"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("99*2"));
        page.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).first()->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("4"));
        page.input()->selectAll();
        page.findChildren<QAction *>(QStringLiteral("insertResult")).last()->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("(-2)"));
        page.routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("4"));
        page.routeEdit(QStringLiteral("actionredo"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("(-2)"));
        page.findChildren<QAction *>(QStringLiteral("copyValue")).first()->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("4"));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("-2"));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("99*2"));
        QCOMPARE(page.history().answer(), -2.0);
        QCOMPARE(page.recordCount(), 2);
    }
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
    void historyMatchesRenderedRecords()
    {
        CalculatorPage page;
        for (const auto &formula : {QStringLiteral(" 2 × 3 "), QStringLiteral("1+"),
                                   QStringLiteral("ans+1"), QStringLiteral("0.1+0.2")})
        {
            page.input()->setPlainText(formula);
            page.submit();
        }
        const auto &entries = page.history().records();
        const auto formulas = page.findChildren<QLabel *>(QStringLiteral("recordFormula"));
        const auto results = page.findChildren<QLabel *>(QStringLiteral("recordResult"));
        const auto numbers = page.findChildren<QLabel *>(QStringLiteral("recordNumber"));
        QCOMPARE(page.recordCount(), 4);
        QCOMPARE(entries.size(), page.recordCount());
        QCOMPARE(formulas.size(), entries.size());
        QCOMPARE(results.size(), entries.size());
        QCOMPARE(numbers.size(), entries.size());
        QCOMPARE(entries.first().expression, QStringLiteral("2 × 3"));
        for (int index = 0; index < entries.size(); ++index)
        {
            const auto &entry = entries.at(index);
            QCOMPARE(numbers.at(index)->text(), QStringLiteral("%1").arg(entry.id, 2, 10, QLatin1Char('0')));
            QCOMPARE(formulas.at(index)->text(), entry.expression);
            QCOMPARE(results.at(index)->text(), entry.result.ok
                ? QStringLiteral("= %1").arg(entry.result.text)
                : QStringLiteral("无法计算：%1").arg(entry.result.text));
            QCOMPARE(results.at(index)->property("error").toBool(), !entry.result.ok);
        }
        QVERIFY(!entries.at(1).result.ok);
        QCOMPARE(entries.at(1).answerBefore, 6.0);
        QCOMPARE(entries.at(1).result.errorPosition, 2);
        QCOMPARE(entries.at(2).answerBefore, 6.0);
        QCOMPARE(entries.at(2).result.value, 7.0);
        QVERIFY(entries.last().result.value != entries.last().result.text.toDouble());
    }
    void reuseReadsModelAndUsesCurrentAnswer()
    {
        CalculatorPage page;
        for (const auto &formula : {QStringLiteral("6*7"), QStringLiteral("ans+1"), QStringLiteral("1+")})
        {
            page.input()->setPlainText(formula);
            page.submit();
        }
        auto *reuse = page.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).at(1);
        auto *retry = page.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).at(2);
        // 追加足够多次后再点击旧按钮，检验回调没有持有失效的记录引用。
        for (int index = 0; index < 64; ++index)
        {
            page.input()->setPlainText(QString::number(index));
            page.submit();
        }
        page.input()->setPlainText(QStringLiteral("100"));
        page.submit();
        page.findChildren<QLabel *>(QStringLiteral("recordFormula")).at(1)->setText(QStringLiteral("999"));
        page.findChildren<QLabel *>(QStringLiteral("recordResult")).at(1)->setText(QStringLiteral("= 999"));
        reuse->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ans+1"));
        QCOMPARE(page.history().answer(), 100.0);
        const int count = page.recordCount();
        page.submit();
        QCOMPARE(page.recordCount(), count + 1);
        QCOMPARE(page.history().records().last().answerBefore, 100.0);
        QCOMPARE(page.history().records().last().result.value, 101.0);
        QCOMPARE(page.history().record(2)->answerBefore, 42.0);
        QCOMPARE(page.history().record(2)->result.value, 43.0);
        retry->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+"));
        page.input()->insertPlainText(QStringLiteral("2"));
        page.submit();
        QCOMPARE(page.history().records().last().result.value, 3.0);
        QVERIFY(!page.history().record(3)->result.ok);
    }
    void rejectedSubmissionsDoNotConsumeIds()
    {
        CalculatorPage page;
        page.input()->setPlainText(QStringLiteral(" \n "));
        page.submit();
        page.input()->setPlainText(QStringLiteral("@sq"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("ans+1"));
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(page.input(), &preedit);
        page.submit();
        QCOMPARE(page.history().count(), 0);
        QCOMPARE(page.history().answer(), 0.0);
        QInputMethodEvent finish;
        QApplication::sendEvent(page.input(), &finish);
        page.submit();
        QCOMPARE(page.history().records().first().id, quint64(1));
        QCOMPARE(page.history().answer(), 1.0);
        page.input()->setPlainText(QStringLiteral("1+"));
        page.submit();
        QCOMPARE(page.history().records().last().id, quint64(2));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+"));
        QCOMPARE(page.input()->textCursor().position(), 2);
        QCOMPARE(page.history().answer(), 1.0);
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
    void recordMenuCopiesInternalValues()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("0.1+0.2"));
        page.submit();
        auto *more = page.findChild<QToolButton *>(QStringLiteral("recordActions"));
        QVERIFY(more);
        QCOMPARE(more->menu()->actions().size(), 3);
        auto *copy = more->findChild<QAction *>(QStringLiteral("copyValue"));
        QVERIFY(copy);
        page.input()->setPlainText(QStringLiteral("未提交草稿"));
        page.input()->selectAll();
        const QTextCursor before = page.input()->textCursor();
        QApplication::clipboard()->setText(QStringLiteral("before"));
        QVERIFY(chooseRecordAction(more, copy));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0.30000000000000004"));
        QVERIFY(ExpressionEngine::evaluate(QApplication::clipboard()->text()).value == page.history().answer());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("未提交草稿"));
        QCOMPARE(page.input()->textCursor().position(), before.position());
        QCOMPARE(page.input()->textCursor().anchor(), before.anchor());
        QCOMPARE(page.recordCount(), 1);
        more->findChild<QAction *>(QStringLiteral("copyCalculation"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0.1+0.2\n= 0.30000000000000004"));
    }
    void recordActionsReadModelAfterGrowth()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("ans+2"));
        page.submit();
        auto *more = page.findChild<QToolButton *>(QStringLiteral("recordActions"));
        for (int index = 0; index < 64; ++index)
        {
            page.input()->setPlainText(QString::number(index));
            page.submit();
        }
        page.findChild<QLabel *>(QStringLiteral("recordFormula"))->setText(QStringLiteral("999"));
        page.findChild<QLabel *>(QStringLiteral("recordResult"))->setText(QStringLiteral("= 999"));
        more->findChild<QAction *>(QStringLiteral("copyCalculation"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("ans+2\n= 2"));
        more->findChild<QAction *>(QStringLiteral("copyValue"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("2"));
        page.input()->setPlainText(QStringLiteral("10+"));
        page.input()->moveCursor(QTextCursor::End);
        more->findChild<QAction *>(QStringLiteral("insertResult"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("10+2"));
        QCOMPARE(page.history().answer(), 63.0);
        QCOMPARE(page.recordCount(), 65);
        page.submit();
        QCOMPARE(page.history().answer(), 12.0);
        QCOMPARE(page.history().record(1)->result.value, 2.0);
    }
    void failedRecordsOnlyCopyFormulaAndError()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("1+中"));
        page.submit();
        auto *more = page.findChild<QToolButton *>(QStringLiteral("recordActions"));
        QCOMPARE(more->menu()->actions().size(), 1);
        QVERIFY(!more->findChild<QAction *>(QStringLiteral("copyValue")));
        QVERIFY(!more->findChild<QAction *>(QStringLiteral("insertResult")));
        more->findChild<QAction *>(QStringLiteral("copyCalculation"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("1+中\n无法计算：未知常量，或函数缺少括号"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+中"));
        QCOMPARE(page.recordCount(), 1);
        QCOMPARE(page.history().answer(), 0.0);
    }
    void insertionPreservesSurroundingFormula_data()
    {
        QTest::addColumn<QString>("draft");
        QTest::addColumn<int>("start");
        QTest::addColumn<int>("end");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<double>("value");
        QTest::newRow("base-of-power") << QStringLiteral("^2") << 0 << 0 << QStringLiteral("(-2)^2") << 4.0;
        QTest::newRow("exponent") << QStringLiteral("2^") << 2 << 2 << QStringLiteral("2^(-2)") << .25;
        QTest::newRow("subtract") << QStringLiteral("3-") << 2 << 2 << QStringLiteral("3-(-2)") << 5.0;
        QTest::newRow("divide") << QStringLiteral("8/") << 2 << 2 << QStringLiteral("8/(-2)") << -4.0;
        QTest::newRow("function-argument") << QStringLiteral("max(,1)") << 4 << 4 << QStringLiteral("max((-2),1)") << 1.0;
        QTest::newRow("replace-selection") << QStringLiteral("3+99*4") << 2 << 4 << QStringLiteral("3+(-2)*4") << -5.0;
        QTest::newRow("reverse-selection") << QStringLiteral("3+99*4") << 4 << 2 << QStringLiteral("3+(-2)*4") << -5.0;
        QTest::newRow("unicode-prefix") << QStringLiteral("8÷99×3") << 2 << 4 << QStringLiteral("8÷(-2)×3") << -12.0;
    }
    void insertionPreservesSurroundingFormula()
    {
        QFETCH(QString, draft);
        QFETCH(int, start);
        QFETCH(int, end);
        QFETCH(QString, expected);
        QFETCH(double, value);
        CalculatorPage page;
        prepare(page, QStringLiteral("-2"));
        page.submit();
        page.input()->setPlainText(draft);
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        // 在结果标签取得焦点后仍使用输入区原选区，撤销路由返回输入框。
        page.findChild<QLabel *>(QStringLiteral("recordResult"))->setFocus();
        QApplication::clipboard()->setText(QStringLiteral("keep clipboard"));
        QVERIFY(chooseRecordAction(page.findChild<QToolButton *>(QStringLiteral("recordActions")),
                                   page.findChild<QAction *>(QStringLiteral("insertResult"))));
        QCOMPARE(page.input()->toPlainText(), expected);
        QCOMPARE(page.input()->textCursor().position(), qMin(start, end) + 4);
        QTRY_VERIFY(page.input()->hasFocus());
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("keep clipboard"));
        QCOMPARE(page.recordCount(), 1);
        QCOMPARE(page.history().answer(), -2.0);
        page.routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(page.input()->toPlainText(), draft);
        page.routeEdit(QStringLiteral("actionredo"));
        QCOMPARE(page.input()->toPlainText(), expected);
        page.submit();
        QCOMPARE(page.history().answer(), value);
    }
    void insertionRespectsCompositionAndCompletion()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("0.1+0.2"));
        page.submit();
        auto *insert = page.findChild<QAction *>(QStringLiteral("insertResult"));
        page.input()->setPlainText(QStringLiteral("2+@sq"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(2);
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        insert->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("2+0.30000000000000004"));
        QVERIFY(!completion(page)->isVisible());
        page.input()->setPlainText(QStringLiteral("10+"));
        page.input()->moveCursor(QTextCursor::End);
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(page.input(), &preedit);
        insert->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("10+"));
        QCOMPARE(page.recordCount(), 1);
        QInputMethodEvent finish;
        QApplication::sendEvent(page.input(), &finish);
        insert->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("10+0.30000000000000004"));
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
    void completionTemplates_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<int>("position");
        QTest::addColumn<QString>("after");
        QTest::addColumn<int>("cursor");
        QTest::newRow("sqrt") << QStringLiteral("@sq") << 3 << QStringLiteral("sqrt()") << 5;
        QTest::newRow("middle-formula") << QStringLiteral("2+@sq*3") << 5 << QStringLiteral("2+sqrt()*3") << 7;
        QTest::newRow("middle-query") << QStringLiteral("2+@sqrt*3") << 5 << QStringLiteral("2+sqrt()*3") << 7;
        QTest::newRow("existing-call") << QStringLiteral("2+@sq(9)*3") << 5 << QStringLiteral("2+sqrt(9)*3") << 7;
        QTest::newRow("existing-call-space") << QStringLiteral("@sq (9)") << 3 << QStringLiteral("sqrt (9)") << 6;
        QTest::newRow("binary-function") << QStringLiteral("@pow") << 4 << QStringLiteral("pow(, )") << 4;
        QTest::newRow("chinese-root") << QStringLiteral("@平方根") << 4 << QStringLiteral("sqrt()") << 5;
        QTest::newRow("chinese-answer") << QStringLiteral("@上次") << 3 << QStringLiteral("ans") << 3;
        QTest::newRow("uppercase") << QStringLiteral("@SQ") << 3 << QStringLiteral("sqrt()") << 5;
        QTest::newRow("division-argument") << QStringLiteral("2/@pi") << 5 << QStringLiteral("2/pi") << 4;
        QTest::newRow("nested-argument") << QStringLiteral("max(1,@pi)") << 9 << QStringLiteral("max(1,pi)") << 8;
        QTest::newRow("unicode-operator") << QStringLiteral("2×@pi") << 5 << QStringLiteral("2×pi") << 4;
    }
    void completionTemplates()
    {
        QFETCH(QString, before);
        QFETCH(int, position);
        QFETCH(QString, after);
        QFETCH(int, cursor);
        CalculatorPage page;
        prepare(page, before);
        QTextCursor inputCursor = page.input()->textCursor();
        inputCursor.setPosition(position);
        page.input()->setTextCursor(inputCursor);
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Return);
        QCOMPARE(page.input()->toPlainText(), after);
        QCOMPARE(page.input()->textCursor().position(), cursor);
        QCOMPARE(page.recordCount(), 0);
        QVERIFY(!completion(page)->isVisible());
    }
    void completionSearchAndCalculation()
    {
        CalculatorPage page;
        prepare(page, QString());
        QTest::keyClicks(page.input(), "@");
        QTRY_VERIFY(completion(page)->isVisible());
        QCOMPARE(completion(page)->model()->rowCount(), 17);
        QTest::keyClicks(page.input(), "s");
        QCOMPARE(completion(page)->model()->rowCount(), 2);
        QTest::keyClicks(page.input(), "q");
        QCOMPARE(completion(page)->model()->rowCount(), 1);
        QTest::keyClick(page.input(), Qt::Key_Tab);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("sqrt()"));
        QTest::keyClicks(page.input(), "9");
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.recordCount(), 1);
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("recordResult"))->text(), QStringLiteral("= 3"));
        page.input()->setPlainText(QStringLiteral("@对数"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        QCOMPARE(completion(page)->model()->rowCount(), 2);
    }
    void completionNavigationCancellationAndMouse()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("@p"));
        QTRY_VERIFY(completion(page)->isVisible());
        QCOMPARE(completion(page)->model()->rowCount(), 2);
        QTest::keyClick(completion(page), Qt::Key_Down);
        QCOMPARE(completion(page)->currentIndex().row(), 1);
        QTest::keyClick(completion(page), Qt::Key_Up);
        QCOMPARE(completion(page)->currentIndex().row(), 0);
        QTest::keyClick(completion(page), Qt::Key_Escape);
        QVERIFY(!completion(page)->isVisible());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("@p"));
        QTest::keyClick(page.input(), Qt::Key_Return);
        QCOMPARE(page.recordCount(), 0);
        QVERIFY(!completion(page)->isVisible());
        QTest::keyClicks(page.input(), "i");
        QTRY_VERIFY(completion(page)->isVisible());
        const QModelIndex index = completion(page)->model()->index(0, 0);
        QTest::mouseClick(completion(page)->viewport(), Qt::LeftButton, Qt::NoModifier,
                         completion(page)->visualRect(index).center());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("pi"));
        QCOMPARE(page.recordCount(), 0);
    }
    void incompleteCompletionDoesNotSubmit()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("6*7"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("ans+@s"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(completion(page), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.recordCount(), 1);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ans+@s"));
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().contains(QStringLiteral("请先完成")));
        page.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QCOMPARE(page.recordCount(), 1);
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.submit();
        QCOMPARE(page.findChildren<QLabel *>(QStringLiteral("recordResult")).last()->text(), QStringLiteral("= 43"));
    }
    void ordinaryInputAndNoMatches()
    {
        CalculatorPage page;
        prepare(page, QString());
        for (const auto &formula : {QStringLiteral("8/4"), QStringLiteral("1e3"), QStringLiteral("sin(pi)"),
                                   QStringLiteral("2@sq"), QStringLiteral("@unknown"), QStringLiteral("@@sq")})
        {
            page.input()->setPlainText(formula);
            page.input()->moveCursor(QTextCursor::End);
            QVERIFY(!completion(page)->isVisible());
            QTest::keyClick(page.input(), Qt::Key_Return);
            QCOMPARE(page.input()->toPlainText(), formula);
            QCOMPARE(page.recordCount(), 0);
        }
        page.input()->setPlainText(QStringLiteral("@sq"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        page.input()->selectAll();
        QVERIFY(!completion(page)->isVisible());
    }
    void completionUndoRedoAndLifetime()
    {
        auto *page = new CalculatorPage;
        prepare(*page, QStringLiteral("2+@sq*3"));
        QTextCursor cursor = page->input()->textCursor();
        cursor.setPosition(5);
        page->input()->setTextCursor(cursor);
        QTRY_VERIFY(completion(*page)->isVisible());
        QTest::keyClick(page->input(), Qt::Key_Tab);
        QCOMPARE(page->input()->toPlainText(), QStringLiteral("2+sqrt()*3"));
        page->routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(page->input()->toPlainText(), QStringLiteral("2+@sq*3"));
        page->routeEdit(QStringLiteral("actionredo"));
        QCOMPARE(page->input()->toPlainText(), QStringLiteral("2+sqrt()*3"));
        page->input()->setPlainText(QStringLiteral("@"));
        page->input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(*page)->isVisible());
        page->hide();
        QVERIFY(!completion(*page)->isVisible());
        prepare(*page, QStringLiteral("@sq"));
        QTRY_VERIFY(completion(*page)->isVisible());
        QPointer<QAbstractItemView> popup = completion(*page);
        delete page;
        QCoreApplication::processEvents();
        QVERIFY(popup.isNull());
    }
    void completionRespectsInputMethod()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("@"));
        QTRY_VERIFY(completion(page)->isVisible());
        QInputMethodEvent preedit(QStringLiteral("平方"), {});
        QApplication::sendEvent(page.input(), &preedit);
        QVERIFY(!completion(page)->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.recordCount(), 0);
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("平方根"));
        QApplication::sendEvent(page.input(), &commit);
        QTRY_COMPARE(page.input()->toPlainText(), QStringLiteral("@平方根"));
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Return);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("sqrt()"));
        QCOMPARE(page.recordCount(), 0);
    }
    void searchableHelpAndAbout()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("1+2"));
        page.findChild<QPushButton *>(QStringLiteral("helpButton"))->click();
        auto *dialog = page.findChild<QDialog *>(QStringLiteral("calctabddHelpDialog"));
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        QVERIFY(!dialog->isModal());
        auto *search = dialog->findChild<QLineEdit *>(QStringLiteral("helpSearch"));
        auto *operations = dialog->findChild<QTextBrowser *>(QStringLiteral("operationsHelp"));
        auto *precision = dialog->findChild<QTextBrowser *>(QStringLiteral("precisionHelp"));
        auto *tabs = dialog->findChild<QTabWidget *>();
        for (const auto &entry : CalculationCatalog::entries())
            QVERIFY2((entry.kind == CalculationCatalog::Kind::Precision ? precision : operations)->toPlainText().contains(entry.signature), qPrintable(entry.signature));
        search->setText(QStringLiteral("平方根"));
        QVERIFY(operations->toPlainText().contains(QStringLiteral("sqrt(x)")));
        QVERIFY(!operations->toPlainText().contains(QStringLiteral("cos(x)")));
        search->setText(QStringLiteral("草稿"));
        QVERIFY(operations->toPlainText().contains(QStringLiteral("Alt+↑ / Alt+↓")));
        QVERIFY(operations->toPlainText().contains(QStringLiteral("光标和选区")));
        QVERIFY(operations->toPlainText().contains(QStringLiteral("重置输入框撤销栈")));
        search->setText(QStringLiteral("非正规数"));
        QCOMPARE(tabs->currentIndex(), 1);
        QVERIFY(precision->toPlainText().contains(QStringLiteral("4.9406564584124654e-324")));
        search->setText(QStringLiteral("no-such-capability"));
        QVERIFY(operations->toPlainText().contains(QStringLiteral("没有匹配项")));
        QVERIFY(precision->toPlainText().contains(QStringLiteral("没有匹配项")));
        search->clear();
        showCalculatorHelp(&page);
        QCOMPARE(page.findChildren<QDialog *>(QStringLiteral("calctabddHelpDialog")).size(), 1);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+2"));
        QPointer<QDialog> oldDialog = dialog;
        dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(oldDialog.isNull());
        showCalculatorAbout(&page);
        auto *about = page.findChild<QDialog *>(QStringLiteral("calctabddAboutDialog"));
        QVERIFY(about);
        const QString text = about->findChild<QTextBrowser *>()->toPlainText();
        QVERIFY(text.contains(QStringLiteral(CALCTABDD_VERSION)));
        QVERIFY(text.contains(QStringLiteral("v3.8.3 / v3.9.0")));
        QVERIFY(text.contains(QStringLiteral("源码接口参考")));
        QVERIFY(text.contains(QStringLiteral("手动确认")));
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
        if (!directory.isEmpty())
        {
            QVERIFY(page.grab().save(directory + QStringLiteral("/calculator-dark.png")));
            page.activateWindow();
            page.focusInput();
            page.input()->setPlainText(QStringLiteral("2+@s"));
            page.input()->moveCursor(QTextCursor::End);
            QTRY_VERIFY(completion(page)->isVisible());
            QVERIFY(completion(page)->grab().save(directory + QStringLiteral("/completion-dark.png")));
            QTest::keyClick(page.input(), Qt::Key_Escape);
            showCalculatorHelp(&page);
            auto *help = page.findChild<QDialog *>(QStringLiteral("calctabddHelpDialog"));
            QTest::qWait(30);
            QVERIFY(help->styleSheet().contains(QStringLiteral("#e4e8ef")));
            QVERIFY(help->grab().save(directory + QStringLiteral("/help-dark.png")));
            showCalculatorAbout(&page);
            auto *about = page.findChild<QDialog *>(QStringLiteral("calctabddAboutDialog"));
            QTest::qWait(30);
            QVERIFY(about->grab().save(directory + QStringLiteral("/about-dark.png")));
        }
    }
};
QTEST_MAIN(PageTests)
#include "page_tests.moc"
