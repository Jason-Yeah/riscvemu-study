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
    if(fread(phdr, 1, sizeof(elf64_phdr_t), file) != sizeof(elf64_phdr_t))
    {
        fatal("read file failed!\nfile too small"); // 如果fread函数返回的字节数不等于要读取的字节数，表示读取文件失败，输出错误信息并退出程序
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
    
    // u64 remaining_bss = ROUNDUP(memsz, page_size) - ROUNDDOWN(filesz, page_size); // 计算BSS段的剩余大小，BSS段是未初始化的数据段，通常需要在内存中分配空间但不需要从文件中加载数据。ROUNDUP函数将地址向上对齐到页面边界，确保BSS段的结束地址也是页面对齐的。
    // if (remaining_bss > 0)
    // {
    //     /**
    //      * mmap函数的参数解释：
    //      * (void *)(aligned_vaddr + ROUNDDOWN(filesz, page_size))：这是要映射到内存中的地址，计算方式是对齐后的虚拟地址加上对齐后的文件大小，确保BSS段紧跟在加载的文件段之后
    //      * remaining_bss：这是要映射的内存大小，即BSS段的剩余大小
    //      * prot：这是内存的保护标志，之前通过flags_to_mmap_prot函数计算得到，确保BSS段具有正确的访问权限
    //      * MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS：这是映射的标志，MAP_PRIVATE表示映射是私有的，MAP_FIXED表示映射必须在指定的地址进行，MAP_ANONYMOUS表示映射不与任何文件关联，适用于BSS段这种不需要从文件中加载数据的情况
    //      * -1：这是文件描述符参数，对于匿名映射，文件描述符应该设置为-1
    //      * 0：这是文件偏移参数，对于匿名映射，偏移应该设置为0
    //      * 该调用将在内存中分配一块空间用于BSS段，并且该空间的权限与程序段的权限相同，确保BSS段能够正确地被访问和使用。
    //      */
    //     u64 bss_addr = (u64)mmap((void *)(aligned_vaddr + ROUNDDOWN(filesz, page_size)), remaining_bss, 
    //                             prot, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
    //     assert(bss_addr == aligned_vaddr + ROUNDUP(filesz, page_size)); 
    // }

    u64 file_end = aligned_vaddr + filesz;
    u64 file_page_end = aligned_vaddr + ROUNDUP(filesz, page_size);
    // u64 mem_end = aligned_vaddr + memsz;
    u64 mem_page_end = aligned_vaddr + ROUNDUP(memsz, page_size);

    // 先把文件尾页中属于 BSS 的那部分清零
    if (memsz > filesz && file_page_end > file_end)  // 1. 有没有bss 2. 文件尾页中有没有bss
    {
        memset((void *)file_end, 0, file_page_end - file_end);
    }

    // 再补后续整页 BSS
    if (mem_page_end > file_page_end) 
    {
        u64 bss_addr = (u64)mmap((void *)file_page_end,
                                mem_page_end - file_page_end,
                                prot,
                                MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                                -1, 0);
        assert(bss_addr == file_page_end);
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