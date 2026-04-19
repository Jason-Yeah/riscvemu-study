# STAGE I：能稳定跑通最小 RISC-V ELF（解释执行）并具备基础调试能力

本文总结本阶段从“最初段错误/无输出”到“能跑通 testrv、能做基础自检与定位”的完整工作流与结果。

## 目标与范围

本阶段目标：

- 在 x86_64 Linux 上运行 `./riscvemu playground/testrv`，稳定输出预期内容并正常退出
- 解释执行路径可靠（不因指令分派错位/内存映射问题导致崩溃）
- 基础 syscall 可用（至少覆盖 newlib 的 `isatty/fstat/brk/write/exit/close` 路径）
- 具备最小调试抓手（syscall trace、基础自检程序）

明确不做：

- 不做 JIT
- 不追求完整 Linux 用户态兼容（大量 syscall/结构体 ABI 仍未覆盖）

## 初始问题（从 0 到能复现）

初始复现：

```bash
./riscvemu playground/testrv
```

现象：

- 直接段错误（SIGSEGV）或退出但无输出

第一步策略：

- 先复现并把“崩溃点”从随机变成稳定
- 通过 gdb/ASan 抓到“第一处真正出错的位置”

## 模拟器模型与流程（解释执行版）

### 核心概念

- Host：当前运行模拟器的机器（x86_64 Linux）
- Guest：被模拟执行的 RISC-V 64 ELF 程序
- 目标：在 Host 上复现 Guest 的寄存器/内存/控制流变化，并在 `ecall` 时转发到 Host syscall

### 地址空间模型（TO_HOST / TO_GUEST）

本项目采用“固定偏移映射”的简化模型：

- Guest 的虚拟地址 `guest_addr` 映射到 Host 地址 `host_addr = guest_addr + GUEST_MEMORY_OFFSET`
- 指令取值、数据读写统一通过 `TO_HOST()` 转换后直接解引用（或 memcpy）

这套模型要求：

- ELF 加载段映射必须落到预期 host 地址（`MAP_FIXED`）
- 后续堆/栈扩展的匿名页也必须落到预期 host 地址（否则 `TO_HOST()` 会访问未映射地址）

### 机器状态（state_t）

解释器只维护一个“Guest 视角的 CPU 状态”：

- `gp_regs[32]`：x0-x31（x0 恒为 0）
- `fp_regs[32]`：f0-f31
- `pc`：当前取指地址（Guest VA）
- `exit_reason/reenter_pc`：用于“执行一个 block 后返回上层”的协议字段

### 启动流程（加载 ELF + 构造栈）

入口：`src/riscvemu.c`

1. `machine_load_program()`
   - 打开 ELF
   - `mmu_load_elf()` 将 PT_LOAD 段映射进 Host 地址空间
   - 设置 `state.pc = mmu.entry`
2. `machine_setup()`
   - `mmu_alloc()` 分配一段 Guest 栈（默认 32MB）
   - 初始化 `sp`
   - 在 Guest 栈上构造 `argc/argv`（并对齐 sp 到 16 字节）
   - 设置 `a0=argc`、`a1=argv`

### 执行流程（取指-译码-执行）

解释执行核心：`src/interpret.c: exec_block_interp(state_t *state)`

伪代码（简化）：

```text
loop:
  data = *(u32*)TO_HOST(pc)          // fetch
  insn = decode(data)                // decode
  exec(insn, state)                  // execute (大 switch / funcs[insn.type])
  x0 = 0
  if insn.cont:
     break                           // ecall / branch / jal/jalr 等
  pc += (insn.rvc ? 2 : 4)
```

其中：

- `src/decode.c` 负责把二进制指令拆字段，并给出 `insn.type`
- `src/interpret.c` 负责按 `insn.type` 分派到对应 handler，直接读写 `state->gp_regs/fp_regs/pc`

### 控制流与退出协议（exit_reason / reenter_pc）

解释器不是“一次只跑一条指令然后返回”，而是“一次跑一个 block”，直到遇到需要上层介入的事件：

- 直接/间接分支、jal/jalr：设置 `exit_reason = direct_branch/indirect_branch`，并写入 `reenter_pc`
- ecall：设置 `exit_reason = ecall`，并写入 `reenter_pc`
- `pc == 0`：设置 `exit_reason = halt`（兜底，避免继续取指崩溃）

上层调度：`src/machine.c: machine_step()`

- 调用 `exec_block_interp()` 跑一个 block
- 根据 `exit_reason` 决定：
  - branch：更新 `pc = reenter_pc` 继续跑
  - ecall：返回给 `main` 处理 syscall
  - halt：安全退出

