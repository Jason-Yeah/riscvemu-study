#include "types.h"


#define EI_NIDENT 16
#define ELFMAG "\177ELF"

#define EM_RISCV 243

#define EI_CLASS     4
#define ELFCLASSNONE 0
#define ELFCLASS32   1
#define ELFCLASS64   2
#define ELFCLASSNUM  3

#define PT_LOAD 1

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4


#define R_X86_64_PC32 2

/**
 * 定义的是ELF文件的头部结构体
 */
typedef struct {
    u8 e_ident[EI_NIDENT];  // ELF文件的标识字段，包含了ELF文件的魔数、文件类型、机器架构等信息
    u16 e_type;
    u16 e_machine;          // 指定了ELF文件所针对的机器架构，这里定义了一个常量EM_RISCV，表示RISC-V架构
    u32 e_version;
    u64 e_entry;            // 程序入口点的地址
    u64 e_phoff;            // 程序头部表在文件中的偏移量，指向程序头部表的起始位置
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;        // 程序头部表中每个程序头部的大小，物理上与segment大小相同
    u16 e_phnum;            // 程序头部表中的程序头部数量，物理上与segment数量相同
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} elf64_ehdr_t;

/**
 * 定义的是ELF文件中程序头部的结构体(Program Header)，它描述了一个程序段(segment)在文件中的位置和内存中的位置，以及该段的大小和属性等信息。
 */
typedef struct {
    u32 p_type;             // 程序段的类型，指示了该段的用途，例如PT_LOAD表示可加载的程序段
    u32 p_flags;            // 程序段的属性标志，指示了该段的访问权限和其他属性，例如PF_R表示可读，PF_W表示可写，PF_X表示可执行
    u64 p_offset;           // 程序段在文件中的偏移量，指示了该段在ELF文件中的位置
    u64 p_vaddr;            // 程序段在内存中的虚拟地址，指示了该段被加载到内存中的位置
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;            // 程序段的对齐要求，指示了该段在内存中应该按照多少字节对齐
} elf64_phdr_t; // 与ehdr中的e_phentsize大小一致

typedef struct {
    u32 sh_name;
    u32 sh_type;
    u32 sh_flags;
    u64 sh_addr;
    u64 sh_offset;
    u64 sh_size;
    u32 sh_link;
    u32 sh_info;
    u64 sh_addralign;
    u64 sh_entsize;
} elf64_shdr_t;

typedef struct {
	u32 st_name;
	u8  st_info;
	u8  st_other;
	u16 st_shndx;
	u64 st_value;
	u64 st_size;
} elf64_sym_t;

typedef struct {
    u64 r_offset;
    u32 r_type;
    u32 r_sym;
    i64 r_addend;
} elf64_rela_t;
