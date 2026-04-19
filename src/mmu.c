#include "../inc/riscvemu.h"

/**
 * 由于只在mmu.c中使用，所以将函数load_phdr定义为static，限制其作用域仅在当前文件内（对外不可见）。
 * 该函数的作用是加载ELF文件中的程序头部信息，并将其存储在phdr结构体中，以便后续使用。
 */
static void load_phdr(elf64_phdr_t *phdr, elf64_ehdr_t *ehdr, i64 i, FILE *file)
{
    // fseek函数用于在文件中移动文件指针，参数file是文件流指针，ehdr->e_phoff是程序头部表在文件中的偏移量，i * ehdr->e_phentsize是当前要加载的程序头部的偏移量，SEEK_SET表示从文件开头开始计算偏移量。
    if (fseek(file, ehdr->e_phoff + i * ehdr->e_phentsize, SEEK_SET) != 0)
    {
        fatal("seek file failed!"); // 如果fseek函数返回非0值，表示移动文件指针失败，输出错误信息并退出程序
    }

    // fread函数用于从文件中读取数据，参数phdr是存储读取数据的缓冲区，1表示每次读取1字节，sizeof(elf64_phdr_t)表示要读取的字节数，file是文件流指针。
    if(fread((void *)phdr, 1, sizeof(elf64_phdr_t), file) != sizeof(elf64_phdr_t))
    {
        fatal("file too small"); // 如果fread函数返回的字节数不等于要读取的字节数，表示读取文件失败，输出错误信息并退出程序
    }
}

/**
 * 将ELF文件中程序段的权限标志转换为mmap函数所需的保护标志，以便在加载程序段时正确设置内存权限。
 */
static int flags_to_mmap_prot(u32 flags)
{
    return ((flags & PF_R) ? PROT_READ : 0) |
           ((flags & PF_W) ? PROT_WRITE : 0) |
           ((flags & PF_X) ? PROT_EXEC : 0);
}

