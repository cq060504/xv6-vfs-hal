# LoongArch架构模拟器调试使用说明

# LA_EMU

LA_EMU 可作为模型运行的参考标准，支持以下调试功能。

# Checkpoint保存

LA_EMU支持保存Checkpoint。

运行以下命令:

```shell
cd ./LA_EMU
make
make ckp KERNEL_DIR={/path/to/linux/} CHECKPOINT_PC=##### 
```

shell 

其中，CHECKPOINT_PC即为期望保存现场的PC值。

LA_EMU运行时，如果运行到与CHECKPOINT_PC对应的现场，终端即会打印相关信息。

CheckPoint has successfully saved into file! 

shell 

运行结束后，在LA_EMU目录下，即可获得Checkpoint相关文件，文件命名均以"checkpoint_"开头。

# 单步调试

LA_EMU支持单步调试。

运行以下命令:

shell 

```batch
cd ./LA_EMU
make CLI=1 
```

编译结束后，开始运行。

shell 

```txt
$ ./build/la_emu_kernel -w -n -z -m 8G -k ~/kernel/linux-6.10/vmlinux hit Breakpoint 0 at pc:0x0x90000000006b9000 (debug)s Continuing.  
singlestep pc:0x90000000006b9004 (debug)s 10 Continuing.  
singlestep pc:0x90000000006b9044 (debug)quit 
```

在debug端口输入s，即可单步运行。

可在对应PC处暂停，打印相应信息，进行调试。

# QEMU-LoongArch

# QEMU 参数说明

QEMU 常用的启动参数，如下所示:

# 1. 设备类型: -machine/-M

在qemu中，不同的指令集的模拟器会编译成不同的可执行文件，可以运行在不同的平台上，可使用 -machine/-M 指定模拟器运行的设备信息。

shell 

```powershell
$ qemu -M ?
Supported machines are:
none empty machine
virt Loongson-3A5000 LS7A1000 machine (default) 
```

# 2. 内存大小: -m

参数-m 1G就是指定虚拟机内部的内存大小为1GB，使用说明如下:

```txt
shell
$ qemu -m ?
qemu-system-loongarch64: -m ?: Parameter 'size' expects a non-negative number below Optional suffix k, M, G, T, P or E means kilo-, mega-, giga-, tera-, peta- and exabytes, respectively. 
```

# 3. 核心数: -smp

现代cpu往往是对称多核心的，因此通过添加启动参数 -smp 8 可以指定虚拟机核心数为 8。

```txt
$ qemu -smp ?
smp-opts options:
books=<num>
clusters=<num>
cores=<num>
cpus=<num>
dies=<num>
drawers=<num>
maxcpus=<num>
sockets=<num>
threads=<num> 
```

# 4. 关闭图像输出: -nographic

参数关闭了图像输出模式，QEMU 运行时不再弹出新窗口，信息输入输出，通过 serial 串口，在终端显示交互。

# 5. 串口输出重定向: -serial

-serial 选项用于配置 QEMU 中虚拟机的串行端口（UART），将串行端口重定向到宿主机指定的字符设备（char dev），如标准输入/输出（终端）、TCP端口、文件等。

该参数决定了虚拟机串口的数据“从哪里来，到哪里去”，在调试嵌入式系统和操作系统启动过程中非常有用。

-serial 的基本用法是 -serial dev，其中的 dev 代表你要重定向到的目标设备。

在启动 Linux 时，常见用法为 -serial mon:stdio。该选项表示将 QEMU 的监视器（monitor）和虚拟机串口复用，一起重定位到QEMU进程的标准输入输出(终端)。

# 6. 调试参数: -s -S

参数选项用于建立 gdb 服务。

其中，-s 选项会让 QEMU 在 TCP 端口 1234 监听来自 gdb 的传入连接，-S 选项会使 QEMU 在启动后，不立即运行 guest，而是等待主机 gdb 发起连接。

# 7. 指定镜像: -kernel

熟悉上述参数后，可在 QEMU 时添加对应参数，进入调试模式。

首先需要在启动 Linux Kernel 时加入额外参数。

参数指定传入 QEMU 的内核的镜像文件，一般是 ELF 文化，也可以是 ulmage 等。

