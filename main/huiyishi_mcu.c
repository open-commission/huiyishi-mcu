#include "task_manager.h"

void app_main()
{
#ifdef XIANCHANG_MOD
    // 现场模式
    create_xianchang_tasks();
#else
    // 会议室模式
    create_huiyishi_tasks();
#endif
}