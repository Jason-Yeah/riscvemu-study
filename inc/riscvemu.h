/* The main header file for the RISC-V emulator. */
#ifndef RISCVEMU_H
#define RISCVEMU_H

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "types.h"
#include "elfdef.h"
#include "reg.h"

// stage1
/* Error handling(exit the program when facing some serious errors) */
#define fatalf(fmt, ...) (fprintf(stderr, "fatal: %s:%d " fmt "\n", __FILE__, __LINE__, __VA_ARGS__), exit(1))
#define fatal(msg) fatalf("%s", msg)

// stage3: 用在switch语句上，告诉编译器有些分支不可达
#define unreachable() (fatal("unreachable"), __builtin_unreachable())

// stage2
// 向上/下对齐整数倍
#define ROUNDDOWN(x, k) ((x) & -(k))
#define ROUNDUP(x, k)   (((x) + (k)-1) & -(k))
#define MIN(x, y)       ((y) > (x) ? (x) : (y))
#define MAX(x, y)       ((y) < (x) ? (x) : (y))

#define ARRAY_SIZE(x)   (sizeof(x)/sizeof((x)[0]))


#define GUEST_MEMORY_OFFSET 0x088800000000ULL

// Host Program是模拟器本身，要模拟的程序叫做Guest Program
/**
 * Host Program在一个高位内存中加载，低位的用不到
 * 低位的给Guest Program使用（暂时不涉及页表机制）【模拟的逻辑空间】
 */
#define TO_HOST(addr)  (addr + GUEST_MEMORY_OFFSET)
#define TO_GUEST(addr) (addr - GUEST_MEMORY_OFFSET)

#define FORCE_INLINE inline __attribute__((always_inline))

// 指令类型
enum insn_type_t {
    insn_lb, insn_lh, insn_lw, insn_ld, insn_lbu, insn_lhu, insn_lwu,
    insn_fence, insn_fence_i,
    insn_addi, insn_slli, insn_slti, insn_sltiu, insn_xori, insn_srli, insn_srai, insn_ori, insn_andi, insn_auipc, insn_addiw, insn_slliw, insn_srliw, insn_sraiw,
    insn_sb, insn_sh, insn_sw, insn_sd,
    insn_add, insn_sll, insn_slt, insn_sltu, insn_xor, insn_srl, insn_or, insn_and,
    insn_mul, insn_mulh, insn_mulhsu, insn_mulhu, insn_div, insn_divu, insn_rem, insn_remu,
    insn_sub, insn_sra, insn_lui,
    insn_addw, insn_sllw, insn_srlw, insn_mulw, insn_divw, insn_divuw, insn_remw, insn_remuw, insn_subw, insn_sraw,
    insn_beq, insn_bne, insn_blt, insn_bge, insn_bltu, insn_bgeu,
    insn_jalr, insn_jal, insn_ecall,
    insn_csrrc, insn_csrrci, insn_csrrs, insn_csrrsi, insn_csrrw, insn_csrrwi,
    insn_flw, insn_fsw,
    insn_fmadd_s, insn_fmsub_s, insn_fnmsub_s, insn_fnmadd_s, insn_fadd_s, insn_fsub_s, insn_fmul_s, insn_fdiv_s, insn_fsqrt_s,
    insn_fsgnj_s, insn_fsgnjn_s, insn_fsgnjx_s,
    insn_fmin_s, insn_fmax_s,
    insn_fcvt_w_s, insn_fcvt_wu_s, insn_fmv_x_w,
    insn_feq_s, insn_flt_s, insn_fle_s, insn_fclass_s,
    insn_fcvt_s_w, insn_fcvt_s_wu, insn_fmv_w_x, insn_fcvt_l_s, insn_fcvt_lu_s,
    insn_fcvt_s_l, insn_fcvt_s_lu,
    insn_fld, insn_fsd,
    insn_fmadd_d, insn_fmsub_d, insn_fnmsub_d, insn_fnmadd_d,
    insn_fadd_d, insn_fsub_d, insn_fmul_d, insn_fdiv_d, insn_fsqrt_d,
    insn_fsgnj_d, insn_fsgnjn_d, insn_fsgnjx_d,
    insn_fmin_d, insn_fmax_d,
    insn_fcvt_s_d, insn_fcvt_d_s,
    insn_feq_d, insn_flt_d, insn_fle_d, insn_fclass_d,
    insn_fcvt_w_d, insn_fcvt_wu_d, insn_fcvt_d_w, insn_fcvt_d_wu,
    insn_fcvt_l_d, insn_fcvt_lu_d,
    insn_fmv_x_d, insn_fcvt_d_l, insn_fcvt_d_lu, insn_fmv_d_x,
    num_insns, // 指令总数
};

