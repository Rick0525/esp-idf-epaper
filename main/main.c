// 主程序入口：thin dispatcher，把控制权移交给 demos/ 下的具体 demo。
//
// 切屏 = 改 main/CMakeLists.txt 两行（SRCS + REQUIRES），main.c 不动。
// 每个 demo 文件全自包含，导出 void demo_run(void) 即可。

extern void demo_run(void);

void app_main(void)
{
    demo_run();
}
