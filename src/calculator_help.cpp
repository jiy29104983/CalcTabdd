#include "calculator_help.h"
#include "calculator_style.h"
#include "calculation_catalog.h"

#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QScopedValueRollback>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <functional>

namespace {
QColor blendColor(const QColor &a, const QColor &b, int percentB)
{
    return QColor((a.red() * (100 - percentB) + b.red() * percentB) / 100,
                  (a.green() * (100 - percentB) + b.green() * percentB) / 100,
                  (a.blue() * (100 - percentB) + b.blue() * percentB) / 100);
}

class InformationDialog : public QDialog
{
public:
    explicit InformationDialog(QWidget *owner) : QDialog(owner)
    {
        setAttribute(Qt::WA_WindowPropagation, true);
        setPalette(owner->palette());
        applyPalette();
    }
    void setRefreshCallback(std::function<void()> callback)
    {
        m_refresh = std::move(callback);
        if (m_refresh) m_refresh();
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
        CalculatorStyle::applyDialog(this, palette());
        if (m_refresh) m_refresh();
    }
    std::function<void()> m_refresh;
    bool m_applying = false;
};

void present(QDialog *dialog)
{
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
}

void showCalculatorHelp(QWidget *owner, bool programmer)
{
    if (!owner) return;
    auto *existing = owner->findChild<QDialog *>(QStringLiteral("calctabddHelpDialog"), Qt::FindDirectChildrenOnly);
    if (existing)
    {
        existing->findChild<QComboBox *>(QStringLiteral("helpMode"))->setCurrentIndex(programmer ? 1 : 0);
        present(existing);
        return;
    }
    auto *dialog = new InformationDialog(owner);
    dialog->setObjectName(QStringLiteral("calctabddHelpDialog"));
    dialog->setWindowTitle(QStringLiteral("CalcTabdd · 运算与精度帮助"));
    dialog->setModal(false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const QSize available = owner->screen() ? owner->screen()->availableGeometry().size() : QSize(1024, 768);
    dialog->resize(qMin(840, available.width() - 40), qMin(650, available.height() - 60));
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(10);
    auto *mode = new QComboBox(dialog);
    mode->setObjectName(QStringLiteral("helpMode"));
    mode->setAccessibleName(QStringLiteral("帮助模式"));
    mode->addItem(QStringLiteral("普通／自定义计算"));
    mode->addItem(QStringLiteral("程序员"));
    mode->setCurrentIndex(programmer ? 1 : 0);
    layout->addWidget(mode);
    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(10);
    auto *search = new QLineEdit(dialog);
    search->setObjectName(QStringLiteral("helpSearch"));
    search->setAccessibleName(QStringLiteral("搜索运算与精度"));
    search->setPlaceholderText(QStringLiteral("搜索函数、中文关键词或精度，例如 sqrt、平方根、取余、精度"));
    search->setClearButtonEnabled(true);
    searchRow->addWidget(search, 1);
    auto *count = new QLabel(dialog);
    count->setObjectName(QStringLiteral("helpMatchCount"));
    searchRow->addWidget(count);
    layout->addLayout(searchRow);
    auto *tabs = new QTabWidget(dialog);
    tabs->setObjectName(QStringLiteral("helpTabs"));
    auto *operations = new QTextBrowser(tabs);
    operations->setObjectName(QStringLiteral("operationsHelp"));
    auto *precision = new QTextBrowser(tabs);
    precision->setObjectName(QStringLiteral("precisionHelp"));
    tabs->addTab(operations, QStringLiteral("运算速查"));
    tabs->addTab(precision, QStringLiteral("精度与限制"));
    layout->addWidget(tabs, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    CalculatorStyle::prepareButton(buttons->button(QDialogButtonBox::Close));
    const auto filter = [dialog, mode, operations, precision, tabs, count](const QString &query) {
        const QPalette pal = CalculatorStyle::palette(dialog->palette());
        const QColor base = pal.color(QPalette::Base);
        const QColor text = pal.color(QPalette::Text);
        const QColor accent = pal.color(QPalette::Highlight);
        const bool dark = base.lightness() < 128;
        const QString cardBg = blendColor(base, text, dark ? 5 : 3).name();
        const QString codeBg = blendColor(base, accent, dark ? 22 : 12).name();
        const QString borderCol = blendColor(base, text, dark ? 18 : 14).name();
        const QString mutedCol = blendColor(base, text, 68).name();
        const QString accentCol = (dark ? accent.lighter(130) : accent.darker(115)).name();

        QString html[2];
        QString category[2];
        int matches[2] = {0, 0};
        for (const auto &entry : CalculationCatalog::entries(mode->currentIndex() == 1))
        {
            if (!entry.matches(query)) continue;
            const int page = entry.kind == CalculationCatalog::Kind::Precision ? 1 : 0;
            if (category[page] != entry.category)
            {
                category[page] = entry.category;
                html[page] += QStringLiteral("<h2>%1</h2>").arg(entry.category.toHtmlEscaped());
            }
            html[page] += QStringLiteral(
                "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"10\" "
                "style=\"margin-top: 6px; margin-bottom: 10px; background-color: %1; border: 1px solid %2;\">"
                "<tr><td>"
                "<p style=\"margin-top: 0; margin-bottom: 5px;\">"
                "<code style=\"background-color: %3; color: %4; font-family: monospace; font-weight: bold;\">%5</code>"
                " &mdash; <b>%6</b></p>"
                "<p style=\"margin-top: 4px; margin-bottom: 6px;\">%7</p>"
                "<p style=\"margin-top: 0; margin-bottom: 0; color: %8;\"><b>示例：</b>"
                "<code style=\"background-color: %3; color: %9; font-family: monospace;\">%10</code></p>"
                "</td></tr></table>")
                .arg(cardBg, borderCol, codeBg, accentCol,
                     entry.signature.toHtmlEscaped(), entry.title.toHtmlEscaped(),
                     entry.description.toHtmlEscaped(), mutedCol, text.name(),
                     entry.example.toHtmlEscaped());
            ++matches[page];
        }
        const QString empty = QStringLiteral("<p>没有匹配项。可尝试英文函数名、中文关键词，或清空搜索查看全部内容。</p>");
        const QString style = QStringLiteral(
            "<style>h2 { margin-top: 14px; margin-bottom: 8px; }"
            "p { margin-top: 4px; margin-bottom: 8px; line-height: 1.4; }</style>");
        operations->setHtml(style + (matches[0] ? html[0] : empty));
        precision->setHtml(style + (matches[1] ? html[1] : empty));
        count->setText(QStringLiteral("运算 %1 项 · 精度 %2 项").arg(matches[0]).arg(matches[1]));
        if (!matches[tabs->currentIndex()] && matches[1 - tabs->currentIndex()])
            tabs->setCurrentIndex(1 - tabs->currentIndex());
    };
    QObject::connect(search, &QLineEdit::textChanged, dialog, filter);
    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog, [search, filter]() {
        search->clear();
        filter(search->text());
    });
    dialog->setRefreshCallback([search, filter]() { filter(search->text()); });
    present(dialog);
    search->setFocus();
}

void showCalculatorAbout(QWidget *owner)
{
    if (!owner) return;
    auto *existing = owner->findChild<QDialog *>(QStringLiteral("calctabddAboutDialog"), Qt::FindDirectChildrenOnly);
    if (existing)
    {
        present(existing);
        return;
    }
    auto *dialog = new InformationDialog(owner);
    dialog->setObjectName(QStringLiteral("calctabddAboutDialog"));
    dialog->setWindowTitle(QStringLiteral("关于 CalcTabdd"));
    dialog->setModal(false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(440, 260);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    auto *text = new QTextBrowser(dialog);
    text->setObjectName(QStringLiteral("aboutDetails"));
    text->setStyleSheet(QStringLiteral(
        "QTextBrowser#aboutDetails { background: transparent; border: none; padding: 0; }"));
    dialog->setRefreshCallback([dialog, text]() {
        const QPalette pal = CalculatorStyle::palette(dialog->palette());
        const QColor base = pal.color(QPalette::Base);
        const QColor fg = pal.color(QPalette::Text);
        const QString muted = blendColor(base, fg, 68).name();
        text->setHtml(QStringLiteral(
            "<h2 style=\"margin-top: 0; margin-bottom: 6px;\">CalcTabdd</h2>"
            "<p style=\"margin-top: 0; margin-bottom: 14px; color: %1;\">notepad-- 标签页计算器</p>"
            "<p style=\"margin-top: 0; margin-bottom: 6px;\"><b>版本 %2</b></p>"
            "<p style=\"margin-top: 0; margin-bottom: 0; color: %1;\">开源许可：GNU GPL v3.0 or later</p>")
            .arg(muted, QStringLiteral(CALCTABDD_VERSION)));
    });
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);
    CalculatorStyle::prepareButton(buttons->button(QDialogButtonBox::Close));
    present(dialog);
}