/**
 * 表示RISC-V指令的解码结果。
 */
typedef struct {
    // riscv有32个寄存器，i8类型足够表示寄存器编号了（4*8=32）
    i8 rd;          // 指令中的目的寄存器编号
    i8 rs1;         // 指令中的第一个源寄存器编号
    i8 rs2;         // 指令中的第二个源寄存器编号
    i8 rs3;         // 指令中的第三个源寄存器编号，主要用于某些特定类型的指令，如浮点指令
    i32 imm;       // 指令中的立即数值，通常用于计算地址、偏移量或作为操作数的一部分    
    i16 csr;        // 指令中的控制状态寄存器地址
    enum insn_type_t type; // 指令的类型，使用枚举类型insn_type_t来表示不同的指令类型，例如加载、存储、算术运算、分支等
    bool rvc;       // 指示指令是否是RVC（RISC-V Compressed）指令，RVC指令是一种压缩格式的指令，占用更少的字节，通常用于提高代码密度和性能
    bool cont;      // 指示指令是否是连续指令，连续指令是指在RVC指令中，某些指令可能需要与下一条指令一起解码和执行，以实现更复杂的操作
} insn_t;

/**
 * stack.c
 */
#define STACK_CAP 256
#define STACK_SIZE 32 * 1024 * 1024
#define AUXV_SIZE 8
#define ENVP_SIZE 8
#define ARGV_END_SIZE 8
#define ARGC_SIZE 8

typedef struct {
    i64 top;
    u64 elems[STACK_CAP];
} stack_t;

void stack_push(stack_t *, u64);
bool stack_pop(stack_t *, u64 *);
void stack_reset(stack_t *);
void stack_print(stack_t *);

/**
 * str.c
 */
#define STR_MAX_PREALLOC (1024 * 1024)
#define STRHDR(s) ((strhdr_t *)((s)-(sizeof(strhdr_t))))

#define DECLEAR_STATIC_STR(name)   \
    static str_t name = NULL;  \
    if (name) str_clear(name); \
    else name = str_new();     \

typedef char * str_t;

typedef struct {
    u64 len;
    u64 alloc;
    char buf[];
} strhdr_t;

FORCE_INLINE str_t str_new() {
    strhdr_t *h = (strhdr_t *)calloc(1, sizeof(strhdr_t));
    return h->buf;
}

FORCE_INLINE size_t str_len(const str_t str) { return STRHDR(str)->len; }

void str_clear(str_t);

str_t str_append(str_t, const char *);

/**
 * mmu.c
 * mmu_t represents the memory management unit (MMU) of the RISC-V machine. It contains fields for the entry point of the program (entry), host memory allocation (host_alloc), guest memory allocation (alloc), and the base address of the memory (base). The mmu_load_elf function is responsible for loading an ELF format program file into the machine's memory, while the mmu_alloc function is used to allocate memory for the guest. The mmu_write function is a helper function that writes data to a specified address in the guest memory, using memcpy to copy data from the host to the guest.
*/
typedef struct {
    u64 entry;      // 程序入口点的地址
    u64 host_alloc; // Host Program的内存分配指针，指向Host Program在内存中分配的空间的当前末尾位置（已对齐的）
    u64 alloc;
    u64 base;       // 内存的基地址，指向Guest Program在内存中的起始位置（程序段和堆栈的分界线）
} mmu_t;

