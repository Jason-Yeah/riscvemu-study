# riscvemu

一个用 C 编写的 RISC-V 64 用户态模拟器，用于在 x86_64 Linux 上加载并运行 RISC-V ELF 程序。当前实现了：

- 解释执行：保证覆盖面与正确性，适合调试与冷代码
- 热点代码块 JIT：对被频繁执行的 basic block 生成等价的 C 源码并调用 clang 即时编译为 x86_64 机器码，加速循环密集型 workload

## 目录结构

- `src/`：核心实现（解码、解释执行、MMU、系统调用、机器状态）
- `inc/`：公共头文件与类型定义
- `playground/`：示例/测试程序（例如 `testrv`）
- `doc/`：阶段记录与调试文档（`STAGE_I.md`、`STAGE_II.md`、`debug260419.md` 等）
- `Makefile`：构建脚本

## 快速开始

构建：

```bash
make
```

运行一个最小示例（验证 ELF 加载、syscall、输出链路）：

```bash
./riscvemu playground/testrv
```

运行一个循环密集示例（更容易触发热点 JIT）：

```bash
./riscvemu playground/prime
```

## 构建

依赖：

- clang（用于构建模拟器；并且 JIT 运行时会调用 clang 编译生成的代码）
- make

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

## 工作原理（简述）

### 解释执行路径

- `machine_load_program()`：加载 RISC-V ELF（PT_LOAD 段映射到 Host 地址空间）
- `machine_setup()`：分配并初始化 Guest 栈，构造 `argc/argv`，设置 `a0/a1/sp`
- `exec_block_interp()`：循环执行“取指 -> 解码 -> 按类型分派执行”，遇到 `branch/jal/jalr/ecall` 后退出一个 block
- `do_syscall()`：遇到 `ecall` 后由 Host 执行对应 syscall，并把返回值写回 `a0`

更详细流程见 `doc/STAGE_I.md`。

### JIT 路径（热点 block）

- `cache_hot()`：对同一 `pc` 做计数，达到阈值后认为是热点
- `machine_genblock()`：从当前 `pc` 出发，生成覆盖控制流的 block C 源码（label + goto 形式）
- `machine_compile()`：把生成的 C 源码喂给 clang，得到 `.o`，并做最小重定位后写入可执行缓存
- `cache_lookup()`：后续再次命中该 `pc` 时，直接跳到缓存中的 x86_64 机器码执行

更详细设计与限制见 `doc/STAGE_II.md`。

## 实现细节

### CPU 状态与执行模型

- 目标架构：RISC-V 64 用户态（支持 RVC 压缩指令；浮点寄存器与部分 RV64G 指令在解释器中已覆盖）
- 状态结构：`state_t` 维护 `gp_regs[32]`、`fp_regs[32]`、`pc`，以及 `exit_reason/reenter_pc` 作为 block 退出协议
- 执行粒度：一次运行一个“block”（顺序执行直到遇到控制流改变或 `ecall`），而不是每条指令都回到上层

### 取指/译码/分派

- 取指：通过 `TO_HOST(pc)` 将 Guest 虚拟地址转换为 Host 指针后直接读取（`u32`）
- 译码：`insn_decode()` 将指令字段拆解为 `insn_t`，并给出 `insn.type`（指令类别）
- 分派：解释器侧通过 `funcs[insn.type]` 调用对应 handler 执行

### 内存模型（MMU）

- 模型：固定偏移映射（`host_addr = guest_addr + GUEST_MEMORY_OFFSET`）
- ELF 加载：对 PT_LOAD 段用 `mmap(MAP_FIXED)` 映射到预期的 Host 地址（保证 `TO_HOST` 有效）
- 堆/栈扩展：通过 `mmu_alloc()` 在同一映射体系下扩展匿名页，保证后续 `mmu_write/TO_HOST` 访问不跑飞

### syscall 支持（最小可用）

当前能跑通 newlib 常见路径（例如 `testrv` 打印）主要依赖：

- `write`、`read`、`close`、`lseek`
- `brk`（用于 newlib 的堆增长）
- `fstat`（包含对 Guest ABI `struct stat` 的写回适配，避免把 Host `struct stat` 直接写进 Guest 内存）
- `gettimeofday`
- `exit/exit_group`

其他 syscall 目前仍可能直接 `fatal("unimplemented syscall")`。

### JIT（热点 basic block）

JIT 的核心是“把 Guest 指令翻译成等价的 C 代码，再让 clang 生成 x86_64 机器码”：

- 热点判定：按 `pc` 计数，达到阈值后才编译（`src/cache.c`）
- block 生成：`machine_genblock()` 从入口 `pc` 出发，用 label + goto 形式生成 C 代码，覆盖可达控制流（`src/codegen.c`）
- 寄存器搬运优化：用 tracer 记录本 block 实际用到的寄存器，只生成必要的 load/store（减少冗余内存访问）
- 编译与装载：`machine_compile()` 调用 clang 生成 `.o` 并解析内存中的 ELF section，将 `.text/.rodata` 拷贝进可执行缓存，同时做最小重定位（当前只支持 x86_64 的 `R_X86_64_PC32`，见 `src/compile.c`）
- 执行：最终得到的 x86_64 机器码函数指针符合 `exec_block_func_t` 签名，直接 `((exec_block_func_t)code)(&state)`

### 可观测性与自检

- syscall trace：`RISCVEMU_TRACE_SYSCALL=1` 打印每次 `ecall` 的号与参数（便于定位卡在哪个 syscall）
- stackcheck：`playground/stackcheck.c` 用递归与 canary 粗测栈与调用链是否正常

## Playground

栈/函数调用自检程序：

```bash
riscv64-unknown-elf-gcc -O2 -g -march=rv64gc -mabi=lp64d -static -o playground/stackcheck playground/stackcheck.c
./riscvemu playground/stackcheck 64
```

说明：`playground/` 下也包含已编译的 RISC-V ELF（如 `testrv`、`prime`），可直接运行。

## 调试

开启 syscall 跟踪（打印每次 ecall 的编号与 a0-a3 参数到 stderr）：

```bash
RISCVEMU_TRACE_SYSCALL=1 ./riscvemu playground/testrv
```

说明：这是运行时通过 shell 注入的环境变量，不是仓库内的固定配置；代码在 `src/riscvemu.c` 中通过 `getenv("RISCVEMU_TRACE_SYSCALL")` 读取。

## 已知边界

- JIT 已启用，但当前实现偏“最小可用”（热点阈值/缓存策略/编译路径仍可继续优化）。
- syscall 与结构体 ABI 适配仍会影响对更复杂用户态程序的兼容性。
- 详细背景与阶段总结见 `doc/STAGE_I.md`、`doc/STAGE_II.md`、`doc/debug260419.md`。