// Implementation for loading segment into MMU
static void mmu_load_segment(mmu_t *mmu, elf64_phdr_t *phdr, int fd)
{
    // <unistd.h>
    int page_size = getpagesize(); // 获取系统的页面大小，通常是4096字节
    u64 offset = phdr->p_offset;
    u64 vaddr = TO_HOST(phdr->p_vaddr);
    u64 aligned_vaddr = ROUNDDOWN(vaddr, page_size); // 将虚拟地址向下对齐到页面边界（页的起始地址）
    u64 filesz = phdr->p_filesz + (vaddr - aligned_vaddr); // 计算要加载的文件大小，包括对齐后的部分。vaddr - aligned_vaddr是程序与当前页面起始地址之间的偏移量，确保加载的文件大小包括了对齐后的部分。
    u64 memsz = phdr->p_memsz + (vaddr - aligned_vaddr); // 计算要加载的内存大小，包括对齐后的部分

    int prot = flags_to_mmap_prot(phdr->p_flags);
    // mmap 映射的文件偏移（offset）也必须是页对齐的，返回值是映射到内存中的地址，参数解释如下：
    u64 addr = (u64)mmap((void *)aligned_vaddr, filesz, prot, MAP_PRIVATE | MAP_FIXED, 
                        fd, ROUNDDOWN(offset, page_size));
    assert(addr == aligned_vaddr); // 确保mmap函数返回的地址与对齐后的虚拟地址相同，如果不相同，说明映射失败，程序将终止执行
    
    // .bss section
    u64 remaining_bss = ROUNDUP(memsz, page_size) - ROUNDUP(filesz, page_size);
    if (remaining_bss > 0) {
        u64 addr = (u64)mmap((void *)(aligned_vaddr + ROUNDUP(filesz, page_size)),
             remaining_bss, prot, MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
        assert(addr == aligned_vaddr + ROUNDUP(filesz, page_size));
    }

    // [        | host_alloc        
    // [    ELF | malloc           | ]
    // 分配的内存空间是页的整数倍
    mmu->host_alloc = MAX(mmu->host_alloc, (aligned_vaddr + ROUNDUP(memsz, page_size))); // 更新Host Program的内存分配指针，确保它指向Host Program在内存中分配的空间的当前末尾位置
    // GUEST视角
    // [        | base             | alloc]
    mmu->base = mmu->alloc = TO_GUEST(mmu->host_alloc); // 更新MMU的基地址和分配指针，确保它们指向Guest Program在内存中的起始位置
}

void mmu_load_elf(mmu_t *mmu, int fd)
{
    size_t bytes_read; //计算大小或数个数可以用size_t类型
    elf64_ehdr_t *ehdr;

    // 读取ELF文件的头部信息到buf中，sizeof(elf64_ehdr_t)是ELF文件头部结构体的大小
    u8 buf[sizeof(elf64_ehdr_t)];
    // 将一个已经存在的“文件描述符”（fd）转换成一个标准 I/O 的“文件流指针”（file），并以二进制只读模式进行操作
    FILE *file = fdopen(fd, "rb");
    bytes_read = fread(buf, 1, sizeof(elf64_ehdr_t), file); // 读取ELF文件头部信息，存储在buf中，每次读取1字节，读取sizeof(elf64_ehdr_t)次
    if (bytes_read != sizeof(elf64_ehdr_t))
    {
        fatal("file too small!"); // 如果读取的字节数不等于ELF文件头部结构体的大小，输出错误信息并退出程序
    }

    ehdr = (elf64_ehdr_t *)buf; // 将读取到的ELF文件头部信息转换成elf64_ehdr_t结构体指针，方便后续访问ELF文件头部的各个字段
    if (*(u32 *)ehdr != *(u32 *)ELFMAG)
    {
        fatal("not an ELF file!"); 
    }
    
    // 当前仅支持RISC-V架构的ELF文件，并且要求是64位的ELF文件。
    if (ehdr->e_machine != EM_RISCV || ehdr->e_ident[EI_CLASS] != ELFCLASS64)
    {
        fatal("only support 64-bit RISC-V ELF file!");
    }

    mmu->entry = ehdr->e_entry;

    elf64_phdr_t phdr;
    // 因为目标架构是64位的，i这里最好使用i64类型，确保能够正确处理较大的文件偏移量和地址
    for (i64 i = 0; i < ehdr->e_phnum; i ++ )
    {
        load_phdr(&phdr, ehdr, i, file); // 拿到第i个程序头部的信息，存储在phdr结构体中
        if (phdr.p_type == PT_LOAD)
        {
            mmu_load_segment(mmu, &phdr, fd);
        }
    }
}

// i64是说明当前可以开新内存也可以释放内存
u64 mmu_alloc(mmu_t *mmu, i64 size)
{
    // mmap实现申请内存，对齐到pagesize上
    int pagesize = getpagesize();
    
    u64 base = mmu->alloc;
    assert(base >= mmu->base); 

    // Guest上先更新内存情况，再更新Host上的内存情况
    mmu->alloc += size;
    assert(mmu->alloc >= mmu->base); // 确保分配指针没有回绕到基地址之前（不能动了前面程序段的部分）

    if (size > 0 && mmu->alloc > TO_GUEST(mmu->host_alloc))
    {
        u64 len = ROUNDUP(mmu->alloc - TO_GUEST(mmu->host_alloc), pagesize);
        if (mmap((void *)mmu->host_alloc, len,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
            fatal("mmap failed!");
        mmu->host_alloc += len;
    }
    else if (size < 0 && ROUNDUP(mmu->alloc, pagesize) < TO_GUEST(mmu->host_alloc))
    {
        u64 len = TO_GUEST(mmu->host_alloc) - ROUNDUP(mmu->alloc, pagesize); // 对齐
        if (munmap((void *)TO_HOST(ROUNDUP(mmu->alloc, pagesize)), len) == -1)
            fatal(strerror(errno));
        mmu->host_alloc -= len;
    }
    
    return base;  // RISCV视角旧边界
}