void mmu_load_elf(mmu_t *mmu, int fd);
u64 mmu_alloc(mmu_t *mmu, i64 size);

FORCE_INLINE void mmu_write(u64 addr, u8 *data, size_t len) 
{
    memcpy((void *)TO_HOST(addr), (void *)data, len);
} 

/**
 * cache.c
*/
#define CACHE_ENTRY_SIZE (64 * 1024)
#define CACHE_SIZE       (64 * 1024 * 1024)

typedef struct {
    u64 pc;
    u64 hot;
    u64 offset;
} cache_item_t;

typedef struct {
    u8 *jitcode;
    u64 offset;
    cache_item_t table[CACHE_ENTRY_SIZE];
} cache_t;

cache_t *new_cache();
u8 *cache_lookup(cache_t *, u64);
u8 *cache_add(cache_t *, u64, u8 *, size_t, u64);
bool cache_hot(cache_t *, u64);

/**
 * state.c
*/
enum exit_reason_t {
    none,
    direct_branch,
    indirect_branch,
    interp,
    ecall,
};

/**
 * （浮点）控制状态寄存器类型
 * fflags寄存器用于存储浮点运算的异常标志，
 * frm寄存器用于存储浮点运算的舍入模式，
 * fcsr寄存器是一个组合寄存器，包含了fflags和frm的内容。
 */
enum csr_t {
    fflags = 0x001,
    frm    = 0x002,
    fcsr   = 0x003,
};

// stage1
/**
 * state.c
 * state_t represents the state of the RISC-V machine, 
 * including the general-purpose registers (gp_regs), 
 * the program counter (pc), and the reason for exiting (exit_reason). 
 * The reenter_pc field is used to store the program counter value when re-entering the emulator after an exit, allowing the emulator to resume execution from that point.
 */
typedef struct {
    enum exit_reason_t exit_reason; // 退出原因，none表示正常执行，direct_branch表示直接分支指令，indirect_branch表示间接分支指令，interp表示需要解释执行的指令，ecall表示系统调用指令
    u64 reenter_pc; // 重新进入程序时（block）的程序计数器值，主要用于在遇到需要解释执行的指令或系统调用指令时，保存当前的程序计数器值，以便在处理完这些指令后能够正确地返回到原来的位置继续执行
    u64 gp_regs[num_gp_regs]; // RISC-V有32个整数寄存器，使用一个数组来表示它们
    fp_reg_t fp_regs[num_fp_regs];
    u64 pc;
} state_t;

void state_print_regs(state_t *);

// stage1
/**
 * machine.c
 * machine_t represents the entire RISC-V machine,
 * which includes the state of the machine (state),
 * the memory management unit (mmu),
 * and a pointer to the cache (cache).
*/
typedef struct {
    state_t state;
    mmu_t mmu;
    cache_t *cache;
} machine_t;

typedef void (*exec_block_func_t)(state_t *);

FORCE_INLINE u64 machine_get_gp_reg(machine_t *m, i32 reg) {
    assert(reg >= 0 && reg < num_gp_regs);
    return m->state.gp_regs[reg];
}

FORCE_INLINE void machine_set_gp_reg(machine_t *m, i32 reg, u64 data) {
    assert(reg >= 0 && reg < num_gp_regs);
    m->state.gp_regs[reg] = data;
}

void machine_setup(machine_t *m, int argc, char *argv[]);
str_t machine_genblock(machine_t *);
u8 *machine_compile(machine_t *, str_t);
enum exit_reason_t machine_step(machine_t *m);
void machine_load_program(machine_t *m, char *prog);

/**
 * interp.c
*/
void exec_block_interp(state_t *);

/**
 * set.c
*/

#define SET_SIZE (32 * 1024)

typedef struct {
    u64 table[SET_SIZE];
} set_t;

bool set_has(set_t *, u64);
bool set_add(set_t *, u64);
void set_reset(set_t *);

/**
 * decode.c
*/
void insn_decode(insn_t * insn, u32 data);

/**
 * syscall.c
*/

u64 do_syscall(machine_t *, u64);

#endif