```shell
bash
# replace your real path with {/path/to/qemu}.
{/path/to/qemu}/bin/qemu-system-loongarch64 -M virt -cpu la464 -m 4G -smp 1 -nograph 
```

打开一个新终端，并在终端中运行以下命令：

```txt
gdb ./vmlinux
bash 
```

进入 gdb 程序后，使用以下命令，与 QEMU 进行连接。

```txt
(gdb)target remote localhost:1234 bash 
```

:::note 端口 1234 是 QEMU 默认的 gdb 连接端口，特殊情况下，当该端口不可用时，可使用 -gdb tcp::xxxx 来代替 QEMU 启动参数 -s，其中 xxxx 代表可使用的新端口。

同时，在 gdb 启动时候，使用 target remote localhost:xxxx 来与 QEMU 建立连接，同样，xxxx 代表可使用的新端口，并与 QEMU 启动时配置的新端口相同。

# QEMU 调试教程

# 串口输出重定向

通过 -serial 参数，将虚拟机的调试信息，通过串口进行输出，并在宿主机上重定向到指定设备。

可使用 -serial mon:stdio 到当前终端。

也可将调试信息保存到指定文件。

可通过如下命令:

```shell
bash
# replace your real path with {/path/to/qemu}.
# replace your filename with 'debug.log'
{/path/to/qemu}/bin/qemu-system-loongarch64 -M virt -cpu la464 -m 8G -smp 1 -nograph 
```

在当前文件下，会生成 debug.log 文件(或指定名字的文件)。查看文件，可得到调试信息(查看调试信息文件之前，需要通过 Ctrl + C 结束 QEMU 进程)。

# QEMU Monitor

QEMU内部有一个管理控制台，提供了一套命令来动态查询和修改虚拟机状态，而无需重启虚拟机。

默认情况下，可以通过 Ctrl+Alt+2 组合键在图形界面中切换到Monitor。在无图形界面中，可通过 -monitor stdio 将其重定向到当前终端，也可以与串口复用：-serial mon:stdio，此时可通过特定转义键（如Ctrl+a c）在串口和Monitor之间切换。

在 Monitor 状态下，可以使用一些命令，查看虚拟机状态，进行调试。

调试类命令，与 GDB 模式相同。如可使用 info 命令查询虚拟机状态信息。

控制类命令，则支持 qemu 的快照功能。

QEMU 的快照功能是将虚拟机在虚拟机在某个时间点上的，磁盘信息、内存信息，进行备份，以便在需要时可以快速恢复到该状态。该功能常用于内核模块编译、修改和测试等场景。

保存快照时，首先使用以下命令，创建一个 qcow2 格式的磁盘镜像(raw格式不支持快照):

```txt
# replace your real path with {/path/to/qemu}.
{/path/to/qemu}/bin/qemu-img create -f qcow2 save.qcow2 10G 
```

系统启动参数，修改为如下命令:

bash 

# replace your real path with {/path/to/qemu}. 

{/path/to/qemu}/bin/qemu-system-loongarch64 -M virt -cpu la464 -m 8G -smp 1 -nograph 

# 日志工具

QEMU 中使用 -d 和 -D 参数配合使用是调试操作系统非常有效的方法，可以记录详细的内部调试信息，帮助开发者分析操作系统的行为、定位问题。

-d 参数用于启用调试信息的打印，后面可以跟一个或多个调试信息类型，用逗号分隔。

常见调试信息类型包括：

- in_asm: 记录汇编指令执行

- exec: 记录指令执行

- int: 记录中断信息

- mmu：记录内存管理单元(MMU)操作

- cpu：记录CPU操作

- mem: 记录内存访问

-D 参数用于指定调试日志文件的路径，将调试信息输出到指定文件中，而不是显示在终端上。

在编译小型操作系统时，两个参数可配合使用，可以获取内核完全启动前的信息，方便调试。

# GDB连接

使用以下命令，进入 gdb 调试模式:

```shell
bash
# replace your real path with {/path/to/qemu}.
{/path/to/qemu}/bin/qemu-system-loongarch64 -M virt -cpu la464 -m 4G -smp 1 -nograph 
```

交叉编译器的 gdb 与 QEMU 连接，使用以下命令:

```txt
loongarch64-unknown-linux-gnu-gdb ./vmlinux 
```

bash 

