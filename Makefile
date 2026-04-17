# 编译选择，O3是最高级别优化，Wall开启所有警告，Werror将警告视为错误，Wimplicit-fallthrough禁止隐式的case穿透（检查switch-case语句是否漏写了break）
CFLAGS=-O3 -Wall -Werror -Wimplicit-fallthrough
# 自动化文件扫描，找到src目录下的所有.c文件和inc目录下的所有.h文件，并将它们分别存储在SRCS和HDRS变量中。这样当添加新的源文件或头文件时，Makefile会自动识别并包含它们，无需手动修改Makefile。
SRCS=$(wildcard src/*.c)
HDRS=$(wildcard inc/*.h)
# 将源文件路径转换为目标文件路径，例如src/main.c会被转换为obj/main.o
OBJS=$(patsubst src/%.c, obj/%.o, $(SRCS))
# 指定编译器，这里使用clang，可以根据需要替换为gcc等其他编译器（C Compiler）
CC=clang

# 链接阶段：$@: 代表目标名 riscvemu；$^: 代表所有依赖文件（即所有的 .o）；-lm: 链接数学库（Math library），模拟器处理浮点指令或时钟计算时经常用到；$(LDFLAGS): 预留的链接选项（比如以后要加 -lpthread 就可以通过命令行传入）
riscvemu: $(OBJS)
	$(CC) $(CFLAGS) -lm -o $@ $^ $(LDFLAGS)

# 编译阶段：$<: 代表第一个依赖文件（即当前正在编译的 .c 文件）；$@: 代表目标文件（即对应的 .o 文件）
# obj/%.o: src/%.c: 告诉 Make 如何一对一地把 .c 变成 .o，% 是一个通配符，表示任意文件名。这个规则会被 Make 自动应用到所有的 .c 文件上。
# $(HDRS): 任何一个头文件改了，所有源文件都会重新编译。虽然有点保守，但能绝对保证代码一致性。
$(OBJS): obj/%.o: src/%.c $(HDRS)
	@mkdir -p $$(dirname $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# 清理阶段：删除生成的可执行文件和所有的目标文件（.o）。rm -rf 是一个强制删除命令，确保无论文件是否存在都不会报错。
clean:
	rm -rf riscvemu obj/

# .PHONY 声明 clean 不是一个实际的文件目标，而是一个伪目标，确保每次执行 make clean 时都会运行对应的命令，而不会因为存在同名文件而被跳过。
.PHONY: clean
