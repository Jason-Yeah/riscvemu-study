# riscvemu

一个用 C 编写的 RISC-V 64 用户态模拟器（解释执行为主），用于在 x86_64 Linux 上加载并运行 RISC-V ELF 程序。

## 目录结构

- `src/`：核心实现（解码、解释执行、MMU、系统调用、机器状态）
- `inc/`：公共头文件与类型定义
- `playground/`：示例/测试程序（例如 `testrv`）
- `doc/`：调试与实现记录（例如 `debug260419.md`）
- `Makefile`：构建脚本

## 构建

```bash
make
```

清理：

```bash
make clean
```

## 运行

```bash
./riscvemu playground/testrv
```

期望输出示例：

```
Hello, RISC-V Emulator!
```

## Playground

栈/函数调用自检程序：

```bash
/home/jason/riscv-toolchain/riscv/bin/riscv64-unknown-elf-gcc -O2 -g -march=rv64gc -mabi=lp64d -static -o playground/stackcheck playground/stackcheck.c
./riscvemu playground/stackcheck 64
```

## 调试

开启 syscall 跟踪（打印每次 ecall 的编号与 a0-a3 参数到 stderr）：

```bash
RISCVEMU_TRACE_SYSCALL=1 ./riscvemu playground/testrv
```

说明：这是运行时通过 shell 注入的环境变量，不是仓库内的固定配置；代码在 `src/riscvemu.c` 中通过 `getenv("RISCVEMU_TRACE_SYSCALL")` 读取。

## 已知边界

- 当前以解释执行为主，尚未启用/实现 JIT 优化。
- syscall 与结构体 ABI 适配会影响对更复杂用户态程序的兼容性；相关排查记录见 `doc/debug260419.md`。
