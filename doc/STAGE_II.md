# STAGE II：引入 JIT（hot block）加速并跑通基本程序

本文记录当前 JIT 优化（热点代码块编译执行）的设计、实现路径、关键数据结构、以及本阶段遇到的问题与修复点。

## 目标与范围

本阶段目标：

- 在不改变外部接口（仍然 `./riscvemu <riscv-elf>`）的前提下，引入热点检测与 JIT 编译执行
- 热点代码走 JIT，冷代码仍走解释器
- 至少能稳定跑通 `playground/testrv`、`playground/prime` 等示例

明确不做：

- 不实现跨平台 JIT（当前“mini-linker/relocation”仅支持 x86_64）
- 不实现完整 Linux 用户态 ABI/系统调用覆盖
- 不做复杂的寄存器分配、SSA、IR 优化等编译器工程

## JIT 总览（从入口到执行）

关键路径：

1. `src/riscvemu.c`
   - 初始化 `machine_t` 后，创建 `machine.cache = new_cache()`
2. `src/machine.c` 的 `machine_step()`
   - 查询缓存：`cache_lookup(cache, pc)`
   - 若没有可用 JIT code，则用 `cache_hot(cache, pc)` 更新热度并判断是否达到阈值
   - 达到阈值：`machine_genblock()` 生成 C 源码 -> `machine_compile()` 编译成 x86_64 机器码 -> `cache_add()` 写入缓存
   - 未达到阈值：回退到 `exec_block_interp`
3. 执行阶段
   - `code = (u8*)exec_block_interp` 或 `code = jit_entry`
   - 统一通过函数指针调用：`((exec_block_func_t)code)(&m->state)`

核心思想：

- 解释器负责“正确性兜底”和“冷代码”
- 热点 block 生成等价的 C 代码，让 clang 直接产出高效的 x86_64 机器码

## 热点策略（Hotness）

文件：`src/cache.c`

- 采用简单计数：同一 `pc` 被重复执行达到阈值后，认为是热点
- 阈值：`CACHE_HOT_COUNT = 100000`
- 查找逻辑：
  - `cache_lookup()` 只有在 `entry.hot >= CACHE_HOT_COUNT` 时才返回 JIT code
  - `cache_hot()` 每次对该 pc 计数 +1，并返回是否达到阈值

设计效果：

- 初次/少次执行的 block 不会触发编译开销
- 达到阈值后，才会 `machine_compile()`，并从此走 JIT

## Cache 结构与代码内存布局

文件：`inc/riscvemu.h`、`src/cache.c`

数据结构（简化语义）：

- `cache->table[]`：哈希表，key 为 guest `pc`
- `cache->jitcode`：一整段 mmap 的可执行内存（当前使用 `PROT_READ|PROT_WRITE|PROT_EXEC`）
- `cache->offset`：下一段代码写入位置（支持对齐）

`cache_add()` 的行为：

- 将编译出的 `.text`（以及必要时 `.rodata`）拷贝到 `jitcode + offset`
- 写入 `table[index] = { pc, hot, offset }`
- 用 `__builtin___clear_cache()` 刷新指令缓存

注意：

- `jitcode` 使用 RWX 映射属于“简单实现”，后续可改为 W^X（写时 RW，写完改 RX）

## Block 生成（machine_genblock）

文件：`src/codegen.c`

`machine_genblock(machine_t *m)` 生成一段 C 源码，核心特征：

- 以当前 `m->state.pc` 为入口点，生成一个“基本块图”覆盖的代码区域
- 通过 `stack_t` + `set_t` 做工作队列与去重（避免重复生成同一 pc 的 label）
- 每个 pc 都会生成一个 label：`insn_<pc>:`，并在末尾 `goto` 到“下一条顺序指令”
- 遇到控制流改变（分支/jal/jalr/ecall 等）时，会：
  - 生成对应的控制流更新逻辑
  - 设置 `state->exit_reason` 和 `state->reenter_pc`
  - 结束当前 block（跳转到 `end:` 统一收尾）

取指与解码：

- `u32 data = *(u32 *)TO_HOST(pc);`
- `insn_decode(&insn, data);`
- 调用 `funcs[insn.type]` 生成该指令对应的 C 代码片段

## Tracer：只保存/恢复用到的寄存器

文件：`src/codegen.c`

问题背景：

- JIT 生成的函数 `start(state_t *state)` 会在 C 层面使用局部变量来承载寄存器值（例如 `uint64_t x1 = state->gp_regs[1];`）
- 如果无差别地把 32 个整数寄存器 + 32 个浮点寄存器全搬进来，会产生大量无谓 load/store

当前做法：

- `tracer_t` 记录每条指令实际使用到的 gp/fp 寄存器集合
- prologue 只 load 被用到的寄存器到局部变量
- epilogue 只 store 被用到的寄存器回 `state`

这属于“很轻量的优化”，成本小，收益明显。

## 编译与装载（machine_compile）

文件：`src/compile.c`

整体流程：

1. 将 `machine_genblock()` 产生的 C 源码喂给 clang：

```bash
clang -O3 -c -xc -o /dev/stdout -
```

2. 通过 pipe 读取 clang 输出的 ELF 目标文件（.o）到内存缓冲区 `elfbuf`
3. 从内存里的 ELF 解析出：
   - `.text` 段（必须）
   - `.symtab`（用于重定位）
   - `.rela.text` 与 `.rodata.*`（如果 clang 生成了常量池/只读数据）
4. 将 `.rodata` 和 `.text` 拷贝进 `cache->jitcode`，并在存在 `.rela.text` 时做一次“mini-link”：
   - 当前只支持 `R_X86_64_PC32` 重定位
   - 计算 `S + A - P` 并写回 `.text` 的 relocation 位置
5. 返回 `.text` 的入口地址作为 JIT 函数指针

约束与限制：

- `#ifndef __x86_64__` 直接 `fatal("only support x86_64 for now")`
- 只处理 `.text` 的 PC-relative relocation，属于“足够跑通现阶段生成代码”的最小实现

## 运行语义（exit_reason / reenter_pc）

JIT 与解释器共享同一套“退出协议”：

- 遇到直接/间接分支：设置 `exit_reason = direct_branch/indirect_branch`，并写 `reenter_pc`
- 遇到 `ecall`：设置 `exit_reason = ecall`，并写 `reenter_pc`
- 遇到需要回退解释的复杂指令：设置 `exit_reason = interp`

`machine_step()` 的策略：

- 如果是分支，且目标 `reenter_pc` 在 cache 里有 JIT code，就继续用 JIT 跑
- 否则回退解释器或跳出让外层处理 syscall

## 本阶段关键 Bug 与修复：开启 JIT 后段错误

现象：

- 启用 JIT 逻辑后运行立刻 SIGSEGV

根因：

- `machine_step()` 中调用 `cache_lookup(m->cache, ...)`，但 `main` 没有初始化 `m->cache`
- `m->cache == NULL` 时会在 `cache_lookup()` 解引用 `cache->table` 触发崩溃

修复：

- 在 `src/riscvemu.c` 中初始化：`machine.cache = new_cache();`
- 同时在 `machine_step()` 入口增加 `pc==0` 处理，避免 `cache_lookup()` 的 `assert(pc != 0)` 或走到 `unreachable()`

## 使用建议（调试与验证）

- syscall 跟踪：

```bash
RISCVEMU_TRACE_SYSCALL=1 ./riscvemu playground/testrv
```

- 用 `playground/prime` 作为热点工作负载：循环密集，容易触发 hot block，验证 JIT 路径稳定性。
