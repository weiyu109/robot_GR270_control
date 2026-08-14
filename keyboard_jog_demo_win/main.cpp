// Windows 兼容入口。
// 正式实现统一维护在项目根目录 apps/keyboard_jog_demo.cpp，禁止在此复制第二套控制流程。
// 这样从 keyboard_jog_demo_win 构建时，也会使用与 Linux 相同的模式切换、
// 清错、上下电、三轴点动、状态恢复和退出保护逻辑。
#include "../apps/keyboard_jog_demo.cpp"
