#include "calculator_help.h"
#include "calculation_catalog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QScopedValueRollback>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {
class InformationDialog : public QDialog
{
public:
    explicit InformationDialog(QWidget *owner) : QDialog(owner)
    {
        setAttribute(Qt::WA_WindowPropagation, true);
        setPalette(owner->palette());
        applyPalette();
    }
protected:
    void changeEvent(QEvent *event) override
    {
        QDialog::changeEvent(event);
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
            applyPalette();
    }
private:
    void applyPalette()
    {
        if (m_applying) return;
        QScopedValueRollback<bool> applying(m_applying, true);
        const QPalette colors = palette();
        setStyleSheet(QStringLiteral(
            "QDialog { background: %1; color: %2; } QLabel { color: %2; }"
            "QTextBrowser, QLineEdit { background: %3; color: %2; border: 1px solid %4; padding: 4px; }"
            "QTabBar::tab { background: %1; color: %2; border: 1px solid %4; padding: 6px 12px; }"
            "QTabBar::tab:selected { background: %3; }"
            "QPushButton { background: %3; color: %2; border: 1px solid %4; padding: 5px 16px; }")
            .arg(colors.color(QPalette::Window).name(), colors.color(QPalette::Text).name(),
                 colors.color(QPalette::Base).name(), colors.color(QPalette::Mid).name()));
    }
    bool m_applying = false;
};

void present(QDialog *dialog)
{
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

QString section(const CalculationCatalog::Entry &entry)
{
    return QStringLiteral("<h3>%1 — %2</h3><p>%3</p><p><b>示例：</b>%4</p>")
        .arg(entry.signature.toHtmlEscaped(), entry.title.toHtmlEscaped(),
             entry.description.toHtmlEscaped(), entry.example.toHtmlEscaped());
}
}

void showCalculatorHelp(QWidget *owner)
{
    if (!owner) return;
    auto *dialog = owner->findChild<QDialog *>(QStringLiteral("calctabddHelpDialog"), Qt::FindDirectChildrenOnly);
    if (dialog)
    {
        present(dialog);
        return;
    }
    dialog = new InformationDialog(owner);
    dialog->setObjectName(QStringLiteral("calctabddHelpDialog"));
    dialog->setWindowTitle(QStringLiteral("CalcTabdd · 运算与精度帮助"));
    dialog->setModal(false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const QSize available = owner->screen() ? owner->screen()->availableGeometry().size() : QSize(1024, 768);
    dialog->resize(qMin(820, available.width() - 40), qMin(640, available.height() - 60));
    auto *layout = new QVBoxLayout(dialog);
    auto *search = new QLineEdit(dialog);
    search->setObjectName(QStringLiteral("helpSearch"));
    search->setAccessibleName(QStringLiteral("搜索运算与精度"));
    search->setPlaceholderText(QStringLiteral("搜索函数、中文关键词或精度，例如 sqrt、平方根、取余、精度"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    auto *count = new QLabel(dialog);
    count->setObjectName(QStringLiteral("helpMatchCount"));
    layout->addWidget(count);
    auto *tabs = new QTabWidget(dialog);
    tabs->setObjectName(QStringLiteral("helpTabs"));
    auto *operations = new QTextBrowser(tabs);
    operations->setObjectName(QStringLiteral("operationsHelp"));
    auto *precision = new QTextBrowser(tabs);
    precision->setObjectName(QStringLiteral("precisionHelp"));
    tabs->addTab(operations, QStringLiteral("运算速查"));
    tabs->addTab(precision, QStringLiteral("精度与限制"));
    layout->addWidget(tabs);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    const auto filter = [operations, precision, tabs, count](const QString &query) {
        QString html[2];
        QString category[2];
        int matches[2] = {0, 0};
        for (const auto &entry : CalculationCatalog::entries())
        {
            if (!entry.matches(query)) continue;
            const int page = entry.kind == CalculationCatalog::Kind::Precision ? 1 : 0;
            if (category[page] != entry.category)
            {
                category[page] = entry.category;
                html[page] += QStringLiteral("<h2>%1</h2>").arg(entry.category.toHtmlEscaped());
            }
            html[page] += section(entry);
            ++matches[page];
        }
        const QString empty = QStringLiteral("<p>没有匹配项。可尝试英文函数名、中文关键词，或清空搜索查看全部内容。</p>");
        operations->setHtml(matches[0] ? html[0] : empty);
        precision->setHtml(matches[1] ? html[1] : empty);
        count->setText(QStringLiteral("运算 %1 项 · 精度 %2 项").arg(matches[0]).arg(matches[1]));
        if (!matches[tabs->currentIndex()] && matches[1 - tabs->currentIndex()])
            tabs->setCurrentIndex(1 - tabs->currentIndex());
    };
    QObject::connect(search, &QLineEdit::textChanged, dialog, filter);
    filter(QString());
    present(dialog);
    search->setFocus();
}

void showCalculatorAbout(QWidget *owner)
{
    if (!owner) return;
    auto *dialog = owner->findChild<QDialog *>(QStringLiteral("calctabddAboutDialog"), Qt::FindDirectChildrenOnly);
    if (dialog)
    {
        present(dialog);
        return;
    }
    dialog = new InformationDialog(owner);
    dialog->setObjectName(QStringLiteral("calctabddAboutDialog"));
    dialog->setWindowTitle(QStringLiteral("关于 CalcTabdd"));
    dialog->setModal(false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(580, 380);
    auto *layout = new QVBoxLayout(dialog);
    auto *text = new QTextBrowser(dialog);
    text->setObjectName(QStringLiteral("aboutDetails"));
    text->setHtml(QStringLiteral(
        "<h2>CalcTabdd %1 测试版</h2>"
        "<p>notepad-- 标签页计算器 · GNU GPL v3.0 or later</p>"
        "<p><b>目标宿主：</b>Windows x64 版 notepad--，Qt 5.15.2 / MSVC v142。</p>"
        "<p><b>源码兼容参考：</b>notepad-- v3.8.3 / v3.9.0，固定源码提交 "
        "<code>91105f68b74382128f3313ac5af8accdc77de918</code>。</p>"
        "<p>上述版本对应源码接口参考，不代表两个发布二进制都已实机验收；"
        "真实宿主 DLL 加载、输入法、关闭退出与 DPI 仍需手动确认。其他宿主版本未验证。</p>"
        "<p><b>本次构建：</b>Qt %2；当前 Qt 运行库 %3；%4 位。</p>"
        "<p>双精度实数计算，最多显示 15 位有效数字。详细能力见“运算与精度帮助”。"
        "计算历史只保留在当前标签，关闭后清空。</p>")
        .arg(QStringLiteral(CALCTABDD_VERSION), QStringLiteral(QT_VERSION_STR), QString::fromLatin1(qVersion()))
        .arg(sizeof(void *) * 8));
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    present(dialog);
}