gdb 中，常用参数：

# 设置与管理断点

# 1. 设置断点

可使用 “break” 命令，或使用简写 “b”。断点位置由函数名，虚拟地址，文件行号等信息确定。

在 gdb 界面，使用以下:

```txt
# replace _start with all function name which you need
(gdb)b _start

# or input any va
(gdb)b *0x9000000000200000

# or line number in file
(gdb)b file.c:6 
```

输入命令，使 QEMU 开始运行 guest：

```txt
(gdb)continue shell 
```

当 PC 运行到断点对应函数、或对应地址，QEMU 将暂停运行。

# 2. 断点的管理。

可使用以下命令，查询已设置的断点。

```txt
(gdb) info breakpoint

Num Type Disp Enb Address What
1 breakpoint keep y 0x900000000020006c in main at kernel/main.c:16
breakpoint already hit 1 time 
```

使用以下命令，可以保存已设置的断点。

```txt
(gdb)save breakpoint file-name-to-save bash 
```

下次调试时，可以使用以下命令，批量设置保存的断点。

```txt
(gdb)save source file-name-to-save bash 
```

使用以下命令，可以删除已设置的断点。

shell 

```txt
(gdb) delete breakpoints 1
(gdb) info breakpoint
No breakpoints, watchpoints, tracepoints, or catchpoints. 
```

# 3. 设置临时断点。

如果想让断点只生效一次，可以使用命令 "tbreak" 或 "tb"。

以下方代码为例，进行说明。

C 

```c
#include <stdio.h>
#include <pthread.h>
typedef struct 
{
    int a;
    int b;
    int c;
    int d;
    pthread_mutex_t mutex;
} ex_st;

int main(void) {
    ex_st st = {1, 2, 3, 4, PTHREAD_MUTEX_INITIALIZER};
    printf("%d,%d,%d,%d\n", st.a, st.b, st.c, st.d);
    return 0;
} 
```

调试时在文件的第15行设置临时断点，当程序断住后，用“i b”（"info breakpoints"缩写）命令查看断点，发现断点没有了。也就是断点命中一次后，就被删掉了。

bash 

```txt
(gdb) tb a.c:15
Temporary breakpoint 1 at 0x400500: file a.c, line 15.
(gdb) i b
Num Type Disp Enb Address What
1 breakpoint del y 0x0000000000400500 in main at a.c:15
(gdb) r
Starting program: /data2/home/nanxiao/a
Temporary breakpoint 1, main () at a.c:15
15 printf("%d,%d,%d,%d\n", st.a, st.b, st.c, st.d); 
```

```txt
(gdb) i b
No breakpoints or watchpoints. 
```

# 4. 设置条件断点。

gdb 可以设置条件断点，即，只有在条件满足时，断点才会被出发。命令为 “break ... if cond” 以下方代码为例，进行说明。

C 

```c
#include <stdio.h>
int main(void)
{
    int i = 0;
    int sum = 0;
    for (i = 1; i <= 200; i++)
    {
    sum += i;
    }
    printf("%d\n", sum);
    return 0;
} 
```

gdb 调试时，可设置变量 “i” 的值为 101 时触发。

bash 

```txt
(gdb) start
Temporary breakpoint 1 at 0x4004cc: file a.c, line 5.
Starting program: /data2/home/nanxiao/a
Temporary breakpoint 1, main () at a.c:5
5    int i = 0;
(gdb) b 10 if i==101
Breakpoint 2 at 0x4004e3: file a.c, line 10.
(gdb) r
Starting program: /data2/home/nanxiao/a
Breakpoint 2, main () at a.c:10
10
(gdb) p sum
$1 = 5050 
```

# 设置与管理观察点

# 1. 设置观察点

gdb 可以设置观察点，即，当一个变量值发生变化时，程序会暂停运行。

可使用 “watch variable” 命令，为变量 variable 设置观察点，当 variable 的值发生任意变化时，程序暂停，在 gdb 窗口中打印旧值与新值的信息。

也可以使用 "watch * (data type) * address" 方式，通过指定地址与数据类型/数据宽度的方式，达到设置观察点的目的。

# 打印变量

gdb 中，使用 “p variable” 命令，打印变量 variable 信息。

# 1. 普通变量

如果 variable 是一个普通变量，则会打印变量数值。

