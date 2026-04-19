/* The Location of main */
#include "../inc/riscvemu.h"

static bool trace_syscall_enabled(void) {
    static bool enabled = false;
    static bool inited = false;
    if (!inited) {
        enabled = getenv("RISCVEMU_TRACE_SYSCALL") != NULL;
        inited = true;
    }
    return enabled;
}

int main(int argc, char *argv[])    // argc是命令行参数的数量，argv是一个字符串数组，包含了所有的命令行参数。argv[0]通常是程序的名称，argv[1]开始才是用户输入的参数。
{
    assert(argc > 1); // 确保用户输入了至少一个参数，即要加载的程序文件的路径
    machine_t machine = {0};
    machine.cache = new_cache();

    // memset(&machine, 0, sizeof(machine)); // 初始化machine结构体，将其所有成员变量设置为0;
    machine_load_program(&machine, argv[1]); // 加载用户输入的程序到机器中，argv[1]是用户输入的第一个参数，通常是要加载的程序文件的路径。
    machine_setup(&machine, argc, argv);
    
    while(true)
    {
        enum exit_reason_t reason = machine_step(&machine);
        if (reason == halt) {
            break;
        }
        assert(reason == ecall); // 断言退出原因是系统调用指令(ecall)，如果不是，说明程序执行过程中发生了错误，程序将终止执行

        //  handle syscall here
        u64 syscall = machine_get_gp_reg(&machine, a7); // 从寄存器a7中获取系统调用号，a7是RISC-V架构中用于传递系统调用号的寄存器
        if (trace_syscall_enabled()) {
            fprintf(stderr, "ecall n=%lu a0=0x%lx a1=0x%lx a2=0x%lx a3=0x%lx\n",
                    syscall,
                    machine_get_gp_reg(&machine, a0),
                    machine_get_gp_reg(&machine, a1),
                    machine_get_gp_reg(&machine, a2),
                    machine_get_gp_reg(&machine, a3));
        }
        u64 ret = do_syscall(&machine, syscall);
        machine_set_gp_reg(&machine, a0, ret); // 将系统调用的返回值存储在寄存器a0中，a0是RISC-V架构中用于传递系统调用返回值的寄存器
    }

    return 0;
}