### syscall 处理（ecall -> do_syscall）

`src/riscvemu.c` 的主循环：

- `machine_step()` 返回 `ecall` 后：
  - 从 `a7` 取 syscall 号
  - `do_syscall(machine, n)` 在 Host 上执行对应 syscall
  - 把返回值写回 `a0`

这也是 Stage I 中“能打印输出”的关键链路：newlib 会先 `fstat/isatty/brk`，再 `write` 打印字符串。

## 修复与里程碑

### 里程碑 1：解释器分派稳定（修复 funcs[] 与 enum 对齐）

问题类型：

- `decode` 产生的 `insn_type_t` 与解释器 `funcs[]` 不一致，导致索引错位
- 从某条指令开始，执行会跳到“错误的 handler”，最终崩溃

处理方式：

- 为 `fence/fence_i` 增加占位 handler（no-op）
- 按 `enum insn_type_t` 顺序重排 `funcs[]`
- 加编译期 `_Static_assert(ARRAY_SIZE(funcs) == num_insns)`，避免以后再次错位

结果：

- 不再出现“函数指针乱跳”类崩溃

### 里程碑 2：内存模型一致（修复 mmu_alloc 固定映射）

问题类型：

- 地址模型采用 `TO_HOST(guest_addr) = guest_addr + offset`
- program segments 用 `MAP_FIXED` 映射到了预期地址
- 但 `mmu_alloc()` 扩展匿名页未使用 `MAP_FIXED`，导致内核可能返回不同地址
- 随后 `mmu_write()` 用 `TO_HOST()` 写入未映射地址，ASan 直接报错

处理方式：

- `mmu_alloc()` 扩展匿名页时使用 `MAP_FIXED`，保证映射落点符合 offset 模型
- 同步修正 `munmap` 的起始地址计算，避免释放错误区间

结果：

- 启动阶段（`machine_setup` 写 argv）不再因 mmu 写入崩溃

### 里程碑 3：输出链路跑通（修复 sys_fstat 的结构体 ABI 写回）

问题类型：

- `testrv`（newlib）在真正 `write(1,...)` 打印前，会先走 `isatty/fstat`
- 旧实现把 Host(x86_64) 的 `struct stat` 直接写进 Guest 内存
- Guest 期望的是 RISC-V/Linux ABI 的 `struct stat` 布局
- 布局/大小不一致会覆盖 Guest 栈等内存，引发“无输出/控制流跑飞/pc 归零”

处理方式：

- Host 侧先 `fstat()` 到本地变量
- 再转换填充一个明确布局的 `riscv_stat_t`（128 字节）并写回 Guest 指针
- 为兼容 IDE/宏差异，时间字段使用 `st_atime/st_mtime/st_ctime`（秒级），纳秒先置 0

结果：

- `./riscvemu playground/testrv` 能稳定输出 `Hello, RISC-V Emulator!` 并正常退出

### 里程碑 4：安全兜底与定位抓手（halt + syscall trace）

问题类型：

- 当 Guest `pc == 0` 时继续取指会崩溃（典型是返回地址被破坏或 ret 到 ra=0）

处理方式：

- 增加 `halt` 退出原因：`pc==0` 时结束解释执行并让上层 break，避免继续非法取指

调试抓手：

- 增加 `RISCVEMU_TRACE_SYSCALL=1` 环境变量开关：打印每次 ecall 的编号与 a0-a3 参数到 stderr
- 用于快速确认程序停在/卡在/反复调用的 syscall

结果：

- 崩溃更可控；定位更直接

## 自检程序（验证栈与调用链）

新增 `playground/stackcheck.c`：

- 读取当前 sp、检查 16 字节对齐
- 深度递归 + canary 校验（看栈帧是否被破坏）
- 大块局部数组写读校验（粗测栈读写映射是否正常）

构建与运行示例：

```bash
/home/jason/riscv-toolchain/riscv/bin/riscv64-unknown-elf-gcc -O2 -g -march=rv64gc -mabi=lp64d -static -o playground/stackcheck playground/stackcheck.c
./riscvemu playground/stackcheck 64
```

## 验证清单（本阶段 Done 的可重复验证）

1. 构建：

```bash
make clean && make
```

2. testrv 输出：

```bash
./riscvemu playground/testrv
```

3. syscall trace：

```bash
RISCVEMU_TRACE_SYSCALL=1 ./riscvemu playground/testrv
```

4. 栈自检：

```bash
./riscvemu playground/stackcheck 64
```
