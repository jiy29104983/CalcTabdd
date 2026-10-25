#include "record_text.h"
#include "numeric_test_helpers.h"
#include "calculator_page.h"
#include "calculator_help.h"
#include "calculation_catalog.h"
#include <QAbstractItemView>
#include <QCompleter>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QMenu>
#include <QDialogButtonBox>
#include <QToolButton>
#include <QTimer>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QInputMethodEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QLayout>
#include <QtTest>

class PageTests : public QObject
{
    Q_OBJECT
    QAbstractItemView *completion(CalculatorPage &page)
    {
        return page.input()->findChild<QCompleter *>()->popup();
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
    bool acceptSessionFile(CalculatorPage &page, const QString &path, bool restore)
    {
        auto *dialog = page.findChild<QFileDialog *>(QStringLiteral("sessionFileDialog"));
        if (!dialog || !dialog->isVisible()) return false;
        dialog->setDirectory(QFileInfo(path).absolutePath());
        auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
        if (!name) return false;
        name->setText(QFileInfo(path).fileName());
        auto *button = dialog->findChild<QDialogButtonBox *>()->button(restore ? QDialogButtonBox::Open : QDialogButtonBox::Save);
        if (!button || !button->isEnabled()) return false;
        button->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return !page.findChild<QFileDialog *>(QStringLiteral("sessionFileDialog"));
    }
    bool selectSession(CalculatorPage &page, const QString &path, bool restore)
    {
        page.findChild<QAction *>(restore ? QStringLiteral("restoreSession") : QStringLiteral("saveSessionAs"))->trigger();
        return acceptSessionFile(page, path, restore);
    }
    CalculationSession readSession(const QString &path)
    {
        QFile file(path);
        CalculationSession session;
        if (!file.open(QIODevice::ReadOnly)) qFatal("Saved session cannot be read");
        const auto error = SessionFormat::decode(file.readAll(), session);
        if (!error.isEmpty()) qFatal("%s", qPrintable(error));
        return session;
    }
    bool defineFormula(CalculatorPage &page, const QString &definition)
    {
        auto *edit = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        if (!edit) return false;
        if (!edit->isVisible())
            page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        if (!edit->isVisible()) return false;
        edit->setFocus();
        edit->setPlainText(definition);
        QTest::keyClick(edit, Qt::Key_Return);
        return page.input()->isVisible() && page.findChild<QLabel *>(QStringLiteral("customDefinitionError"))->text().isEmpty();
    }
    void setProgrammerConfig(CalculatorPage &page, int width, bool isSigned)
    {
        auto *bits = page.findChild<QComboBox *>(QStringLiteral("programmerBitWidth"));
        bits->setCurrentIndex(bits->findData(width));
        page.findChild<QComboBox *>(QStringLiteral("programmerIntegerType"))->setCurrentIndex(isSigned ? 1 : 0);
    }
    void setDraft(CalculatorPage &page, const QString &text, int anchor, int position)
    {
        page.input()->setPlainText(text);
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(anchor);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
    }
private slots:
    void programmerCopyFormats_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<bool>("signedMode");
        QTest::addColumn<bool>("maximumBits");
        for (int width : {8, 16, 32, 64})
            for (bool signedMode : {false, true})
                for (bool maximumBits : {false, true})
                    QTest::newRow(qPrintable(QStringLiteral("%1-%2-%3").arg(width).arg(signedMode).arg(maximumBits)))
                        << width << signedMode << maximumBits;
    }
    void programmerCopyFormats()
    {
        QFETCH(int, width);
        QFETCH(bool, signedMode);
        QFETCH(bool, maximumBits);
        CalculatorPage page;
        prepare(page, QString());
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        setProgrammerConfig(page, width, signedMode);
        const QString hex = QStringLiteral("0x") + QString(width / 4, maximumBits ? QLatin1Char('F') : QLatin1Char('0'));
        page.input()->setPlainText(hex);
        page.submit();
        const auto record = page.history().records().first();
        const quint64 magnitude = maximumBits ? ~quint64(0) >> (64 - width) : 0;
        const QString decimal = maximumBits && signedMode ? QStringLiteral("-1") : QString::number(magnitude);
        const QString binary = QStringLiteral("0b") + QString(width, maximumBits ? QLatin1Char('1') : QLatin1Char('0'));
        const QString octal = QStringLiteral("0o") + QString::number(magnitude, 8);
        // 当前设置与显示文本均改变后，菜单仍从原记录读取；详情保持折叠。
        setProgrammerConfig(page, width == 8 ? 64 : 8, !signedMode);
        page.findChild<RecordText *>(QStringLiteral("recordResult"))->setPlainText(QStringLiteral("= changed"));
        setDraft(page, QStringLiteral("未提交草稿😀"), 7, 1);
        auto *more = page.findChild<QToolButton *>(QStringLiteral("recordActions"));
        QVERIFY(more && more->isVisible());
        const QStringList names = {QStringLiteral("copyValue"), QStringLiteral("copyHex"), QStringLiteral("copyBin"), QStringLiteral("copyOct")};
        const QStringList expected = {decimal, hex, binary, octal};
        for (int index = 0; index < names.size(); ++index)
        {
            auto *action = more->findChild<QAction *>(names.at(index));
            QVERIFY(action);
            if (index == 2 && width == 8 && signedMode && maximumBits)
                QVERIFY(chooseRecordAction(more, action));
            else action->trigger();
            QCOMPARE(QApplication::clipboard()->text(), expected.at(index));
        }
        more->findChild<QAction *>(QStringLiteral("copyCalculation"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), record.calculationText());
        QVERIFY(QApplication::clipboard()->text().contains(QStringLiteral("DEC  ") + decimal));
        QVERIFY(QApplication::clipboard()->text().contains(QStringLiteral("BIN  ")));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("未提交草稿😀"));
        QCOMPARE(page.input()->textCursor().anchor(), 7);
        QCOMPARE(page.input()->textCursor().position(), 1);
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(page.history().programmerAnswer(), record.programmerValue);
        QVERIFY(!page.findChild<QWidget *>(QStringLiteral("radixDetails"))->isVisible());
    }
    void crossModeResultInsertion_data()
    {
        QTest::addColumn<bool>("sourceProgrammer");
        QTest::addColumn<int>("sourceWidth");
        QTest::addColumn<bool>("sourceSigned");
        QTest::addColumn<QString>("expression");
        QTest::addColumn<int>("targetMode"); // 0 普通，1 自定义，2 程序员。
        QTest::addColumn<int>("width");
        QTest::addColumn<bool>("signedMode");
        QTest::addColumn<QString>("expected");
        const auto row = [](const char *name, const QString &expression, int width, bool sign, const QString &expected) {
            QTest::newRow(name) << false << 32 << false << expression << 2 << width << sign << expected;
        };
        row("u8-max", QStringLiteral("255"), 8, false, QStringLiteral("255"));
        row("u8-overflow", QStringLiteral("256"), 8, false, {});
        row("s8-max", QStringLiteral("127"), 8, true, QStringLiteral("127"));
        row("s8-overflow", QStringLiteral("128"), 8, true, {});
        row("s8-min", QStringLiteral("-128"), 8, true, QStringLiteral("(-128)"));
        row("s8-underflow", QStringLiteral("-129"), 8, true, {});
        row("negative-unsigned", QStringLiteral("-1"), 16, false, {});
        row("fraction", QStringLiteral("1.5"), 64, true, {});
        row("near-integer-fraction", QStringLiteral("127.999999999999999999999999999999999999999999999"), 8, true, {});
        row("tiny-fraction", QStringLiteral("1e-999"), 64, false, {});
        row("negative-zero", QStringLiteral("-0"), 8, false, QStringLiteral("0"));
        row("rounded-integer", QStringLiteral("floor(1/3)"), 8, false, QStringLiteral("0"));
        row("approximate-integer", QStringLiteral("sin(0)"), 8, false, QStringLiteral("0"));
        row("conversion-loss", QStringLiteral("floor(sin(0)+0.1)"), 8, false, QStringLiteral("0"));
        row("approximate-fraction", QStringLiteral("sin(1)"), 64, false, {});
        row("above-binary-precision", QStringLiteral("9007199254740993"), 64, false, QStringLiteral("9007199254740993"));
        row("u64-max", QStringLiteral("18446744073709551615"), 64, false, QStringLiteral("18446744073709551615"));
        row("u64-overflow", QStringLiteral("18446744073709551616"), 64, false, {});
        row("s64-max", QStringLiteral("9223372036854775807"), 64, true, QStringLiteral("9223372036854775807"));
        row("s64-overflow", QStringLiteral("9223372036854775808"), 64, true, {});
        row("s64-min", QStringLiteral("-9223372036854775808"), 64, true, QStringLiteral("(-9223372036854775808)"));
        row("s64-underflow", QStringLiteral("-9223372036854775809"), 64, true, {});
        row("binary-integer-full-value", QStringLiteral("18446744073709549568+sin(0)"), 64, false, QStringLiteral("18446744073709549568"));
        row("binary-uint64-overflow", QStringLiteral("18446744073709551615+sin(0)"), 64, false, {});
        row("scientific-overflow", QStringLiteral("1e21"), 64, false, {});
        QTest::newRow("integer-to-math") << true << 8 << true << QStringLiteral("0xFF") << 0 << 32 << false << QStringLiteral("(-1)");
        QTest::newRow("integer-to-parameter") << true << 8 << true << QStringLiteral("0xFF") << 1 << 32 << false << QStringLiteral("-1");
        QTest::newRow("integer-to-signed-wide") << true << 8 << true << QStringLiteral("0xFF") << 2 << 16 << true << QStringLiteral("(-1)");
        QTest::newRow("integer-to-unsigned") << true << 8 << true << QStringLiteral("0xFF") << 2 << 16 << false << QString();
        QTest::newRow("integer-sign-overflow") << true << 8 << false << QStringLiteral("255") << 2 << 8 << true << QString();
        QTest::newRow("integer-narrow-overflow") << true << 16 << false << QStringLiteral("256") << 2 << 8 << false << QString();
        QTest::newRow("wrapped-integer-new-input") << true << 8 << false << QStringLiteral("255+1") << 2 << 16 << false << QStringLiteral("0");
        QTest::newRow("integer-uint64-to-math") << true << 64 << false << QStringLiteral("18446744073709551615") << 0 << 32 << false << QStringLiteral("18446744073709551615");
    }
    void crossModeResultInsertion()
    {
        QFETCH(bool, sourceProgrammer);
        QFETCH(int, sourceWidth);
        QFETCH(bool, sourceSigned);
        QFETCH(QString, expression);
        QFETCH(int, targetMode);
        QFETCH(int, width);
        QFETCH(bool, signedMode);
        QFETCH(QString, expected);
        CalculatorPage page;
        prepare(page, QString());
        if (sourceProgrammer)
        {
            page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
            setProgrammerConfig(page, sourceWidth, sourceSigned);
        }
        page.input()->setPlainText(expression);
        page.submit();
        QVERIFY(page.history().record(1)->result.ok);
        const auto original = page.history().records().first();
        const auto math = page.history().answer();
        const auto integer = page.history().programmerAnswer();
        if (targetMode == 1) QVERIFY(defineFormula(page, QStringLiteral("A=x")));
        else page.findChild<QAction *>(targetMode == 2 ? QStringLiteral("programmerCalculationMode") : QStringLiteral("normalCalculationMode"))->trigger();
        if (targetMode == 2) setProgrammerConfig(page, width, signedMode);
        const QString prefix = targetMode == 1 ? QStringLiteral("x=") : QStringLiteral("0+");
        page.input()->setPlainText(prefix);
        page.input()->moveCursor(QTextCursor::End);
        QTest::keyClicks(page.input(), "99");
        const QString draft = prefix + QStringLiteral("99");
        setDraft(page, draft, 4, 2);
        // setDraft 重置撤销栈；留下一个实际编辑以检查失败路径不破坏撤销。
        page.input()->moveCursor(QTextCursor::End);
        QTest::keyClicks(page.input(), "0");
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(5);
        cursor.setPosition(2, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        QApplication::clipboard()->setText(QStringLiteral("clipboard unchanged"));
        auto *insert = page.findChild<QAction *>(QStringLiteral("insertResult"));
        QVERIFY(insert);
        insert->trigger();
        QCOMPARE(page.input()->toPlainText(), expected.isEmpty() ? prefix + QStringLiteral("990") : prefix + expected);
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(page.history().answer(), math);
        QCOMPARE(page.history().programmerAnswer(), integer);
        QCOMPARE(page.history().record(1)->calculationText(), original.calculationText());
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("clipboard unchanged"));
        if (expected.isEmpty())
        {
            QCOMPARE(page.input()->textCursor().anchor(), 5);
            QCOMPARE(page.input()->textCursor().position(), 2);
            QVERIFY(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().contains(QStringLiteral("无法插入")));
            page.input()->undo();
            QCOMPARE(page.input()->toPlainText(), draft);
            return;
        }
        QTRY_VERIFY(page.input()->hasFocus());
        page.routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(page.input()->toPlainText(), prefix + QStringLiteral("990"));
        page.routeEdit(QStringLiteral("actionredo"));
        QCOMPARE(page.input()->toPlainText(), prefix + expected);
        page.submit();
        QCOMPARE(page.history().count(), 2);
        const auto &result = page.history().records().last();
        QVERIFY2(result.result.ok, qPrintable(result.result.text));
        QVERIFY(result.sourceText().isEmpty());
        QCOMPARE(result.valueText(), QString(expected).remove(QLatin1Char('(')).remove(QLatin1Char(')')));
    }
    void programmerFailureCopyAndMixedExport_data()
    {
        QTest::addColumn<bool>("markdown");
        QTest::newRow("txt") << false;
        QTest::newRow("markdown") << true;
    }
    void programmerFailureCopyAndMixedExport()
    {
        QFETCH(bool, markdown);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        CalculatorPage page;
        prepare(page, QStringLiteral("1/3"));
        page.submit();
        QVERIFY(defineFormula(page, QStringLiteral("A=x+sin(0)")));
        page.input()->setPlainText(QStringLiteral("x=2"));
        page.submit();
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        setProgrammerConfig(page, 8, false);
        page.input()->setPlainText(QStringLiteral("255+1"));
        page.submit();
        setProgrammerConfig(page, 16, true);
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("1/0"));
        page.submit();
        auto *more = page.findChildren<QToolButton *>(QStringLiteral("recordActions")).last();
        QVERIFY(more->isVisible());
        QCOMPARE(more->menu()->actions().size(), 1);
        more->findChild<QAction *>(QStringLiteral("copyCalculation"))->trigger();
        const auto failure = page.history().records().last();
        QCOMPARE(QApplication::clipboard()->text(), failure.calculationText());
        QVERIFY(!QApplication::clipboard()->text().contains(QStringLiteral("DEC")));
        const auto snapshot = CalculationExport::serialize(page.history().records(),
            markdown ? CalculationExport::Format::Markdown : CalculationExport::Format::Text);
        const auto math = page.history().answer();
        const auto integer = page.history().programmerAnswer();
        setProgrammerConfig(page, 64, false);
        setDraft(page, QStringLiteral("未提交草稿"), 4, 1);
        page.findChild<QAction *>(markdown ? QStringLiteral("exportMarkdown") : QStringLiteral("exportText"))->trigger();
        auto *dialog = page.findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"));
        QVERIFY(dialog && dialog->isVisible());
        dialog->setDirectory(directory.path());
        dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(QStringLiteral("mixed"));
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        QFile file(directory.filePath(markdown ? QStringLiteral("mixed.md") : QStringLiteral("mixed.txt")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), snapshot);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("未提交草稿"));
        QCOMPARE(page.input()->textCursor().anchor(), 4);
        QCOMPARE(page.input()->textCursor().position(), 1);
        QCOMPARE(page.history().count(), 5);
        QCOMPARE(page.history().answer(), math);
        QCOMPARE(page.history().programmerAnswer(), integer);
        QCOMPARE(page.findChild<QComboBox *>(QStringLiteral("programmerBitWidth"))->currentData().toInt(), 64);
    }
    void programmerAnswerConversion_data()
    {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<int>("oldWidth");
        QTest::addColumn<bool>("oldSigned");
        QTest::addColumn<int>("newWidth");
        QTest::addColumn<bool>("newSigned");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("narrow-overflow") << QStringLiteral("256") << 16 << false << 8 << false << false;
        QTest::newRow("unsigned-to-signed-overflow") << QStringLiteral("255") << 8 << false << 8 << true << false;
        QTest::newRow("unsigned-to-wide-signed") << QStringLiteral("255") << 8 << false << 16 << true << true;
        QTest::newRow("signed-extension") << QStringLiteral("-1") << 8 << true << 16 << true << true;
        QTest::newRow("negative-to-unsigned") << QStringLiteral("-1") << 8 << true << 16 << false << false;
        QTest::newRow("above-double-precision") << QStringLiteral("9007199254740993") << 64 << false << 64 << true << true;
        QTest::newRow("unsigned-64-overflow") << QStringLiteral("18446744073709551615") << 64 << false << 64 << true << false;
        QTest::newRow("signed-64-minimum") << QStringLiteral("-9223372036854775808") << 64 << true << 64 << true << true;
    }
    void programmerAnswerConversion()
    {
        QFETCH(QString, expression);
        QFETCH(int, oldWidth);
        QFETCH(bool, oldSigned);
        QFETCH(int, newWidth);
        QFETCH(bool, newSigned);
        QFETCH(bool, accepted);
        CalculatorPage page;
        prepare(page, QStringLiteral("sin(0)"));
        page.submit();
        const auto math = page.history().answer();
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        setProgrammerConfig(page, oldWidth, oldSigned);
        page.input()->setPlainText(expression);
        page.submit();
        const auto answer = page.history().programmerAnswer();
        QVERIFY(page.history().record(2)->result.ok);
        setDraft(page, QStringLiteral(" ans "), 4, 1);
        setProgrammerConfig(page, newWidth, newSigned);
        QCOMPARE(page.history().count(), 2);
        QCOMPARE(page.history().programmerAnswer(), answer);
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("answerBadge"))->toolTip(), QStringLiteral("上次成功结果：ans = ") + expression);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral(" ans "));
        QCOMPARE(page.input()->textCursor().anchor(), 4);
        QCOMPARE(page.input()->textCursor().position(), 1);
        page.submit();
        const auto &record = page.history().records().last();
        QCOMPARE(record.programmerAnswerBefore, answer);
        QCOMPARE(record.programmerConfig, (ProgrammerConfig{newWidth, newSigned}));
        QCOMPARE(record.result.ok, accepted);
        if (accepted)
        {
            QCOMPARE(page.history().programmerAnswer().text(), expression);
            QCOMPARE(page.history().programmerAnswer().config(), record.programmerConfig);
        }
        else
        {
            QCOMPARE(record.result.error, CalculationError::Overflow);
            QCOMPARE(page.history().programmerAnswer(), answer);
            QCOMPARE(page.input()->toPlainText(), QStringLiteral(" ans "));
            QCOMPARE(page.input()->extraSelections().size(), 1);
            // 不使用 ans 的新表达式无需转换旧值，成功后才替换整数 ans。
            page.input()->setPlainText(QStringLiteral("7"));
            page.submit();
            QVERIFY(page.history().records().last().result.ok);
            QCOMPARE(page.history().programmerAnswer().text(), QStringLiteral("7"));
            QCOMPARE(page.history().programmerAnswer().config(), (ProgrammerConfig{newWidth, newSigned}));
        }
        QCOMPARE(page.history().answer(), math);
        QCOMPARE(page.history().record(2)->programmerValue, answer);
    }
    void threeModeDraftsAndHistory_data()
    {
        QTest::addColumn<int>("origin");
        QTest::addColumn<int>("exit");
        for (int origin = 0; origin < 3; ++origin)
            for (int exit = 0; exit < 4; ++exit)
                QTest::newRow(qPrintable(QStringLiteral("mode-%1-exit-%2").arg(origin).arg(exit))) << origin << exit;
    }
    void threeModeDraftsAndHistory()
    {
        QFETCH(int, origin);
        QFETCH(int, exit); // 越过最新、成功提交、确认清空、手动切换。
        CalculatorPage page;
        prepare(page, QStringLiteral("10"));
        page.submit();
        const QStringList modes{QStringLiteral("normalCalculationMode"), QStringLiteral("customCalculationMode"),
                                QStringLiteral("programmerCalculationMode")};
        const QStringList drafts{QStringLiteral("  100+\n "), QStringLiteral("y=7"), QStringLiteral("  ans+5\n ")};
        setDraft(page, drafts.at(0), 3, 1);
        QVERIFY(defineFormula(page, QStringLiteral("A=x+ans")));
        page.input()->setPlainText(QStringLiteral("x=2"));
        page.submit();
        QVERIFY(defineFormula(page, QStringLiteral("B=y+ans")));
        setDraft(page, drafts.at(1), 3, 1);
        auto *definition = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        definition->setPlainText(QStringLiteral("C=y+9")); // 未应用定义也须保留在原自定义草稿中。
        page.findChild<QAction *>(modes.at(2))->trigger();
        setProgrammerConfig(page, 8, false);
        page.input()->setPlainText(QStringLiteral("255+1"));
        page.submit();
        setProgrammerConfig(page, 16, false);
        page.input()->setPlainText(QStringLiteral("ans+2"));
        page.submit();
        const auto original = *page.history().record(4);
        setProgrammerConfig(page, 64, true);
        setDraft(page, drafts.at(2), 3, 1);
        page.findChild<QAction *>(modes.at(origin))->trigger();
        const auto reuse = page.findChildren<QPushButton *>(QStringLiteral("reuseFormula"));
        reuse.at(3)->click();
        QVERIFY(page.programmerMode());
        QCOMPARE(page.findChild<QComboBox *>(QStringLiteral("programmerBitWidth"))->currentData().toInt(), 16);
        setProgrammerConfig(page, 32, true);
        setDraft(page, QStringLiteral("ans+3"), 5, 2);
        reuse.at(1)->click();
        QCOMPARE(definition->toPlainText(), QStringLiteral("A=x+ans"));
        setDraft(page, QStringLiteral("x=8"), 3, 1);
        reuse.at(0)->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("10"));
        reuse.at(1)->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=8"));
        QCOMPARE(page.input()->textCursor().position(), 1);
        QCOMPARE(page.input()->textCursor().anchor(), 3);
        reuse.at(3)->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ans+3"));
        QCOMPARE(page.input()->textCursor().position(), 2);
        QCOMPARE(page.input()->textCursor().anchor(), 5);
        QCOMPARE(page.findChild<QComboBox *>(QStringLiteral("programmerBitWidth"))->currentData().toInt(), 32);
        QCOMPARE(page.findChild<QComboBox *>(QStringLiteral("programmerIntegerType"))->currentData().toBool(), true);
        page.input()->setPlainText(QStringLiteral("ans/0"));
        page.submit();
        QCOMPARE(page.history().count(), 5);
        QCOMPARE(page.history().answer().text(), QStringLiteral("12"));
        QCOMPARE(page.history().programmerAnswer(), original.programmerValue);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ans/0"));
        QCOMPARE(page.input()->extraSelections().size(), 1);
        auto *badge = page.findChild<QLabel *>(QStringLiteral("historyRecallBadge"));
        QVERIFY(badge->text().contains(QStringLiteral("04/05")));
        setDraft(page, QStringLiteral("ans+3"), 5, 2);
        if (exit == 0)
        {
            QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier); // 新增的失败记录。
            QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier); // 原草稿。
        }
        else if (exit == 1)
        {
            page.submit();
            QCOMPARE(page.history().count(), 6);
            QCOMPARE(page.history().programmerAnswer().text(), QStringLiteral("5"));
            QCOMPARE(page.history().programmerAnswer().sourceText(), QStringLiteral("含 8 位回绕"));
            QCOMPARE(page.history().records().last().programmerAnswerBefore, original.programmerValue);
        }
        else if (exit == 2)
        {
            auto *clear = page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"));
            clear->click();
            auto *dialog = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
            QVERIFY(dialog);
            dialog->reject();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCOMPARE(page.history().count(), 5);
            QCOMPARE(page.input()->toPlainText(), QStringLiteral("ans+3"));
            QCOMPARE(page.input()->textCursor().position(), 2);
            QCOMPARE(page.input()->textCursor().anchor(), 5);
            clear->click();
            dialog = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(page.history().count(), 0);
            QCOMPARE(page.history().answer(), NumericValue());
            QCOMPARE(page.history().programmerAnswer(), ProgrammerValue());
            QVERIFY(page.findChildren<QFrame *>(QStringLiteral("calculationRecord")).isEmpty());
        }
        else page.findChild<QAction *>(modes.at(origin))->trigger();
        QVERIFY(!badge->isVisible());
        QCOMPARE(page.input()->toPlainText(), drafts.at(origin));
        for (int mode : {0, 1, 2})
        {
            page.findChild<QAction *>(modes.at(mode))->trigger();
            QCOMPARE(page.input()->toPlainText(), drafts.at(mode));
            QCOMPARE(page.input()->textCursor().position(), 1);
            QCOMPARE(page.input()->textCursor().anchor(), 3);
            if (mode == 1) QCOMPARE(definition->toPlainText(), QStringLiteral("C=y+9"));
        }
        QCOMPARE(page.findChild<QComboBox *>(QStringLiteral("programmerBitWidth"))->currentData().toInt(), 64);
        QCOMPARE(page.findChild<QComboBox *>(QStringLiteral("programmerIntegerType"))->currentData().toBool(), true);
        if (exit != 2)
        {
            QCOMPARE(page.history().record(4)->expression, original.expression);
            QCOMPARE(page.history().record(4)->programmerConfig, original.programmerConfig);
            QCOMPARE(page.history().record(4)->programmerValue, original.programmerValue);
            QCOMPARE(page.history().record(4)->programmerAnswerBefore, original.programmerAnswerBefore);
            QCOMPARE(page.history().record(2)->parameterInput, QStringLiteral("x=2"));
        }
        else
        {
            page.input()->setPlainText(QStringLiteral("ans+1"));
            page.submit();
            QCOMPARE(page.history().records().first().id, quint64(1));
            QCOMPARE(page.history().programmerAnswer().text(), QStringLiteral("1"));
            QCOMPARE(page.history().programmerAnswer().sources(), 0u);
        }
    }
    void firstCustomSwitchEndsMixedHistory_data()
    {
        QTest::addColumn<bool>("programmerOrigin");
        QTest::addColumn<bool>("apply");
        for (bool origin : {false, true})
            for (bool apply : {false, true})
                QTest::newRow(qPrintable(QStringLiteral("programmer-%1-apply-%2").arg(origin).arg(apply))) << origin << apply;
    }
    void firstCustomSwitchEndsMixedHistory()
    {
        QFETCH(bool, programmerOrigin);
        QFETCH(bool, apply);
        CalculatorPage page;
        prepare(page, QStringLiteral("10"));
        page.submit();
        setDraft(page, QStringLiteral("100+"), 4, 1);
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        setProgrammerConfig(page, 8, false);
        page.input()->setPlainText(QStringLiteral("20"));
        page.submit();
        setProgrammerConfig(page, 64, true);
        setDraft(page, QStringLiteral("0x12+"), 5, 2);
        const QString originMode = programmerOrigin ? QStringLiteral("programmerCalculationMode") : QStringLiteral("normalCalculationMode");
        page.findChild<QAction *>(originMode)->trigger();
        page.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).at(programmerOrigin ? 0 : 1)->click();
        page.input()->setPlainText(QStringLiteral("777"));
        page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        QVERIFY(!page.findChild<QLabel *>(QStringLiteral("historyRecallBadge"))->isVisible());
        auto *definition = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        QVERIFY(definition->isVisible());
        definition->setPlainText(QStringLiteral("A=x+ans"));
        QTest::keyClick(definition, apply ? Qt::Key_Return : Qt::Key_Escape);
        if (apply)
        {
            QCOMPARE(page.input()->toPlainText(), QStringLiteral("x="));
            page.findChild<QAction *>(originMode)->trigger();
        }
        QCOMPARE(page.programmerMode(), programmerOrigin);
        QCOMPARE(page.input()->toPlainText(), programmerOrigin ? QStringLiteral("0x12+") : QStringLiteral("100+"));
        QCOMPARE(page.input()->textCursor().position(), programmerOrigin ? 2 : 1);
        QCOMPARE(page.input()->textCursor().anchor(), programmerOrigin ? 5 : 4);
        QCOMPARE(page.history().count(), 2);
        QCOMPARE(page.history().answer().text(), QStringLiteral("10"));
        QCOMPARE(page.history().programmerAnswer().text(), QStringLiteral("20"));
    }
    void recalledProgrammerErrorRequiresOriginalSettings_data()
    {
        QTest::addColumn<bool>("changeSign");
        QTest::newRow("width") << false;
        QTest::newRow("sign") << true;
    }
    void recalledProgrammerErrorRequiresOriginalSettings()
    {
        QFETCH(bool, changeSign);
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        setProgrammerConfig(page, 8, changeSign);
        const QString formula = changeSign ? QStringLiteral("128") : QStringLiteral("256");
        page.input()->setPlainText(formula);
        page.submit();
        QVERIFY(!page.history().record(2)->result.ok);
        setProgrammerConfig(page, 64, false);
        page.input()->setPlainText(QStringLiteral("original draft"));
        const auto reuse = page.findChildren<QPushButton *>(QStringLiteral("reuseFormula"));
        reuse.at(1)->click();
        QCOMPARE(page.input()->extraSelections().size(), 1);
        setProgrammerConfig(page, changeSign ? 8 : 16, false);
        QVERIFY(page.input()->extraSelections().isEmpty());
        reuse.at(0)->click();
        reuse.at(1)->click();
        QCOMPARE(page.input()->toPlainText(), formula);
        QVERIFY(page.input()->extraSelections().isEmpty());
        page.submit();
        QVERIFY(page.history().records().last().result.ok);
        QCOMPARE(page.history().programmerAnswer().text(), formula);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("original draft"));
        QVERIFY(!page.history().record(2)->result.ok);
    }
    void programmerResultsAndFrozenDetails_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<bool>("signedMode");
        QTest::addColumn<QString>("expression");
        QTest::addColumn<QString>("decimal");
        QTest::addColumn<QString>("hex");
        QTest::addColumn<QString>("binary");
        QTest::addColumn<QString>("octal");
        QTest::newRow("mask-shift") << 16 << false << QStringLiteral("(0x12 & 0x0F) << 4")
            << QStringLiteral("32") << QStringLiteral("0x0020") << QStringLiteral("0000 0000 0010 0000") << QStringLiteral("0o40");
        QTest::newRow("negative") << 8 << true << QStringLiteral("0xFF")
            << QStringLiteral("-1") << QStringLiteral("0xFF") << QStringLiteral("1111 1111") << QStringLiteral("0o377");
        QTest::newRow("zero") << 8 << false << QStringLiteral("0")
            << QStringLiteral("0") << QStringLiteral("0x00") << QStringLiteral("0000 0000") << QStringLiteral("0o0");
        QTest::newRow("max64") << 64 << false << QStringLiteral("0xFFFFFFFFFFFFFFFF")
            << QStringLiteral("18446744073709551615") << QStringLiteral("0xFFFFFFFFFFFFFFFF")
            << QStringLiteral("1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111")
            << QStringLiteral("0o1777777777777777777777");
        QTest::newRow("min64") << 64 << true << QStringLiteral("-9223372036854775808")
            << QStringLiteral("-9223372036854775808") << QStringLiteral("0x8000000000000000")
            << QStringLiteral("1000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000")
            << QStringLiteral("0o1000000000000000000000");
    }
    void programmerResultsAndFrozenDetails()
    {
        QFETCH(int, width);
        QFETCH(bool, signedMode);
        QFETCH(QString, expression);
        QFETCH(QString, decimal);
        QFETCH(QString, hex);
        QFETCH(QString, binary);
        QFETCH(QString, octal);
        CalculatorPage page;
        prepare(page, QString());
        page.findChild<QToolButton *>(QStringLiteral("programmerModeButton"))->click();
        auto *bits = page.findChild<QComboBox *>(QStringLiteral("programmerBitWidth"));
        auto *type = page.findChild<QComboBox *>(QStringLiteral("programmerIntegerType"));
        QCOMPARE(bits->currentData().toInt(), 32);
        QCOMPARE(type->currentData().toBool(), false);
        bits->setCurrentIndex(bits->findData(width));
        type->setCurrentIndex(signedMode ? 1 : 0);
        page.input()->setPlainText(expression);
        QTest::keyClick(page.input(), Qt::Key_Return);
        QCOMPARE(page.input()->toPlainText(), expression);
        QCOMPARE(page.history().count(), 0);
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.history().count(), 1);
        QVERIFY(page.history().record(1)->programmer);
        QVERIFY(page.history().record(1)->result.ok);
        QCOMPARE(page.history().record(1)->valueText(), decimal);
        QCOMPARE(page.history().answer().text(), QStringLiteral("0"));
        QCOMPARE(page.findChild<RecordText *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= ") + decimal);
        QCOMPARE(page.findChild<RecordText *>(QStringLiteral("recordHex"))->toPlainText(), QStringLiteral("HEX  ") + hex);
        auto *details = page.findChild<QWidget *>(QStringLiteral("radixDetails"));
        auto *toggle = page.findChild<QToolButton *>(QStringLiteral("toggleRadixDetails"));
        QVERIFY(details->isHidden());
        auto *badge = page.findChild<QLabel *>(QStringLiteral("recordModeBadge"));
        const QString originalBadge = badge->text();
        QVERIFY(originalBadge.contains(QStringLiteral("%1 位").arg(width)));
        page.input()->setPlainText(QStringLiteral("1/0")); // 展开详情不得求值当前草稿。
        bits->setCurrentIndex(bits->findData(width == 8 ? 64 : 8));
        type->setCurrentIndex(signedMode ? 0 : 1);
        toggle->setFocus();
        QTest::keyClick(toggle, Qt::Key_Space);
        QVERIFY(!details->isHidden());
        QCOMPARE(page.findChild<RecordText *>(QStringLiteral("recordBin"))->toPlainText(), QStringLiteral("BIN  ") + binary);
        QCOMPARE(page.findChild<RecordText *>(QStringLiteral("recordOct"))->toPlainText(), QStringLiteral("OCT  ") + octal);
        QCOMPARE(badge->text(), originalBadge);
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(page.history().programmerAnswer().text(), decimal);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1/0"));
        QTest::keyClick(toggle, Qt::Key_Space);
        QVERIFY(details->isHidden());
        auto *value = page.findChild<RecordText *>(QStringLiteral("recordResult"));
        value->setFocus();
        value->selectAll();
        page.routeEdit(QStringLiteral("actioncopy"));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("= ") + decimal);
    }
    void programmerModeRecallAndErrors()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("2^3"));
        page.submit();
        QCOMPARE(page.history().answer().text(), QStringLiteral("8"));
        page.input()->setPlainText(QStringLiteral("100+"));
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        page.input()->setPlainText(QStringLiteral("2^3"));
        page.submit();
        QCOMPARE(page.history().programmerAnswer().text(), QStringLiteral("1"));
        QCOMPARE(page.history().answer().text(), QStringLiteral("8"));
        auto *bits = page.findChild<QComboBox *>(QStringLiteral("programmerBitWidth"));
        bits->setCurrentIndex(0);
        page.input()->setPlainText(QStringLiteral("255+1"));
        page.submit();
        QCOMPARE(page.findChildren<RecordText *>(QStringLiteral("recordSources")).last()->toPlainText(),
                 QStringLiteral("含 8 位回绕"));
        const auto answer = page.history().programmerAnswer();
        page.input()->setPlainText(QStringLiteral("  1<<8"));
        page.submit();
        const auto &failed = page.history().records().last();
        QVERIFY(!failed.result.ok);
        QCOMPARE(page.history().programmerAnswer(), answer);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("  1<<8"));
        const auto highlight = page.input()->extraSelections();
        QCOMPARE(highlight.size(), 1);
        QCOMPARE(highlight.first().cursor.selectionStart(), 2 + failed.result.errorPosition);
        auto *card = page.findChildren<QFrame *>(QStringLiteral("calculationRecord")).last();
        QVERIFY(!card->findChild<QWidget *>(QStringLiteral("radixDetails")));
        page.input()->setPlainText(QStringLiteral("12+34"));
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(1);
        cursor.setPosition(4, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("100+"));
        auto *second = page.findChildren<QFrame *>(QStringLiteral("calculationRecord")).at(1);
        second->findChild<QPushButton *>(QStringLiteral("reuseFormula"))->click();
        QVERIFY(page.programmerMode());
        QCOMPARE(bits->currentData().toInt(), 32);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("2^3"));
        page.submit();
        QVERIFY(!page.programmerMode());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("100+"));
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        QCOMPARE(bits->currentData().toInt(), 8);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("12+34"));
        QCOMPARE(page.input()->textCursor().position(), 4);
        QCOMPARE(page.input()->textCursor().anchor(), 1);
        const int count = page.history().count();
        QTest::keyClick(page.input(), Qt::Key_Tab);
        auto *calculate = page.findChild<QPushButton *>(QStringLiteral("calculateButton"));
        QTRY_VERIFY(calculate->hasFocus());
        QTest::keyClick(calculate, Qt::Key_Backtab);
        QTRY_VERIFY(page.input()->hasFocus());
        QTest::keyClick(page.input(), Qt::Key_Tab);
        QTest::keyClick(calculate, Qt::Key_Space);
        QCOMPARE(page.history().count(), count + 1);
        QCOMPARE(page.history().programmerAnswer().text(), QStringLiteral("46"));
    }
    void programmerCompletionTemplates_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("position");
        QTest::addColumn<int>("key");
        QTest::newRow("answer-enter") << QStringLiteral("@上次结果") << QStringLiteral("ans") << 3 << int(Qt::Key_Return);
        QTest::newRow("complement-tab") << QStringLiteral("@取反") << QStringLiteral("~()") << 2 << int(Qt::Key_Tab);
        QTest::newRow("and-after-value") << QStringLiteral("0xFF @按位与") << QStringLiteral("0xFF & ") << 7 << int(Qt::Key_Return);
        QTest::newRow("logical-right") << QStringLiteral("0x80 @逻辑右移") << QStringLiteral("0x80 >>> ") << 9 << int(Qt::Key_Tab);
    }
    void programmerCompletionTemplates()
    {
        QFETCH(QString, query);
        QFETCH(QString, expected);
        QFETCH(int, position);
        QFETCH(int, key);
        CalculatorPage page;
        prepare(page, QString());
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        page.input()->setPlainText(query);
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        page.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QCOMPARE(page.history().count(), 0);
        QCOMPARE(page.input()->toPlainText(), query);
        QTest::keyClick(page.input(), Qt::Key(key));
        QCOMPARE(page.input()->toPlainText(), expected);
        QCOMPARE(page.input()->textCursor().position(), position);
        QCOMPARE(page.history().count(), 0);
        page.input()->undo();
        QCOMPARE(page.input()->toPlainText(), query);
        page.input()->redo();
        QCOMPARE(page.input()->toPlainText(), expected);
    }
    void programmerCompletionImeAndHelp()
    {
        CalculatorPage page;
        prepare(page, QString());
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        page.input()->setPlainText(QStringLiteral("@"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        QCOMPARE(completion(page)->model()->rowCount(), 8);
        QTest::keyClick(page.input(), Qt::Key_Escape);
        QVERIFY(!completion(page)->isVisible());
        page.submit();
        QCOMPARE(page.history().count(), 0);
        page.input()->setPlainText(QStringLiteral("@平方根"));
        page.input()->moveCursor(QTextCursor::End);
        QVERIFY(!completion(page)->isVisible());
        page.input()->setPlainText(QStringLiteral("42"));
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(page.input(), &preedit);
        page.submit();
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.history().count(), 0);
        QVERIFY(!page.findChild<QComboBox *>(QStringLiteral("programmerBitWidth"))->isEnabled());
        QVERIFY(!page.findChild<QToolButton *>(QStringLiteral("normalModeButton"))->isEnabled());
        QInputMethodEvent end;
        QApplication::sendEvent(page.input(), &end);
        page.submit();
        QCOMPARE(page.history().count(), 1);
        page.findChild<QPushButton *>(QStringLiteral("helpButton"))->click();
        auto *help = page.findChild<QDialog *>(QStringLiteral("calctabddHelpDialog"));
        QVERIFY(help);
        auto *mode = help->findChild<QComboBox *>(QStringLiteral("helpMode"));
        QCOMPARE(mode->currentIndex(), 1);
        auto *search = help->findChild<QLineEdit *>(QStringLiteral("helpSearch"));
        search->setText(QStringLiteral("^"));
        auto *text = help->findChild<QTextBrowser *>(QStringLiteral("operationsHelp"));
        QVERIFY(text->toPlainText().contains(QStringLiteral("按位异或")));
        QVERIFY(!text->toPlainText().contains(QStringLiteral("pow(")));
        mode->setCurrentIndex(0);
        search->setText(QStringLiteral("乘方函数"));
        QVERIFY(text->toPlainText().contains(QStringLiteral("pow(")));
        help->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        QVERIFY(!page.programmerMode());
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("answerBadge"))->text(), QStringLiteral("ans = 0"));
        page.findChild<QPushButton *>(QStringLiteral("helpButton"))->click();
        help = page.findChild<QDialog *>(QStringLiteral("calctabddHelpDialog"));
        QCOMPARE(help->findChild<QComboBox *>(QStringLiteral("helpMode"))->currentIndex(), 0);
        help->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::keyClick(page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition")), Qt::Key_Escape);
        QVERIFY(page.programmerMode());
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("answerBadge"))->text(), QStringLiteral("ans = 42"));
        page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        page.activateWindow();
        page.focusInput();
        QTRY_VERIFY(page.input()->hasFocus());
        page.input()->setPlainText(QStringLiteral("@"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        QCOMPARE(completion(page)->model()->rowCount(), 17);
    }
    void savedSessionAllowsProgrammerModeAndFurtherMath()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("math.calctabdd"));
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        QVERIFY(selectSession(page, path, false));
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        QVERIFY(page.programmerMode());
        setProgrammerConfig(page, 64, false);
        page.input()->setPlainText(QStringLiteral("0xFFFFFFFFFFFFFFFF"));
        page.submit();
        QCOMPARE(readSession(path).history.answer().text(), QStringLiteral("42"));
        QCOMPARE(readSession(path).history.programmerAnswer().text(), QStringLiteral("18446744073709551615"));
        page.input()->setPlainText(QStringLiteral("10+"));
        page.input()->moveCursor(QTextCursor::End);
        page.findChild<QAction *>(QStringLiteral("insertResult"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("10+42"));
        page.findChild<QAction *>(QStringLiteral("exportText"))->trigger();
        auto *exportDialog = page.findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"));
        QVERIFY(exportDialog);
        exportDialog->reject();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.submit();
        QCOMPARE(readSession(path).history.answer().text(), QStringLiteral("43"));
        QCOMPARE(readSession(path).history.programmerAnswer().text(), QStringLiteral("18446744073709551615"));
        page.findChild<QAction *>(QStringLiteral("stopSavingSession"))->trigger();
        const auto before = SessionFormat::encode(readSession(path));
        page.input()->setPlainText(QStringLiteral("100"));
        page.submit();
        QCOMPARE(SessionFormat::encode(readSession(path)), before);
        const QString other = directory.filePath(QStringLiteral("mixed.calctabdd"));
        QVERIFY(selectSession(page, other, false));
        QCOMPARE(readSession(other).history.count(), 4);
    }
    void mixedSessionRestoresThreeDraftsAndBrowsing_data()
    {
        QTest::addColumn<int>("origin");
        QTest::addColumn<int>("browse");
        for (int origin = 0; origin < 3; ++origin)
            for (int browse = -1; browse < 3; ++browse)
                QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(origin).arg(browse))) << origin << browse;
    }
    void mixedSessionRestoresThreeDraftsAndBrowsing()
    {
        QFETCH(int, origin);
        QFETCH(int, browse);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("混合😀.calctabdd"));
        const QStringList modes{QStringLiteral("normalCalculationMode"), QStringLiteral("customCalculationMode"),
                                QStringLiteral("programmerCalculationMode")};
        const QStringList drafts{QStringLiteral("普通草稿😀+"), QStringLiteral("x=3"), QStringLiteral("  ans+😀")};
        CalculationSession saved;
        {
            CalculatorPage page;
            prepare(page, QStringLiteral("0.1+0.2"));
            page.submit();
            QVERIFY(defineFormula(page, QStringLiteral("A=x+ans")));
            page.input()->setPlainText(QStringLiteral("x=2"));
            page.submit();
            page.findChild<QAction *>(modes.at(2))->trigger();
            setProgrammerConfig(page, 8, false);
            page.input()->setPlainText(QStringLiteral("255+1"));
            page.submit();
            setProgrammerConfig(page, 16, false);
            page.input()->setPlainText(QStringLiteral("ans+2"));
            page.submit();
            page.input()->setPlainText(QStringLiteral("1/0"));
            page.submit();
            page.findChild<QAction *>(modes.at(0))->trigger();
            page.input()->setPlainText(QStringLiteral("ln(0)"));
            page.submit();
            for (int mode = 0; mode < 3; ++mode)
            {
                page.findChild<QAction *>(modes.at(mode))->trigger();
                if (mode == 2) setProgrammerConfig(page, 16, true);
                setDraft(page, drafts.at(mode), 1, 3);
            }
            page.findChild<QAction *>(modes.at(origin))->trigger();
            if (browse >= 0)
            {
                page.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).at(browse)->click();
                if (browse == 2) setProgrammerConfig(page, 32, true);
                setDraft(page, browse == 1 ? QStringLiteral("x=11") : QStringLiteral("ans+7"), 4, 2);
                page.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).at(3)->click();
                setDraft(page, QStringLiteral("ans+9"), 5, 1);
                page.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).at(browse)->click();
            }
            QVERIFY(selectSession(page, path, false));
            saved = readSession(path);
            QCOMPARE(saved.historyPosition, browse);
            QCOMPARE(saved.history.count(), 6);
            QCOMPARE(saved.history.programmerAnswer().sources(), 1u);
        }
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".lock")));
        CalculatorPage restored;
        prepare(restored, QString());
        QVERIFY(selectSession(restored, path, true));
        QCOMPARE(restored.history().answer(), saved.history.answer());
        QCOMPARE(restored.history().programmerAnswer(), saved.history.programmerAnswer());
        QCOMPARE(restored.history().count(), 6);
        QCOMPARE(restored.programmerMode(), saved.input.programmer);
        QCOMPARE(restored.input()->toPlainText(), saved.input.text);
        QCOMPARE(restored.input()->textCursor().position(), saved.input.position);
        QCOMPARE(restored.input()->textCursor().anchor(), saved.input.anchor);
        QCOMPARE(SessionFormat::encode(readSession(path)), SessionFormat::encode(saved));
        const auto results = restored.findChildren<RecordText *>(QStringLiteral("recordResult"));
        QCOMPARE(results.at(0)->toPlainText(), QStringLiteral("= 0.3"));
        QCOMPARE(results.at(2)->toPlainText(), QStringLiteral("= 0"));
        QCOMPARE(results.at(3)->toPlainText(), QStringLiteral("= 2"));
        if (browse >= 0)
        {
            restored.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).at(3)->click();
            QCOMPARE(restored.input()->toPlainText(), QStringLiteral("ans+9"));
            for (int i = 3; i < 6; ++i)
                QTest::keyClick(restored.input(), Qt::Key_Down, Qt::AltModifier);
        }
        QCOMPARE(restored.input()->toPlainText(), drafts.at(origin));
        for (int mode = 0; mode < 3; ++mode)
        {
            restored.findChild<QAction *>(modes.at(mode))->trigger();
            QCOMPARE(restored.input()->toPlainText(), drafts.at(mode));
            QCOMPARE(restored.input()->textCursor().anchor(), 1);
            QCOMPARE(restored.input()->textCursor().position(), 3);
            if (mode == 2)
            {
                QCOMPARE(restored.findChild<QComboBox *>(QStringLiteral("programmerBitWidth"))->currentData().toInt(), 16);
                QCOMPARE(restored.findChild<QComboBox *>(QStringLiteral("programmerIntegerType"))->currentIndex(), 1);
            }
        }
        restored.input()->setPlainText(QStringLiteral("ans+1"));
        restored.submit();
        QCOMPARE(restored.history().records().last().id, quint64(7));
        QCOMPARE(restored.history().programmerAnswer().text(), QStringLiteral("3"));
        QCOMPARE(restored.history().programmerAnswer().sources(), 1u);
        restored.findChild<QAction *>(modes.at(0))->trigger();
        restored.input()->setPlainText(QStringLiteral("ans+1"));
        restored.submit();
        QCOMPARE(restored.history().answer().text(), QStringLiteral("3.3"));
    }
    void programmerSessionRestoresDiagnosticsOnlyForOriginalConfig_data()
    {
        QTest::addColumn<bool>("changed");
        QTest::newRow("same-config") << false;
        QTest::newRow("changed-config") << true;
    }
    void programmerSessionRestoresDiagnosticsOnlyForOriginalConfig()
    {
        QFETCH(bool, changed);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("error.calctabdd"));
        {
            CalculatorPage page;
            prepare(page, QString());
            page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
            setProgrammerConfig(page, 8, false);
            page.input()->setPlainText(QStringLiteral("256"));
            page.submit();
            if (changed) setProgrammerConfig(page, 16, false);
            QVERIFY(selectSession(page, path, false));
        }
        CalculatorPage restored;
        prepare(restored, QString());
        QVERIFY(selectSession(restored, path, true));
        QCOMPARE(restored.input()->extraSelections().isEmpty(), changed);
        QCOMPARE(restored.history().count(), 1);
        if (changed)
        {
            restored.submit();
            QCOMPARE(restored.history().programmerAnswer().text(), QStringLiteral("256"));
        }
    }
    void rejectedSessionFilesPreserveBlankPageAndBytes_data()
    {
        QTest::addColumn<bool>("oldVersion");
        QTest::newRow("version-3") << true;
        QTest::newRow("damaged-integer") << false;
    }
    void rejectedSessionFilesPreserveBlankPageAndBytes()
    {
        QFETCH(bool, oldVersion);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("old.calctabdd"));
        CalculationSession source;
        source.history.calculateProgrammer(QStringLiteral("0xFFFFFFFFFFFFFFFF"), {64, false});
        auto root = QJsonDocument::fromJson(SessionFormat::encode(source)).object();
        if (oldVersion) root.insert(QStringLiteral("version"), 3);
        else
        {
            auto answer = root.value(QStringLiteral("programmerAnswer")).toObject();
            answer.insert(QStringLiteral("bits"), 18446744073709551615.0);
            root.insert(QStringLiteral("programmerAnswer"), answer);
        }
        const auto bytes = QJsonDocument(root).toJson();
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        CalculatorPage page;
        prepare(page, QString());
        QVERIFY(selectSession(page, path, true));
        QVERIFY(!page.programmerMode());
        QVERIFY(page.input()->toPlainText().isEmpty());
        QCOMPARE(page.history().count(), 0);
        QVERIFY(!page.findChild<QAction *>(QStringLiteral("saveSessionNow"))->isEnabled());
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".lock")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
    }
    void savedProgrammerConfigUpdatesWithoutEditingText()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("config.calctabdd"));
        {
            CalculatorPage page;
            prepare(page, QString());
            page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
            setProgrammerConfig(page, 16, false);
            page.input()->setPlainText(QStringLiteral("256"));
            page.submit();
            QVERIFY(selectSession(page, path, false));
            setProgrammerConfig(page, 8, true);
            QTRY_COMPARE(readSession(path).input.programmerConfig, (ProgrammerConfig{8, true}));
            QCOMPARE(readSession(path).history.programmerAnswer().config(), (ProgrammerConfig{16, false}));
        }
        CalculatorPage restored;
        prepare(restored, QString());
        QVERIFY(selectSession(restored, path, true));
        restored.input()->setPlainText(QStringLiteral("ans"));
        restored.submit();
        QVERIFY(!restored.history().records().last().result.ok);
        QCOMPARE(restored.history().programmerAnswer().text(), QStringLiteral("256"));
        setProgrammerConfig(restored, 16, true);
        restored.submit();
        QVERIFY(restored.history().records().last().result.ok);
        QCOMPARE(readSession(path).history.programmerAnswer().config(), (ProgrammerConfig{16, true}));
        QCOMPARE(readSession(path).history.programmerAnswer().text(), QStringLiteral("256"));
    }
    void pendingFirstDefinitionSavesPreviousProgrammerDraft()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("pending.calctabdd"));
        {
            CalculatorPage page;
            prepare(page, QStringLiteral("ordinary"));
            page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
            setProgrammerConfig(page, 8, true);
            setDraft(page, QStringLiteral("5+"), 2, 1);
            page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
            page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"))->setPlainText(QStringLiteral("A=x+"));
            QVERIFY(selectSession(page, path, false));
            const auto saved = readSession(path);
            QVERIFY(saved.input.programmer);
            QCOMPARE(saved.input.programmerConfig, (ProgrammerConfig{8, true}));
            QCOMPARE(saved.normalInput.text, QStringLiteral("ordinary"));
            QVERIFY(saved.customInput.customDefinition.isEmpty());
        }
        CalculatorPage restored;
        prepare(restored, QString());
        QVERIFY(selectSession(restored, path, true));
        QVERIFY(restored.programmerMode());
        QCOMPARE(restored.input()->toPlainText(), QStringLiteral("5+"));
        QCOMPARE(restored.input()->textCursor().position(), 1);
        QCOMPARE(restored.input()->textCursor().anchor(), 2);
        restored.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        QCOMPARE(restored.input()->toPlainText(), QStringLiteral("ordinary"));
    }
    void clearSavedMixedSessionResetsAnswersAndPreservesDrafts()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("clear.calctabdd"));
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        setDraft(page, QStringLiteral("normal"), 3, 1);
        QVERIFY(defineFormula(page, QStringLiteral("A=x")));
        setDraft(page, QStringLiteral("x=2"), 2, 1);
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        setProgrammerConfig(page, 8, false);
        page.input()->setPlainText(QStringLiteral("255+1"));
        page.submit();
        setProgrammerConfig(page, 16, true);
        setDraft(page, QStringLiteral("ans+1"), 5, 2);
        QVERIFY(selectSession(page, path, false));
        page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        auto *dialog = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        QVERIFY(dialog);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        const auto saved = readSession(path);
        QCOMPARE(saved.history.count(), 0);
        QCOMPARE(saved.history.answer(), NumericValue());
        QCOMPARE(saved.history.programmerAnswer(), ProgrammerValue());
        QCOMPARE(saved.normalInput.text, QStringLiteral("normal"));
        QCOMPARE(saved.customInput.text, QStringLiteral("x=2"));
        QCOMPARE(saved.programmerInput.text, QStringLiteral("ans+1"));
        QCOMPARE(saved.input.programmerConfig, (ProgrammerConfig{16, true}));
        QCOMPARE(saved.input.position, 2);
        QCOMPARE(saved.input.anchor, 5);
    }
    void programmerConstrainedLayouts_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<int>("pointSize");
        QTest::addColumn<bool>("dark");
        QTest::newRow("narrow-light") << 360 << 680 << 12 << false;
        QTest::newRow("large-dark") << 480 << 680 << 18 << true;
        QTest::newRow("wide-light") << 960 << 900 << 12 << false;
        QTest::newRow("wide-dark") << 960 << 900 << 12 << true;
    }
    void programmerConstrainedLayouts()
    {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(int, pointSize);
        QFETCH(bool, dark);
        QWidget owner;
        QFont font = owner.font();
        font.setPointSize(pointSize);
        owner.setFont(font);
        owner.resize(width, height);
        CalculatorPage page(&owner);
        page.setGeometry(owner.rect());
        QPalette colors = page.palette();
        colors.setColor(QPalette::Window, QColor(dark ? "#202329" : "#f0f0f0"));
        colors.setColor(QPalette::Base, QColor(dark ? "#202329" : "#ffffff"));
        colors.setColor(QPalette::Text, QColor(dark ? "#e4e8ef" : "#202329"));
        owner.show();
        page.show();
        page.setPalette(colors);
        owner.activateWindow();
        page.findChild<QAction *>(QStringLiteral("programmerCalculationMode"))->trigger();
        auto *bits = page.findChild<QComboBox *>(QStringLiteral("programmerBitWidth"));
        bits->setCurrentIndex(3);
        page.input()->setPlainText(QStringLiteral("0xFFFFFFFFFFFFFFFF"));
        auto *composer = page.findChild<QScrollArea *>(QStringLiteral("formulaComposerScroll"));
        auto *calculate = page.findChild<QPushButton *>(QStringLiteral("calculateButton"));
        QTRY_VERIFY(([&]() {
            composer->ensureWidgetVisible(calculate, 0, 0);
            return page.childAt(calculate->mapTo(&page, calculate->rect().center())) == calculate;
        })());
        QTest::mouseClick(calculate, Qt::LeftButton);
        QCOMPARE(page.history().programmerAnswer().text(), QStringLiteral("18446744073709551615"));
        auto *history = page.findChild<QScrollArea *>(QStringLiteral("calculationHistory"));
        auto *toggle = page.findChild<QToolButton *>(QStringLiteral("toggleRadixDetails"));
        QTRY_VERIFY(([&]() {
            history->ensureWidgetVisible(toggle, 0, 0);
            return page.childAt(toggle->mapTo(&page, toggle->rect().center())) == toggle;
        })());
        QTest::mouseClick(toggle, Qt::LeftButton);
        auto *binary = page.findChild<RecordText *>(QStringLiteral("recordBin"));
        QTRY_VERIFY(binary->height() >= binary->document()->size().height());
        QCOMPARE(binary->toPlainText().remove(QLatin1Char(' ')), QStringLiteral("BIN") + QString(64, QLatin1Char('1')));
        QTRY_COMPARE(history->horizontalScrollBar()->maximum(), 0);
        QTRY_COMPARE(composer->horizontalScrollBar()->maximum(), 0);
        for (const auto *name : {"normalModeButton", "customModeButton", "programmerModeButton",
                                 "programmerBitWidth", "programmerIntegerType"})
        {
            auto *control = page.findChild<QWidget *>(QString::fromLatin1(name));
            QVERIFY(control);
            QTRY_VERIFY(control->width() >= control->minimumSizeHint().width());
            QTRY_VERIFY(([&]() {
                composer->ensureWidgetVisible(control, 0, 0);
                const QRect bounds(control->mapTo(composer->viewport(), QPoint()), control->size());
                return composer->viewport()->rect().contains(bounds);
            })());
        }
        auto *card = page.findChild<QFrame *>(QStringLiteral("calculationRecord"));
        composer->verticalScrollBar()->setValue(0);
        history->verticalScrollBar()->setValue(0);
        // 样式、换行及滚动条会排队更新布局；验收最终尺寸，不截取中间帧。
        QTRY_VERIFY(([&]() {
            QCoreApplication::processEvents();
            auto *panel = page.findChild<QWidget *>(QStringLiteral("programmerPanel"));
            auto *hint = page.findChild<QLabel *>(QStringLiteral("programmerHint"));
            auto *availability = page.findChild<QLabel *>(QStringLiteral("programmerAvailabilityHint"));
            auto *inputHint = page.findChild<QLabel *>(QStringLiteral("inputHint"));
            auto *body = composer->widget();
            auto *type = page.findChild<QComboBox *>(QStringLiteral("programmerIntegerType"));
            const int settingsBottom = qMax(bits->mapTo(panel, QPoint(0, bits->height())).y(),
                                           type->mapTo(panel, QPoint(0, type->height())).y());
            if (hint->y() < settingsBottom || availability->y() < hint->geometry().bottom()
                || panel->height() < availability->geometry().bottom()
                || page.input()->y() < panel->geometry().bottom()
                || inputHint->y() < page.input()->geometry().bottom()
                || calculate->y() < inputHint->geometry().bottom()
                || body->height() < calculate->geometry().bottom()) return false;
            for (auto *text : card->findChildren<RecordText *>())
            {
                if (text->isHidden()) continue;
                if (qAbs(text->height() - text->document()->size().height()) > 2 * text->frameWidth() + 2)
                    return false;
                if (text->document()->textWidth() < text->viewport()->width() - 2) return false;
            }
            return true;
        })());
        QTRY_VERIFY(([&]() {
            int bottom = 0;
            for (const auto *name : {"recordNumber", "recordModeBadge", "reuseFormula"})
            {
                auto *control = card->findChild<QWidget *>(QString::fromLatin1(name));
                bottom = qMax(bottom, control->mapTo(card, QPoint(0, control->height())).y());
            }
            const int top = card->findChild<RecordText *>(QStringLiteral("recordFormula"))->mapTo(card, QPoint()).y();
            return top >= bottom && top - bottom <= 12;
        })());
        QTRY_VERIFY(([&]() {
            int bottom = 0;
            for (const auto *name : {"recordFormula", "recordResult", "recordHex", "recordBin", "recordOct"})
            {
                auto *text = card->findChild<RecordText *>(QString::fromLatin1(name));
                const int top = text->mapTo(card, QPoint()).y();
                if (top < bottom || text->viewport()->height() < text->document()->size().height()) return false;
                bottom = top + text->height();
            }
            return bottom <= card->height();
        })());
        QVERIFY(page.styleSheet().contains(dark ? QStringLiteral("#202329") : QStringLiteral("#f0f0f0")));
        const QString directory = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        if (!directory.isEmpty())
        {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(page.grab().save(directory + QStringLiteral("/programmer-%1.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
        }
    }
    void refactoredControlsFitNarrowPage_data()
    {
        QTest::addColumn<int>("pointSize");
        QTest::newRow("normal-font") << 12;
        QTest::newRow("large-font") << 18;
    }
    void refactoredControlsFitNarrowPage()
    {
        QFETCH(int, pointSize);
        QWidget owner;
        QFont font = owner.font();
        font.setPointSize(pointSize);
        owner.setFont(font);
        owner.resize(480, 900);
        CalculatorPage page(&owner);
        page.setGeometry(owner.rect());
        owner.show();
        page.show();
        owner.activateWindow();
        page.focusInput();
        page.input()->setPlainText(QString(50, QLatin1Char('9')));
        page.submit();
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        auto *recall = page.findChild<QLabel *>(QStringLiteral("historyRecallBadge"));
        QVERIFY(recall && recall->isVisible());
        for (const QString &name : {QStringLiteral("normalModeButton"), QStringLiteral("customModeButton")})
        {
            auto *button = page.findChild<QToolButton *>(name);
            QVERIFY(button);
            QTRY_VERIFY(button->width() >= button->sizeHint().width());
            const QRect bounds(button->mapTo(&page, QPoint()), button->size());
            QVERIFY(page.rect().contains(bounds));
            QCOMPARE(page.childAt(bounds.center()), button);
        }
        auto *ans = page.findChild<QLabel *>(QStringLiteral("answerBadge"));
        QVERIFY(ans && ans->toolTip().contains(page.history().answer().text()));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QVERIFY(defineFormula(page, QStringLiteral("A=parameter_one+parameter_two+parameter_three+parameter_four")));
        page.input()->setPlainText(QStringLiteral("parameter_one=1\nparameter_two=2\nparameter_three=3\nparameter_four=4"));
        page.submit();
        auto *scroll = page.findChild<QScrollArea *>();
        QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        auto *record = page.findChildren<QFrame *>(QStringLiteral("calculationRecord")).last();
        for (const QString &name : {QStringLiteral("reuseFormula"), QStringLiteral("recordActions")})
        {
            auto *button = record->findChild<QAbstractButton *>(name);
            QVERIFY(button);
            QTRY_VERIFY(([&]() {
                scroll->ensureWidgetVisible(button, 0, 0);
                return scroll->viewport()->rect().contains(
                    QRect(button->mapTo(scroll->viewport(), QPoint()), button->size()));
            })());
            const QRect bounds(button->mapTo(scroll->viewport(), QPoint()), button->size());
            QVERIFY2(scroll->viewport()->rect().contains(bounds),
                qPrintable(QStringLiteral("%1 bounds=%2,%3 %4x%5 viewport=%6x%7").arg(name)
                    .arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())
                    .arg(scroll->viewport()->width()).arg(scroll->viewport()->height())));
            QCOMPARE(page.childAt(button->mapTo(&page, button->rect().center())), button);
        }
        auto *definition = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        definition->moveCursor(QTextCursor::End);
        definition->insertPlainText(QStringLiteral("+1"));
        auto *apply = page.findChild<QPushButton *>(QStringLiteral("applyDefinitionButton"));
        QTRY_VERIFY(apply->width() >= apply->sizeHint().width());
        const QRect applyBounds(apply->mapTo(&page, QPoint()), apply->size());
        QVERIFY(page.rect().contains(applyBounds));
        QCOMPARE(page.childAt(applyBounds.center()), apply);
        QTest::mouseClick(apply, Qt::LeftButton);
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("customDefinitionStateBadge"))->text(), QStringLiteral("已应用"));
        // 应用定义会异步改变多个控件高度；先核对最终排版，再截取界面。
        QCoreApplication::processEvents();
        auto *composer = page.findChild<QWidget *>(QStringLiteral("formulaComposer"));
        auto *hint = page.findChild<QLabel *>(QStringLiteral("inputHint"));
        auto *parameterHeader = page.findChild<QWidget *>(QStringLiteral("customParameterHeader"));
        QVERIFY(composer && hint && parameterHeader);
        QTRY_VERIFY(hint->mapTo(composer, QPoint()).y()
            >= page.input()->mapTo(composer, QPoint(0, page.input()->height())).y());
        QTRY_VERIFY(parameterHeader->mapTo(composer, QPoint()).y()
            >= definition->mapTo(composer, QPoint(0, definition->height())).y());
        for (auto *card : page.findChildren<QFrame *>(QStringLiteral("calculationRecord")))
        {
            QTRY_VERIFY(([&]() {
                int previousBottom = 0;
                for (auto *button : card->findChildren<QAbstractButton *>())
                    previousBottom = qMax(previousBottom, button->mapTo(card, QPoint(0, button->height())).y());
                for (const QString &name : {QStringLiteral("recordCustomMeta"), QStringLiteral("recordFormula"),
                                           QStringLiteral("recordResult"), QStringLiteral("recordSources")})
                {
                    auto *text = card->findChild<RecordText *>(name);
                    if (!text) continue;
                    const int top = text->mapTo(card, QPoint()).y();
                    if (top < previousBottom || text->viewport()->height() < text->document()->size().height())
                        return false;
                    previousBottom = top + text->height();
                }
                return previousBottom <= card->height();
            })());
        }
        QTRY_COMPARE(ans->text(), QStringLiteral("ans = 10"));
        auto *composerScroll = page.findChild<QScrollArea *>(QStringLiteral("formulaComposerScroll"));
        QVERIFY(composerScroll);
        composerScroll->verticalScrollBar()->setValue(0);
        scroll->ensureWidgetVisible(record, 0, 0);
        const auto directory = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        if (!directory.isEmpty())
        {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(page.grab().save(directory + QStringLiteral("/review-narrow-%1.png").arg(pointSize)));
        }
    }
    void recordTextWrapsAndCopiesAfterResize()
    {
        CalculatorPage page;
        prepare(page, QString());
        QVERIFY(defineFormula(page, QStringLiteral("A=first_parameter+second_parameter")));
        page.input()->setPlainText(QStringLiteral("first_parameter=") + QString(45, QLatin1Char('1'))
            + QStringLiteral("\nsecond_parameter=2"));
        page.submit();
        for (int width : {480, 800, 440})
        {
            page.resize(width, 900);
            for (const QString &name : {QStringLiteral("recordFormula"), QStringLiteral("recordCustomMeta")})
            {
                auto *text = page.findChild<RecordText *>(name);
                QVERIFY(text);
                QTRY_VERIFY(text->viewport()->width() > 0);
                QTRY_VERIFY(text->viewport()->height() >= text->document()->size().height());
                QTRY_COMPARE(text->horizontalScrollBar()->maximum(), 0);
                text->setFocus();
                QTRY_VERIFY(text->hasFocus());
                text->selectAll();
                page.routeEdit(QStringLiteral("actioncopy"));
                QCOMPARE(QApplication::clipboard()->text(), text->toPlainText());
                if (name == QStringLiteral("recordFormula"))
                    QCOMPARE(text->toPlainText(), page.history().records().first().expression);
            }
        }
    }
    void modeSwitchAndDefinitionApplyAreKeyboardAccessible()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("2+3"));
        QTRY_VERIFY(page.input()->hasFocus());
        auto *custom = page.findChild<QToolButton *>(QStringLiteral("customModeButton"));
        QVERIFY(custom);
        for (int count = 0; count < 24 && !custom->hasFocus(); ++count)
        {
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
            QCoreApplication::processEvents();
        }
        QVERIFY(custom->hasFocus());
        QTest::keyClick(custom, Qt::Key_Space);
        auto *definition = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        QVERIFY(definition && definition->isVisible());
        definition->setPlainText(QStringLiteral("A=x+1"));
        definition->setFocus();
        auto *apply = page.findChild<QPushButton *>(QStringLiteral("applyDefinitionButton"));
        QVERIFY(apply);
        for (int count = 0; count < 24 && !apply->hasFocus(); ++count)
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Backtab);
        QVERIFY(apply->hasFocus());
        QTest::keyClick(apply, Qt::Key_Space);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x="));
        QVERIFY(page.findChild<QToolButton *>(QStringLiteral("customModeButton"))->isChecked());
        QCOMPARE(page.history().count(), 0);
    }
    void constrainedComposerKeepsEditorsAndActionsUsable()
    {
        QWidget owner;
        QFont font = owner.font();
        font.setPointSize(18);
        owner.setFont(font);
        owner.resize(480, 760);
        CalculatorPage page(&owner);
        page.setGeometry(owner.rect());
        owner.show();
        page.show();
        owner.activateWindow();
        QVERIFY(defineFormula(page, QStringLiteral("A=a+b+c+d")));
        page.input()->setPlainText(QStringLiteral("a=1\nb=2\nc=3\nd=4"));
        auto *composer = page.findChild<QWidget *>(QStringLiteral("formulaComposer"));
        auto *hint = page.findChild<QLabel *>(QStringLiteral("inputHint"));
        auto *parameters = page.findChild<QWidget *>(QStringLiteral("customParameterHeader"));
        auto *definition = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        auto *calculate = page.findChild<QPushButton *>(QStringLiteral("calculateButton"));
        QVERIFY(composer && hint && parameters && definition && calculate);
        QTRY_VERIFY(hint->mapTo(composer, QPoint()).y()
            >= page.input()->mapTo(composer, QPoint(0, page.input()->height())).y());
        QTRY_VERIFY(parameters->mapTo(composer, QPoint()).y()
            >= definition->mapTo(composer, QPoint(0, definition->height())).y());
        // 输入区能在短窗口里滚动到按钮，鼠标实际命中后正常提交。
        auto *scroll = page.findChild<QScrollArea *>(QStringLiteral("formulaComposerScroll"));
        QVERIFY(scroll);
        QTRY_VERIFY(([&]() {
            scroll->ensureWidgetVisible(calculate, 0, 0);
            return page.childAt(calculate->mapTo(&page, calculate->rect().center())) == calculate;
        })());
        QTest::mouseClick(calculate, Qt::LeftButton);
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(page.history().answer().text(), QStringLiteral("10"));
    }
    void pasteFromHistoryTargetsVisibleEditor_data()
    {
        QTest::addColumn<bool>("formulaLabel");
        QTest::addColumn<int>("mode");
        for (bool label : {false, true})
            for (int mode = 0; mode < 3; ++mode)
                QTest::newRow(qPrintable(QStringLiteral("%1-mode-%2").arg(label ? "formula" : "result").arg(mode))) << label << mode;
    }
    void pasteFromHistoryTargetsVisibleEditor()
    {
        QFETCH(bool, formulaLabel);
        QFETCH(int, mode);
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("100+"));
        page.input()->moveCursor(QTextCursor::End);
        auto *definition = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        if (mode == 1)
            page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        else if (mode == 2)
        {
            QVERIFY(defineFormula(page, QStringLiteral("A=x")));
            page.input()->setPlainText(QStringLiteral("x=7"));
            page.input()->moveCursor(QTextCursor::End);
        }
        if (mode) { definition->setPlainText(QStringLiteral("B=")); definition->moveCursor(QTextCursor::End); }
        const QString inputBefore = page.input()->toPlainText();
        QWidget *record = formulaLabel
            ? static_cast<QWidget *>(page.findChild<RecordText *>(QStringLiteral("recordFormula")))
            : static_cast<QWidget *>(page.findChild<RecordText *>(QStringLiteral("recordResult")));
        record->setFocus();
        QTRY_VERIFY(record->hasFocus());
        QApplication::clipboard()->setText(QStringLiteral("99"));
        page.routeEdit(QStringLiteral("actionpaste"));
        auto *target = mode ? definition : page.input();
        QTRY_VERIFY(target->hasFocus());
        QCOMPARE(target->toPlainText(), mode ? QStringLiteral("B=99") : QStringLiteral("100+99"));
        if (mode) QCOMPARE(page.input()->toPlainText(), inputBefore);
        page.routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(target->toPlainText(), mode ? QStringLiteral("B=") : inputBefore);
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(page.history().answer().text(), QStringLiteral("42"));
    }
    void completionSurvivesDocumentReset()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("@sq"));
        QTRY_VERIFY(completion(page)->isVisible());
        // setPlainText 发出的同步信号可能临时带空光标；Debug 与 Release 都需覆盖。
        for (const QString &text : {QStringLiteral("2*(3+4)"), QString(), QStringLiteral("@sq")})
        {
            page.input()->setPlainText(text);
            page.input()->moveCursor(QTextCursor::End);
        }
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Return);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("sqrt()"));
        QVERIFY(defineFormula(page, QStringLiteral("A=x+y")));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=\ny="));
        page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("sqrt()"));
        page.input()->setPlainText(QStringLiteral("@pi"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Tab);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("pi"));
    }
    void typedResultsCopyInsertAndSourceRows()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("sin(0)"));
        page.submit();
        const auto original = page.history().answer();
        QVERIFY(original.isBinary());
        QCOMPARE(original.sources(), unsigned(NumericValue::Approximate));
        QCOMPARE(page.findChild<RecordText *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 0"));
        QCOMPARE(page.findChild<RecordText *>(QStringLiteral("recordSources"))->toPlainText(), QStringLiteral("来源：含近似计算"));
        page.findChild<QAction *>(QStringLiteral("copyValue"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0"));
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().contains(QStringLiteral("不保留原计算来源")));
        page.findChild<QAction *>(QStringLiteral("copyCalculation"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("sin(0)\n= 0\n来源：含近似计算"));
        page.input()->setPlainText(QStringLiteral("ans+0.1"));
        page.submit();
        QVERIFY(page.history().answer().isBinary());
        QCOMPARE(page.history().answer().sources(), unsigned(NumericValue::Approximate | NumericValue::ConversionLoss));
        page.input()->clear();
        auto *insert = page.findChildren<QAction *>(QStringLiteral("insertResult")).first();
        QCOMPARE(insert->text(), QStringLiteral("插入结果数值"));
        insert->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("0"));
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().contains(QStringLiteral("不保留原计算来源")));
        page.input()->insertPlainText(QStringLiteral("+0.1"));
        page.submit();
        QCOMPARE(page.history().answer(), decimalNumber(QStringLiteral("0.1")));
        QCOMPARE(page.history().records().first().result.value, original);
        QCOMPARE(page.findChildren<RecordText *>(QStringLiteral("recordSources")).size(), 2);
    }
    void decimalCustomAndSavedSources()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("typed.calctabdd"));
        NumericValue savedAnswer;
        {
            CalculatorPage page;
            prepare(page, QStringLiteral("draft"));
            QVERIFY(defineFormula(page, QStringLiteral("A=x+y")));
            page.input()->setPlainText(QStringLiteral("x=0.100\ny=0.200"));
            page.submit();
            QCOMPARE(page.history().answer(), decimalNumber(QStringLiteral("0.3")));
            QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=0.100\ny=0.200"));
            QCOMPARE(page.findChild<RecordText *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 0.3"));
            page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
            page.input()->setPlainText(QStringLiteral("ans-0.3"));
            page.submit();
            QCOMPARE(page.history().answer(), NumericValue());
            page.input()->setPlainText(QStringLiteral("sin(1/3)"));
            page.submit();
            savedAnswer = page.history().answer();
            QCOMPARE(savedAnswer.sources(), unsigned(NumericValue::Rounded | NumericValue::Approximate | NumericValue::ConversionLoss));
            page.input()->setPlainText(QStringLiteral("ans+"));
            QVERIFY(selectSession(page, path, false));
            QCOMPARE(readSession(path).history.answer(), savedAnswer);
        }
        CalculatorPage restored;
        prepare(restored, QString());
        QVERIFY(selectSession(restored, path, true));
        QCOMPARE(restored.history().answer(), savedAnswer);
        QCOMPARE(restored.input()->toPlainText(), QStringLiteral("ans+"));
        QCOMPARE(restored.findChild<RecordText *>(QStringLiteral("recordSources"))->toPlainText(), savedAnswer.sourceText());
        restored.input()->setPlainText(QStringLiteral("1/0"));
        restored.submit();
        QCOMPARE(restored.history().answer(), savedAnswer);
        restored.input()->setPlainText(QStringLiteral("ans-ans"));
        restored.submit();
        QVERIFY(restored.history().answer().isBinary());
        QVERIFY(restored.history().answer().isZero());
        QCOMPARE(restored.history().answer().sources(), savedAnswer.sources());
        restored.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        QCOMPARE(restored.input()->toPlainText(), QStringLiteral("x=0.100\ny=0.200"));
    }
    void negativeZeroAndLongResults_data()
    {
        QTest::addColumn<QString>("formula");
        QTest::addColumn<int>("width");
        for (int width : {440, 760})
        {
            QTest::newRow(qPrintable(QStringLiteral("negative-zero-%1").arg(width))) << QStringLiteral("-0.000") << width;
            QTest::newRow(qPrintable(QStringLiteral("fifty-digits-%1").arg(width))) << QString(50, '9') << width;
            QTest::newRow(qPrintable(QStringLiteral("maximum-length-%1").arg(width))) << QStringLiteral("-9.") + QString(49, '9') + QStringLiteral("e-6") << width;
            QTest::newRow(qPrintable(QStringLiteral("rounded-%1").arg(width))) << QStringLiteral("1/3") << width;
            QTest::newRow(qPrintable(QStringLiteral("all-sources-%1").arg(width))) << QStringLiteral("sin(1/3)") << width;
        }
    }
    void negativeZeroAndLongResults()
    {
        QFETCH(QString, formula);
        QFETCH(int, width);
        CalculatorPage page;
        prepare(page, formula);
        QFont font = page.font();
        font.setPointSize(18);
        page.setFont(font);
        page.resize(width, 720);
        page.submit();
        QVERIFY(page.history().records().first().result.ok);
        auto *label = page.findChild<RecordText *>(QStringLiteral("recordResult"));
        const auto text = page.history().answer().text();
        QCOMPARE(label->toPlainText(), QStringLiteral("= ") + text);
        QCoreApplication::processEvents();
        label->setFocus();
        QTRY_VERIFY(label->hasFocus());
        QTextCursor selection = label->textCursor();
        selection.setPosition(2);
        selection.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        label->setTextCursor(selection);
        QCOMPARE(label->textCursor().selectedText(), text);
        page.routeEdit(QStringLiteral("actioncopy"));
        QCOMPARE(QApplication::clipboard()->text(), text);
        page.findChild<QAction *>(QStringLiteral("copyValue"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), text);
        auto *actions = page.findChild<QToolButton *>(QStringLiteral("recordActions"));
        QVERIFY(actions->isVisible());
        QVERIFY(page.rect().contains(actions->mapTo(&page, actions->rect().center())));
        if (text.size() > 40)
        {
            const int lines = qMax(1, (label->fontMetrics().horizontalAdvance(label->toPlainText()) + label->width() - 1) / label->width());
            QVERIFY2(label->height() >= lines * label->fontMetrics().height(), qPrintable(QStringLiteral("height=%1 width=%2 lines=%3").arg(label->height()).arg(label->width()).arg(lines)));
        }
        if (auto *sources = page.findChild<RecordText *>(QStringLiteral("recordSources")))
        {
            QCOMPARE(sources->toPlainText(), page.history().answer().sourceText());
            // 按实际文档排版检查可见高度，含中文字体回退与窄窗口换行。
            const qreal needed = sources->document()->size().height();
            QVERIFY2(sources->viewport()->height() >= needed, qPrintable(QStringLiteral("source height=%1 needed=%2 width=%3").arg(sources->viewport()->height()).arg(needed).arg(sources->width())));
            sources->setFocus();
            QTRY_VERIFY(sources->hasFocus());
            sources->selectAll();
            page.routeEdit(QStringLiteral("actioncopy"));
            QCOMPARE(QApplication::clipboard()->text(), page.history().answer().sourceText());
            QVERIFY(page.rect().contains(sources->mapTo(&page, sources->rect().bottomRight())));
        }
        const auto directory = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        if (!directory.isEmpty())
        {
            QDir().mkpath(directory);
            QVERIFY(page.grab().save(directory + QStringLiteral("/numeric-%1.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
        }
    }
    void customDefinitionAndExplicitSubmission()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("2*(3+4)"));
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(2);
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        QVERIFY(defineFormula(page, QStringLiteral("A=x+y")));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=\ny="));
        QCOMPARE(page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"))->toPlainText(), QStringLiteral("A=x+y"));
        page.input()->setPlainText(QStringLiteral("x=1\ny=2"));
        QTest::qWait(250);
        QCOMPARE(page.history().count(), 0);
        QVERIFY(page.findChildren<RecordText *>(QStringLiteral("recordResult")).isEmpty());
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(asDouble(page.history().answer()), 3.0);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=1\ny=2"));
        QCOMPARE(page.findChild<RecordText *>(QStringLiteral("recordFormula"))->toPlainText(), QStringLiteral("A=1+2"));
        QCOMPARE(page.findChild<RecordText *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 3"));
        page.input()->setPlainText(QStringLiteral("x=4\ny=5"));
        page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("2*(3+4)"));
        QCOMPARE(page.input()->textCursor().position(), 5);
        QCOMPARE(page.input()->textCursor().anchor(), 2);
        page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=4\ny=5"));
        QCOMPARE(page.history().count(), 1);
        page.submit();
        QCOMPARE(page.history().count(), 2);
        QCOMPARE(page.history().records().first().expression, QStringLiteral("A=1+2"));
        QCOMPARE(page.history().records().last().expression, QStringLiteral("A=4+5"));
        page.submit();
        QCOMPARE(page.history().count(), 3);
        QCOMPARE(asDouble(page.history().answer()), 9.0);
    }
    void customParametersRequireSeparateLines()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        QVERIFY(defineFormula(page, QStringLiteral("A=x+y")));
        const QString oneLine = QStringLiteral("x=1 y=2");
        page.input()->setPlainText(oneLine);
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(asDouble(page.history().answer()), 42.0);
        QCOMPARE(page.input()->toPlainText(), oneLine);
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().contains(QStringLiteral("每行只能填写一个参数")));
        page.input()->setPlainText(QStringLiteral("x=1\ny=2"));
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.history().count(), 2);
        QCOMPARE(asDouble(page.history().answer()), 3.0);
        QCOMPARE(page.history().records().last().expression, QStringLiteral("A=1+2"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=1\ny=2"));
        page.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).last()->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=1\ny=2"));
    }
    void inlineDefinitionCompletion_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<int>("position");
        QTest::addColumn<QString>("after");
        QTest::addColumn<int>("cursor");
        QTest::newRow("assignment") << QStringLiteral("A=@sq") << 5 << QStringLiteral("A=sqrt()") << 7;
        QTest::newRow("operator") << QStringLiteral("A=x+@sq") << 7 << QStringLiteral("A=x+sqrt()") << 9;
        QTest::newRow("existing-arguments") << QStringLiteral("A=@sq(x)+1") << 5 << QStringLiteral("A=sqrt(x)+1") << 7;
        QTest::newRow("chinese") << QStringLiteral("A=@平方根") << 6 << QStringLiteral("A=sqrt()") << 7;
        QTest::newRow("constant") << QStringLiteral("A=@pi*r^2") << 5 << QStringLiteral("A=pi*r^2") << 4;
    }
    void inlineDefinitionCompletion()
    {
        QFETCH(QString, before);
        QFETCH(int, position);
        QFETCH(QString, after);
        QFETCH(int, cursor);
        CalculatorPage page;
        prepare(page, QStringLiteral("normal draft"));
        page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        auto *edit = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        edit->setPlainText(before);
        auto textCursor = edit->textCursor();
        textCursor.setPosition(position);
        edit->setTextCursor(textCursor);
        auto *popup = edit->findChild<QCompleter *>()->popup();
        QTRY_VERIFY(popup->isVisible());
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(edit->toPlainText(), after);
        QCOMPARE(edit->textCursor().position(), cursor);
        QVERIFY(!page.input()->isVisible());
        QCOMPARE(page.history().count(), 0);
        page.routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(edit->toPlainText(), before);
        page.routeEdit(QStringLiteral("actionredo"));
        QCOMPARE(edit->toPlainText(), after);
        QTest::keyClick(edit, Qt::Key_Escape);
        QVERIFY(!edit->isVisible());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("normal draft"));
    }
    void inlineDefinitionEnterAppliesWithoutCalculation()
    {
        CalculatorPage page;
        prepare(page, QString());
        page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        auto *edit = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        QTest::keyClicks(edit, "A=@sq");
        auto *popup = edit->findChild<QCompleter *>()->popup();
        QTRY_VERIFY(popup->isVisible());
        QTest::keyClick(popup, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!page.input()->isVisible());
        page.findChild<QAction *>(QStringLiteral("restoreSession"))->trigger();
        QVERIFY(!page.findChild<QFileDialog *>());
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("customDefinitionError"))->text().contains(QLatin1Char('@')));
        QTest::keyClick(popup, Qt::Key_Return);
        QTest::keyClicks(edit, "x");
        QTest::keyClick(edit, Qt::Key_Enter, Qt::KeypadModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x="));
        QVERIFY(page.input()->hasFocus());
        QCOMPARE(page.history().count(), 0);
        page.input()->setPlainText(QStringLiteral("x=9"));
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(asDouble(page.history().answer()), 3.0);
        edit->setFocus();
        edit->setPlainText(QStringLiteral("B=x+y"));
        page.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QCOMPARE(page.history().count(), 1);
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=9\ny="));
        QCOMPARE(page.history().count(), 1);
    }
    void inlineDefinitionDraftAndConfirmedSession()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("inline.calctabdd"));
        CalculatorPage page;
        prepare(page, QStringLiteral("ordinary draft"));
        QVERIFY(defineFormula(page, QStringLiteral("A=x")));
        page.input()->setPlainText(QStringLiteral("x=2"));
        page.submit();
        auto *edit = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        edit->setPlainText(QStringLiteral("B=x+y"));
        page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ordinary draft"));
        page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        QCOMPARE(edit->toPlainText(), QStringLiteral("B=x+y"));
        page.submit();
        QCOMPARE(page.history().count(), 1);
        QVERIFY(selectSession(page, path, false));
        const auto session = readSession(path);
        QCOMPARE(session.input.customDefinition, QStringLiteral("A=x"));
        QVERIFY(session.input.definitionDraft.isNull());
        QCOMPARE(session.input.text, QStringLiteral("x=2"));
        QCOMPARE(edit->toPlainText(), QStringLiteral("B=x+y"));
        QTest::keyClick(edit, Qt::Key_Return);
        page.findChild<QAction *>(QStringLiteral("saveSessionNow"))->trigger();
        QTRY_COMPARE(readSession(path).input.customDefinition, QStringLiteral("B=x+y"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=2\ny="));
    }
    void inlineDefinitionImeDoesNotApply()
    {
        CalculatorPage page;
        prepare(page, QString());
        QVERIFY(defineFormula(page, QStringLiteral("A=x")));
        auto *edit = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        edit->setFocus();
        edit->setPlainText(QStringLiteral("B=@"));
        edit->moveCursor(QTextCursor::End);
        auto *popup = edit->findChild<QCompleter *>()->popup();
        QTRY_VERIFY(popup->isVisible());
        QInputMethodEvent preedit(QStringLiteral("平方"), {});
        QApplication::sendEvent(edit, &preedit);
        QVERIFY(!popup->isVisible());
        QTest::keyClick(edit, Qt::Key_Return);
        page.submit();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x="));
        QCOMPARE(page.history().count(), 0);
        QInputMethodEvent end;
        QApplication::sendEvent(edit, &end);
        edit->setPlainText(QStringLiteral("B=@平方根"));
        edit->moveCursor(QTextCursor::End);
        QTRY_VERIFY(popup->isVisible());
        QTest::keyClick(popup, Qt::Key_Tab);
        QCOMPARE(edit->toPlainText(), QStringLiteral("B=sqrt()"));
    }
    void customRedefinitionKeepsValuesAndRejectsInvalidDefinition()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("ordinary draft"));
        QVERIFY(defineFormula(page, QStringLiteral("A=x+y")));
        page.input()->setPlainText(QStringLiteral("x=1e-\ny=2"));
        QVERIFY(defineFormula(page, QStringLiteral("B=y+x+z")));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("y=2\nx=1e-\nz="));
        auto *edit = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        edit->setFocus();
        edit->setPlainText(QStringLiteral("B=sqrt(x,y)"));
        QTest::keyClick(edit, Qt::Key_Return);
        QVERIFY(!page.findChild<QLabel *>(QStringLiteral("customDefinitionError"))->text().isEmpty());
        QCOMPARE(edit->toPlainText(), QStringLiteral("B=sqrt(x,y)"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("y=2\nx=1e-\nz="));
        page.submit();
        QCOMPARE(page.history().count(), 0);
        QTest::keyClick(edit, Qt::Key_Escape);
        QCOMPARE(edit->toPlainText(), QStringLiteral("B=y+x+z"));
        QVERIFY(defineFormula(page, QStringLiteral("C=y")));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("y=2"));
        page.submit();
        QCOMPARE(asDouble(page.history().answer()), 2.0);
        QVERIFY(defineFormula(page, QStringLiteral("D=ans+1")));
        QVERIFY(page.input()->toPlainText().isEmpty());
        page.submit();
        QCOMPARE(asDouble(page.history().answer()), 3.0);
    }
    void customInputValidationAndNumericResultInsertion()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("-2"));
        page.submit();
        QVERIFY(defineFormula(page, QStringLiteral("A=x^2+ans")));
        page.submit();
        QCOMPARE(page.history().count(), 1);
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().contains(QStringLiteral("数字")));
        page.findChild<QAction *>(QStringLiteral("insertResult"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=-2"));
        page.submit();
        QCOMPARE(page.history().records().last().expression, QStringLiteral("A=(-2)^2+ans"));
        QCOMPARE(asDouble(page.history().answer()), 2.0);
        QVERIFY(defineFormula(page, QStringLiteral("B=1/x")));
        page.input()->setPlainText(QStringLiteral("x=0"));
        page.submit();
        QCOMPARE(page.history().count(), 3);
        QCOMPARE(asDouble(page.history().answer()), 2.0);
        QCOMPARE(page.history().records().last().result.error, CalculationError::DivisionByZero);
        QVERIFY(page.input()->extraSelections().isEmpty());
        page.input()->setPlainText(QStringLiteral("x=2"));
        page.submit();
        QCOMPARE(asDouble(page.history().answer()), 0.5);
        page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.submit();
        QCOMPARE(asDouble(page.history().answer()), 1.5);
    }
    void customMixedHistoryRestoresBothDraftsAndTemporaryEdits()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("10"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("normal draft"));
        QVERIFY(defineFormula(page, QStringLiteral("A=x+y")));
        page.input()->setPlainText(QStringLiteral("x=1\ny=2"));
        page.submit();
        QVERIFY(defineFormula(page, QStringLiteral("B=z*2")));
        page.input()->setPlainText(QStringLiteral("z=8"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=1\ny=2"));
        QCOMPARE(page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"))->toPlainText(), QStringLiteral("A=x+y"));
        page.input()->setPlainText(QStringLiteral("x=4\ny=5"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("10"));
        QVERIFY(!page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"))->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=4\ny=5"));
        page.submit();
        QCOMPARE(asDouble(page.history().answer()), 9.0);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("z=8"));
        QCOMPARE(page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"))->toPlainText(), QStringLiteral("B=z*2"));
        page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("normal draft"));
        page.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).at(1)->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=1\ny=2"));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("normal draft"));
        page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("z=8"));
    }
    void customPersistenceRestoresModesAndBrowsing_data()
    {
        QTest::addColumn<bool>("browsing");
        QTest::newRow("normal-mode") << false;
        QTest::newRow("mixed-browsing") << true;
    }
    void customPersistenceRestoresModesAndBrowsing()
    {
        QFETCH(bool, browsing);
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("custom.calctabdd"));
        {
            CalculatorPage page;
            prepare(page, QStringLiteral("12+"));
            QVERIFY(defineFormula(page, QStringLiteral("A=x+y")));
            page.input()->setPlainText(QStringLiteral("x=1\ny=2"));
            page.submit();
            QVERIFY(defineFormula(page, QStringLiteral("B=z*2")));
            page.input()->setPlainText(QStringLiteral("z=8"));
            page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
            if (browsing)
            {
                QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
                page.input()->setPlainText(QStringLiteral("x=5\ny=6"));
            }
            QVERIFY(selectSession(page, path, false));
        }
        CalculatorPage page;
        prepare(page, QString());
        QVERIFY(selectSession(page, path, true));
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(asDouble(page.history().answer()), 3.0);
        if (browsing)
        {
            QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=5\ny=6"));
            QCOMPARE(page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"))->toPlainText(), QStringLiteral("A=x+y"));
            QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        }
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("12+"));
        page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("z=8"));
        QCOMPARE(page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"))->toPlainText(), QStringLiteral("B=z*2"));
        page.submit();
        QCOMPARE(asDouble(readSession(path).history.answer()), 16.0);
        page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"))->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(page.history().count(), 0);
        QCOMPARE(readSession(path).customInput.customDefinition, QStringLiteral("B=z*2"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("z=8"));
        page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("12+"));
    }
    void customSaveFailureAndActiveModeRecovery()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("first.calctabdd"));
        const QString rescue = directory.filePath(QStringLiteral("rescue.calctabdd"));
        {
            CalculatorPage page;
            prepare(page, QStringLiteral("normal draft"));
            QVERIFY(defineFormula(page, QStringLiteral("A=x+y")));
            page.input()->setPlainText(QStringLiteral("x=1\ny=2"));
            QVERIFY(selectSession(page, path, false));
            page.input()->setPlainText(QStringLiteral("x=4\ny=5"));
            QTRY_COMPARE(readSession(path).customInput.text, QStringLiteral("x=4\ny=5"));
            QCOMPARE(page.history().count(), 0);
            QFile external(path);
            QVERIFY(external.open(QIODevice::WriteOnly));
            external.write("external change");
            external.close();
            page.submit();
            QCOMPARE(asDouble(page.history().answer()), 9.0);
            QVERIFY(page.findChild<QLabel *>(QStringLiteral("sessionSaveStatus"))->text().contains(QStringLiteral("外部修改")));
            QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=4\ny=5"));
            QVERIFY(selectSession(page, rescue, false));
            QVERIFY(external.open(QIODevice::ReadOnly));
            QCOMPARE(external.readAll(), QByteArray("external change"));
            page.input()->setPlainText(QStringLiteral("x=4\ny=1e-"));
        }
        CalculatorPage restored;
        prepare(restored, QString());
        QVERIFY(selectSession(restored, rescue, true));
        QCOMPARE(asDouble(restored.history().answer()), 9.0);
        QCOMPARE(restored.history().count(), 1);
        QCOMPARE(restored.input()->toPlainText(), QStringLiteral("x=4\ny=1e-"));
        QVERIFY(restored.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"))->isVisible());
        restored.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        QCOMPARE(restored.input()->toPlainText(), QStringLiteral("normal draft"));
    }
    void customEnterImeAndInlineIsolation()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("draft"));
        page.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        auto *edit = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        QVERIFY(edit && edit->isVisible());
        QVERIFY(!page.input()->isVisible());
        edit->setPlainText(QStringLiteral("A=x+y"));
        page.submit();
        QCOMPARE(page.history().count(), 0);
        page.hide();
        page.show();
        QCOMPARE(edit->toPlainText(), QStringLiteral("A=x+y"));
        QTest::keyClick(edit, Qt::Key_Escape);
        QVERIFY(!edit->isVisible());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("draft"));
        QVERIFY(defineFormula(page, QStringLiteral("A=x+y")));
        page.input()->setPlainText(QStringLiteral("x=1"));
        page.input()->moveCursor(QTextCursor::End);
        QTest::keyClick(page.input(), Qt::Key_Return);
        QTest::keyClicks(page.input(), "y=2");
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("x=1\ny=2"));
        QCOMPARE(page.history().count(), 0);
        QInputMethodEvent preedit(QStringLiteral("拼"), {});
        QApplication::sendEvent(page.input(), &preedit);
        QVERIFY(!page.findChild<QPushButton *>(QStringLiteral("calculateButton"))->isEnabled());
        page.submit();
        page.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        QVERIFY(edit->isVisible());
        QCOMPARE(page.history().count(), 0);
        QInputMethodEvent end;
        QApplication::sendEvent(page.input(), &end);
        QVERIFY(page.findChild<QPushButton *>(QStringLiteral("calculateButton"))->isEnabled());
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(asDouble(page.history().answer()), 3.0);
        page.input()->setPlainText(QStringLiteral("@"));
        QCoreApplication::processEvents();
        QVERIFY(!completion(page)->isVisible());
    }
    void customPaletteLayoutAndDraftRestoreProtection()
    {
        CalculatorPage page;
        prepare(page, QString());
        QVERIFY(defineFormula(page, QStringLiteral("A=sqrt(x^2+y^2)+max(z,0)")));
        page.input()->setPlainText(QStringLiteral("x=3\ny=4\nz=2"));
        page.submit();
        const QString screenshots = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) QVERIFY(QDir().mkpath(screenshots));
        page.resize(600, 620);
        for (bool dark : {false, true})
        {
            QPalette colors = page.palette();
            colors.setColor(QPalette::Window, QColor(dark ? "#202329" : "#f0f0f0"));
            colors.setColor(QPalette::Base, QColor(dark ? "#202329" : "#ffffff"));
            colors.setColor(QPalette::Text, QColor(dark ? "#e4e8ef" : "#202329"));
            colors.setColor(QPalette::WindowText, colors.color(QPalette::Text));
            page.setPalette(colors);
            QTest::qWait(20);
            auto *definition = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
            QVERIFY(definition->isVisible());
            QVERIFY(page.input()->viewport()->height() >= 3 * page.input()->fontMetrics().lineSpacing());
            QVERIFY(page.rect().contains(QRect(definition->mapTo(&page, QPoint()), definition->size())));
            if (!screenshots.isEmpty()) QVERIFY(page.grab().save(screenshots + (dark ? "/custom-dark.png" : "/custom-light.png")));
            QTest::mouseClick(definition->viewport(), Qt::LeftButton);
            QVERIFY(definition->hasFocus());
            QCOMPARE(definition->palette().color(QPalette::Base), colors.color(QPalette::Base));
            QTest::keyClick(definition, Qt::Key_Escape);
        }
        CalculatorPage unused;
        prepare(unused, QString());
        QVERIFY(defineFormula(unused, QStringLiteral("A=x")));
        unused.findChild<QAction *>(QStringLiteral("normalCalculationMode"))->trigger();
        QVERIFY(unused.input()->toPlainText().isEmpty());
        unused.findChild<QAction *>(QStringLiteral("restoreSession"))->trigger();
        QVERIFY(!unused.findChild<QFileDialog *>());
        unused.findChild<QAction *>(QStringLiteral("customCalculationMode"))->trigger();
        QCOMPARE(unused.input()->toPlainText(), QStringLiteral("x="));
    }
    void sessionPaletteAndPendingSaveStatus()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("配色.calctabdd"));
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        QVERIFY(selectSession(page, path, false));
        page.resize(600, 500);
        const QString screenshots = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) QVERIFY(QDir().mkpath(screenshots));
        for (const bool dark : {false, true})
        {
            QPalette colors = page.palette();
            colors.setColor(QPalette::Window, QColor(dark ? "#202329" : "#f0f0f0"));
            colors.setColor(QPalette::Base, QColor(dark ? "#202329" : "#ffffff"));
            colors.setColor(QPalette::Text, QColor(dark ? "#e4e8ef" : "#202329"));
            colors.setColor(QPalette::WindowText, colors.color(QPalette::Text));
            page.setPalette(colors);
            page.findChild<QAction *>(QStringLiteral("saveSessionNow"))->trigger();
            auto *status = page.findChild<QLabel *>(QStringLiteral("sessionSaveStatus"));
            QTest::qWait(30);
            QVERIFY(status->isVisible());
            QVERIFY(page.rect().contains(QRect(status->mapTo(&page, QPoint()), status->size())));
            QVERIFY(status->height() >= status->heightForWidth(status->width()));
            if (!screenshots.isEmpty())
                QVERIFY(page.grab().save(screenshots + (dark ? QStringLiteral("/session-dark.png") : QStringLiteral("/session-light.png"))));
            page.findChild<QAction *>(QStringLiteral("saveSessionAs"))->trigger();
            auto *dialog = page.findChild<QFileDialog *>(QStringLiteral("sessionFileDialog"));
            QVERIFY(dialog && dialog->isVisible());
            QCOMPARE(dialog->palette().color(QPalette::Window), page.palette().color(QPalette::Window));
            dialog->setDirectory(directory.path());
            QTest::qWait(30);
            if (!screenshots.isEmpty())
                QVERIFY(dialog->grab().save(screenshots + (dark ? QStringLiteral("/session-dialog-dark.png") : QStringLiteral("/session-dialog-light.png"))));
            dialog->reject();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("external edit");
        file.close();
        page.findChild<QAction *>(QStringLiteral("saveSessionNow"))->trigger();
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("sessionSaveStatus"))->text().contains(QStringLiteral("外部修改")));
        // 普通计算状态和补全提示不覆盖持久保存错误。
        page.input()->setPlainText(QStringLiteral("@"));
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("sessionSaveStatus"))->text().contains(QStringLiteral("外部修改")));
        QTest::keyClick(page.input(), Qt::Key_Escape);
        QTest::qWait(30);
        if (!screenshots.isEmpty()) QVERIFY(page.grab().save(screenshots + QStringLiteral("/session-failure-dark.png")));
        const QString rescue = directory.filePath(QStringLiteral("rescue.calctabdd"));
        QVERIFY(selectSession(page, rescue, false));
        QCOMPARE(readSession(rescue).input.text, QStringLiteral("@"));
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".lock")));
        QCOMPARE(readSession(rescue).history.count(), 1);
    }
    void sessionMenuSavesAndRestoresFullBrowsingState()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("会话😀.calctabdd"));
        {
            CalculatorPage page;
            prepare(page, QStringLiteral("0.1+0.2"));
            page.submit();
            page.input()->setPlainText(QStringLiteral("ans+1"));
            page.submit();
            page.input()->setPlainText(QStringLiteral("ln(0)"));
            page.submit();
            page.input()->setPlainText(QStringLiteral("  草稿😀\n99+ "));
            QTextCursor cursor = page.input()->textCursor();
            cursor.setPosition(2);
            cursor.setPosition(5, QTextCursor::KeepAnchor);
            page.input()->setTextCursor(cursor);
            QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
            page.input()->setPlainText(QStringLiteral("ln(9)"));
            QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
            page.input()->setPlainText(QStringLiteral("ans+7"));
            page.input()->moveCursor(QTextCursor::End);
            QTest::keyClick(page.input(), Qt::Key_Left, Qt::ShiftModifier);
            auto *button = page.findChild<QToolButton *>(QStringLiteral("sessionButton"));
            QVERIFY(chooseRecordAction(button, page.findChild<QAction *>(QStringLiteral("saveSessionAs"))));
            QVERIFY(acceptSessionFile(page, path, false));
            QVERIFY(page.findChild<QAction *>(QStringLiteral("saveSessionNow"))->isEnabled());
            const auto saved = readSession(path);
            QCOMPARE(saved.history.count(), 3);
            QCOMPARE(saved.historyPosition, 1);
            QCOMPARE(saved.input.text, QStringLiteral("ans+7"));
            QCOMPARE(saved.input.position, 4);
            QCOMPARE(saved.input.anchor, 5);
            QCOMPARE(saved.draft.text, QStringLiteral("  草稿😀\n99+ "));
            QCOMPARE(saved.draft.position, 5);
            QCOMPARE(saved.draft.anchor, 2);
            QCOMPARE(saved.recalledInputs.value(3).text, QStringLiteral("ln(9)"));
        }
        CalculatorPage restored;
        prepare(restored, QString());
        QVERIFY(selectSession(restored, path, true));
        QCOMPARE(restored.history().count(), 3);
        QCOMPARE(asDouble(restored.history().answer()), 1.3);
        QCOMPARE(restored.input()->toPlainText(), QStringLiteral("ans+7"));
        QCOMPARE(restored.input()->textCursor().anchor(), 5);
        QCOMPARE(restored.input()->textCursor().position(), 4);
        QCOMPARE(restored.findChildren<RecordText *>(QStringLiteral("recordResult")).first()->toPlainText(), QStringLiteral("= 0.3"));
        QTest::keyClick(restored.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(restored.input()->toPlainText(), QStringLiteral("ln(9)"));
        QTest::keyClick(restored.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(restored.input()->toPlainText(), QStringLiteral("  草稿😀\n99+ "));
        QCOMPARE(restored.input()->textCursor().position(), 5);
        QCOMPARE(restored.input()->textCursor().anchor(), 2);
        restored.input()->setPlainText(QStringLiteral("ans+1"));
        restored.submit();
        QCOMPARE(restored.history().records().last().id, quint64(4));
        QCOMPARE(asDouble(readSession(path).history.answer()), 2.3);
    }
    void sessionAutosaveFlushesDraftBeforeClose()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("draft.calctabdd"));
        {
            CalculatorPage page;
            prepare(page, QStringLiteral("42"));
            page.submit();
            QVERIFY(selectSession(page, path, false));
            page.input()->setPlainText(QStringLiteral("自动保存😀"));
            QTRY_COMPARE(readSession(path).input.text, QStringLiteral("自动保存😀"));
            page.input()->setPlainText(QStringLiteral("关闭前的最后草稿"));
            page.input()->selectAll();
            // 不等待 200 ms：析构必须保存最后文本与选区。
        }
        const auto saved = readSession(path);
        QCOMPARE(saved.input.text, QStringLiteral("关闭前的最后草稿"));
        QCOMPARE(saved.input.anchor, 0);
        QCOMPARE(saved.input.position, saved.input.text.size());
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".lock")));
    }
    void sessionSaveFailureKeepsVisibleStateAndRetries()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("retry.calctabdd"));
        CalculatorPage page;
        prepare(page, QStringLiteral("7"));
        page.submit();
        QVERIFY(selectSession(page, path, false));
        QVERIFY(QFile::rename(path, path + QStringLiteral(".bak")));
        QVERIFY(QDir().mkdir(path));
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.submit();
        QCOMPARE(page.history().count(), 2);
        QCOMPARE(asDouble(page.history().answer()), 8.0);
        auto *status = page.findChild<QLabel *>(QStringLiteral("sessionSaveStatus"));
        QVERIFY(status->text().startsWith(QStringLiteral("保存失败")));
        page.input()->setPlainText(QStringLiteral("草稿"));
        page.input()->selectAll();
        page.findChild<QAction *>(QStringLiteral("stopSavingSession"))->trigger();
        QVERIFY(page.findChild<QAction *>(QStringLiteral("saveSessionNow"))->isEnabled());
        QCOMPARE(page.input()->textCursor().selectedText(), QStringLiteral("草稿"));
        QVERIFY(QDir().rmdir(path));
        QVERIFY(QFile::rename(path + QStringLiteral(".bak"), path));
        page.findChild<QAction *>(QStringLiteral("saveSessionNow"))->trigger();
        QVERIFY(status->text().startsWith(QStringLiteral("本地保存已开启")));
        QCOMPARE(readSession(path).history.count(), 2);
        QCOMPARE(readSession(path).input.text, QStringLiteral("草稿"));
    }
    void stopSavingKeepsSnapshotAndSaveAsChangesDestination()
    {
        QTemporaryDir directory;
        const QString first = directory.filePath(QStringLiteral("first.calctabdd"));
        const QString second = directory.filePath(QStringLiteral("second.calctabdd"));
        CalculatorPage page;
        prepare(page, QStringLiteral("3"));
        page.submit();
        QVERIFY(selectSession(page, first, false));
        page.input()->setPlainText(QStringLiteral("最后草稿"));
        page.findChild<QAction *>(QStringLiteral("stopSavingSession"))->trigger();
        QVERIFY(!page.findChild<QAction *>(QStringLiteral("saveSessionNow"))->isEnabled());
        QCOMPARE(readSession(first).input.text, QStringLiteral("最后草稿"));
        QVERIFY(!QFileInfo::exists(first + QStringLiteral(".lock")));
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.submit();
        QCOMPARE(readSession(first).history.count(), 1);
        QVERIFY(selectSession(page, first, false));
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("sessionSaveStatus"))->text().contains(QStringLiteral("文件已存在")));
        QCOMPARE(readSession(first).history.count(), 1);
        QVERIFY(selectSession(page, second, false));
        QCOMPARE(readSession(second).history.count(), 2);
        // 保存期间另存失败不能释放现有会话锁。
        QVERIFY(selectSession(page, first, false));
        QVERIFY(QFileInfo::exists(second + QStringLiteral(".lock")));
        page.input()->setPlainText(QStringLiteral("9"));
        page.submit();
        QCOMPARE(asDouble(readSession(second).history.answer()), 9.0);
    }
    void restoringDamagedOrBusyFileKeepsBlankPageAndFile()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("session.calctabdd"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("broken");
        file.close();
        CalculatorPage page;
        prepare(page, QString());
        QVERIFY(selectSession(page, path, true));
        QCOMPARE(page.history().count(), 0);
        QVERIFY(page.input()->toPlainText().isEmpty());
        QVERIFY(!page.findChild<QAction *>(QStringLiteral("saveSessionNow"))->isEnabled());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("broken"));
        file.close();
        QVERIFY(QFile::remove(path));
        CalculatorPage other;
        prepare(other, QStringLiteral("42"));
        other.submit();
        QVERIFY(selectSession(other, path, false));
        QVERIFY(selectSession(page, path, true));
        QCOMPARE(page.history().count(), 0);
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("sessionSaveStatus"))->text().contains(QStringLiteral("其他窗口或进程")));
        QCOMPARE(other.history().count(), 1);
    }
    void restoreRequiresBlankPageAndCompositionBlocksDialogs()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("草稿"));
        page.findChild<QAction *>(QStringLiteral("restoreSession"))->trigger();
        QVERIFY(!page.findChild<QFileDialog *>());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("草稿"));
        page.input()->setPlainText(QStringLiteral("1"));
        page.submit();
        page.findChild<QAction *>(QStringLiteral("restoreSession"))->trigger();
        QVERIFY(!page.findChild<QFileDialog *>());
        QCOMPARE(page.history().count(), 1);
        QInputMethodEvent preedit(QStringLiteral("拼音"), {});
        QApplication::sendEvent(page.input(), &preedit);
        QVERIFY(!page.findChild<QToolButton *>(QStringLiteral("sessionButton"))->isEnabled());
        page.findChild<QAction *>(QStringLiteral("saveSessionAs"))->trigger();
        QVERIFY(!page.findChild<QFileDialog *>());
        QInputMethodEvent end;
        QApplication::sendEvent(page.input(), &end);
        QVERIFY(page.findChild<QToolButton *>(QStringLiteral("sessionButton"))->isEnabled());
    }
    void clearSavedSessionKeepsDraftAndUpdatesFile()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("clear.calctabdd"));
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        QVERIFY(selectSession(page, path, false));
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.input()->selectAll();
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        page.input()->setPlainText(QStringLiteral("temporary"));
        page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        auto *confirmation = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        QVERIFY(confirmation);
        QVERIFY(confirmation->findChild<QLabel *>(QStringLiteral("clearSessionDetails"))->text().contains(QStringLiteral("更新会话文件")));
        confirmation->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        const auto saved = readSession(path);
        QCOMPARE(saved.history.count(), 0);
        QCOMPARE(asDouble(saved.history.answer()), 0.0);
        QCOMPARE(saved.input.text, QStringLiteral("ans+1"));
        QCOMPARE(saved.input.anchor, 0);
        QCOMPARE(saved.input.position, 5);
        QCOMPARE(saved.historyPosition, -1);
        QVERIFY(saved.recalledInputs.isEmpty());
        page.submit();
        QCOMPARE(readSession(path).history.records().first().id, quint64(1));
        QCOMPARE(asDouble(readSession(path).history.answer()), 1.0);
    }
    void sessionDialogCancelAndCommandIsolation_data()
    {
        QTest::addColumn<int>("cancel");
        QTest::newRow("button") << 0;
        QTest::newRow("escape") << 1;
        QTest::newRow("close") << 2;
    }
    void sessionDialogCancelAndCommandIsolation()
    {
        QFETCH(int, cancel);
        CalculatorPage page;
        prepare(page, QStringLiteral("1/0"));
        page.submit();
        const QString diagnostic = page.input()->toolTip();
        page.findChild<QAction *>(QStringLiteral("saveSessionAs"))->trigger();
        QPointer<QFileDialog> dialog = page.findChild<QFileDialog *>(QStringLiteral("sessionFileDialog"));
        QVERIFY(dialog && dialog->isVisible());
        QCOMPARE(dialog->windowModality(), Qt::WindowModal);
        QCOMPARE(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->text(), QStringLiteral("取消"));
        page.submit();
        page.findChild<QAction *>(QStringLiteral("exportText"))->trigger();
        page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        page.routeEdit(QStringLiteral("actioncut"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1/0"));
        QVERIFY(!page.findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog")));
        QVERIFY(!page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation")));
        if (cancel == 0) dialog->reject();
        else if (cancel == 1) QTest::keyClick(dialog, Qt::Key_Escape);
        else dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        QCOMPARE(page.input()->toolTip(), diagnostic);
        QVERIFY(!page.input()->extraSelections().isEmpty());
        QTRY_VERIFY(page.input()->hasFocus());
    }
    void restoredErrorsKeepSnapshotDiagnosisAndSelection()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("error.calctabdd"));
        {
            CalculatorPage page;
            prepare(page, QStringLiteral("  ln(0)"));
            page.submit();
            page.input()->selectAll();
            QVERIFY(selectSession(page, path, false));
        }
        CalculatorPage restored;
        prepare(restored, QString());
        QVERIFY(selectSession(restored, path, true));
        QCOMPARE(restored.input()->toPlainText(), QStringLiteral("  ln(0)"));
        QCOMPARE(restored.input()->textCursor().selectedText(), QStringLiteral("  ln(0)"));
        QVERIFY(!restored.input()->extraSelections().isEmpty());
        QVERIFY(restored.input()->toolTip().contains(QStringLiteral("必须大于 0")));
        QCOMPARE(asDouble(restored.history().answer()), 0.0);
        QCOMPARE(restored.history().count(), 1);
    }
    void exportDialogWritesSelectedFormatWithoutChangingSession_data()
    {
        QTest::addColumn<bool>("markdown");
        QTest::newRow("txt") << false;
        QTest::newRow("markdown") << true;
    }
    void exportDialogWritesSelectedFormatWithoutChangingSession()
    {
        QFETCH(bool, markdown);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        CalculatorPage page;
        prepare(page, QStringLiteral("0.1+0.2"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("1+中"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("草稿😀\nans+ "));
        page.input()->moveCursor(QTextCursor::End);
        QTest::keyClicks(page.input(), "9");
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(1);
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        const QString draft = page.input()->toPlainText();
        QApplication::clipboard()->setText(QStringLiteral("keep clipboard"));
        auto *button = page.findChild<QToolButton *>(QStringLiteral("exportHistoryButton"));
        QVERIFY(button && button->isEnabled());
        auto *action = page.findChild<QAction *>(markdown ? QStringLiteral("exportMarkdown") : QStringLiteral("exportText"));
        QVERIFY(chooseRecordAction(button, action));
        QPointer<QFileDialog> dialog = page.findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"));
        QVERIFY(dialog && dialog->isVisible());
        QCOMPARE(dialog->windowModality(), Qt::WindowModal);
        QCOMPARE(dialog->acceptMode(), QFileDialog::AcceptSave);
        QCOMPARE(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->text(), QStringLiteral("取消"));
        QCOMPARE(dialog->defaultSuffix(), markdown ? QStringLiteral("md") : QStringLiteral("txt"));
        // 使用用户实际可达的文件名输入和导出按钮，验证扩展名自动补齐。
        dialog->setDirectory(directory.path());
        auto *fileName = dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
        QVERIFY(fileName);
        fileName->setText(QStringLiteral("计算记录"));
        page.submit();
        page.routeEdit(QStringLiteral("actioncut"));
        page.findChild<QAction *>(QStringLiteral("insertResult"))->trigger();
        page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        action->trigger();
        QCOMPARE(page.findChildren<QFileDialog *>().size(), 1);
        QVERIFY(!page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation")));
        auto *save = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
        QVERIFY(save && save->isEnabled());
        QTest::mouseClick(save, Qt::LeftButton);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        QFile file(directory.filePath(markdown ? QStringLiteral("计算记录.md") : QStringLiteral("计算记录.txt")));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        const QString exported = QString::fromUtf8(file.readAll());
        QVERIFY(exported.contains(QStringLiteral("0.1+0.2\n= 0.3")));
        QVERIFY(exported.contains(QStringLiteral("1+中\n无法计算：")));
        QCOMPARE(exported.startsWith(QStringLiteral("# CalcTabdd")), markdown);
        QVERIFY(!exported.contains(draft));
        QCOMPARE(page.input()->toPlainText(), draft);
        QCOMPARE(page.input()->textCursor().anchor(), 1);
        QCOMPARE(page.input()->textCursor().position(), 5);
        QCOMPARE(page.history().count(), 2);
        QCOMPARE(asDouble(page.history().answer()), 0.3);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("keep clipboard"));
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().startsWith(QStringLiteral("已导出 2 条记录")));
        QTRY_VERIFY(page.input()->hasFocus());
        page.routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("草稿😀\nans+ "));
        page.routeEdit(QStringLiteral("actionredo"));
        QCOMPARE(page.input()->toPlainText(), draft);
    }
    void cancellingExportKeepsRecallDraftAndDiagnostics_data()
    {
        QTest::addColumn<int>("method");
        QTest::newRow("cancel") << 0;
        QTest::newRow("escape") << 1;
        QTest::newRow("close") << 2;
    }
    void cancellingExportKeepsRecallDraftAndDiagnostics()
    {
        QFETCH(int, method);
        CalculatorPage page;
        prepare(page, QStringLiteral("ln(0)"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("  草稿😀\nans+1 "));
        page.input()->selectAll();
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QVERIFY(!page.input()->extraSelections().isEmpty());
        const QString status = page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text();
        const int position = page.input()->textCursor().position();
        const QString tooltip = page.input()->toolTip();
        page.findChild<QAction *>(QStringLiteral("exportText"))->trigger();
        QPointer<QFileDialog> dialog = page.findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"));
        QVERIFY(dialog);
        if (method == 0) dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        else if (method == 1) QTest::keyClick(dialog, Qt::Key_Escape);
        else dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ln(0)"));
        QCOMPARE(page.input()->textCursor().position(), position);
        QCOMPARE(page.input()->toolTip(), tooltip);
        QVERIFY(!page.input()->extraSelections().isEmpty());
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text(), status);
        QCOMPARE(asDouble(page.history().answer()), 0.0);
        QCOMPARE(page.history().count(), 1);
        QTRY_VERIFY(page.input()->hasFocus());
        QTest::keyClicks(page.input(), "9");
        const QString temporary = page.input()->toPlainText();
        page.findChild<QAction *>(QStringLiteral("exportText"))->trigger();
        page.findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"))->reject();
        QCOMPARE(page.input()->toPlainText(), temporary);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("  草稿😀\nans+1 "));
        QCOMPARE(page.input()->textCursor().selectedText(), QStringLiteral("  草稿😀\u2029ans+1 "));
    }
    void exportOverwriteRequiresConfirmation_data()
    {
        QTest::addColumn<bool>("overwrite");
        QTest::newRow("default-cancel") << false;
        QTest::newRow("confirm") << true;
    }
    void exportOverwriteRequiresConfirmation()
    {
        QFETCH(bool, overwrite);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray original("old file contents");
        file.write(original);
        file.close();
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        page.findChild<QAction *>(QStringLiteral("exportText"))->trigger();
        auto *dialog = page.findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"));
        QVERIFY(dialog);
        dialog->setDirectory(directory.path());
        dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(QStringLiteral("existing.txt"));
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        QPointer<QDialog> confirmation = page.findChild<QDialog *>(QStringLiteral("exportOverwriteConfirmation"));
        QVERIFY(confirmation && confirmation->isVisible());
        QCOMPARE(confirmation->windowModality(), Qt::WindowModal);
        auto *buttons = confirmation->findChild<QDialogButtonBox *>();
        QVERIFY(buttons->button(QDialogButtonBox::Cancel)->isDefault());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
        file.close();
        page.submit();
        QCOMPARE(page.history().count(), 1);
        if (overwrite) buttons->button(QDialogButtonBox::Ok)->click();
        else QTest::keyClick(confirmation, Qt::Key_Return);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(confirmation.isNull());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray contents = file.readAll();
        if (overwrite) QVERIFY(contents.contains("42\n= 42"));
        else QCOMPARE(contents, original);
        QCOMPARE(asDouble(page.history().answer()), 42.0);
    }
    void failedExportCanRetryAndKeepsHistoryEdits()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString destination = directory.filePath(QStringLiteral("destination"));
        QVERIFY(QDir().mkdir(destination));
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("草稿"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClicks(page.input(), "+7");
        auto *exportAction = page.findChild<QAction *>(QStringLiteral("exportText"));
        exportAction->trigger();
        auto *dialog = page.findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"));
        QVERIFY(dialog);
        dialog->setDirectory(destination);
        dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(QStringLiteral("result.txt"));
        bool removed = false;
        // 文件选择通过后目录消失：真实写入失败，不依赖 root 权限或平台 ACL。
        connect(dialog, &QFileDialog::fileSelected, dialog, [&](const QString &) { removed = QDir().rmdir(destination); });
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        QVERIFY(removed);
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().contains(QStringLiteral("导出失败")));
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().contains(QStringLiteral("重试")));
        QVERIFY(!QFileInfo::exists(destination + QStringLiteral("/result.txt")));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("42+7"));
        QCOMPARE(asDouble(page.history().answer()), 42.0);
        QCOMPARE(page.history().count(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(QDir().mkdir(destination));
        exportAction->trigger();
        dialog = page.findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"));
        QVERIFY(dialog);
        QCOMPARE(dialog->selectedFiles().first(), destination + QStringLiteral("/result.txt"));
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        QFile file(destination + QStringLiteral("/result.txt"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray contents = file.readAll();
        QVERIFY(contents.contains("42\n= 42"));
        QVERIFY(!contents.contains("42+7"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("42+7"));
        page.routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("42"));
        page.routeEdit(QStringLiteral("actionredo"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("42+7"));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("草稿"));
    }
    void exportAvailabilityFollowsRecordsAndComposition()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("1+"));
        auto *button = page.findChild<QToolButton *>(QStringLiteral("exportHistoryButton"));
        QVERIFY(button && !button->isEnabled());
        auto *action = page.findChild<QAction *>(QStringLiteral("exportText"));
        action->trigger();
        QVERIFY(!page.findChild<QFileDialog *>());
        page.submit();
        QVERIFY(button->isEnabled()); // 仅错误记录也可导出。
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(page.input(), &preedit);
        QVERIFY(!button->isEnabled());
        action->trigger();
        QVERIFY(!page.findChild<QFileDialog *>());
        QInputMethodEvent finish;
        finish.setCommitString(QStringLiteral("中文"));
        QApplication::sendEvent(page.input(), &finish);
        QVERIFY(button->isEnabled());
        page.input()->setPlainText(QStringLiteral("@sq"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        QVERIFY(chooseRecordAction(button, action));
        QTRY_VERIFY(!completion(page)->isVisible());
        auto *dialog = page.findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"));
        QVERIFY(dialog);
        dialog->reject();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("@sq"));
        QTRY_VERIFY(page.input()->hasFocus());
        page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        action->trigger();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!page.findChild<QFileDialog *>());
        page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"))->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(!button->isEnabled());
    }
    void exportPaletteAndNarrowLayout()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QFile file(temporary.filePath(QStringLiteral("已有记录.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("previous contents");
        file.close();
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        page.resize(560, 480);
        const QString screenshots = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        for (const bool dark : {false, true})
        {
            QPalette colors = page.palette();
            colors.setColor(QPalette::Window, QColor(dark ? "#202329" : "#f0f0f0"));
            colors.setColor(QPalette::Base, QColor(dark ? "#202329" : "#ffffff"));
            colors.setColor(QPalette::Text, QColor(dark ? "#e4e8ef" : "#202329"));
            colors.setColor(QPalette::WindowText, colors.color(QPalette::Text));
            page.setPalette(colors);
            QCoreApplication::processEvents();
            auto *button = page.findChild<QToolButton *>(QStringLiteral("exportHistoryButton"));
            QVERIFY(page.rect().contains(QRect(button->mapTo(&page, QPoint()), button->size())));
            if (!screenshots.isEmpty())
            {
                QVERIFY(QDir().mkpath(screenshots));
                QVERIFY(page.grab().save(screenshots + (dark ? QStringLiteral("/export-page-dark.png") : QStringLiteral("/export-page-light.png"))));
            }
            page.findChild<QAction *>(QStringLiteral("exportText"))->trigger();
            auto *dialog = page.findChild<QFileDialog *>(QStringLiteral("exportHistoryDialog"));
            QVERIFY(dialog);
            dialog->setDirectory(temporary.path());
            dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))->setText(QStringLiteral("已有记录.txt"));
            QTest::qWait(30);
            QCOMPARE(dialog->palette().color(QPalette::Window), page.palette().color(QPalette::Window));
            if (!screenshots.isEmpty())
                QVERIFY(dialog->grab().save(screenshots + (dark ? QStringLiteral("/export-dialog-dark.png") : QStringLiteral("/export-dialog-light.png"))));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            auto *confirmation = page.findChild<QDialog *>(QStringLiteral("exportOverwriteConfirmation"));
            QVERIFY(confirmation);
            QCOMPARE(confirmation->palette().color(QPalette::Window), page.palette().color(QPalette::Window));
            QTest::qWait(30);
            if (!screenshots.isEmpty())
                QVERIFY(confirmation->grab().save(screenshots + (dark ? QStringLiteral("/export-overwrite-dark.png") : QStringLiteral("/export-overwrite-light.png"))));
            confirmation->reject();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
    void clearConfirmationCanBeCancelled_data()
    {
        QTest::addColumn<int>("method");
        QTest::newRow("cancel-button") << 0;
        QTest::newRow("escape") << 1;
        QTest::newRow("window-close") << 2;
        QTest::newRow("default-enter") << 3;
    }
    void clearConfirmationCanBeCancelled()
    {
        QFETCH(int, method);
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        auto *clear = page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"));
        QVERIFY(clear && !clear->isEnabled());
        page.submit();
        page.input()->setPlainText(QStringLiteral("  草稿😀\n100+ "));
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(8);
        cursor.setPosition(2, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClicks(page.input(), "+3");
        cursor = page.input()->textCursor();
        cursor.setPosition(3);
        cursor.setPosition(1, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        const QString status = page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text();
        QTest::mouseClick(clear, Qt::LeftButton);
        QPointer<QDialog> dialog = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        QVERIFY(dialog && dialog->isVisible());
        QCOMPARE(dialog->windowModality(), Qt::WindowModal);
        QVERIFY(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->isDefault());
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("clearSessionDetails"))->text().contains(QStringLiteral("丢弃本轮")));
        page.submit(); // 对话框期间延迟回调／菜单不得修改待确认会话。
        page.routeEdit(QStringLiteral("actioncut"));
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("42+3"));
        clear->click();
        QCOMPARE(page.findChildren<QDialog *>(QStringLiteral("clearSessionConfirmation")).size(), 1);
        if (method == 0) QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel), Qt::LeftButton);
        else if (method == 1) QTest::keyClick(dialog, Qt::Key_Escape);
        else if (method == 2) dialog->close();
        else QTest::keyClick(dialog, Qt::Key_Return);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(dialog.isNull());
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(asDouble(page.history().answer()), 42.0);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("42+3"));
        QCOMPARE(page.input()->textCursor().anchor(), 3);
        QCOMPARE(page.input()->textCursor().position(), 1);
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text(), status);
        QTRY_VERIFY(page.input()->hasFocus());
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("  草稿😀\n100+ "));
        QCOMPARE(page.input()->textCursor().anchor(), 8);
        QCOMPARE(page.input()->textCursor().position(), 2);
    }
    void clearPreservesDraftUndoAndRemovesAllRecordWidgets()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("1/0"));
        page.submit();
        page.input()->setPlainText(QStringLiteral(" 100+ "));
        page.input()->moveCursor(QTextCursor::End);
        QTest::keyClicks(page.input(), "5");
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(5);
        cursor.setPosition(1, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        const auto oldRows = page.findChildren<QWidget *>(QStringLiteral("calculationRecord"));
        QPointer<QWidget> oldRow = oldRows.first();
        QPointer<QAction> oldAction = page.findChild<QAction *>(QStringLiteral("insertResult"));
        QApplication::clipboard()->setText(QStringLiteral("clipboard-unchanged"));
        // 清空前焦点／菜单目标是历史结果，清空后必须安全转回输入框。
        auto *label = page.findChild<RecordText *>(QStringLiteral("recordResult"));
        label->setFocus();
        label->selectAll();
        auto *clear = page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"));
        QTest::mouseClick(clear, Qt::LeftButton);
        auto *dialog = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        QVERIFY(dialog);
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("clearSessionDetails"))->text().contains(QStringLiteral("选区会保留")));
        QTest::mouseClick(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QVERIFY(oldRow.isNull());
        QVERIFY(oldAction.isNull());
        QVERIFY(page.findChildren<QWidget *>(QStringLiteral("calculationRecord")).isEmpty());
        QCOMPARE(page.history().count(), 0);
        QCOMPARE(asDouble(page.history().answer()), 0.0);
        QVERIFY(!page.history().record(1));
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("recordCount"))->text(), QStringLiteral("0 条记录"));
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("emptyHistory"))->isVisible());
        QVERIFY(!clear->isEnabled());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral(" 100+ 5"));
        QCOMPARE(page.input()->textCursor().anchor(), 5);
        QCOMPARE(page.input()->textCursor().position(), 1);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("clipboard-unchanged"));
        QVERIFY(page.input()->document()->isUndoAvailable());
        page.routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral(" 100+ "));
        page.routeEdit(QStringLiteral("actionredo"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral(" 100+ 5"));
        QCOMPARE(page.history().count(), 0); // 输入撤销不会恢复历史。
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral(" 100+ 5"));
        page.input()->setPlainText(QStringLiteral("ans+1"));
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(page.history().records().first().id, quint64(1));
        QCOMPARE(asDouble(page.history().records().first().answerBefore), 0.0);
        QCOMPARE(asDouble(page.history().answer()), 1.0);
        QCOMPARE(page.findChild<QLabel *>(QStringLiteral("recordNumber"))->text(), QStringLiteral("01"));
        QVERIFY(!page.findChild<QLabel *>(QStringLiteral("emptyHistory"))->isVisible());
        QVERIFY(clear->isEnabled());
        page.findChild<QAction *>(QStringLiteral("copyValue"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("1"));
    }
    void clearWhileRecallingRestoresOriginalDraft_data()
    {
        QTest::addColumn<QString>("draft");
        QTest::newRow("empty") << QString();
        QTest::newRow("unicode-multiline") << QStringLiteral("  草稿😀\n 12+34\t ");
        QTest::newRow("over-input-limit") << QString(4200, QLatin1Char('1')) + QStringLiteral("\n尾部 ");
    }
    void clearWhileRecallingRestoresOriginalDraft()
    {
        QFETCH(QString, draft);
        CalculatorPage page;
        prepare(page, QStringLiteral("11"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("1+"));
        page.submit();
        page.input()->setPlainText(draft);
        QTextCursor cursor = page.input()->textCursor();
        cursor.setPosition(draft.size());
        cursor.setPosition(0, QTextCursor::KeepAnchor);
        page.input()->setTextCursor(cursor);
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClicks(page.input(), "9");
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClicks(page.input(), "+99");
        page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        auto *dialog = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        QVERIFY(dialog);
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(page.history().count(), 0);
        QCOMPARE(page.input()->toPlainText(), draft);
        QCOMPARE(page.input()->textCursor().anchor(), draft.size());
        QCOMPARE(page.input()->textCursor().position(), 0);
        QVERIFY(!page.input()->document()->isUndoAvailable());
        for (int key : {Qt::Key_Up, Qt::Key_Down})
        {
            QTest::keyClick(page.input(), Qt::Key(key), Qt::AltModifier);
            QCOMPARE(page.input()->toPlainText(), draft);
        }
        page.input()->setPlainText(QStringLiteral("ans+2"));
        page.submit();
        QVERIFY(page.input()->toPlainText().isEmpty()); // 不得再次恢复旧草稿。
        QCOMPARE(asDouble(page.history().answer()), 2.0);
        page.input()->setPlainText(QStringLiteral("new draft"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ans+2"));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("new draft"));
    }
    void clearWorksWithOnlyErrorsAndResetsScrolling()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("1/0"));
        for (int i = 0; i < 24; ++i) page.submit();
        auto *bar = page.findChild<QScrollArea *>()->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > 0);
        bar->triggerAction(QAbstractSlider::SliderToMinimum);
        auto *clear = page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"));
        QVERIFY(clear->isEnabled());
        clear->click();
        page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"))->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1/0"));
        QTRY_COMPARE(bar->value(), 0);
        QTRY_COMPARE(bar->maximum(), 0);
        for (int i = 0; i < 24; ++i)
        {
            page.input()->setPlainText(QStringLiteral("ans+1"));
            page.submit();
        }
        QTRY_VERIFY(bar->maximum() > 0);
        QTRY_COMPARE(bar->value(), bar->maximum());
        QCOMPARE(asDouble(page.history().answer()), 24.0);
        clear->click();
        page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"))->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(page.history().count(), 0);
    }
    void clearDefersToCompositionAndKeepsCompletionDraft()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("@sq"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        auto *clear = page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"));
        QTest::mouseClick(clear, Qt::LeftButton);
        auto *dialog = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        QVERIFY(dialog);
        QTRY_VERIFY(!completion(page)->isVisible());
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("@sq"));
        QCOMPARE(page.history().count(), 0);
        QTRY_VERIFY(page.input()->hasFocus());
        QTest::keyClick(page.input(), Qt::Key_End);
        QTest::keyClick(page.input(), Qt::Key_Backspace);
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Return);
        QVERIFY(!page.input()->toPlainText().contains(QLatin1Char('@')));
        page.input()->setPlainText(QStringLiteral("42"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("9+"));
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(page.input(), &preedit);
        QVERIFY(!clear->isEnabled());
        clear->click();
        QCOMPARE(page.history().count(), 1);
        QInputMethodEvent finish;
        finish.setCommitString(QStringLiteral("中文"));
        QApplication::sendEvent(page.input(), &finish);
        QVERIFY(clear->isEnabled());
        const QString committed = page.input()->toPlainText();
        clear->click();
        page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"))->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(page.input()->toPlainText(), committed);
        QCOMPARE(page.history().count(), 0);
    }
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
        QCOMPARE(page.history().count(), 3);
        QCOMPARE(asDouble(page.history().answer()), 12.0);
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
        QCOMPARE(page.history().count(), 2);
        QCOMPARE(asDouble(page.history().answer()), 2.0);
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
        QCOMPARE(page.history().count(), 2);
        QTest::keyClick(page.input(), Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier);
        QCOMPARE(asDouble(page.history().records().last().answerBefore), 43.0);
        QCOMPARE(asDouble(page.history().answer()), 44.0);
        QCOMPARE(asDouble(page.history().record(2)->result.value), 43.0);
        QCOMPARE(page.history().count(), 3);
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
        QCOMPARE(page.history().count(), 2);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+"));
        QCOMPARE(page.input()->textCursor().position(), 2);
        QCOMPARE(asDouble(page.history().answer()), 0.0);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+"));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("7*8"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QTest::keyClicks(page.input(), "2");
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(asDouble(page.history().answer()), 3.0);
        QCOMPARE(page.history().count(), 3);
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
        QCOMPARE(page.history().count(), 1);
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("draft"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        page.input()->setPlainText(QStringLiteral("@sq"));
        page.input()->moveCursor(QTextCursor::End);
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.history().count(), 1);
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
        QCOMPARE(page.history().count(), 1);
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
        QCOMPARE(page.history().count(), 1);
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
        QCOMPARE(asDouble(page.history().answer()), -2.0);
        QCOMPARE(page.history().count(), 2);
    }
    void keyboardAndReuse()
    {
        CalculatorPage page;
        page.resize(900, 620);
        page.show();
        page.focusInput();
        QTest::keyClicks(page.input(), "1+2*3");
        QTest::keyClick(page.input(), Qt::Key_Return);
        QCOMPARE(page.history().count(), 0);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+2*3"));
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(page.findChild<RecordText *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 7"));
        QVERIFY(page.input()->toPlainText().isEmpty());
        QTest::mouseClick(page.findChild<QPushButton *>(QStringLiteral("reuseFormula")), Qt::LeftButton);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+2*3"));
        QTest::keyClick(page.input(), Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier);
        QCOMPARE(page.history().count(), 2);
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.history().count(), 2);
    }
    void inputDiagnosticRanges_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("position");
        QTest::addColumn<int>("length");
        QTest::addColumn<QString>("location");
        QTest::newRow("leading-and-trailing-whitespace") << QStringLiteral(" \n ln(0)  ") << 6 << 1 << QStringLiteral("第 2 行，第 5 列");
        QTest::newRow("missing-at-end") << QStringLiteral(" \n (1+2  ") << 7 << 0 << QStringLiteral("第 2 行，第 6 列");
        QTest::newRow("surrogate-pair") << QStringLiteral("2+\n😀") << 3 << 2 << QStringLiteral("第 2 行，第 1 列");
        QTest::newRow("argument") << QStringLiteral("  pow(2, ) ") << 9 << 1 << QStringLiteral("第 1 行，第 10 列");
        QTest::newRow("unicode-operators") << QStringLiteral(" \n1 ÷ (2 − 2)   ") << 6 << 7 << QStringLiteral("第 2 行，第 5 列");
    }
    void inputDiagnosticRanges()
    {
        QFETCH(QString, text);
        QFETCH(int, position);
        QFETCH(int, length);
        QFETCH(QString, location);
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        page.input()->setPlainText(text);
        const bool modified = page.input()->document()->isModified();
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(page.input()->toPlainText(), text);
        QCOMPARE(asDouble(page.history().answer()), 42.0);
        QCOMPARE(page.history().count(), 2);
        QCOMPARE(page.input()->document()->isModified(), modified);
        QCOMPARE(page.input()->textCursor().position(), position);
        QVERIFY(!page.input()->textCursor().hasSelection());
        const auto selections = page.input()->extraSelections();
        QCOMPARE(selections.size(), 1);
        QCOMPARE(selections.first().cursor.selectionStart(), position);
        QCOMPARE(selections.first().cursor.selectionEnd(), position + length);
        if (length)
            QCOMPARE(selections.first().format.underlineStyle(), QTextCharFormat::WaveUnderline);
        else QVERIFY(selections.first().format.boolProperty(QTextFormat::FullWidthSelection));
        auto *status = page.findChild<QLabel *>(QStringLiteral("calculationStatus"));
        QVERIFY2(status->text().contains(location), qPrintable(status->text()));
        QCOMPARE(page.input()->accessibleDescription(), status->text());
        QCOMPARE(page.input()->toolTip(), status->text());
        // 标记不进入文本撤销栈，也不会作为输入时的替换选区。
        page.input()->insertPlainText(QStringLiteral("1"));
        QString edited = text;
        edited.insert(position, QLatin1Char('1'));
        QCOMPARE(page.input()->toPlainText(), edited);
        QVERIFY(page.input()->extraSelections().isEmpty());
        QVERIFY(page.input()->toolTip().isEmpty());
        QVERIFY(page.input()->accessibleDescription().isEmpty());
        page.routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(page.input()->toPlainText(), text);
        QVERIFY(page.input()->extraSelections().isEmpty());
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.submit();
        QCOMPARE(asDouble(page.history().answer()), 43.0);
        QVERIFY(page.input()->extraSelections().isEmpty());
    }
    void recalledErrorsUseOnlyUnmodifiedFormulaRanges()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral(" \nln(0)  "));
        page.submit();
        page.input()->setPlainText(QStringLiteral("未提交草稿"));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ln(0)"));
        QCOMPARE(page.input()->extraSelections().size(), 1);
        QCOMPARE(page.input()->extraSelections().first().cursor.selectionStart(), 3);
        QCOMPARE(page.input()->textCursor().position(), 3);
        QTest::keyClick(page.input(), Qt::Key_Delete);
        QTest::keyClicks(page.input(), "1");
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ln(1)"));
        QVERIFY(page.input()->extraSelections().isEmpty());
        page.input()->moveCursor(QTextCursor::End);
        QTest::keyClick(page.input(), Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(asDouble(page.history().answer()), 0.0);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("未提交草稿"));
        QVERIFY(page.input()->extraSelections().isEmpty());
        page.findChildren<QPushButton *>(QStringLiteral("reuseFormula")).first()->click();
        QCOMPARE(page.input()->extraSelections().size(), 1);
        page.input()->insertPlainText(QStringLiteral("1"));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QVERIFY(page.input()->extraSelections().isEmpty());
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ln(10)"));
        QVERIFY(page.input()->extraSelections().isEmpty());
        QVERIFY(!page.history().record(1)->result.ok);
    }
    void preeditAndClearSessionRemoveDiagnosticDecoration()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("sqrt(-1)"));
        page.submit();
        QVERIFY(!page.input()->extraSelections().isEmpty());
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(page.input(), &preedit);
        QVERIFY(page.input()->extraSelections().isEmpty());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("sqrt(-1)"));
        page.submit();
        QCOMPARE(page.history().count(), 1);
        QInputMethodEvent finish;
        QApplication::sendEvent(page.input(), &finish);
        page.submit();
        QCOMPARE(page.input()->extraSelections().size(), 1);
        page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        auto *confirmation = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        QVERIFY(confirmation);
        confirmation->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(page.input()->extraSelections().size(), 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
        confirmation = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
        confirmation->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(page.input()->extraSelections().isEmpty());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("sqrt(-1)"));
        QCOMPARE(page.history().count(), 0);
        page.input()->setPlainText(QStringLiteral("@sq"));
        page.input()->moveCursor(QTextCursor::End);
        page.activateWindow();
        page.focusInput();
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Tab);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("sqrt()"));
        QVERIFY(page.input()->extraSelections().isEmpty());
    }
    void diagnosticPaletteAndScreenshots()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("42"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("  12 + ln(2 - 2)"));
        page.submit();
        const QString directory = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        for (bool dark : {false, true})
        {
            QPalette colors = page.palette();
            colors.setColor(QPalette::Window, QColor(dark ? "#202329" : "#f0f0f0"));
            colors.setColor(QPalette::Base, QColor(dark ? "#202329" : "#ffffff"));
            colors.setColor(QPalette::Text, QColor(dark ? "#e4e8ef" : "#202329"));
            colors.setColor(QPalette::WindowText, colors.color(QPalette::Text));
            page.setPalette(colors);
            QCoreApplication::processEvents();
            const auto selections = page.input()->extraSelections();
            QCOMPARE(selections.size(), 1);
            QCOMPARE(selections.first().cursor.selectedText(), QStringLiteral("2 - 2"));
            QCOMPARE(selections.first().format.foreground().color(), QColor(dark ? "#ffe2dd" : "#8f2922"));
            QCOMPARE(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->palette().color(QPalette::WindowText), colors.color(QPalette::Text));
            QCOMPARE(page.input()->palette().color(QPalette::Text), colors.color(QPalette::Text));
            QCOMPARE(page.input()->palette().color(QPalette::Base), colors.color(QPalette::Base));
            QVERIFY(!page.input()->textCursor().hasSelection());
            if (!directory.isEmpty())
            {
                QVERIFY(QDir().mkpath(directory));
                QVERIFY(page.grab().save(directory + (dark ? QStringLiteral("/error-dark.png") : QStringLiteral("/error-light.png"))));
            }
        }
        page.resize(560, 480);
        page.input()->setPlainText(QStringLiteral(" \n (2+3  "));
        page.submit();
        QCoreApplication::processEvents();
        const auto selection = page.input()->extraSelections().first();
        QVERIFY(selection.format.boolProperty(QTextFormat::FullWidthSelection));
        QCOMPARE(selection.cursor.position(), 7);
        if (!directory.isEmpty())
        {
            QTest::qWait(30);
            QVERIFY(page.grab().save(directory + QStringLiteral("/error-missing-narrow.png")));
        }
    }
    void errorsPreserveInputAndAnswer()
    {
        CalculatorPage page;
        page.input()->setPlainText(QStringLiteral("6*7"));
        page.submit();
        page.input()->setPlainText(QStringLiteral("12/(3-3)"));
        page.submit();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("12/(3-3)"));
        const auto results = page.findChildren<RecordText *>(QStringLiteral("recordResult"));
        QVERIFY(results.last()->toPlainText().contains(QStringLiteral("除数不能为 0")));
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.submit();
        QCOMPARE(page.findChildren<RecordText *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 43"));
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
        const auto formulas = page.findChildren<RecordText *>(QStringLiteral("recordFormula"));
        const auto results = page.findChildren<RecordText *>(QStringLiteral("recordResult"));
        const auto numbers = page.findChildren<QLabel *>(QStringLiteral("recordNumber"));
        QCOMPARE(page.history().count(), 4);
        QCOMPARE(entries.size(), page.history().count());
        QCOMPARE(formulas.size(), entries.size());
        QCOMPARE(results.size(), entries.size());
        QCOMPARE(numbers.size(), entries.size());
        QCOMPARE(entries.first().expression, QStringLiteral("2 × 3"));
        for (int index = 0; index < entries.size(); ++index)
        {
            const auto &entry = entries.at(index);
            QCOMPARE(numbers.at(index)->text(), QStringLiteral("%1").arg(entry.id, 2, 10, QLatin1Char('0')));
            QCOMPARE(formulas.at(index)->toPlainText(), entry.expression);
            QCOMPARE(results.at(index)->toPlainText(), entry.result.ok
                ? QStringLiteral("= %1").arg(entry.result.text)
                : QStringLiteral("无法计算：%1").arg(entry.result.text));
            QCOMPARE(results.at(index)->property("error").toBool(), !entry.result.ok);
        }
        QVERIFY(!entries.at(1).result.ok);
        QCOMPARE(asDouble(entries.at(1).answerBefore), 6.0);
        QCOMPARE(entries.at(1).result.errorPosition, 2);
        QCOMPARE(asDouble(entries.at(2).answerBefore), 6.0);
        QCOMPARE(asDouble(entries.at(2).result.value), 7.0);
        QCOMPARE(entries.last().result.value, decimalNumber(QStringLiteral("0.3")));
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
        page.findChildren<RecordText *>(QStringLiteral("recordFormula")).at(1)->setPlainText(QStringLiteral("999"));
        page.findChildren<RecordText *>(QStringLiteral("recordResult")).at(1)->setPlainText(QStringLiteral("= 999"));
        reuse->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ans+1"));
        QCOMPARE(asDouble(page.history().answer()), 100.0);
        const int count = page.history().count();
        page.submit();
        QCOMPARE(page.history().count(), count + 1);
        QCOMPARE(asDouble(page.history().records().last().answerBefore), 100.0);
        QCOMPARE(asDouble(page.history().records().last().result.value), 101.0);
        QCOMPARE(asDouble(page.history().record(2)->answerBefore), 42.0);
        QCOMPARE(asDouble(page.history().record(2)->result.value), 43.0);
        retry->click();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+"));
        page.input()->insertPlainText(QStringLiteral("2"));
        page.submit();
        QCOMPARE(asDouble(page.history().records().last().result.value), 3.0);
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
        QCOMPARE(asDouble(page.history().answer()), 0.0);
        QInputMethodEvent finish;
        QApplication::sendEvent(page.input(), &finish);
        page.submit();
        QCOMPARE(page.history().records().first().id, quint64(1));
        QCOMPARE(asDouble(page.history().answer()), 1.0);
        page.input()->setPlainText(QStringLiteral("1+"));
        page.submit();
        QCOMPARE(page.history().records().last().id, quint64(2));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+"));
        QCOMPARE(page.input()->textCursor().position(), 2);
        QCOMPARE(asDouble(page.history().answer()), 1.0);
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
        const auto *formula = page.findChild<RecordText *>(QStringLiteral("recordFormula"));
        const auto *result = page.findChild<RecordText *>(QStringLiteral("recordResult"));
        QVERIFY(formula && result);
        // 25 条记录的布局通过事件队列更新；Debug/Windows 不保证在固定 50ms 内完成。
        QTRY_VERIFY(result->mapTo(&page, QPoint()).y() > formula->mapTo(&page, QPoint()).y());
        auto *scroll = page.findChild<QScrollArea *>();
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
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
        auto *label = page.findChild<RecordText *>(QStringLiteral("recordResult"));
        label->setFocus();
        QTRY_VERIFY(label->hasFocus());
        QTextCursor selection = label->textCursor();
        selection.setPosition(2);
        selection.setPosition(4, QTextCursor::KeepAnchor);
        label->setTextCursor(selection);
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
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0.3"));
        QVERIFY(ExpressionEngine::evaluate(QApplication::clipboard()->text()).value == page.history().answer());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("未提交草稿"));
        QCOMPARE(page.input()->textCursor().position(), before.position());
        QCOMPARE(page.input()->textCursor().anchor(), before.anchor());
        QCOMPARE(page.history().count(), 1);
        more->findChild<QAction *>(QStringLiteral("copyCalculation"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("0.1+0.2\n= 0.3"));
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
        page.findChild<RecordText *>(QStringLiteral("recordFormula"))->setPlainText(QStringLiteral("999"));
        page.findChild<RecordText *>(QStringLiteral("recordResult"))->setPlainText(QStringLiteral("= 999"));
        more->findChild<QAction *>(QStringLiteral("copyCalculation"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("ans+2\n= 2"));
        more->findChild<QAction *>(QStringLiteral("copyValue"))->trigger();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("2"));
        page.input()->setPlainText(QStringLiteral("10+"));
        page.input()->moveCursor(QTextCursor::End);
        more->findChild<QAction *>(QStringLiteral("insertResult"))->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("10+2"));
        QCOMPARE(asDouble(page.history().answer()), 63.0);
        QCOMPARE(page.history().count(), 65);
        page.submit();
        QCOMPARE(asDouble(page.history().answer()), 12.0);
        QCOMPARE(asDouble(page.history().record(1)->result.value), 2.0);
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
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("1+中\n无法计算：未知函数或常量：中"));
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("1+中"));
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(asDouble(page.history().answer()), 0.0);
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
        auto *result = page.findChild<RecordText *>(QStringLiteral("recordResult"));
        QVERIFY(result);
        result->setFocus();
        QTRY_VERIFY(result->hasFocus());
        QApplication::clipboard()->setText(QStringLiteral("keep clipboard"));
        QVERIFY(chooseRecordAction(page.findChild<QToolButton *>(QStringLiteral("recordActions")),
                                   page.findChild<QAction *>(QStringLiteral("insertResult"))));
        QCOMPARE(page.input()->toPlainText(), expected);
        QCOMPARE(page.input()->textCursor().position(), qMin(start, end) + 4);
        QCoreApplication::processEvents();
        QTRY_VERIFY(page.input()->hasFocus());
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("keep clipboard"));
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(asDouble(page.history().answer()), -2.0);
        page.routeEdit(QStringLiteral("actionundo"));
        QCOMPARE(page.input()->toPlainText(), draft);
        page.routeEdit(QStringLiteral("actionredo"));
        QCOMPARE(page.input()->toPlainText(), expected);
        page.submit();
        QCOMPARE(asDouble(page.history().answer()), value);
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
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("2+0.3"));
        QVERIFY(!completion(page)->isVisible());
        page.input()->setPlainText(QStringLiteral("10+"));
        page.input()->moveCursor(QTextCursor::End);
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(page.input(), &preedit);
        insert->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("10+"));
        QCOMPARE(page.history().count(), 1);
        QInputMethodEvent finish;
        QApplication::sendEvent(page.input(), &finish);
        insert->trigger();
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("10+0.3"));
    }
    void compositionDoesNotSubmit()
    {
        CalculatorPage page;
        page.input()->setPlainText(QStringLiteral("1+2"));
        QInputMethodEvent preedit(QStringLiteral("中"), {});
        QApplication::sendEvent(page.input(), &preedit);
        page.submit();
        QCOMPARE(page.history().count(), 0);
        QInputMethodEvent finish;
        QApplication::sendEvent(page.input(), &finish);
        page.submit();
        QCOMPARE(page.history().count(), 1);
    }
    void textIsNotHtml()
    {
        CalculatorPage page;
        page.input()->setPlainText(QStringLiteral("<b>1</b>"));
        page.submit();
        auto *formula = page.findChild<RecordText *>(QStringLiteral("recordFormula"));
        QCOMPARE(formula->toPlainText(), QStringLiteral("<b>1</b>"));
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
        QCOMPARE(page.history().count(), 0);
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
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(page.findChild<RecordText *>(QStringLiteral("recordResult"))->toPlainText(), QStringLiteral("= 3"));
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
        QCOMPARE(page.history().count(), 0);
        QVERIFY(!completion(page)->isVisible());
        QTest::keyClicks(page.input(), "i");
        QTRY_VERIFY(completion(page)->isVisible());
        const QModelIndex index = completion(page)->model()->index(0, 0);
        QTest::mouseClick(completion(page)->viewport(), Qt::LeftButton, Qt::NoModifier,
                         completion(page)->visualRect(index).center());
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("pi"));
        QCOMPARE(page.history().count(), 0);
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
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("ans+@s"));
        QVERIFY(page.findChild<QLabel *>(QStringLiteral("calculationStatus"))->text().contains(QStringLiteral("请先完成")));
        page.findChild<QPushButton *>(QStringLiteral("calculateButton"))->click();
        QCOMPARE(page.history().count(), 1);
        page.input()->setPlainText(QStringLiteral("ans+1"));
        page.submit();
        QCOMPARE(page.findChildren<RecordText *>(QStringLiteral("recordResult")).last()->toPlainText(), QStringLiteral("= 43"));
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
            QCOMPARE(page.history().count(), 0);
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
        QCOMPARE(page.history().count(), 0);
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("平方根"));
        QApplication::sendEvent(page.input(), &commit);
        QTRY_COMPARE(page.input()->toPlainText(), QStringLiteral("@平方根"));
        QTRY_VERIFY(completion(page)->isVisible());
        QTest::keyClick(page.input(), Qt::Key_Return);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("sqrt()"));
        QCOMPARE(page.history().count(), 0);
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
        QVERIFY(operations->toPlainText().contains(QStringLiteral("切换历史记录后不能通过撤销恢复上一条输入")));
        search->setText(QStringLiteral("清空会话"));
        QVERIFY(operations->toPlainText().contains(QStringLiteral("ans 重置为 0")));
        QVERIFY(operations->toPlainText().contains(QStringLiteral("不可撤销")));
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
        QVERIFY(text.contains(QStringLiteral("notepad-- 标签页计算器")));
        QVERIFY(text.contains(QStringLiteral("GNU GPL v3.0 or later")));
        const QString directory = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        if (!directory.isEmpty())
        {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(about->grab().save(directory + QStringLiteral("/about-light.png")));
        }
    }
    void responsiveActionsAndKeyboardSubmission_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("pointSize");
        QTest::addColumn<bool>("dark");
        QTest::newRow("wide-light") << 960 << 12 << false;
        QTest::newRow("narrow-light") << 480 << 12 << false;
        QTest::newRow("narrow-dark") << 480 << 12 << true;
        QTest::newRow("large-font-dark") << 480 << 18 << true;
    }
    void responsiveActionsAndKeyboardSubmission()
    {
        QFETCH(int, width);
        QFETCH(int, pointSize);
        QFETCH(bool, dark);
        QWidget owner;
        QFont large = owner.font();
        large.setPointSize(pointSize);
        owner.setFont(large);
        CalculatorPage page(&owner);
        owner.resize(width, 760);
        page.setGeometry(owner.rect());
        owner.show();
        QPalette colors = page.palette();
        colors.setColor(QPalette::Window, QColor(dark ? "#202329" : "#f0f0f0"));
        colors.setColor(QPalette::Base, QColor(dark ? "#202329" : "#ffffff"));
        colors.setColor(QPalette::Text, QColor(dark ? "#e4e8ef" : "#202329"));
        page.setPalette(colors);
        page.resize(width, 760);
        page.show();
        owner.activateWindow();
        page.focusInput();
        page.input()->setPlainText(QStringLiteral("6*7"));
        auto *calculate = page.findChild<QPushButton *>(QStringLiteral("calculateButton"));
        QTRY_VERIFY(page.input()->hasFocus());
        QTest::keyClick(page.input(), Qt::Key_Tab);
        QTRY_VERIFY(calculate->hasFocus());
        QTest::keyClick(calculate, Qt::Key_Space);
        QCOMPARE(page.history().count(), 1);
        QCOMPARE(asDouble(page.history().answer()), 42.0);
        QCoreApplication::processEvents();
        QCOMPARE(page.width(), width);
        QList<QWidget *> controls;
        for (const QString &name : {QStringLiteral("exportHistoryButton"), QStringLiteral("sessionButton"),
                                   QStringLiteral("clearSessionButton"), QStringLiteral("helpButton")})
            controls.append(page.findChild<QWidget *>(name));
        int buttonHeight = controls.first()->height();
        QVector<QRect> rectangles;
        for (auto *button : controls)
        {
            QVERIFY(button && button->isVisible());
            QCOMPARE(button->height(), buttonHeight);
            const QRect bounds(button->mapTo(&page, QPoint()), button->size());
            QVERIFY(page.rect().contains(bounds));
            QVERIFY(bounds.bottom() < page.input()->mapTo(&page, QPoint()).y());
            for (const QRect &other : rectangles) QVERIFY(!bounds.intersects(other));
            rectangles.append(bounds);
            // 真实控件命中，而非仅有一个看起来正确但不可点的按钮。
            QCOMPARE(page.childAt(bounds.center()), button);
        }
        QCOMPARE(controls.first()->font().pointSize(), pointSize);
        if (pointSize == 18) QVERIFY(rectangles.last().top() > rectangles.first().top());
        QVERIFY(page.rect().contains(QRect(calculate->mapTo(&page, QPoint()), calculate->size())));
        QVERIFY(page.input()->width() >= width - 60);
        auto *reuse = page.findChild<QPushButton *>(QStringLiteral("reuseFormula"));
        auto *more = page.findChild<QToolButton *>(QStringLiteral("recordActions"));
        QCOMPARE(reuse->height(), more->height());
        QCOMPARE(reuse->width(), more->width());
        QVERIFY(chooseRecordAction(more, more->menu()->findChild<QAction *>(QStringLiteral("copyValue"))));
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("42"));
        const QString directory = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
        if (!directory.isEmpty())
        {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(page.grab().save(directory + QStringLiteral("/layout-%1.png").arg(QString::fromLatin1(QTest::currentDataTag()))));
        }
    }
    void themedDefinitionAndMenusFollowPaletteChanges()
    {
        CalculatorPage page;
        prepare(page, QStringLiteral("2+3"));
        auto *normalMode = page.findChild<QToolButton *>(QStringLiteral("normalModeButton"));
        auto *customMode = page.findChild<QToolButton *>(QStringLiteral("customModeButton"));
        QVERIFY(normalMode && customMode);
        QCOMPARE(normalMode->text(), QStringLiteral("普通计算"));
        QCOMPARE(customMode->text(), QStringLiteral("自定义公式"));
        QVERIFY(normalMode->isChecked() && !customMode->isChecked());
        QTest::mouseClick(customMode, Qt::LeftButton);
        QVERIFY(!normalMode->isChecked() && customMode->isChecked());
        auto *edit = page.findChild<QPlainTextEdit *>(QStringLiteral("currentCustomDefinition"));
        QVERIFY(edit && edit->isVisible());
        for (bool dark : {true, false})
        {
            QPalette colors = page.palette();
            colors.setColor(QPalette::Window, QColor(dark ? "#202329" : "#f0f0f0"));
            colors.setColor(QPalette::Base, QColor(dark ? "#202329" : "#ffffff"));
            colors.setColor(QPalette::Text, QColor(dark ? "#e4e8ef" : "#202329"));
            page.setPalette(colors);
            QCoreApplication::processEvents();
            QCOMPARE(edit->palette().color(QPalette::Base), colors.color(QPalette::Base));
            QCOMPARE(edit->palette().color(QPalette::Text), colors.color(QPalette::Text));
            const QString directory = qEnvironmentVariable("CALCTABDD_SCREENSHOT_DIR");
            if (!directory.isEmpty())
            {
                QVERIFY(QDir().mkpath(directory));
                edit->setPlainText(QStringLiteral("A=sqrt(x^2+y^2)"));
                QVERIFY(page.grab().save(directory + (dark ? QStringLiteral("/definition-dark.png") : QStringLiteral("/definition-light.png"))));
            }
        }
        auto *stateBadge = page.findChild<QLabel *>(QStringLiteral("customDefinitionStateBadge"));
        auto *applyBtn = page.findChild<QPushButton *>(QStringLiteral("applyDefinitionButton"));
        auto *paramChips = page.findChild<QLabel *>(QStringLiteral("parameterChipsBadge"));
        auto *ansBadge = page.findChild<QLabel *>(QStringLiteral("answerBadge"));
        auto *recallBadge = page.findChild<QLabel *>(QStringLiteral("historyRecallBadge"));
        QVERIFY(stateBadge && applyBtn && paramChips && ansBadge && recallBadge);
        QCOMPARE(stateBadge->text(), QStringLiteral("待按 Enter 应用"));
        QCOMPARE(ansBadge->text(), QStringLiteral("ans = 0"));
        QVERIFY(!recallBadge->isVisible());
        QTest::keyClick(edit, Qt::Key_Escape);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(page.input()->toPlainText(), QStringLiteral("2+3"));
        QCOMPARE(page.history().count(), 0);
        QTest::mouseClick(customMode, Qt::LeftButton);
        edit->setPlainText(QStringLiteral("A=sqrt(x^2+y^2)"));
        QTest::mouseClick(applyBtn, Qt::LeftButton);
        QCOMPARE(stateBadge->text(), QStringLiteral("已应用"));
        QCOMPARE(paramChips->text(), QStringLiteral("参数：x · y"));
        page.input()->setPlainText(QStringLiteral("x=3\ny=4"));
        page.submit();
        QCOMPARE(ansBadge->text(), QStringLiteral("ans = 5"));
        auto *customMeta = page.findChild<RecordText *>(QStringLiteral("recordCustomMeta"));
        QVERIFY(customMeta && customMeta->isVisible());
        QVERIFY(customMeta->toPlainText().contains(QStringLiteral("A=sqrt(x^2+y^2)")));
        QVERIFY(customMeta->toPlainText().contains(QStringLiteral("x=3, y=4")));
        QTest::keyClick(page.input(), Qt::Key_Up, Qt::AltModifier);
        QVERIFY(recallBadge->isVisible());
        QVERIFY(recallBadge->text().contains(QStringLiteral("01/01")));
        QTest::keyClick(page.input(), Qt::Key_Down, Qt::AltModifier);
        QVERIFY(!recallBadge->isVisible());
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
            page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
            auto *confirmation = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
            QTest::qWait(30);
            QVERIFY(confirmation->grab().save(directory + QStringLiteral("/clear-confirmation-light.png")));
            confirmation->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
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
            page.findChild<QPushButton *>(QStringLiteral("clearSessionButton"))->click();
            auto *confirmation = page.findChild<QDialog *>(QStringLiteral("clearSessionConfirmation"));
            QTest::qWait(30);
            QCOMPARE(confirmation->palette().color(QPalette::Window), QColor("#202329"));
            QVERIFY(confirmation->styleSheet().contains(QStringLiteral("#e4e8ef")));
            QVERIFY(confirmation->grab().save(directory + QStringLiteral("/clear-confirmation-dark.png")));
            confirmation->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            page.activateWindow();
            page.focusInput();
            QTRY_VERIFY(page.input()->hasFocus());
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
