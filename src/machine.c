/**
 * machine.c
 * 该文件包含了与RISC-V机器相关的函数实现。
 */

#include "../inc/riscvemu.h"

/**
 * machine_load_program
 * 该函数负责加载用户输入的程序到RISC-V机器中。它接受一个指向machine_t结构体的指针和一个字符串参数prog，表示要加载的程序文件的路径。
 */
void machine_load_program(machine_t *m, char *prog)
{
    int fd = open(prog, O_RDONLY); // 只读方式打开程序文件
    if (fd < 0)
    {
        // stderror在string.h中声明，返回一个描述最近一次错误的字符串。errno是一个全局变量(在errno.h中声明)，包含了最近一次系统调用失败的错误代码。
        fatal(strerror(errno)); // 如果打开文件失败，输出错误信息并退出程序
    }

    // 调用函数mmu_load_elf来加载ELF格式的程序文件到机器的内存中
    mmu_load_elf(&m->mmu, fd);
    close(fd);

    m->state.pc = m->mmu.entry; // 将机器的程序计数器(pc)设置为ELF文件中指定的入口点(entry)
}