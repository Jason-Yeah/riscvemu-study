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

## stage3

```c
typedef void (func_t)(state_t *, insn_t *);
//      |    |       |
//      |    |       └── 3. 参数列表：接受两个指针参数
//      |    └── 2. 名字：定义了一个叫 func_t 的新类型
//      └── 1. 返回值：返回 void (无返回值)
```

func_t 代表了一种函数类型，这种函数没返回值，有两个参数

如果不适用：

```c
// 原始写法：很难读懂
void (*execute_fn)(state_t *, insn_t *);
```

如果使用：

```c
// 使用 func_t 写法
func_t *execute_fn; 
```

看着舒服点

### 解释执行

解释执行不是提前把整本书翻译好，而是看一句、翻译一句、执行一句。

解释执行通常遵循一个死循环，也就是著名的 "Fetch-Decode-Execute"（取指-译码-执行） 循环

由于当前Host不支持RISC-V，模拟器读到0x005302b3 (ADD 指令) 时，Intel/AMD CPU 是一脸懵逼的，于是模拟器代码发挥了作用：它像个翻译官，通过一堆 switch-case 告诉 Host CPU指令是干啥的。

### inline

性能优化建议。它告诉编译器：“这个函数可能很短小，如果可以的话，请直接把它的代码复制粘贴到调用它的地方，省去函数调用的开销。”

static inline结合：inline解决效率问题，static解决链接冲突问题

### 宏模板

```c
#define FUNC(typ)                                          \
    u64 addr = state->gp_regs[insn->rs1] + (i64)insn->imm; \
    state->gp_regs[insn->rd] = *(typ *)TO_HOST(addr);      \

static void func_lb(state_t *state, insn_t *insn) {
    FUNC(i8);
}
#undef FUNC
```

- #define FUNC(typ)：定义了一个名为 FUNC 的宏，它接受一个参数 typ（代表类型）。
- \：反斜杠是续行符，告诉编译器这行代码还没结束，下一行也是宏的一部分。

替换后实际上是：

```c
static void func_lb(state_t *state, insn_t *insn) {
    // typ 被替换成了 i8
    u64 addr = state->gp_regs[insn->rs1] + (i64)insn->imm;
    state->gp_regs[insn->rd] = *(i8 *)TO_HOST(addr); // 强制转换为 i8*，读取 1 字节
}
```

## stage4

Decode实现

## stage5

配置Stack Space

当前内存分布设计

```text
MMU
             |top      |bottom
[  program  ][  stack  ]  argv实际数据  [  heap  ]
                                       ^ alloc

sp -> stack top
stack size: 32M

stack space:
[           | argc argv envp auxv]
            ^ stack bottom (sp)
```

stack中相关参数：

- argc: arugment count，int8
- argv: argument vector 字符串指针数组以0结尾
- envp: environment pointer包含系统所有环境变量（PATH,USER,PWD等）
- auxv: Auxiliary Vector辅助向量，内核直接传递给用户空间（通常是给动态链接器`ld-linux.so`信息，每个元素是一个`(Type, Value)`结构）

设计栈空间，需要有申请内存的方法（设计）

### mmap

```c
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
```

- addr: 地址
- prot: 保护权限
  - PROT_READ：可读。
  - PROT_WRITE：可写。
  - PROT_EXEC：可执行（.text 段必须有这个，否则 CPU 无法取指）。
  - PROT_NONE：不可访问（常用于设置“后花园”防止越界）。
- flags: 映射特性
  - MAP_PRIVATE: 私有映射，对内存的修改不会写回源文件
  - MAP_FIXED: 强制地址，必须映射到addr指定的位置
  - MAP_ANONYMOUS: 匿名映射，不关联文件
- fd: 文件描述符，如果是MAP_ANONYMOUS就固定传-1
- offset: 文件偏移，从文件哪个地方开始映射，必须是pagesize的倍数，不映射到文件就写0

### munmap

```c
int munmap(void *addr, size_t length);
```

- addr是起始地址
- length是要释放的大小

### memcpy

Memory copy

```c
void *memcpy(void *dest, const void *src, size_t n);
```

- dest：目标内存地址
- src：源内存地址
- n：多少字节

### STACK SETUP

由于栈是向下增长，于是先让sp指向最大栈空间