# 2. 数组

如果 variable 是一个数组，则会打印数据所有元素，缺省最多显示 200 个元素。

如果想要全部显示，可使用命令 “set print elements number-of-elements” 或 “set print elements 0” 进行配置，在使用 “p variable” 进行打印，则可显示数据全部元素。

如果要打印数组中任意连续元素的值，可以使用“p variable”[index]@num”命令。其中 index 是数组索引（从0开始计数），num 是连续多少个元素。

# 3. 字符串

可以使用 “x/s variable” 命令打印 ASCII 字符串。

# 打印寄存器信息

可使用以下命令，打印寄存器信息:

```txt
(gdb)info registers # or: i r
(gdb) info registers
r0 0x0 0
r1 0x9000000000200058 0x9000000000200058 <spin>
r2 0x0 0x0
r3 0x9000000000308d00 0x9000000000308d00 <stack0+4080>
r4 0x1000 4096
r5 0x1 1
r6 0x200 512
r7 0x0 0
r8 0x0 0 
```

shell 

```asm
r9 0x0 0
r10 0x0 0
r11 0x0 0
r12 0x8 8
r13 0x0 0
r14 0x0 0
r15 0x0 0
r16 0x0 0
r17 0x0 0
r18 0x0 0
r19 0x0 0
r20 0x0 0
r21 0x0 0
r22 0x9000000000308d10 0x9000000000308d10 <uart_tx_lock>
r23 0x0 0
r24 0x0 0
r25 0x0 0
r26 0x0 0
r27 0x0 0
r28 0x0 0
r29 0x0 0
r30 0x0 0
r31 0x0 0
orig_a0 0x0 0
pc 0x90000000020006c 0x900000002006c <main+16>
badv 0x0 0x0 
```

可指定寄存器，进行查询:

(gdb) info registers r4 

r4 

0x1000 

4096 

# 查看内存数值

可使用以下命令，打印虚拟内存对应地址的数值:

(gdb) print /x *0x90000000002016a4 

$7 = 0x4c000020 

shell 

shell 

# 查看堆栈信息

可使用以下命令，查看当前调用堆栈:

shell 

```txt
(gdb) bt
#0 initlock (lk=lk@entry=0x9000000000308d10 <uart_tx_lock>, name=name@entry=0x90000
#1 0x900000000200158 in uartinit () at kernel/uart.c:77
#2 0x900000000201f54 in consoleinit () at kernel/console.c:187
#3 0x9000000002000a0 in main () at kernel/main.c:17 
```

可使用以下命令，切换到其他堆栈处。

shell 

```c
(gdb) frame 0
#0 initlock (lk=lk@entry=0x9000000000308d10 <uart_tx_lock>, name=name@entry=0x90000
14 lk->name = name;
(gdb) frame 1
#1 0x9000000000200158 in uartinit () at kernel/uart.c:77
77 initlock(&uart_tx_lock, "uart"); 
```

# 单步执行

gdb 模式下，使用 next 或 step 指令，均可实现程序单步执行。

- next 命令。如何当前行包含函数调用，next 命令会执行整个函数调用，然后停到函数调用后的下一行。该命令不会进入到函数内部，而是直接执行完函数。

- step 命令。如果当前行包含函数调用，step 会进入函数内部，开始调试函数内部的代码。该指令会逐行执行函数内部的指令。

# 设置变量数值

在 gdb 中，可使用命令 “set var variable=expr”，设置变量的值。其中，variable 为变量名称，expr 为变量新的赋值。

以下方代码为例，进行说明。

```c
#include <stdio.h>
int func(void)
{
    int i = 2;
    return i; 
```

C 

```c
}
int main(void)
{
    int a = 0;
    a = func();
    printf("%d\n", a);
    return 0;
} 
```

gdb 调试时，在特定位置设置断点，进行调试:

bash 

```txt
Breakpoint 2, func () at a.c:5
5    int i = 2;
(gdb) n
7    return i;
(gdb) set var i = 8
(gdb) p i
$4 = 8 
```

# 退出正在调试的函数

当单步调试一个函数时，如果不想继续跟踪下去，有两种方式实现退出。

以下方代码为例，进行说明。

C 

