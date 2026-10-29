#include "calculation_catalog.h"
#include "numeric_value.h"
#include <QStringList>

namespace CalculationCatalog {
bool Entry::isCompletion() const
{
    return kind == Kind::Function || kind == Kind::Constant || kind == Kind::BitOperator;
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

QString Entry::insertion() const
{
    if (kind == Kind::Constant) return name;
    if (kind == Kind::BitOperator) return name == QStringLiteral("~") ? QStringLiteral("~()") : name + QLatin1Char(' ');
    if (kind != Kind::Function) return {};
    return NumericValue::argumentCount(name) > 1 ? name + QStringLiteral("(, )")
                                                 : name + QStringLiteral("()");
}

const QVector<Entry> &entries(bool programmer)
{
    static const QVector<Entry> integers = {
        {Kind::Constant, QStringLiteral("ans"), QStringLiteral("ans"), QStringLiteral("整数上次结果"), QStringLiteral("上次结果"),
            QStringLiteral("保留程序员模式上次成功的整数值与回绕来源，初始为 0；与普通／自定义共享的数学 ans 独立。失败不更新，清空重置。更改位宽或符号不立即改变 ans，实际引用时才保持原数值转换；新范围无法表示时会报错并保留原值，不截断或重新解释位模式。不引用 ans 的表达式可按新设置计算，成功后替换整数 ans。"),
            QStringLiteral("8 位无符号先算 255+1，再算 ans+1 → 1，保留含 8 位回绕提示"), QStringLiteral("上次 上一次 结果 答案 answer")},
        {Kind::BitOperator, QStringLiteral("~"), QStringLiteral("~x"), QStringLiteral("位运算"), QStringLiteral("按位取反"),
            QStringLiteral("反转当前位宽内的所有位，不新增回绕提示。补全插入 ~()，光标位于括号内。"),
            QStringLiteral("8 位无符号：~0x0F → 240／0xF0"), QStringLiteral("取反 按位取反 complement")},
        {Kind::BitOperator, QStringLiteral("&"), QStringLiteral("x & y"), QStringLiteral("位运算"), QStringLiteral("按位与"),
            QStringLiteral("仅保留两个操作数中同为 1 的位。"), QStringLiteral("0x12 & 0x0F → 2"), QStringLiteral("按位与 掩码 and")},
        {Kind::BitOperator, QStringLiteral("|"), QStringLiteral("x | y"), QStringLiteral("位运算"), QStringLiteral("按位或"),
            QStringLiteral("保留任一操作数中为 1 的位。"), QStringLiteral("0x10 | 0x03 → 19"), QStringLiteral("按位或 标志 or")},
        {Kind::BitOperator, QStringLiteral("^"), QStringLiteral("x ^ y"), QStringLiteral("位运算"), QStringLiteral("按位异或"),
            QStringLiteral("对应位不同时得到 1。程序员模式 ^ 为异或；普通／自定义模式 ^ 为乘方。"),
            QStringLiteral("程序员 2^3 → 1；普通 2^3 → 8"), QStringLiteral("异或 按位异或 xor")},
        {Kind::BitOperator, QStringLiteral("<<"), QStringLiteral("x << n"), QStringLiteral("移位"), QStringLiteral("左移"),
            QStringLiteral("低位补零；数学值乘以 2 的 n 次方超出当前数值范围时保留低位并提示回绕。移位量必须为 0 至位宽减 1。"),
            QStringLiteral("8 位有符号：64<<1 → -128，含 8 位回绕；-1<<1 → -2"), QStringLiteral("左移 shift")},
        {Kind::BitOperator, QStringLiteral(">>"), QStringLiteral("x >> n"), QStringLiteral("移位"), QStringLiteral("右移"),
            QStringLiteral("无符号高位补零，有符号高位补符号位；不新增回绕。移位量必须为 0 至位宽减 1。"),
            QStringLiteral("8 位有符号：0x80>>1 → -64；-3>>1 → -2"), QStringLiteral("右移 算术右移 shift")},
        {Kind::BitOperator, QStringLiteral(">>>"), QStringLiteral("x >>> n"), QStringLiteral("移位"), QStringLiteral("逻辑右移"),
            QStringLiteral("始终高位补零，结果仍使用原位宽与符号解释；不新增回绕。移位量不取模，必须为 0 至位宽减 1。"),
            QStringLiteral("8 位有符号：0x80>>>1 → 64；0x80>>>0 → -128"), QStringLiteral("逻辑右移 补零 unsigned")},
        {Kind::Operator, QStringLiteral("arithmetic"), QStringLiteral("+ - * / % ()"), QStringLiteral("整数运算"), QStringLiteral("整数四则与余数"),
            QStringLiteral("支持一元正负号和括号。除法向零截断，非零余数与被除数同号；除零报错。有符号最小值除以 -1 会回绕，余数为 0 且不新增回绕。优先级由高到低为：括号，一元 + - ~，* / %，+ -，<< >> >>>，&，^，|；同级二元运算从左向右。"),
            QStringLiteral("8 位有符号：-7/3 → -2；-7%3 → -1；1+2<<3 → 24"), QStringLiteral("除法 余数 优先级 运算")},
        {Kind::Syntax, QStringLiteral("numbers"), QStringLiteral("十进制、0x、0b、0o"), QStringLiteral("整数输入"), QStringLiteral("位宽与数字写法"),
            QStringLiteral("8／16／32／64 位，默认 32 位无符号。有符号使用补码。裸数字为十进制，必须在当前数值范围内；0x／0b／0o 按原始位模式解释。前缀和十六进制数字不区分大小写，数字间可加下划线，不能连续使用或紧邻前缀。最小有符号数可直接取负或在幅值外加括号。无符号的 -1 是对合法数字 1 取负，得到全 1 位模式并提示回绕。拒绝小数、科学计数法、科学函数、千分位和赋值。"),
            QStringLiteral("8 位有符号：0xFF → -1，-128 与 -(128) 合法；128 报错。010 → 10；0b1111_0000 → -16"), QStringLiteral("进制 二进制 八进制 十六进制 符号 下划线")},
        {Kind::Syntax, QStringLiteral("controls"), QStringLiteral("提交、切换与历史"), QStringLiteral("输入操作"), QStringLiteral("确认后计算"),
            QStringLiteral("Ctrl+Enter 或点击“计算”提交；Enter 不提交也不换行。空白输入框显示示例、@ 补全与 Alt+↑↓ 历史提示，输入后隐藏；设置行保留位宽、类型及 ^ 异或。三种模式分别保留草稿、光标与选区，程序员草稿还保留位宽与符号。设置只影响下次计算，已有记录保留原配置。清空重置数学与整数两类 ans，保留三份原草稿及设置；历史浏览中先返回原草稿，再丢弃临时编辑。每个窗口独立。"),
            QStringLiteral("先用 16 位无符号计算 (0x12 & 0x0F)<<4 → 32，再改为 8 位，旧记录仍显示 0x0020"), QStringLiteral("切换 模式 历史 草稿 键盘 Enter Tab 清空")},
        {Kind::Syntax, QStringLiteral("history"), QStringLiteral("历史回填与三模式草稿"), QStringLiteral("输入操作"), QStringLiteral("保留原草稿和计算条件"),
            QStringLiteral("Alt+↑↓ 或“再次使用／修改公式”回填原模式、公式及设置。浏览中保留各条临时修改的文本、光标、选区与程序员设置，原记录不改写。重新提交使用当前对应领域的 ans，不恢复历史中的旧 ans；成功追加记录并返回原草稿，失败保留输入与设置以便修正。向后越过最新记录或手动切换模式会结束浏览，恢复对应原草稿；首次切到自定义定义栏同样结束浏览，Esc 返回原草稿。更改位宽或符号后，旧错误标记不再沿用，须重新计算。"),
            QStringLiteral("8 位无符号先算 10，再算 ans+1 得到 11；回填 ans+1 重新提交得到 12，原记录仍为 11。"), QStringLiteral("历史 回填 召回 草稿 设置 临时 编辑 ans 失败")},
        {Kind::Syntax, QStringLiteral("completion"), QStringLiteral("@ 中文位运算补全"), QStringLiteral("输入操作"), QStringLiteral("补全模板"),
            QStringLiteral("输入 @ 后用名称或中文关键词筛选 ans 和位运算；Enter／Tab 仅插入模板，Esc 关闭候选。无候选时 Tab／Shift+Tab 切换焦点。输入法组词或尚未完成 @ 查询时不能提交。模板不添加操作数，不自动计算。"),
            QStringLiteral("@上次结果 → ans；@取反 → ~()；0xFF @按位与 → 0xFF & ；0x80 @逻辑右移 → 0x80 >>> "), QStringLiteral("补全 中文 输入法 模板")},
        {Kind::Syntax, QStringLiteral("display"), QStringLiteral("DEC／HEX／BIN／OCT"), QStringLiteral("结果查看"), QStringLiteral("展开进制详情"),
            QStringLiteral("公式下方默认显示十进制结果及 HEX。展开进制详情查看 BIN／OCT，不重新计算或新增记录。HEX 补足位宽并使用大写数字，BIN 补足位宽且每四位分组，OCT 使用最短位模式。长结果自动换行，均可选中文字复制。"),
            QStringLiteral("8 位有符号 -1：HEX 0xFF，BIN 1111 1111，OCT 0o377"), QStringLiteral("显示 结果 详情 进制 复制")},
        {Kind::Syntax, QStringLiteral("reuse"), QStringLiteral("复制与插入结果"), QStringLiteral("输入操作"), QStringLiteral("复用保存的数值"),
            QStringLiteral("记录操作中的“复制纯数值”复制 DEC；“复制 HEX／BIN／OCT”复制原位模式，带进制前缀、无分组或视觉换行。纯文本不携带原位宽、符号或回绕来源，粘贴后按目标配置解释。“插入结果数值”固定使用十进制，普通／程序员输入中负数带括号，自定义参数使用有符号数字。插入程序员前，保存值本身须为整数且在当前数值范围内；小数和越界值拒绝并保留输入，不截断、不回绕。数学结果即使含舍入、近似或转换损失，只要实际保存值为范围内整数也可插入；长近似整数使用保存的完整整数值，可能与显示的舍入文本不同。插入后按新数字计算，不继承来源。"),
            QStringLiteral("8 位有符号 -1 复制 BIN 为 0b11111111；插入 16 位有符号为 (-1)，插入无符号被拒绝"), QStringLiteral("复制 插入 DEC HEX BIN OCT 小数 范围 来源")},
        {Kind::Syntax, QStringLiteral("export"), QStringLiteral("整条复制与导出"), QStringLiteral("输入操作"), QStringLiteral("保留完整计算上下文"),
            QStringLiteral("“复制整条计算”及 TXT／Markdown 导出保留程序员模式、原位宽、符号、公式、DEC／HEX／BIN／OCT 四种结果和非空回绕来源。详情折叠或当前设置改变不影响文本，不重新计算旧公式。失败记录只包含原配置、公式和错误。混合历史按提交顺序使用各条原配置，数学记录保留原有格式和来源。导出供阅读，不能用于恢复会话。"),
            QStringLiteral("8 位无符号 255+1 导出 DEC 0、HEX 0x00、BIN 0000 0000、OCT 0o0 及“含 8 位回绕”"), QStringLiteral("导出 TXT Markdown 整条 复制 配置 历史")},
        {Kind::Syntax, QStringLiteral("availability"), QStringLiteral("保存与恢复程序员会话"), QStringLiteral("输入操作"), QStringLiteral("可选保留混合记录、两类 ans 和三份草稿"),
            QStringLiteral("通过“本地会话”选择新文件后，三种模式的记录、两类 ans、三份草稿、光标、选区和程序员设置一起保存。整数完整保留位模式、位宽、符号与回绕来源，包括 64 位无符号最大值；当前设置可以与上次整数 ans 的设置不同，恢复后仍在实际引用 ans 时检查数值范围。默认不保存；关闭后从新的空白计算器手动恢复，不重新计算历史。只支持第 4 版会话；第 3 版及更早文件不能直接恢复，不提供迁移，拒绝时原文件不变。"),
            QStringLiteral("64 位无符号 0xFFFFFFFFFFFFFFFF 保存后恢复仍为 18446744073709551615"), QStringLiteral("保存 恢复 导出 插入 会话 草稿 文件")},
        {Kind::Syntax, QStringLiteral("sessionProtection"), QStringLiteral("会话文件保护与保存失败"), QStringLiteral("输入操作"), QStringLiteral("完整恢复与失败救回"),
            QStringLiteral("恢复要求新的空白普通计算器，没有历史、三模式草稿、自定义定义或改过的程序员设置，且未开启保存。同一文件仅允许一个窗口或进程使用；新建与另存不覆盖已有文件。文件须完整校验通过后才恢复，损坏或不支持的文件不部分恢复，也不改原文件。保存文件最多 16 MiB、10000 条记录。写入失败或发现外部修改时保留当前内容，请先“立即保存／重试”或“另存新文件”，成功前不要关闭标签；异常退出只能恢复最后成功保存的快照。"),
            QStringLiteral("保存失败时保持标签打开，另存到可写的新文件，再从空白计算器恢复"), QStringLiteral("会话 保存 恢复 损坏 占用 锁 文件 失败 重试 另存 限制")},
        {Kind::Precision, QStringLiteral("wrap"), QStringLiteral("输入范围与逐步回绕"), QStringLiteral("范围与限制"), QStringLiteral("含回绕的结果"),
            QStringLiteral("输入越界报错；运算每一步超出当前数值范围时保留低位，提示“含 8 位回绕”等。即使后续抵消、乘零或转换位宽，已有来源仍保留。按位运算和右移不新增回绕；整数除法舍弃小数部分不算近似或回绕。"),
            QStringLiteral("8 位无符号：(255+1)/2 → 0，含 8 位回绕；256 直接输入则报错"), QStringLiteral("回绕 溢出 精度 来源")},
        {Kind::Precision, QStringLiteral("limits"), QStringLiteral("整数与表达式限制"), QStringLiteral("范围与限制"), QStringLiteral("完整整数与资源边界"),
            QStringLiteral("整数全程精确保存。无符号范围为 0 至 2^N-1，有符号为 -2^(N-1) 至 2^(N-1)-1。表达式最多 4096 个 UTF-16 字符单位、128 层嵌套。错误时保留公式与整数 ans，标出错误位置。模式和设置可随宽度换行；窗口较矮时滚动底部输入区到“计算”，或按 Ctrl+Enter。"),
            QStringLiteral("64 位无符号最大值：18446744073709551615；64 位有符号最小值：-9223372036854775808"), QStringLiteral("范围 64 整数 精度 错误 布局 窗口 字体")}
    };
    if (programmer) return integers;
    static const QVector<Entry> values = {
        {Kind::Operator, QStringLiteral("+ -"), QStringLiteral("x + y；x - y"), QStringLiteral("基本运算"), QStringLiteral("加法与减法"), QStringLiteral("同级从左向右计算，优先级低于乘除和乘方。"), QStringLiteral("1+2*3 → 7"), QStringLiteral("加法 减法 四则 优先级 结合")},
        {Kind::Operator, QStringLiteral("* /"), QStringLiteral("x * y；x / y"), QStringLiteral("基本运算"), QStringLiteral("乘法与除法"), QStringLiteral("同级从左向右计算；除数不能为 0。×、÷ 也可使用。"), QStringLiteral("8/4/2 → 1"), QStringLiteral("乘法 除法 四则 除零")},
        {Kind::Operator, QStringLiteral("%"), QStringLiteral("x % y"), QStringLiteral("基本运算"), QStringLiteral("取余"), QStringLiteral("不是百分比；非零余数与被除数同号，除数不能为 0。十进制数在支持范围内精确取余；含近似值时，结果可能有浮点误差。"), QStringLiteral("-10%3 → -1；5.5%2 → 1.5"), QStringLiteral("取余 余数 模 百分比")},
        {Kind::Operator, QStringLiteral("^"), QStringLiteral("x ^ y"), QStringLiteral("基本运算"), QStringLiteral("乘方"), QStringLiteral("从右向左计算；优先级高于一元负号。负底数仅接受整数指数；底数为 0 时指数不能为负数，0^0 = 1。十进制整数幂的指数绝对值最多 10000；负整数幂先算正幂再取倒数，中间结果超出精度或范围时会报错。"), QStringLiteral("2^3^2 → 512；-2^2 → -4；2^-3 → 0.125"), QStringLiteral("乘方 幂 指数 优先级 结合")},
        {Kind::Operator, QStringLiteral("unary"), QStringLiteral("+x；-x；(x)"), QStringLiteral("基本运算"), QStringLiteral("正负号与括号"), QStringLiteral("括号改变运算顺序；支持连续正负号。乘法必须显式输入 *，不支持 2pi 或 2(3)。"), QStringLiteral("(-2)^2 → 4；--2 → 2"), QStringLiteral("括号 正号 负号 隐式乘法")},
        {Kind::Function, QStringLiteral("sqrt"), QStringLiteral("sqrt(x)"), QStringLiteral("基础函数"), QStringLiteral("平方根"), QStringLiteral("x 必须大于或等于 0。十进制数能精确开方且结果在支持范围内时，返回精确值；其他情况使用近似计算。输入已是近似值时，结果也保留近似计算标记。"), QStringLiteral("sqrt(9) → 3"), QStringLiteral("平方根 开方 root")},
        {Kind::Function, QStringLiteral("cbrt"), QStringLiteral("cbrt(x)"), QStringLiteral("基础函数"), QStringLiteral("立方根"), QStringLiteral("支持正数、负数和零。十进制数能精确开立方时保留精确值，其余情况使用近似计算；负数立方根请用此函数，不能用非整数幂替代。"), QStringLiteral("cbrt(-8) → -2；cbrt(1e999) → 1e+333"), QStringLiteral("立方根 开立方 cube root")},
        {Kind::Function, QStringLiteral("abs"), QStringLiteral("abs(x)"), QStringLiteral("基础函数"), QStringLiteral("绝对值"), QStringLiteral("返回 x 的绝对值。"), QStringLiteral("abs(-2) → 2"), QStringLiteral("绝对值 absolute")},
        {Kind::Function, QStringLiteral("hypot"), QStringLiteral("hypot(x, y)"), QStringLiteral("基础函数"), QStringLiteral("二维长度"), QStringLiteral("计算平方和的平方根，允许负参数。使用近似计算，并避免直接先平方造成的中间溢出；输入转换和最终结果仍受近似数值范围限制。"), QStringLiteral("hypot(3,4) → 5"), QStringLiteral("长度 距离 斜边 平方和 hypot")},
        {Kind::Function, QStringLiteral("pow"), QStringLiteral("pow(底数, 指数)"), QStringLiteral("基础函数"), QStringLiteral("乘方函数"), QStringLiteral("与 ^ 使用相同的乘方规则；恰好需要两个参数，用英文逗号分隔。"), QStringLiteral("pow(2,10) → 1024"), QStringLiteral("乘方 幂 次方 power")},
        {Kind::Function, QStringLiteral("sin"), QStringLiteral("sin(x)"), QStringLiteral("三角函数"), QStringLiteral("正弦"), QStringLiteral("参数使用弧度；角度可用 rad(度数) 转换。"), QStringLiteral("sin(pi/2) → 1；sin(rad(90)) → 1"), QStringLiteral("正弦 三角 弧度 角度 sine")},
        {Kind::Function, QStringLiteral("cos"), QStringLiteral("cos(x)"), QStringLiteral("三角函数"), QStringLiteral("余弦"), QStringLiteral("参数使用弧度；没有全局角度模式，角度输入可先用 rad 转换。"), QStringLiteral("cos(0) → 1"), QStringLiteral("余弦 三角 弧度 角度 cosine")},
        {Kind::Function, QStringLiteral("tan"), QStringLiteral("tan(x)"), QStringLiteral("三角函数"), QStringLiteral("正切"), QStringLiteral("参数使用弧度；不会单独识别 pi/2 等数学奇点，附近结果可能是很大的有限值。"), QStringLiteral("tan(0) → 0"), QStringLiteral("正切 三角 弧度 角度 tangent")},
        {Kind::Function, QStringLiteral("asin"), QStringLiteral("asin(x)"), QStringLiteral("反三角函数"), QStringLiteral("反正弦"), QStringLiteral("x 须在 -1 至 1 之间，结果使用弧度，范围为 -pi/2 至 pi/2；使用近似计算。"), QStringLiteral("asin(0) → 0；deg(asin(1)) → 90"), QStringLiteral("反正弦 arcsine")},
        {Kind::Function, QStringLiteral("acos"), QStringLiteral("acos(x)"), QStringLiteral("反三角函数"), QStringLiteral("反余弦"), QStringLiteral("x 须在 -1 至 1 之间，结果使用弧度，范围为 0 至 pi；使用近似计算。"), QStringLiteral("acos(1) → 0"), QStringLiteral("反余弦 arccosine")},
        {Kind::Function, QStringLiteral("atan"), QStringLiteral("atan(x)"), QStringLiteral("反三角函数"), QStringLiteral("反正切"), QStringLiteral("结果使用弧度，数学值在 -pi/2 与 pi/2 之间；使用近似计算。"), QStringLiteral("atan(0) → 0"), QStringLiteral("反正切 arctangent")},
        {Kind::Function, QStringLiteral("atan2"), QStringLiteral("atan2(y, x)"), QStringLiteral("反三角函数"), QStringLiteral("方向角"), QStringLiteral("按 y、x 的符号确定象限，返回从正 x 轴起算的弧度角，范围为 -pi 至 pi。参数顺序是 y 在前、x 在后；二者同时为零时提示定义域错误。使用近似计算。"), QStringLiteral("deg(atan2(1,-1)) → 135"), QStringLiteral("方向角 象限 反正切 arctangent")},
        {Kind::Function, QStringLiteral("rad"), QStringLiteral("rad(度数)"), QStringLiteral("角度转换"), QStringLiteral("角度转弧度"), QStringLiteral("将角度换算成弧度，使用圆周率的近似值。"), QStringLiteral("sin(rad(90)) → 1"), QStringLiteral("角度转弧度 度数 弧度 radians")},
        {Kind::Function, QStringLiteral("deg"), QStringLiteral("deg(弧度)"), QStringLiteral("角度转换"), QStringLiteral("弧度转角度"), QStringLiteral("将弧度换算成角度，使用圆周率的近似值。"), QStringLiteral("deg(pi) → 180"), QStringLiteral("弧度转角度 度数 弧度 degrees")},
        {Kind::Function, QStringLiteral("ln"), QStringLiteral("ln(x)"), QStringLiteral("对数与指数"), QStringLiteral("自然对数"), QStringLiteral("以 e 为底，x 必须大于 0；0 和负数会提示定义域错误。"), QStringLiteral("ln(e) → 1"), QStringLiteral("对数 自然对数 logarithm")},
        {Kind::Function, QStringLiteral("log"), QStringLiteral("log(x[, base])"), QStringLiteral("对数与指数"), QStringLiteral("常用或任意底对数"), QStringLiteral("一个参数时以 10 为底；第二个参数指定底数。x 必须大于 0，底数必须大于 0 且不等于 1。使用近似计算。方括号表示参数可省略，输入时不写方括号。"), QStringLiteral("log(1000) → 3；log(8,2) → 3"), QStringLiteral("对数 常用对数 任意底 log10 logarithm")},
        {Kind::Function, QStringLiteral("log2"), QStringLiteral("log2(x)"), QStringLiteral("对数与指数"), QStringLiteral("二进制对数"), QStringLiteral("以 2 为底，x 必须大于 0；使用近似计算。"), QStringLiteral("log2(1024) → 10"), QStringLiteral("二进制对数 对数 底数 logarithm")},
        {Kind::Function, QStringLiteral("exp"), QStringLiteral("exp(x)"), QStringLiteral("对数与指数"), QStringLiteral("自然指数"), QStringLiteral("计算 e 的 x 次幂，使用近似计算。结果过大，或非零结果小到只能表示成 0 时，会提示超出数值范围。"), QStringLiteral("exp(0) → 1"), QStringLiteral("指数 自然指数 exponential")},
        {Kind::Function, QStringLiteral("floor"), QStringLiteral("floor(x)"), QStringLiteral("舍入与取整"), QStringLiteral("向下取整"), QStringLiteral("返回不大于 x 的最大整数，负数也向负无穷方向取整。"), QStringLiteral("floor(-1.2) → -2"), QStringLiteral("取整 向下 舍入")},
        {Kind::Function, QStringLiteral("ceil"), QStringLiteral("ceil(x)"), QStringLiteral("舍入与取整"), QStringLiteral("向上取整"), QStringLiteral("返回不小于 x 的最小整数。"), QStringLiteral("ceil(1.2) → 2"), QStringLiteral("取整 向上 舍入")},
        {Kind::Function, QStringLiteral("trunc"), QStringLiteral("trunc(x)"), QStringLiteral("舍入与取整"), QStringLiteral("向零取整"), QStringLiteral("舍弃小数部分，正数和负数均向 0 方向取整；十进制输入保持十进制计算。"), QStringLiteral("trunc(-1.9) → -1"), QStringLiteral("取整 截断 向零 truncate")},
        {Kind::Function, QStringLiteral("round"), QStringLiteral("round(x[, n])"), QStringLiteral("舍入与取整"), QStringLiteral("指定小数位舍入"), QStringLiteral("省略 n 时取最近整数；n 为 -999 至 999 的整数，正数表示小数位数，负数表示十位、百位等。中点向远离 0 的方向舍入，与除法的半值取偶规则不同。十进制输入按十进制舍入；结果不补尾零，保留上游来源。方括号表示参数可省略，输入时不写方括号。"), QStringLiteral("round(12.345,2) → 12.35；round(-125,-1) → -130"), QStringLiteral("取整 四舍五入 舍入 小数位 金额")},
        {Kind::Function, QStringLiteral("min"), QStringLiteral("min(x, y, ...)"), QStringLiteral("比较与统计"), QStringLiteral("最小值"), QStringLiteral("接受 2 至 256 个参数，用英文逗号分隔；返回最小值，相等时保留最先出现的值，并保留全部参数的计算来源。"), QStringLiteral("min(2,3,-1) → -1"), QStringLiteral("最小 较小 比较 minimum")},
        {Kind::Function, QStringLiteral("max"), QStringLiteral("max(x, y, ...)"), QStringLiteral("比较与统计"), QStringLiteral("最大值"), QStringLiteral("接受 2 至 256 个参数，用英文逗号分隔；返回最大值，相等时保留最先出现的值，并保留全部参数的计算来源。"), QStringLiteral("max(-2,-3,4) → 4"), QStringLiteral("最大 较大 比较 maximum")},
        {Kind::Function, QStringLiteral("sum"), QStringLiteral("sum(x, ...)"), QStringLiteral("比较与统计"), QStringLiteral("求和"), QStringLiteral("接受 1 至 256 个参数，从左向右逐项相加。每个中间结果都须满足精度和范围限制，后面的抵消不能挽回先前的超限。"), QStringLiteral("sum(0.1,0.2,0.3) → 0.6"), QStringLiteral("求和 合计 总和 sum")},
        {Kind::Function, QStringLiteral("avg"), QStringLiteral("avg(x, ...)"), QStringLiteral("比较与统计"), QStringLiteral("平均值"), QStringLiteral("接受 1 至 256 个参数，先从左向右求和，再除以参数数量。中间和须满足精度和范围限制，十进制除法可能产生舍入来源。"), QStringLiteral("avg(1,2,3) → 2"), QStringLiteral("平均值 平均 均值 average mean")},
        {Kind::Constant, QStringLiteral("pi"), QStringLiteral("pi"), QStringLiteral("常量与上次结果"), QStringLiteral("圆周率"), QStringLiteral("圆周率的近似值，显示为 3.1415926535897931。参与计算时会保留近似计算标记。"), QStringLiteral("sin(pi/2) → 1"), QStringLiteral("圆周率 派")},
        {Kind::Constant, QStringLiteral("e"), QStringLiteral("e"), QStringLiteral("常量与上次结果"), QStringLiteral("自然常数"), QStringLiteral("自然常数的近似值，显示为 2.7182818284590451。与科学计数法中的 e 含义不同。"), QStringLiteral("ln(e) → 1"), QStringLiteral("自然常数 欧拉")},
        {Kind::Constant, QStringLiteral("ans"), QStringLiteral("ans"), QStringLiteral("常量与上次结果"), QStringLiteral("上一次成功结果"), QStringLiteral("普通／自定义共享数学 ans，与程序员的整数 ans 独立。初始为 0，保留上一次成功结果及其计算来源；失败计算不更新。清空会话或打开新的空白会话后重置为 0；恢复本地会话时保留已保存值。"), QStringLiteral("先计算 6*7，再计算 ans+1 → 43"), QStringLiteral("上次 上一次 结果 答案 answer")},
        {Kind::Syntax, QStringLiteral("numbers"), QStringLiteral("小数与科学计数法"), QStringLiteral("输入语法"), QStringLiteral("数字写法"), QStringLiteral("小数点固定为 .，支持 .5、1. 和 e/E 指数，不接受千分位或全角数字。"), QStringLiteral("1e3+2.5E-2 → 1000.025"), QStringLiteral("数字 小数 科学计数法 区域设置")},
        {Kind::Syntax, QStringLiteral("input"), QStringLiteral("大小写、嵌套与输入限制"), QStringLiteral("输入语法"), QStringLiteral("公式规则"), QStringLiteral("函数和常量不区分大小写，可嵌套，运算符两侧可留空白。普通／程序员模式 Enter 不提交也不换行，Ctrl+Enter 或“计算”提交；自定义参数区 Enter 换行，定义栏 Enter 应用。公式长度上限为 4096，普通英文字母、数字和运算符各计 1；嵌套过深时请拆分计算。普通模式不支持变量赋值；自定义公式通过底部模式切换定义参数。不支持复数或符号求解。"), QStringLiteral("MAX(sqrt(9),pow(2,3)) → 8"), QStringLiteral("大小写 嵌套 长度 语法 错误 变量")},
        {Kind::Syntax, QStringLiteral("custom"), QStringLiteral("自定义公式与参数"), QStringLiteral("输入操作"), QStringLiteral("定义一次，填写不同参数后确认计算"), QStringLiteral("从底部切换至自定义公式，在参数区上方的公式定义栏直接输入 A=x+y，按 Enter（或点击“应用公式”）应用并生成 x=、y=；后续修改也在该栏按 Enter 确认。公式定义支持 @ 补全，候选打开时 Enter 先选中候选，再按 Enter 应用定义。Esc 还原已确认定义；首次定义时取消则返回此前模式。每行填写一个参数。名称只用英文字母、数字、下划线，不能以数字开头，区分大小写。pi、e、ans 和原有 14 个函数名保留；新增函数名如 sum、log2 在不带括号时仍可用作参数或公式名，以兼容已有公式。调用函数须加括号。参数只填有符号数字、小数或科学计数法；公式支持全部函数、pi、e 和 ans。按 Ctrl+Enter 或点击“计算”后追加记录，成功后保留参数；编辑和普通 Enter 换行不会触发计算。定义修改后显示“未应用”，Enter 应用或 Esc 还原后隐藏；未确认或无效的公式修改会阻止计算；缺少或非法参数不生成记录，数学错误保留错误记录。普通／自定义两种模式共享数学 ans，程序员另用整数 ans；各模式分别保留草稿；修改定义时保留同名参数值，新增参数留空。复用历史会恢复当时的定义和参数；成功提交、越过最新记录或手动切换模式后结束浏览并返回草稿。未确认的定义只在当前窗口保留，本地保存仍使用已确认定义；数值参数区不启用 @ 补全。"), QStringLiteral("A=x+y，分两行填 x=1 和 y=2；Ctrl+Enter 后显示 A=1+2，下一行 = 3"), QStringLiteral("自定义 参数 公式 模式 定义 保存 历史 变量")},
        {Kind::Syntax, QStringLiteral("diagnostics"), QStringLiteral("错误定位与修正"), QStringLiteral("输入操作"), QStringLiteral("标记问题字符或参数"), QStringLiteral("普通／程序员模式计算失败时保留公式和对应 ans，用底色与波浪线标记问题位置，光标移到问题处，并显示行列。末尾缺少内容时标记该行，光标停在需要补充的位置。标记不等于选中文字，继续输入不会替换整段。编辑、输入法组词或清空会话后移除标记，重新提交可再次定位；回填未修改的失败公式会恢复标记；程序员位宽或符号已修改时不沿用旧标记。自定义模式通过状态提示说明参数或计算错误，不在参数输入框中标记代入公式的位置。"), QStringLiteral("普通模式输入 ln(0)，标记参数 0；自定义 A=1/x、x=0，提示除数不能为 0，保留参数以便修正。"), QStringLiteral("错误 定位 高亮 范围 行列 参数 修正 下划线")},
        {Kind::Syntax, QStringLiteral("completion"), QStringLiteral("@ 快捷补全"), QStringLiteral("输入语法"), QStringLiteral("查找函数与常量"), QStringLiteral("普通公式输入和自定义公式定义栏支持 @，可用英文名称或中文关键词筛选；↑↓ 选择，Enter/Tab 插入，Esc 关闭。在定义栏补全后按 Enter 应用公式，再填写参数；普通公式或参数填写完成后，按 Ctrl+Enter 或点击“计算”。中文输入法组词时优先处理输入法候选。"), QStringLiteral("2+@平方根 → 2+sqrt()，光标位于括号内"), QStringLiteral("补全 快捷 输入法 帮助")},
        {Kind::Syntax, QStringLiteral("layout"), QStringLiteral("窗口大小与键盘操作"), QStringLiteral("输入操作"), QStringLiteral("窄窗口和大字体下继续计算"), QStringLiteral("模式按钮、记录操作随可用宽度换行。空白输入框显示示例及当前可用的快捷键提示，填写内容后隐藏；自定义参数区只提示历史召回，不支持 @ 补全。计算、设置等普通反馈约 4 秒后清除；错误和未应用状态不定时消失，修改相关输入或解决问题后更新。窗口高度不足时，可滚动底部输入区查看公式定义、填写参数和点击“计算”。ans 过长时省略显示，悬停查看完整内容。模式和“应用公式”按钮支持 Tab／Shift+Tab 聚焦、空格操作；补全候选打开时 Tab 仍优先插入候选。"), QStringLiteral("输入完成后按 Tab 聚焦“计算”，空格提交；Ctrl+Enter 也可提交。"), QStringLiteral("窗口 布局 缩放 字体 键盘 Tab 滚动 摘要")},
        {Kind::Syntax, QStringLiteral("host"), QStringLiteral("宿主菜单与工具栏"), QStringLiteral("输入操作"), QStringLiteral("计算器中的编辑与保存入口"), QStringLiteral("计算器标签中，宿主编辑菜单与主工具栏的复制、剪切、粘贴、撤销和重做作用于计算器。选中历史文本后粘贴会返回输入区；正在修改尚未应用的公式定义时，粘贴到定义栏，不改参数或另一模式草稿。文档保存、全部保存、自动保存、查找替换等入口不可用。计算器标签右键提供关闭和保存入口提示；保存会话或导出记录请使用页面内按钮。切回普通文档后，宿主命令与工具栏恢复。"), QStringLiteral("计算器标签右键 → 关闭计算器；留存记录 → 本地会话或导出记录"), QStringLiteral("宿主 菜单 工具栏 右键 保存 粘贴 剪切 撤销")},
        {Kind::Syntax, QStringLiteral("history"), QStringLiteral("Alt+↑ / Alt+↓ 历史召回"), QStringLiteral("输入操作"), QStringLiteral("保留草稿并浏览历史公式"), QStringLiteral("在输入框按 Alt+↑ 从最新记录向前浏览，Alt+↓ 向后浏览，越过最新记录返回草稿；最旧处不循环。首次召回保存草稿、光标和选区，本轮浏览中的临时编辑及程序员设置会保留；返回草稿、成功提交或手动切换模式后结束浏览。普通方向键移动光标；输入法组词或 @ 候选可见时不召回。召回恢复原模式和计算条件，重新提交按当前对应领域的 ans 追加记录，不恢复旧 ans；成功返回原草稿，失败保留待修正。“再次使用／修改公式”也保留草稿。切换历史记录后不能通过撤销恢复上一条输入；当前记录中的文字编辑仍可撤销。"), QStringLiteral("先计算 6*7，输入草稿 100+；Alt+↑ 召回并改为 6*7+1，Ctrl+Enter 得到 43 后回到 100+"), QStringLiteral("历史 召回 草稿 键盘 快捷键 Alt 上下 回填 编辑 撤销")},
        {Kind::Syntax, QStringLiteral("session"), QStringLiteral("本地会话保存与恢复"), QStringLiteral("输入操作"), QStringLiteral("可选保存混合记录、两类 ans 与三份草稿"), QStringLiteral("默认不保存。通过“本地会话”选择新文件后，自动保存三种模式的混合记录、数学与整数 ans、三份草稿、光标、选区、程序员设置、已确认的公式定义与参数，以及历史浏览位置、原草稿和临时编辑。数学数值及来源、整数位模式与回绕来源完整保留，恢复不重新计算历史。尚未按 Enter 应用的定义修改不保存。关闭后文件仍在；重开或重启后，从空白普通模式且无草稿、定义、修改过的设置或保存占用的计算器选择“恢复会话…”。每个文件同时只能由一个窗口或进程使用。清空会话也会更新保存文件；停止保存前会保存最后更改。保存失败时请在关闭前重试或另存，外部修改的文件不会覆盖。只支持当前第 4 版会话格式；第 3 版及更早、未知或损坏文件不会载入或修改，不提供迁移。输入法未上屏文字和文字撤销记录不保存；单文件最多 16 MiB、10000 条记录。"), QStringLiteral("本地会话 → 开启保存；关闭标签 → 打开空白计算器 → 恢复会话"), QStringLiteral("保存 恢复 历史 文件 本地 会话 草稿 重启 自动 保存失败 重试")},
        {Kind::Syntax, QStringLiteral("clear"), QStringLiteral("清空会话"), QStringLiteral("输入操作"), QStringLiteral("重新开始当前窗口的计算"), QStringLiteral("点击顶部“清空会话”，确认后清空全部成功和失败记录，将数学与整数 ans 重置为 0，编号从 01 开始。保留三种模式的原草稿、光标、选区和程序员设置，当前输入的文字撤销记录也保留；历史浏览中则返回召回前的草稿，丢弃本轮临时编辑。自定义公式及参数也会保留。确认框默认取消，Esc 或关闭也取消；清空记录不可撤销。其他窗口不受影响；没有记录或输入法组词期间按钮不可用。"), QStringLiteral("计算 42 后保留草稿 ans+1，清空会话再计算得到 1"), QStringLiteral("清空 会话 重置 删除 草稿 确认 取消 ans")},
        {Kind::Precision, QStringLiteral("double"), QStringLiteral("十进制与近似计算"), QStringLiteral("精度与限制"), QStringLiteral("最多 50 位十进制有效数字"), QStringLiteral("直接输入的数字按十进制计算，最多支持 50 位有效数字。十进制数的加、减、乘和取余在支持范围内精确计算，超限报错。三角及反三角函数、角度转换、hypot、对数、exp、pi、e、非整数次幂，以及不能精确开方的平方根和立方根使用近似计算；含近似值的后续运算也使用近似计算。十进制结果完整显示，近似结果最多显示 17 位有效数字，但不保证每一位都准确。显示时，科学计数法指数在 -6 至 20 之间用普通小数，其余用小写 e；省略无意义的尾零，保留 -0。"), QStringLiteral("0.1+0.2 → 0.3；(0.1+0.2)-0.3 → 0"), QStringLiteral("精度 十进制 double binary64 来源 舍入 复制 负零 范围 下溢")},
        {Kind::Precision, QStringLiteral("integer"), QStringLiteral("整数与数值范围"), QStringLiteral("精度与限制"), QStringLiteral("数值与中间结果的限制"), QStringLiteral("最多支持 50 位有效数字，末尾用于表示数量级的零不占有效数字容量，例如 1000 只占 1 位。非零十进制数的科学计数法指数范围为 -999 至 999。每一步计算都要满足精度和范围限制，即使后续运算能抵消超限部分，也会先报错。十进制整数幂的指数绝对值最多 10000；负幂先算正幂再取倒数，因此 2^-200 会因中间正幂超过 50 位而失败。"), QStringLiteral("(1e16+1)-1e16 → 1；(1e50+1)-1e50 报精度超限"), QStringLiteral("精度 十进制 double binary64 来源 舍入 复制 负零 范围 下溢")},
        {Kind::Precision, QStringLiteral("underflow"), QStringLiteral("转换与下溢"), QStringLiteral("精度与限制"), QStringLiteral("过大或过小的数值"), QStringLiteral("最小正十进制数为 1e-999，超出十进制范围时会报错。sin、ln 等近似计算的数值范围更小，参数超出范围时提示转换上溢或下溢。能精确开平方或开立方的十进制数仍按十进制范围处理。近似计算若检测到非零结果小到只能表示成 0，也会报错；仍能表示的极小非零值可以保留，但有效精度可能降低，最小约为 4.9406564584124654e-324。"), QStringLiteral("1e-200*1e-200 → 1e-400；sqrt(1e400) → 1e+200；ln(1e400) 报上溢；sin(1e-400) 和 exp(-1000) 报下溢。"), QStringLiteral("精度 十进制 double binary64 来源 舍入 复制 负零 范围 下溢 非正规数 次正规数")},
        {Kind::Precision, QStringLiteral("reuse"), QStringLiteral("ans、复制与插入结果数值"), QStringLiteral("精度与限制"), QStringLiteral("保留计算来源与重新输入的区别"), QStringLiteral("复制纯数值、选中结果数字复制或向数学输入插入结果，数字正文与显示一致。再次提交时按新输入的十进制数计算，不继承原结果的近似或舍入来源。插入程序员时，保存值本身须为整数且在目标范围内，使用其完整整数正文；长近似整数可能与显示的舍入文本不同，负零插入为 0。使用 ans 或恢复数学会话可以保留完整数值及计算来源。普通／程序员模式插入负数带括号，自定义参数插入有符号数字；复制整条计算和导出记录会附上来源说明。"), QStringLiteral("先计算 sin(0)，再计算 ans+0.1 会保留近似计算标记；复制或插入数字 0 后计算 0+0.1，得到精确十进制 0.1。"), QStringLiteral("精度 十进制 double binary64 来源 舍入 复制 负零 范围 下溢")},
        {Kind::Precision, QStringLiteral("rounding"), QStringLiteral("除法舍入与计算来源"), QStringLiteral("精度与限制"), QStringLiteral("每次除法分别舍入"), QStringLiteral("每次十进制除法最多保留 50 位有效数字，先舍入再继续后续计算。能在 50 位内表示的有限小数商保持精确；有限小数商超过 50 位，或商为无限循环小数时，也需要舍入。舍入取最接近的值，恰好居中时让保留的最后一位为偶数。结果下方会显示“含除法舍入”“含近似计算”“含转换损失”；这些标记说明计算来源，不表示误差大小，也不会因结果为 0 或整数而消失。后续十进制加、减、乘仍需精确完成，超限时会报错。"), QStringLiteral("1/8 → 0.125；1/3 和 1/(2^100) 保留 50 位并提示舍入；(1/6)*6 的乘法结果超过 50 位，会报精度超限。"), QStringLiteral("精度 十进制 double binary64 来源 舍入 复制 负零 范围 下溢")},
        {Kind::Precision, QStringLiteral("domain"), QStringLiteral("定义域、弧度与数学奇点"), QStringLiteral("常见疑问"), QStringLiteral("有限结果不一定可靠"), QStringLiteral("三角函数使用弧度，反三角函数返回弧度，可用 rad/deg 转换。pi 为近似值，所以 sin(pi) 可能约为 1.22e-16；tan(pi/2) 可能返回很大的有限数。sqrt 的参数须大于或等于 0，ln/log/log2 的参数须大于 0，asin/acos 的参数须在 -1 至 1 之间；atan2(0,0) 没有方向，会提示定义域错误。除数恰好为 0 时提示除零错误。错误定位方式见“错误定位与修正”。"), QStringLiteral("cbrt(-8) → -2；pow(-8,1/3) 不会被当作实数立方根"), QStringLiteral("精度 正弦 弧度 度数 奇点 定义域 除零 对数")}
    };
    return values;
}
}
