#include "ndd_plugin_api.h"
#include "plugin_controller.h"

extern "C" {
CALCTABDD_EXPORT bool NDD_PROC_IDENTIFY(NddProcData *data)
{
    if (!data) return false;
    data->pluginName = QStringLiteral("CalcTabdd 计算器");
    data->comment = QStringLiteral("标签页公式计算与历史记录");
    data->version = QStringLiteral(CALCTABDD_VERSION);
    data->author = QStringLiteral("CalcTabdd contributors");
    data->menuType = 1;
    return true;
}

CALCTABDD_EXPORT int NDD_PROC_MAIN(QWidget *host, const QString &pluginFilePath,
                                  NddGetCurrentEditor getEditor,
                                  NddHostCallback callback, NddProcData *data)
{
    Q_UNUSED(pluginFilePath);
    if (!host || !data || !data->rootMenu || !getEditor || !callback) return -1;
    try
    {
        auto *controller = host->findChild<PluginController *>(QStringLiteral("calctabddController"), Qt::FindDirectChildrenOnly);
        if (!controller) controller = new PluginController(host, std::move(callback));
        return controller->installMenu(data->rootMenu) ? 0 : -2;
    }
    catch (...)
    {
        return -3;
    }
}
}
