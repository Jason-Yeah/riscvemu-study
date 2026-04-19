/**
 * machine.c
 * 该文件包含了与RISC-V机器相关的函数实现。
 */

#include "../inc/riscvemu.h"

enum exit_reason_t machine_step(machine_t *m)
{
    while(true)
    {
        if (m->state.pc == 0) {
            return halt;
        }

        // hot就是当前pc的代码具有局部性，被执行很多遍
        bool hot = true; // hot变量用于标识当前指令块是否是热点代码，初始值为true，表示默认情况下认为指令块是热点代码
        
        u8* code = cache_lookup(m->cache, m->state.pc); // 找内存中可执行的区域
        if (code == NULL) // 如果没找到（当前PC没有可执行代码）
        {
            hot = cache_hot(m->cache, m->state.pc); // 判断当前PC是否是热点代码
            if (hot)
            {
                // 该过程是瓶颈段耗时间
                str_t source = machine_genblock(m); // 生成当前PC所在指令块的源代码
                // 保证machine_compile返回的和下面的签名一样
                // 生成代码如格式：`void start(volatile state_t *restrict state) {...}`
                code = machine_compile(m, source); // 将生成的源代码编译成可执行代码，并返回可执行代码的地址
            }
        }
        
        if (!hot) // 如果当前PC不是热点代码
        {
            // 之前的方式（函数签名与上述一致）
            code = (u8 *)exec_block_interp; // 将code指向解释执行函数exec_block_interp，以便以解释的方式执行指令块
        }

        while (true)
        {
            m->state.exit_reason = none; // 每次执行指令块前，重置退出原因为none，表示正常执行
            ((exec_block_func_t)code)(&m->state); // 以函数指针的方式调用code指向的函数，传入机器状态的指针作为参数，以执行指令块
            // exec_block_interp(&m->state); // 以解释的方式执行指令块，直到遇到分支指令、系统调用指令或者需要解释执行的指令为止
            assert(m->state.exit_reason != none); // 断言退出原因不为none，说明exec_block_interp函数已经执行完一个指令块，并且遇到了一个需要处理的情况

            if (m->state.exit_reason == indirect_branch || m->state.exit_reason == direct_branch)
            {
                m->state.pc = m->state.reenter_pc; // 如果退出原因是分支指令，重新设置程序计数器(pc)为重新进入程序时的值(reenter_pc)，以便继续执行下一条指令块
                code = cache_lookup(m->cache, m->state.reenter_pc);
                if (code != NULL) continue; // 是热门的，接着快速执行
                // continue; // 如果退出原因是分支指令，继续执行下一条指令块
            }

            if(m->state.exit_reason == interp) // 不常用比较复杂的指令
            {
                m->state.pc = m->state.reenter_pc;
                code = (u8 *)exec_block_interp; // 以解释的方式执行指令块
                continue;
            }

            // 此时情况是ecall或其他情况需要跳出来
            break;
        }

        m->state.pc = m->state.reenter_pc;
        switch (m->state.exit_reason) // 到底是ecall还是是branch但cache没东西了
        {
            case direct_branch:
            case indirect_branch:
                // continue execution
                break;
            case ecall:
                return ecall; // 如果退出原因是系统调用指令(ecall)，返回ecall，表示需要处理系统调用
            case halt:
                return halt;
            default:
                unreachable(); // 其他情况不应该发生，程序将终止执行
        }
        // assert(m->state.exit_reason == ecall || m->state.exit_reason == halt);
        // return m->state.exit_reason;
    }
}

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

    m->state.pc = (u64)m->mmu.entry; // 将机器的程序计数器(pc)设置为ELF文件中指定的入口点(entry)
}

void machine_setup(machine_t *m, int argc, char *argv[])
{
    // 栈设置
    size_t stack_size = STACK_SIZE; // 32MB的栈空间
    u64 stack = mmu_alloc(&m->mmu, stack_size); // 在机器的内存中分配栈空间，并返回栈的起始地址
    m->state.gp_regs[sp] = stack + stack_size; // 将栈指针(sp)寄存器设置为栈的末尾地址，栈是向下生长的，所以栈顶地址是栈的起始地址加上栈的大小

    m->state.gp_regs[sp] -= (AUXV_SIZE + ENVP_SIZE + ARGV_END_SIZE);
    // argv是以0结尾的字符串数组，我们要的是实际的程序名（第二个参数）
    /**
     * 示例：./riscvemu playground/testrv arg1 arg2 ... argn
     * argc = n + 1，但argv[0] = "./riscvemu"是不需要关心的，
     * Guest看到的argc应该是argc - 1。
     * 对于ARGC_END_SIZE，本例argv[args] = argn, argv[argc] = NULL
     * 该空间在初始化时要减掉
     */
    u64 args = argc - 1;
    for (int i = args; i > 0; i -- ) // i刚开是就是最后一个参数arg_n
    {
        size_t len = strlen(argv[i]);
        u64 addr = mmu_alloc(&m->mmu, len + 1);  // 这里长度包含了最后一个'\0'
        mmu_write(addr, (u8 *)argv[i], len);
        // update sp
        m->state.gp_regs[sp] -= 8; // 表示压入argv[i]
        // 填充sp内容
        mmu_write(m->state.gp_regs[sp], (u8 *)&addr, sizeof(u64));
    }

    m->state.gp_regs[sp] -= ARGC_SIZE;
    mmu_write(m->state.gp_regs[sp], (u8 *)&args, sizeof(u64));
    
    // 设置 a0 为 argc，a1 为 argv
    m->state.gp_regs[a0] = args;
    m->state.gp_regs[a1] = m->state.gp_regs[sp] + ARGC_SIZE;

    u64 sp0 = m->state.gp_regs[sp];
    u64 aligned_sp = sp0 & ~(u64)0xf;
    if (aligned_sp != sp0) {
        size_t used = (stack + stack_size) - sp0;
        memmove((void *)TO_HOST(aligned_sp), (void *)TO_HOST(sp0), used);
        m->state.gp_regs[sp] = aligned_sp;
        m->state.gp_regs[a1] = aligned_sp + ARGC_SIZE;
    }
}
