# Makefile

Makefile 是 make 工具的配置文件。它直接告诉编译器具体的步骤：先编译哪个 .c，再链接哪个 .o。

- 语法：非常底层，依赖于 Shell 命令。
- 局限性：它是不可跨平台的。你在 Linux 下写的 Makefile，拿到 Windows 上用编译环境跑通常会报错，因为它依赖于具体的编译器路径和系统指令。
- 核心逻辑：基于文件的时间戳。如果 hello.c 没改动，make 就不会重新编译它，这大大节省了你的开发时间。

对于CMake（生成的配置文件叫`CMakeLists.txt`）并不是直接用来编译代码的，它是用来生成 Makefile（或其他构建文件）的工具。

```bash
jason@Jason:~/prj_c/riscv/riscvemu$ make
clang -O3 -Wall -Werror -Wimplicit-fallthrough -c -o obj/riscvemu.o src/riscvemu.c
clang -O3 -Wall -Werror -Wimplicit-fallthrough -lm -o riscvemu obj/riscvemu.o 
jason@Jason:~/prj_c/riscv/riscvemu$ objdump -d obj/riscvemu.o 

obj/riscvemu.o:     file format elf64-x86-64


Disassembly of section .text:

0000000000000000 <main>:
   0:   50                      push   %rax
   1:   48 8d 3d 00 00 00 00    lea    0x0(%rip),%rdi        # 8 <main+0x8>
   8:   e8 00 00 00 00          call   d <main+0xd>
   d:   31 c0                   xor    %eax,%eax
   f:   59                      pop    %rcx
  10:   c3                      ret
$ nm -u obj/riscvemu.o
                 U puts
```

可以看出反汇编后，经过-O3优化，最后实际上把printf替换成了puts函数

# `file`命令

`file`通过检查文件的内容（Header/Magic Number）来告诉你文件的真实类型。

# 流程

## stage1

配置头文件搭建项目框架，先实现把elf(目前为.o文件)文件加载入RISC-V机器中，加载后第一步读取ELF头，把标识字段取出进行校验（对魔数、当前指令架构、机器字长进行校验），初始化PC

## stage2

顺势读取Program Header内容，加载到内存中内容主要是`LOAD Segment`

### 对齐设置

假设page_size为4096，p_vaddr为0x401234，那对齐后aligned_vaddr为0x40100，说明这个段真正要放的地方不是页首，在这一页中偏移了。而mmap不能从非对齐地址映射所以代码把长度补上这部分：

```c
filesz = p_filesz + 0x234;
memsz  = p_memsz  + 0x234;
```

对于BSS部分映射，如果没跨页，最后一部分置0即可，如果跨页了，需要mmap出来并置0