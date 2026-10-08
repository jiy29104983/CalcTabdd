#include "calculation_catalog.h"
#include <QStringList>

namespace CalculationCatalog {
bool Entry::isCompletion() const
{
    return kind == Kind::Function || kind == Kind::Constant;
}

bool Entry::matches(const QString &query, bool completion) const
{
    const QString needle = query.trimmed();
    if (needle.isEmpty()) return true;
    if (!completion)
        return (signature + QLatin1Char(' ') + category + QLatin1Char(' ') + title + QLatin1Char(' ') +
                description + QLatin1Char(' ') + example + QLatin1Char(' ') + keywords)
            .contains(needle, Qt::CaseInsensitive);
    if (name.startsWith(needle, Qt::CaseInsensitive) || title.contains(needle, Qt::CaseInsensitive)) return true;
    for (const QString &word : keywords.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (word.startsWith(needle, Qt::CaseInsensitive)) return true;
    return false;
}

const QVector<Entry> &entries()
{
    static const QVector<Entry> values = {
        {Kind::Operator, QStringLiteral("+ -"), QStringLiteral("x + y；x - y"), QStringLiteral("基本运算"), QStringLiteral("加法与减法"), QStringLiteral("同级从左向右计算，优先级低于乘除和乘方。"), QStringLiteral("1+2*3 → 7"), QStringLiteral("加法 减法 四则 优先级 结合"), {}, 0},
        {Kind::Operator, QStringLiteral("* /"), QStringLiteral("x * y；x / y"), QStringLiteral("基本运算"), QStringLiteral("乘法与除法"), QStringLiteral("同级从左向右计算；除数不能为 0。×、÷ 也可使用。"), QStringLiteral("8/4/2 → 1"), QStringLiteral("乘法 除法 四则 除零"), {}, 0},
        {Kind::Operator, QStringLiteral("%"), QStringLiteral("x % y"), QStringLiteral("基本运算"), QStringLiteral("取余"), QStringLiteral("不是百分比；非零余数与被除数同号，除数不能为 0。十进制操作数精确取余；近似路径受浮点误差影响。"), QStringLiteral("-10%3 → -1；5.5%2 → 1.5"), QStringLiteral("取余 余数 模 百分比"), {}, 0},
        {Kind::Operator, QStringLiteral("^"), QStringLiteral("x ^ y"), QStringLiteral("基本运算"), QStringLiteral("乘方"), QStringLiteral("从右向左结合；优先级高于一元负号。负底数仅接受整数指数；底数为 0 时指数不能为负数。沿用 0^0 = 1。十进制整数指数绝对值最多10000；负整数幂先算正幂再取倒数，中间精度或范围超限会失败。"), QStringLiteral("2^3^2 → 512；-2^2 → -4；2^-3 → 0.125"), QStringLiteral("乘方 幂 指数 优先级 结合"), {}, 0},
        {Kind::Operator, QStringLiteral("unary"), QStringLiteral("+x；-x；(x)"), QStringLiteral("基本运算"), QStringLiteral("正负号与括号"), QStringLiteral("括号改变运算顺序；支持连续正负号。乘法必须显式输入 *，不支持 2pi 或 2(3)。"), QStringLiteral("(-2)^2 → 4；--2 → 2"), QStringLiteral("括号 正号 负号 隐式乘法"), {}, 0},
        {Kind::Function, QStringLiteral("sqrt"), QStringLiteral("sqrt(x)"), QStringLiteral("基础函数"), QStringLiteral("平方根"), QStringLiteral("x 必须大于或等于 0；有限可表示的十进制完全平方根精确返回，其他情况使用近似计算。"), QStringLiteral("sqrt(9) → 3"), QStringLiteral("平方根 开方 root"), QStringLiteral("sqrt()"), 5},
        {Kind::Function, QStringLiteral("abs"), QStringLiteral("abs(x)"), QStringLiteral("基础函数"), QStringLiteral("绝对值"), QStringLiteral("返回 x 的绝对值。"), QStringLiteral("abs(-2) → 2"), QStringLiteral("绝对值 absolute"), QStringLiteral("abs()"), 4},
        {Kind::Function, QStringLiteral("sin"), QStringLiteral("sin(x)"), QStringLiteral("三角函数"), QStringLiteral("正弦"), QStringLiteral("参数使用弧度；角度可用 度数*pi/180 转换。"), QStringLiteral("sin(pi/2) → 1"), QStringLiteral("正弦 三角 弧度 角度 sine"), QStringLiteral("sin()"), 4},
        {Kind::Function, QStringLiteral("cos"), QStringLiteral("cos(x)"), QStringLiteral("三角函数"), QStringLiteral("余弦"), QStringLiteral("参数使用弧度；没有角度模式。"), QStringLiteral("cos(0) → 1"), QStringLiteral("余弦 三角 弧度 角度 cosine"), QStringLiteral("cos()"), 4},
        {Kind::Function, QStringLiteral("tan"), QStringLiteral("tan(x)"), QStringLiteral("三角函数"), QStringLiteral("正切"), QStringLiteral("参数使用弧度；不会单独识别 pi/2 等数学奇点，附近结果可能是很大的有限值。"), QStringLiteral("tan(0) → 0"), QStringLiteral("正切 三角 弧度 角度 tangent"), QStringLiteral("tan()"), 4},
        {Kind::Function, QStringLiteral("ln"), QStringLiteral("ln(x)"), QStringLiteral("对数与指数"), QStringLiteral("自然对数"), QStringLiteral("以 e 为底，x 必须大于 0；0 和负数没有实数对数，会标记问题参数。"), QStringLiteral("ln(e) → 1"), QStringLiteral("对数 自然对数 logarithm"), QStringLiteral("ln()"), 3},
        {Kind::Function, QStringLiteral("log"), QStringLiteral("log(x)"), QStringLiteral("对数与指数"), QStringLiteral("常用对数"), QStringLiteral("以 10 为底，x 必须大于 0；0 和负数没有实数对数，会标记问题参数。"), QStringLiteral("log(1000) → 3"), QStringLiteral("对数 常用对数 log10 logarithm"), QStringLiteral("log()"), 4},
        {Kind::Function, QStringLiteral("exp"), QStringLiteral("exp(x)"), QStringLiteral("对数与指数"), QStringLiteral("自然指数"), QStringLiteral("计算 e 的 x 次幂；过大上溢、非零结果下溢为0均报错；使用近似计算。"), QStringLiteral("exp(0) → 1"), QStringLiteral("指数 自然指数 exponential"), QStringLiteral("exp()"), 4},
        {Kind::Function, QStringLiteral("floor"), QStringLiteral("floor(x)"), QStringLiteral("整数舍入"), QStringLiteral("向下取整"), QStringLiteral("返回不大于 x 的最大整数，负数也向负无穷方向取整。"), QStringLiteral("floor(-1.2) → -2"), QStringLiteral("取整 向下 舍入"), QStringLiteral("floor()"), 6},
        {Kind::Function, QStringLiteral("ceil"), QStringLiteral("ceil(x)"), QStringLiteral("整数舍入"), QStringLiteral("向上取整"), QStringLiteral("返回不小于 x 的最小整数。"), QStringLiteral("ceil(1.2) → 2"), QStringLiteral("取整 向上 舍入"), QStringLiteral("ceil()"), 5},
        {Kind::Function, QStringLiteral("round"), QStringLiteral("round(x)"), QStringLiteral("整数舍入"), QStringLiteral("最近整数舍入"), QStringLiteral("取最近整数，恰好半整数时远离0。十进制路径直接取整；与除法保留50位时的半值取偶规则不同。"), QStringLiteral("round(1.5) → 2；round(-1.5) → -2"), QStringLiteral("取整 四舍五入 舍入"), QStringLiteral("round()"), 6},
        {Kind::Function, QStringLiteral("min"), QStringLiteral("min(x, y)"), QStringLiteral("二元函数"), QStringLiteral("较小值"), QStringLiteral("恰好需要两个参数，用英文逗号分隔。"), QStringLiteral("min(2,3) → 2"), QStringLiteral("最小 较小 比较 minimum"), QStringLiteral("min(, )"), 4},
        {Kind::Function, QStringLiteral("max"), QStringLiteral("max(x, y)"), QStringLiteral("二元函数"), QStringLiteral("较大值"), QStringLiteral("恰好需要两个参数，用英文逗号分隔。"), QStringLiteral("max(-2,-3) → -2"), QStringLiteral("最大 较大 比较 maximum"), QStringLiteral("max(, )"), 4},
        {Kind::Function, QStringLiteral("pow"), QStringLiteral("pow(底数, 指数)"), QStringLiteral("二元函数"), QStringLiteral("乘方函数"), QStringLiteral("与 ^ 使用相同数学运算；恰好需要两个参数，参数名称仅为提示。"), QStringLiteral("pow(2,10) → 1024"), QStringLiteral("乘方 幂 次方 power"), QStringLiteral("pow(, )"), 4},
        {Kind::Constant, QStringLiteral("pi"), QStringLiteral("pi"), QStringLiteral("常量与上次结果"), QStringLiteral("圆周率"), QStringLiteral("约为 3.14159265358979，内部是双精度近似值。"), QStringLiteral("sin(pi/2) → 1"), QStringLiteral("圆周率 派"), QStringLiteral("pi"), 2},
        {Kind::Constant, QStringLiteral("e"), QStringLiteral("e"), QStringLiteral("常量与上次结果"), QStringLiteral("自然常数"), QStringLiteral("约为 2.71828182845905；与科学计数法中的 e 含义不同。"), QStringLiteral("ln(e) → 1"), QStringLiteral("自然常数 欧拉"), QStringLiteral("e"), 1},
        {Kind::Constant, QStringLiteral("ans"), QStringLiteral("ans"), QStringLiteral("常量与上次结果"), QStringLiteral("上一次成功结果"), QStringLiteral("初始为 0，失败计算不更新；保留内部精度，清空会话或打开新的空白会话后重置；从本地会话文件恢复时保留已保存值。"), QStringLiteral("先计算 6*7，再计算 ans+1 → 43"), QStringLiteral("上次 上一次 结果 答案 answer"), QStringLiteral("ans"), 3},
        {Kind::Syntax, QStringLiteral("numbers"), QStringLiteral("小数与科学计数法"), QStringLiteral("输入语法"), QStringLiteral("数字写法"), QStringLiteral("小数点固定为 .，支持 .5、1. 和 e/E 指数，不接受千分位或全角数字。"), QStringLiteral("1e3+2.5E-2 → 1000.025"), QStringLiteral("数字 小数 科学计数法 区域设置"), {}, 0},
        {Kind::Syntax, QStringLiteral("input"), QStringLiteral("大小写、嵌套与输入限制"), QStringLiteral("输入语法"), QStringLiteral("公式规则"), QStringLiteral("函数和常量不区分大小写，可嵌套，忽略空白。最多 4096 个 UTF-16 代码单元，递归计数上限 128（并非 128 对括号）。普通模式不支持变量赋值；自定义公式通过专用入口定义参数。不支持复数或符号求解。"), QStringLiteral("MAX(sqrt(9),pow(2,3)) → 8"), QStringLiteral("大小写 嵌套 长度 语法 错误 变量"), {}, 0},
        {Kind::Syntax, QStringLiteral("custom"), QStringLiteral("自定义公式与参数"), QStringLiteral("输入操作"), QStringLiteral("定义一次，填写不同参数后确认计算"), QStringLiteral("底部模式菜单选择“定义／修改公式”，例如 A=x+y；确认后自动生成 x=、y= 分行输入，每行一个参数。名称只用英文字母、数字、下划线，不能数字开头，区分大小写且不能与内置名称冲突；参数只填有符号数字、小数、科学计数法。公式支持全部函数、pi/e/ans。编辑时不计算或预览，Ctrl+Enter 才追加历史，成功后保留参数。普通 Enter 换行。缺少／非法参数不入列；数学错误保留错误记录。两模式共享 ans，并分别保留草稿；修改定义保留同名值，新增参数留空。历史复用恢复当时定义和参数，成功提交或浏览到最后之后返回原草稿；手动切换模式也结束浏览。@ 补全仅用于普通模式。"), QStringLiteral("A=x+y，分两行填 x=1 和 y=2；Ctrl+Enter 后显示 A=1+2，下一行 = 3"), QStringLiteral("自定义 参数 公式 模式 定义 保存 历史 变量"), {}, 0},
        {Kind::Syntax, QStringLiteral("diagnostics"), QStringLiteral("错误定位与修正"), QStringLiteral("输入操作"), QStringLiteral("标记问题字符或参数"), QStringLiteral("计算失败时保留公式和 ans，输入区用底色与波浪线标记错误范围，光标移到问题处，状态栏显示行列。末尾缺少内容时标记该行，光标停在插入点。定位计入输入区前导空白；标记不等于文本选区，继续输入不会替换整段。编辑、输入法组词或清空会话后移除标记，重新提交可重新定位。回填未修改的失败公式会恢复其历史诊断。"), QStringLiteral("ln(0) 标记参数 0，并提示参数必须大于 0"), QStringLiteral("错误 定位 高亮 范围 行列 参数 修正 下划线"), {}, 0},
        {Kind::Syntax, QStringLiteral("completion"), QStringLiteral("@ 快捷补全"), QStringLiteral("输入语法"), QStringLiteral("查找函数与常量"), QStringLiteral("输入 @ 后用英文名称或中文关键词筛选；↑↓ 选择，Enter/Tab 插入，Esc 关闭。补全完成后 Ctrl+Enter 计算。中文输入法组词时优先处理输入法候选。"), QStringLiteral("2+@平方根 → 2+sqrt()，光标位于括号内"), QStringLiteral("补全 快捷 输入法 帮助"), {}, 0},
        {Kind::Syntax, QStringLiteral("history"), QStringLiteral("Alt+↑ / Alt+↓ 历史召回"), QStringLiteral("输入操作"), QStringLiteral("保留草稿并浏览历史公式"), QStringLiteral("在输入框按 Alt+↑ 从最新记录向前浏览，Alt+↓ 向后浏览，越过最新记录返回草稿；最旧处不循环。首次召回保存草稿、光标和选区，浏览中临时编辑在本轮往返时保留；返回草稿或成功提交后结束本轮浏览。普通方向键移动光标；输入法组词或 @ 候选可见时不召回。召回只回填公式，成功提交按当前 ans 追加计算并返回原草稿，失败保留待修正。再次使用／修改公式也保留草稿。切换浏览项会重置输入框撤销栈；当前项的文字编辑仍可撤销。"), QStringLiteral("先计算 6*7，输入草稿 100+；Alt+↑ 召回并改为 6*7+1，Ctrl+Enter 得到 43 后回到 100+"), QStringLiteral("历史 召回 草稿 键盘 快捷键 Alt 上下 回填 编辑 撤销"), {}, 0},
        {Kind::Syntax, QStringLiteral("session"), QStringLiteral("本地会话保存与恢复"), QStringLiteral("输入操作"), QStringLiteral("可选保存记录、ans 与草稿"), QStringLiteral("默认关闭。通过“本地会话”选择新文件后自动保存；恢复需要空白页，直接读取既有结果、ans、两模式草稿、定义、参数、选区和历史浏览状态，不重算。每个文件只能由一个窗口或进程使用。关闭后文件保留，重开或重启后显式选择恢复。清空也会更新文件；停止保存会保留最后快照。保存失败不丢弃当前内容，关闭前请重试或另存；未知格式版本与损坏文件不覆盖。输入法未上屏候选和撤销栈不保存。仅读取当前version 3格式，旧版本拒绝且不修改原文件。保存十进制正文、binary64位模式及来源；单文件最多16 MiB、10000条记录。"), QStringLiteral("本地会话 → 开启保存；关闭标签 → 打开空白计算器 → 恢复会话"), QStringLiteral("保存 恢复 历史 文件 本地 会话 草稿 重启 自动 保存失败 重试"), {}, 0},
        {Kind::Syntax, QStringLiteral("clear"), QStringLiteral("清空会话"), QStringLiteral("输入操作"), QStringLiteral("重新开始当前窗口的计算"), QStringLiteral("点击右上角“清空会话”，确认后清空全部成功和失败记录，将 ans 重置为 0，编号从 01 开始。普通输入的草稿、光标、选区和文字撤销保留；历史浏览中返回召回前的草稿，丢弃本轮临时编辑。确认框默认取消，Esc 或关闭也取消；清空记录不可撤销。其他窗口不受影响；没有记录或输入法组词期间按钮不可用。"), QStringLiteral("计算 42 后保留草稿 ans+1，清空会话再计算得到 1"), QStringLiteral("清空 会话 重置 删除 草稿 确认 取消 ans"), {}, 0},
        {Kind::Precision, QStringLiteral("double"), QStringLiteral("十进制与近似计算"), QStringLiteral("精度与限制"), QStringLiteral("50位十进制与近似路径"), QStringLiteral("数字从原文进入有界十进制。最多50位规范化有效数字，科学指数范围[-999,999]；加减乘精确，超限报错。科学函数、pi/e或混入近似值时使用binary64。十进制完整显示，近似值显示最多17位可往返数字，不保证17位准确度。-6≤科学指数<21时普通小数，其余小写e记法；去无意义尾零，保留-0。"), QStringLiteral("0.1+0.2 → 0.3；(0.1+0.2)-0.3 → 0"), QStringLiteral("精度 十进制 double binary64 来源 舍入 复制 负零 范围 下溢"), {}, 0},
        {Kind::Precision, QStringLiteral("integer"), QStringLiteral("整数与数值范围"), QStringLiteral("精度与限制"), QStringLiteral("有界精度与资源限制"), QStringLiteral("规范化有效数字最多50位，1000的系数为1。十进制整数也适用，非任意精度大整数。逐节点检查中间结果，不能依靠后续相消救回超限值。十进制整数幂指数绝对值最多10000；负幂先正幂再除法，因而2^-200因正幂超50位失败。"), QStringLiteral("(1e16+1)-1e16 → 1；(1e50+1)-1e50 报精度超限"), QStringLiteral("精度 十进制 double binary64 来源 舍入 复制 负零 范围 下溢"), {}, 0},
        {Kind::Precision, QStringLiteral("underflow"), QStringLiteral("转换与下溢"), QStringLiteral("精度与限制"), QStringLiteral("非零值不能静默变为零"), QStringLiteral("最小正十进制值为1e-999。基础运算越界报错；科学函数的参数要转换为binary64，1e400或1e-400的转换分别上溢或下溢，不能绕过。近似运算可判定的非零结果下溢为零也报错；允许有限非零次正规数（非正规数），最小约4.9406564584124654e-324，但精度降低。"), QStringLiteral("1e-200*1e-200 → 1e-400；sin(1e-400)和exp(-1000)报下溢"), QStringLiteral("精度 十进制 double binary64 来源 舍入 复制 负零 范围 下溢"), {}, 0},
        {Kind::Precision, QStringLiteral("reuse"), QStringLiteral("ans、复制与插入结果数值"), QStringLiteral("精度与限制"), QStringLiteral("内部快照与新数字输入"), QStringLiteral("ans、历史快照和当前会话完整保留数值类型、负零与来源。复制纯数值及插入结果数值输出完整正文，再次确认计算时作为新的十进制输入，不继承原类型与来源；近似数字文本按binary64解析可还原原位模式。普通模式负数插入带括号，自定义参数只插入数字。整条复制和导出另附来源说明。"), QStringLiteral("sin(0)后ans+0.1走近似路径；插入0后0+0.1为新的十进制计算"), QStringLiteral("精度 十进制 double binary64 来源 舍入 复制 负零 范围 下溢"), {}, 0},
        {Kind::Precision, QStringLiteral("rounding"), QStringLiteral("除法舍入与计算来源"), QStringLiteral("精度与限制"), QStringLiteral("明确舍入时点和来源"), QStringLiteral("十进制除法最多50位有效数字，最近值、半值取偶，每个除法节点舍入一次。精确有限商保持不变。来源行按顺序显示含除法舍入、含近似计算、含转换损失；来源不表示误差大小，也不因结果为0或整数而清除。加减乘不因上游除法舍入而自动舍入。"), QStringLiteral("1/8 → 0.125；1/3保留50位并提示舍入；(1/6)*6报精度超限"), QStringLiteral("精度 十进制 double binary64 来源 舍入 复制 负零 范围 下溢"), {}, 0},
        {Kind::Precision, QStringLiteral("domain"), QStringLiteral("定义域、弧度与数学奇点"), QStringLiteral("常见疑问"), QStringLiteral("有限结果不一定可靠"), QStringLiteral("pi 为近似值，所以 sin(pi) 可能约为 1.22e-16。三角函数用弧度，tan(pi/2) 可能返回很大的有限数。sqrt 的参数须 >= 0；ln/log 的参数须 > 0，违反时报告定义域错误并标记参数；除数恰好为 0 才触发除零检测。"), QStringLiteral("pow(-8,1/3) 不会被当作实数立方根 -2"), QStringLiteral("精度 正弦 弧度 度数 奇点 定义域 除零 对数"), {}, 0}
    };
    return values;
}
}
