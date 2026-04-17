/* The Location of main */
#include "../inc/riscvemu.h"

int main(int argc, char *argv[])    // argc是命令行参数的数量，argv是一个字符串数组，包含了所有的命令行参数。argv[0]通常是程序的名称，argv[1]开始才是用户输入的参数。
{
    // assert宏在C语言中用于在调试阶段检查程序中的条件是否为真。如果条件为假，assert会输出错误信息并终止程序的执行。
    assert(argc > 1); // 确保用户输入了至少一个参数，即要加载的程序文件的路径
    machine_t machine;
    memset(&machine, 0, sizeof(machine)); // 初始化machine结构体，将其所有成员变量设置为0;
    
    machine_load_program(&machine, argv[1]); // 加载用户输入的程序到机器中，argv[1]是用户输入的第一个参数，通常是要加载的程序文件的路径。
    
    printf("entry: %llx\n", TO_HOST(machine.mmu.entry));
    printf("HOST ALLOC: %lx\n", machine.mmu.host_alloc);
    printf("GUEST ALLOC: %lx\n", machine.mmu.base);

    return 0;
}