```c
#include <stdio.h>
int func(void)
{
    int i = 0;
    i += 2;
    i *= 10;
    return i;
}
int main(void)
{
    int a = 0;
    a = func();
    printf("%d\n", a);
    return 0;
} 
```

方法一，使用 “finish” 命令，函数会继续执行完，并打印返回值。

bash 

```txt
(gdb) n
17 a = func();
(gdb) s
func () at a.c:5
5 int i = 0;
(gdb) n
7 i += 2;
(gdb) fin
find finish
(gdb) finish
Run till exit from #0 func () at a.c:7
0x00000978 in main () at a.c:17
17 a = func();
Value returned is $1 = 20 
```

方法二，使用 “return” 命令，函数不会继续执行下面的语句，而是直接返回。也可以用 “return expression” 命令，指定函数的返回值。

bash 

```txt
(gdb) n
17 a = func();
(gdb) s
func () at a.c:5
5 int i = 0;
(gdb) n
7 i += 2;
(gdb) n
8 i *= 10;
(gdb) return 40
Make func return now? (y or n) y
#0 0x00000978 in main () at a.c:17
17 a = func();
(gdb) n
18 printf("%d\n", a);
(gdb)
40
19 return 0; 
```

# 打印函数堆栈帧信息

gdb 调试时，可使用 “i frame” 或 “info frame” 命令，显示函数堆栈帧信息。

以下方代码为例，进行说明。

C 

```c
#include <stdio.h>
int func(int a, int b)
{
    int c = a * b;
    printf("c is %d\n", c);
}
int main(void)
{
    func(1, 2);
    return 0;
} 
```

使用该命令，gdb 可以输出当前函数堆栈帧的地址，指令寄存器的值，局部变量地址及值等信息，可以对照当前寄存器的值和函数的汇编指令看一下。

console 

```txt
Breakpoint 1, func (a=1, b=2) at a.c:5
5    printf("c is %d\n", c);
(gdb) i frame
Stack level 0, frame at 0x7ffffffffe590:
rip = 0x40054e in func (a.c:5); saved rip = 0x400577
called by frame at 0x7ffffffffe5a0
source language c.
Arglist at 0x7ffffffffe580, args: a=1, b=2
Locals at 0x7ffffffffe580, Previous frame's sp is 0x7ffffffffe590
Saved registers:
rbp at 0x7ffffffffe580, rip at 0x7ffffffffe588
(gdb) 
```

# 配置 GDB 脚本

当gdb启动时，会读取 HOME 目录和当前目录下的的配置文件，执行里面的命令。这个文件通常为“.gdbinit”。

一些 gdb 常用配置，可以直接写到 .gdbinit 文件中。当 gdb 启动时，自动读取文件，进行设置，无需多次手动配置。

# 图形化界面

启动 gdb 时，可通过指定“-tui”参数，或在运行 gdb 过程中，使用“Ctrl+x+a”组合键，进入图形化调试界面。

退出 gdb 模式，可在终端使用以下命令:

bash 

```txt
quit
# or
exit 
```

也可以使用快捷键 Ctrl-d，即可退出 gdb 模式。

# LA32R-NEMU

运行了程序之后就会进入 NEMU 命令行，输入 help 即可查看可使用的命令。

指令支持tlb 命令和 b 命令，使用说明如下：

# 1. tlb指令

输入tlb NUM，可打印当前一共支持多少个TLB表项，即CONFIG_TLB_ENTRIES。

NUM 范围为 [0, CONFIG_TLB_ENTRIES]，当 NUM 不等于 CONFIG_TLB_ENTRIES 时，NUM 作为 index，打印 tlb[NUM]；当 NUM 等于 CONFIG_TLB_ENTRIES 时，会依次打印所有 TLB 表项。

# 2. b指令

输入b pc，在PC处设置一个一次性的断点，之后NEMU会自动开始执行，直到下一条指令在PC时暂停，期间会打印执行过的指令。

# 3. watch指令

输入w expression, expression是一个表达式,可以用来观察每个寄存器的值何时变化,寄存器名称前要加$,如$pc==0x1c000000 $sp!=0x0 在表达式的值变化的时候,NEMU 会打印变化处的 PC。

# QEMU-LA32R

与QEMU-LoongArch基本相同。

Previous page 

U-boot 

Next page 

2k1000LA星云板