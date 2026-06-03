# Application Binary Interface for the LoongArch™ Architecture

Version 2.50 

Copyright © Loongson Technology 2023, 2025. 

# Table of Contents

# Preamble. 3

# Procedure Call Standard for the LoongArch™ Architecture 4

1. Abstract. . 4 

2. Keywords . 4 

3. Version History . 4 

4. Introduction. . 4 

5. Terms and Abbreviations. 4 

6. Processor Architecture 5 

6.1. The registers . 5 

6.1.1. General-purpose registers 5 

6.1.2. Floating-point registers. . 6 

6.2. The memory and the byte order . . 6 

6.3. The base ABI variants. 6 

7. Data Representation 

7.1. Fundamental types . 

7.2. Fundamental types of N-bit integers 

7.3. Structures, arrays and unions . 8 

7.4. Bit-fields. . 8 

7.5. Vectors . 8 

8. Subroutine Calling Sequence. 8 

8.1. The registers . 9 

8.2. The stack 9 

8.3. Passing arguments 9 

8.3.1. Scalars of fundamental types . 10 

8.3.2. Structures. . 10 

8.3.3. Unions . . 11 

8.3.4. Complex floating-points . . 12 

8.3.5. Vectors . 12 

8.3.6. Variadic arguments . . 12 

8.4. Returning 13 

9. Appendix: C data types and machine data types . . 13 

# ELF for the LoongArch™ Architecture 15

1. Abstract. . . . 15 

2. Keywords . . 15 

3. Version History. . . 15 

4. Introduction. . . . 15 

5. Terms and Abbreviations. . 15 

6. ELF Header . . 16 

6.1. e_machine: identifies the machine . . 16 

6.2. e_flags: identifies ABI type and version. . 16 

6.3. EI_CLASS: file class . . 17 

7. Relocations. . 17 

7.1. Relocation types. . 17 

7.2. Variables used in relocation calculation . . 28 

8. Code Models. . 28 

8.1. Normal code model . 28 

8.2. Medium code model . 29 

8.3. Extreme code model. 29 

References . . 29 

# DWARF for the LoongArch™ Architecture 30

1. Abstract. . . 30 

2. Keywords . . 30 

3. Version History . . . 30 

4. Overview . 30 

5. Terms and Abbreviations. . 30 

6. LoongArch-specific DWARF Definitions. . . 30 

6.1. DWARF Register Numbers . 30 

6.2. CFA (Canonical Frame Address) . 30 

6.3. CIE (Common Information Entry). . 31 

6.4. Call frame instructions . 31 

6.5. DWARF expression operations . 31 

References . . 31 

# Preamble

This is the official documentation of the Application Binary Interface for the LoongArch™ Architecture. 

The latest ABI documentation releases are available at https://github.com/loongson/la-abi-specs and are licensed under the Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International (CC BY-NC-ND 4.0) License. 

To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-nd/4.0/ or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA. 

This specification is written in both English and Chinese. In the event of any inconsistency between the same document version in two languages, the Chinese version shall prevail. 

# Procedure Call Standard for the LoongArch™ Architecture

Version 20251210 

Copyright © Loongson Technology 2023, 2025. All rights reserved. 

# 1. Abstract

This document describes the Procedure Call Standard used by the Application Binary Interface (ABI) of the LoongArch Architecture. 

# 2. Keywords

LoongArch, Procedure call, Calling conventions, Data layout 

# 3. Version History

<table><tr><td>Version</td><td>Description</td></tr><tr><td>20230519</td><td>initial version, derived from the original LoongArch ELF psABI document.</td></tr><tr><td>20231103</td><td>revised the parameter passing rules of structures.</td></tr><tr><td>20231219</td><td>added vector arguments passing rules to the base ABI.</td></tr><tr><td>20250521</td><td>added _Float16, __bf16 and _BitInt(N) definitions; fixed descriptive error about GAR for returning values; clarified how unnamed bit-fields occupy space; extended the sign-extension requirement for unsigned integers from the LP64D ABI to the LP64 data model.</td></tr><tr><td>20251210</td><td>revised the calling convention to support 32-bit LoongArch; clarified conditions for integer extension during parameter passing.</td></tr></table>

# 4. Introduction

This document defines the constraints on the program contexts exchanged between the caller and called subroutines or a subroutine and the execution environment. The subroutines following these constraints can be compiled and assembled separately and work together in the same program. The terms "subroutine", "function" and "procedure" may be used interchangeably throughout this document. 

That includes constraints on: 

• The initial program context created by the caller for the callee. 

• The program context when the callee finishes its execution and returns to the caller. 

• How subroutine arguments and return values should be encoded in these program contexts. 

• How certain global states may be accessed and preserved by all subroutines. 

However, this document does not formally define how entities of standard programming languages other than ISO C should be represented in the machine’s program context, and these language bindings should be described separately if needed. 

# 5. Terms and Abbreviations

FP 

Floating-point. 

# GPR

General-purpose register. 

# FPR

Floating-point register. 

# FPU

Floating-point unit, containing the floating-point registers. 

# GAR

General-purpose argument register, belonging to a fixed subset of GPRs. 

# FAR

Floating-point argument register, belonging to a fixed subset of FPRs. 

# GRLEN

The bit-width of a general-purpose register of the current ABI variant. 

# FRLEN

The bit-width of a floating-point register of the current ABI variant 

# 6. Processor Architecture

# 6.1. The registers

All LoongArch machines have 32 general-purpose registers and optionally 32 floating-point registers. Some of these registers may be used for passing arguments and return values between the caller and callee subroutines. 

The bit-width of both general-purpose registers and floating-point registers may be either 32- or 64-bit, depending on whether the machine implements the LA32 or LA64 instruction set, and whether or not do they have a single- or double-precision FPU. 

![image](https://cdn-mineru.openxlab.org.cn/result/2026-06-02/d15978ae-209c-46e3-b677-366582c72d87/ba09c7edf8fc8322001c8f02b19a3d2af6596a8e33f630e3b2b4f0d9d25d4397.jpg)


In the following text, we use the term "temporary register" for referring to caller-saved registers and "static registers" for calleesaved registers. 

# 6.1.1. General-purpose registers


Table 1. General-purpose register convention


<table><tr><td>Name</td><td>Alias</td><td>Meaning</td><td>Preserved across calls</td></tr><tr><td>$r0</td><td>$zero</td><td>Constant zero</td><td>(Constant)</td></tr><tr><td>$r1</td><td>$ra</td><td>Return address</td><td>No</td></tr><tr><td>$r2</td><td>$tp</td><td>Thread pointer</td><td>(Non-allocatable)</td></tr><tr><td>$r3</td><td>$sp</td><td>Stack pointer</td><td>Yes</td></tr><tr><td>$r4 - $r5</td><td>$a0 - $a1</td><td>Argument registers / return value registers</td><td>No</td></tr><tr><td>$r6 - $r11</td><td>$a2 - $a7</td><td>Argument registers</td><td>No</td></tr><tr><td>$r12 - $r20</td><td>$t0 - $t8</td><td>Temporary registers</td><td>No</td></tr><tr><td>$r21</td><td></td><td>Reserved</td><td>(Non-allocatable)</td></tr><tr><td>$r22</td><td>$fp / $s9</td><td>Frame pointer / Static register</td><td>Yes</td></tr><tr><td>$r23 - $r31</td><td>$s0 - $s8</td><td>Static registers</td><td>Yes</td></tr></table>

# 6.1.2. Floating-point registers


Table 2. Floating-point register convention


<table><tr><td>Name</td><td>Alias</td><td>Meaning</td><td>Preserved across calls</td></tr><tr><td>$f0 - $f1</td><td>$fa0 - $fa1</td><td>Argument registers / return value registers</td><td>No</td></tr><tr><td>$f2 - $f7</td><td>$fa2 - $fa7</td><td>Argument registers</td><td>No</td></tr><tr><td>$f8 - $f23</td><td>$ft0 - $ft15</td><td>Temporary registers</td><td>No</td></tr><tr><td>$f24 - $f31</td><td>$fs0 - $fs7</td><td>Static registers</td><td>Yes</td></tr></table>

# 6.2. The memory and the byte order

The memory is byte-addressable for LoongArch machines, and the ordering of the bytes in machinesupported multi-byte data types is little-endian. That is, the least significant byte of a data object is at the lowest byte address the data object occupies in memory. 

The least significant byte of a 32-bit GPR / 64-bit GPR / 32-bit FPR / 64-bit GPR is defined as storing the lowest byte of the data loaded from the memory with one ld.w / ld.d / fld.s / fld.d instruction. This byte order is also respected by other instructions that move typed data across registers such as movgr2fr.d. 

In this document, when referring to a data object (of any type) stored in a register, it is assumed that these objects begin with the least significant byte of the register with no lower-byte paddings. 

# 6.3. The base ABI variants

Depending on the bit-width of the general-purpose registers and the floating-point registers, different ABI variants can be adopted to preserve arguments and return values in the registers as long as it is possible. 


Table 3. Base ABI types


<table><tr><td>Name</td><td>Description</td></tr><tr><td>1p64s</td><td>Uses 64-bit GARs and the stack for passing arguments and return values. Data model is LP64 for programming languages.</td></tr><tr><td>1p64f</td><td>Uses 64-bit GARs, 32-bit FARs and the stack for passing arguments and return values. Data model is LP64 for programming languages.</td></tr><tr><td>1p64d</td><td>Uses 64-bit GARs, 64-bit FARs and the stack for passing arguments and return values. Data model is LP64 for programming languages.</td></tr><tr><td>ilp32s</td><td>Uses 32-bit GARs and the stack for passing arguments and return values. Data model is ILP32 for programming languages.</td></tr><tr><td>ilp32f</td><td>Uses 32-bit GARs, 32-bit FARs and the stack for passing arguments and return values. Data model is ILP32 for programming languages.</td></tr><tr><td>ilp32d</td><td>Uses 32-bit GARs, 64-bit FARs and the stack for passing arguments and return values. Data model is ILP32 for programming languages.</td></tr></table>

Different ABI variants are not expected to be compatible and linking objects in these variants may result in linker errors or run-time failures. 

# 7. Data Representation

This specification defines machine data types that represents ISO C’s scalar, aggregate (structure and array) and union data types, as well as their layout within the program context when passed as arguments and return values of procedures. 

# 7.1. Fundamental types


Table 4. Byte size and byte alignment of the fundamental data (scalar) types


<table><tr><td>Class</td><td>Machine type</td><td>Size (bytes)</td><td>Natural alignment (bytes)</td><td>Note</td></tr><tr><td rowspan="8">Integral</td><td>Unsigned byte</td><td>1</td><td>1</td><td rowspan="2">Character</td></tr><tr><td>Signed byte</td><td>1</td><td>1</td></tr><tr><td>Unsigned half-word</td><td>2</td><td>2</td><td></td></tr><tr><td>Signed half-word</td><td>2</td><td>2</td><td></td></tr><tr><td>Unsigned word</td><td>4</td><td>4</td><td></td></tr><tr><td>Signed word</td><td>4</td><td>4</td><td></td></tr><tr><td>Unsigned double-word</td><td>8</td><td>8</td><td></td></tr><tr><td>Signed double-word</td><td>8</td><td>8</td><td></td></tr><tr><td rowspan="2">Pointer</td><td>32-bit data pointer</td><td>4</td><td>4</td><td></td></tr><tr><td>64-bit data pointer</td><td>8</td><td>8</td><td></td></tr><tr><td rowspan="4">Floating Point</td><td>Half precision (fp16)</td><td>2</td><td>2</td><td>_Float16/__bf16</td></tr><tr><td>Single precision (fp32)</td><td>4</td><td>4</td><td rowspan="3">IEEE 754-2008</td></tr><tr><td>Double precision (fp64)</td><td>8</td><td>8</td></tr><tr><td>Quad-precision (fp128)</td><td>16</td><td>16</td></tr></table>

![image](https://cdn-mineru.openxlab.org.cn/result/2026-06-02/d15978ae-209c-46e3-b677-366582c72d87/af676737455b9a1957f3c23ac8b6f1b9eb605ee423c303bf7bf4d7424237fd58.jpg)


In the following text, the term "integral object" or "integral type" also covers the pointers. 

When passed in registers as subroutine arguments or return values, the unsigned integral objects are zeroextended, and the signed integer data types are sign-extended if the containing register is larger in size. 

One exception to the above rule is that in the LP64 data model, unsigned words, such as those representing unsigned int in C, are stored in general-purpose registers as proper sign extensions of their 32-bit values. 

# 7.2. Fundamental types of N-bit integers

_BitInt(N) (as proposed in ISO/IEC WG14 N2763) is a family of integer types where N specifies the exact number of bits used for its representation. 

• _BitInt(N) objects are stored in little-endian order in memory and are signed by default. 

• For N ≤ 64, a _BitInt(N) object have the same size and alignment of the smallest fundamental integral type that can contain it. The unused high-order bits within this containing type are filled with sign or zero extension of the N-bit value, depending on whether the _BitInt(N) object is signed or unsigned. The _BitInt(N) object propagates its signedness to the containing type and is laid out in a register or memory as an object of this type. 

• For N > 64, _BitInt(N) objects are implemented as structs of 64-bit integer chunks. The number of chunks is the smallest even integer M so that M * 64 ≥ N. These objects are of the same size of the struct 

containing the chunks, but always have 16-byte alignment. If there are unused bits in the highestordered chunk that contains used bits, they are defined as the sign- or zero- extension of the used bits depending on whether the _BitInt(N) object is signed or unsigned. If an entire chunk is unused, its bits are undefined. 

![image](https://cdn-mineru.openxlab.org.cn/result/2026-06-02/d15978ae-209c-46e3-b677-366582c72d87/92ebde12f9f1682255b08ba436daf5bdcce3b8c510201fcfda6ca447e6b08998.jpg)


Currently, the definition of _BitInt(N) is only applicable to 64-bit LoongArch. 

# 7.3. Structures, arrays and unions

The following conventional rules are respected: 

• Structures, arrays and unions assume the alignment of their most strictly aligned components (i.e. with the largest natural alignment). 

• The size of any object is always a multiple of its alignment. Tail paddings are applied to structures and unions if it is necessary to comply with this rule. The state of the padding bytes are not defined. 

• Each member within a structure or an array is consecutively assigned to the lowest available offset with the appropriate alignment, in the order of their definitions. 

Structures and unions may be passed in registers as arguments or return values. The layout rules of their members within the registers are described in the following section. 

# 7.4. Bit-fields

Structures and unions may include bit-fields, which are integral values of a declared integral type with a specified bit-width. The specified bit-width of a bit-field may not be greater than the width of its declared type. 

A bit-field must be contained in a block of memory that is appropriate to store its declared type, but it can share the same addressable byte with adjacent bitfields in the structure. 

When determining the alignment of the structure or the union, only the member bitfields' declared integral types are considered, and their specified widths are irrelevant. 

It is possible to define unnamed bit-fields in C. The declared type of these bit-fields do not affect the alignment of a structure or union but allocate space in the same fashion as named bit-fields. 

# 7.5. Vectors

A vector can be either 128 bits or 256 bits wide and can always be interpreted as an array of multiple elements of the same basic machine type, with each element referred to using an index starting from 0. The lower-indexed elements are located on the lower-ordered bits of the vector. 

# 8. Subroutine Calling Sequence

A subroutine as described in this specification may have none or arbitrary number of arguments and one return value. Each argument or return value have exactly one of the machine data types. 

The standard calling requirements apply only to functions exported to link-editors and dynamic loaders. Local functions that are not reachable from other compilation units may use other calling conventions. 

Empty structure / union arguments and return values should be simply ignored by C compilers which support them as a non-standard extension. 

# 8.1. The registers

The rationale of the LoongArch procedure calling convention is to pass arguments and return values in registers as long as it is possible, so that memory access and/or cache usage can be reduced to improve program performance. 

The registers that can be used for passing arguments and returning values are the argument registers, which include: 

• GARs: 8 general-purpose registers $a0 - $a7, where $a0 and $a1 are also used for returning values. 

• FARs: 8 floating-point registers $fa0 - $fa7, where $fa0 and $fa1 are also used for returning values. 

An argument is passed using the stack only when no appropriate argument register is available. 

Subroutines should ensure that the initial values of the general-purpose registers $s0 - $s9 and floatingpoint registers $fs0 - $fs7 are preserved across the call. 

At the entry of a procedure call, the return address of the call site is stored in $ra. A branch jump to this address should be the last instruction executed in the called procedure. 

# 8.2. The stack

Each called subroutine in a program may have a stack frame on the run-time stack. A stack frame is a contiguous block of memory with the following layout: 

<table><tr><td>Position</td><td>Content</td><td>Frame</td></tr><tr><td>incoming $sp(high address)</td><td>(optional padding)incoming stack arguments</td><td>Previous</td></tr><tr><td></td><td>...saved registerslocal variablespaddings</td><td rowspan="2">Current</td></tr><tr><td>outgoing $sp(low address)</td><td>(optional padding)outgoing stack arguments</td></tr></table>

The stack frame is allocated by subtracting a positive value from the stack pointer $sp. Upon procedure entry, the stack pointer is required to be divisible by 16, ensuring a 16-byte alignment of the frame. 

The first argument object passed on the stack (which may be the argument itself or its on-stack portion) is located at offset 0 of the incoming stack pointer; the following argument objects are stored at the lowest subsequent addresses that meet their respective alignment requirements. 

Procedures must not assume the persistence of on-stack data of which the addresses lie below the stack pointer. 

# 8.3. Passing arguments

When determining the layout of argument data, the arguments should be assigned to their locations in the program context sequentially, in the order they appear in the argument list. 

The location of an argument passed by value may be either one of: 

1. An argument register. 

2. A pair of argument registers with adjacent numbers. 

3. A GAR and an FAR. 

4. A contiguous block of memory in the stack arguments region, with a constant offset from the caller’s outgoing $sp. 

5. A combination of 1. and 4. 

The on-stack part of the structure and scalar arguments are aligned to the greater of the type alignment and GRLEN bits, except when this alignment is larger than the 16-byte stack alignment. In this case, the part of the argument should be 16-byte-aligned. 

In a procedure call, GARs / FARs are generally only used for passing non-floating-point / floating-point argument data, respectively. However, the floating-point member of a structure or union argument, or a vector/floating-point argument wider than FRLEN may be passed in a GAR, specifically: 

• A quadruple-precision floating-point argument may be passed or returned in a pair of GARs if the GARs are 64-bit wide, otherwise it would be passed or returned entirely on the stack. 

• An 128-bit vector may be passed in a pair of GARs with adjacent numbers or the combination of a single GAR and a block of memory on the stack if the GARs are 64-bit wide, otherwise it will be passed or returned entirely on the stack. 

![image](https://cdn-mineru.openxlab.org.cn/result/2026-06-02/d15978ae-209c-46e3-b677-366582c72d87/5a8f60fac16da537c9408078577f6c890999f9b131bcf880a1c5094ffd8238fb.jpg)


In the following text, ${ \bf W } _ { \mathrm { a r g } }$ is used for denoting the size of the argument object in bits. And unless otherwise specified, "passed on the stack" implies "passed by value". 

# 8.3.1. Scalars of fundamental types

An fp16, fp32 or fp64 argument is passed in an FAR if one is available. 

• 0 < warg ≤ FRLEN 

◦ The argument is passed in a single FAR. 

◦ fp16 and fp32 arguments narrower than FRLEN bits are widened to FRLEN bits, with the upper bits undefined. 

Otherwise, the argument is passed in GAR according to the rules described below. 

• 0 < w ≤ GRLEN 

◦ The argument is passed in a single GAR, or on the stack if no register is available. 

◦ A floating-point argument is passed in a GAR, or on the stack if no GAR is available. if its width is narrower than GRLEN bits, it is widened to GRLEN bits, and the upper bits are left undefined. 

◦ An integral argument is passed in a GAR if there is one available. Otherwise, it is passed on the stack. If the argument is narrower than GRLEN bits, the general rules of integral extensions apply. 

• GRLEN < warg ≤ 2 × GRLEN 

◦ The argument is passed in a pair of GARs with adjacent numbers, with the lower-ordered GRLEN bits in the low-numbered register. If only one GAR is available, the lower-ordered GRLEN bits are passed in this register and the higher-ordered GRLEN bits are passed on the stack. If no GAR is available, the whole argument is passed on the stack. 

• w > 2 × GRLEN 

◦ The argument is passed by reference. If a GAR is available, the address is passed in the GAR; otherwise, it is passed on the stack. 

# 8.3.2. Structures

Upon function calls and returns, a structure argument’s storage location is mainly determined by its size and the number of floating-point and/or integer members it contains. A structure argument can be passed in up to two registers if available. 

The storage layout of a structure containing other structures or arrays is the same as its flattened counterpart, where the member structures are replaced by its individual members and member arrays of length n (n > 0) are broken down into n consecutive members of its element type. 

![image](https://cdn-mineru.openxlab.org.cn/result/2026-06-02/d15978ae-209c-46e3-b677-366582c72d87/cf333ddbeb0e535c71630062e58235519f56a0d712ffec86de1cfbcbc784b57f.jpg)


Empty structures or unions are zero-sized in C while they have the size of 1 byte in C++. 

For example, struct { struct { double d[1]; } a[2]; } and struct { double d0; double d1; } should have the same storage layout when passed as function parameters. 

# Structures without floating-point members

• warg > 2 × GRLEN 

◦ The argument is passed by reference, i.e. replaced in the argument list with its memory address. If there is an available GAR, the address is passed in the GAR, otherwise it is passed on the stack. 

• GRLEN < warg ≤ 2 × GRLEN 

◦ The argument is passed in a pair of GARs with adjacent numbers, with the lower-ordered GRLEN bits in the low-numbered register. If only one GAR is available, the lower-ordered GRLEN bits are passed in this register and the higher-ordered GRLEN bits are passed on the stack. If no GAR is available, the whole argument is passed on the stack. 

• 0 < warg ≤ GRLEN 

◦ The argument is passed in a GAR if there is one available with the members laid out as if they were stored in memory. Otherwise, it is passed on the stack. 

• warg = 0 

◦ Zero-sized structure arguments are ignored. 

# Structures with floating-point members

• If the structure consists of one floating-point member within FRLEN bits wide, it is passed in an FAR if available. 

• If the structure consists of two floating-point members both within FRLEN bits wide, it is passed in two FARs if available. 

• If the structure consists of one integer member within GRLEN bits wide and one floating-point member within FRLEN bits wide, it is passed in a GAR and an FAR if available. 

• Additionally, if there are only zero-sized members including structures, arrays or bit-fields, or empty structure in C++, beside the structure members described in the above three rules, these additional members are simply ignored by the compiler, unless the considered additional member is a structure and has a nontrivial destructor or a copy constructor defined in C++. 

![image](https://cdn-mineru.openxlab.org.cn/result/2026-06-02/d15978ae-209c-46e3-b677-366582c72d87/a0ba577d5583c61d4a47a2203490191fd479d7a6625eca1e5590ba5e1fe2f425.jpg)


In the above case, non-zero-length arrays of empty structures in C++ are not ignored by the compiler as additional members in the considered structure. 

• Otherwise, the structure is passed according to the same rule as structures without floating-point members which is described above. 

# 8.3.3. Unions

Unions are only passed in the GARs or on the stack, depending on its size. 

• $\mathbf { W } _ { \mathrm { a r g } } > 2 \times \mathbf { G } \mathbf { R } \mathbf { L } \mathbf { E } \mathbf { N }$ 

◦ The union is passed by reference and is replaced in the argument list with its memory address. If there is an available GAR, the reference is passed in the GAR, otherwise, the address is passed on the stack. 

• GRLEN < warg ≤ 2 × GRLEN 

◦ The argument is passed in a pair of available GARs, with the low-order bits in the lowernumbered GAR and the high-order bits in the higher-numbered GAR. If only one GAR is available, the low-order bits are in the GAR and the high-order bits are on the stack. The arguments are passed on the stack when no GAR is available. 

$\bullet \ 0 < \mathbf { W } _ { \mathrm { a r g } } \leq \mathbf { G } \mathbf { R } \mathbf { L } \mathbf { E } \mathbf { N }$ 

◦ The argument is passed in a GAR, or on the stack if no GAR is available. 

• $\mathbf { W } _ { \mathrm { a r g } } = 0$ 

◦ Zero-sized union arguments are ignored. 

# 8.3.4. Complex floating-points

A complex floating-point number, or a structure containing just one complex fp32 / fp64 number, is passed as though it were a structure containing two fp32 / fp64 members. 

# 8.3.5. Vectors

• 128-bit vector argument 

$\mathrm { ~ \mathrm { ~ \textdegree ~ } ~ } \mathbf { W } _ { \mathrm { a r g } } > 2 \times \mathbf { G } \mathbf { R } \mathbf { L } \mathbf { E } \mathbf { N }$ 

▪ 128-bit vector arguments are passed by reference. If a GAR is available, the address is passed in the GAR; otherwise, it is passed on the stack. 

$\circ \mathrm { ~ \bf ~ 0 ~ } < \bf { W } _ { \mathrm { a r g } } \leq 2 \times \tt G R L E N$ 

▪ An 128-bit vector argument are passed with two GARs with adjacent numbers (if available), with the lower-ordered 64-bit passed in the lower-numbered GAR and the higher-ordered 64-bit passed in the higher-numbered GAR. 

▪ If only one GAR is available when allocating storage for this argument, the lower-ordered 64-bit goes into the GAR and the higher-ordered 64-bit are passed on the stack. 

▪ If no GAR is available, the vector argument is passed entirely on the stack. 

• 256-bit vector argument 

◦ 256-bit vector arguments are always passed by reference. If a GAR is available, the address is passed in the GAR; otherwise, it is passed on the stack. 

Vector members of structure arguments follow the same rules as above. 

# 8.3.6. Variadic arguments

A variadic argument list can appear at the end of a procedure’s argument list, which contains argument objects whose number and types are not statically declared with the procedure itself. 

A variadic argument’s location is also decided using its bit-width. If one of the variadic arguments is passed on the stack, all subsequent arguments should also be passed on the stack. The variadic arguments never occupy the FARs. 

$\bullet _ { \mathrm { \tiny ~ W _ { a r g } } } > 2 \times \mathtt { G R L E N }$ 

◦ The arguments are passed by reference and are replaced in the argument list with the address. If there is an available GAR, the reference is passed in the GAR, and passed on the stack if no GAR is available. 

• GRLEN < warg ≤ 2 × GRLEN 

◦ An argument object in the variadic argument list with 2 × GRLEN alignment and size (e.g. an fp128 object) is passed in a pair of adjacent available GARs of which the first register is evennumbered. If only one GAR is available, the argument is passed on the stack, and this GAR would not be used for passing subsequent argument objects. 

◦ For other types of argument objects, the variadic arguments are passed in a pair of GARs. If only one GAR is available, the low-order bits are in the GAR and the high-order bits are on the stack. 

◦ If no GAR is available, the argument object is passed on the stack by value. 

• 0 < warg ≤ GRLEN 

◦ The variadic arguments are passed in a GAR, or on the stack by value if no GAR is available. 

# 8.4. Returning

In general, $a0 and $a1 are used for returning non-floating-point values, while $fa0 and $fa1 are used for returning floating-point values. 

Values are returned in the same manner the first named argument of the same type would be passed. If such an argument would have been passed by reference, the caller should allocate memory for the return value, and passes the address as an implicit first argument that is stored in $a0. 

# 9. Appendix: C data types and machine data types

![image](https://cdn-mineru.openxlab.org.cn/result/2026-06-02/d15978ae-209c-46e3-b677-366582c72d87/5b0aca8d3595bc56bd103a83dd13697d7d6f3f55beda1ed553723f6773366d04.jpg)


For all base ABI types of LoongArch, the char data type in C is signed by default. 


Table 5. LP64 data model (base ABI types: lp64d lp64f lp64s)


<table><tr><td>Scalar type</td><td>Machine type</td></tr><tr><td>bool / _Bool</td><td>Unsigned byte</td></tr><tr><td>unsigned char / char</td><td>Unsigned / signed byte</td></tr><tr><td>unsigned short / short</td><td>Unsigned / signed half-word</td></tr><tr><td>unsigned int / int</td><td>Unsigned / signed word</td></tr><tr><td>unsigned long / long</td><td>Unsigned / signed double-word</td></tr><tr><td>unsigned long long / long long</td><td>Unsigned / signed double-word</td></tr><tr><td>pointer types</td><td>64-bit data pointer</td></tr><tr><td>_Float16</td><td>Half precision (IEEE754)</td></tr><tr><td>__bf16</td><td>Half precision (bfloat16)</td></tr><tr><td>float</td><td>Single precision (IEEE754)</td></tr><tr><td>double</td><td>Double precision (IEEE754)</td></tr><tr><td>long double</td><td>Quadruple precision (IEEE754)</td></tr><tr><td>unsigned long / long</td><td>Unsigned / signed word</td></tr><tr><td>unsigned long long / long long</td><td>Unsigned / signed double-word</td></tr><tr><td>pointer types</td><td>32-bit data pointer</td></tr><tr><td>_Float16</td><td>Half precision (IEEE754)</td></tr><tr><td>__bf16</td><td>Half precision (bfloat16)</td></tr><tr><td>float</td><td>Single precision (IEEE754)</td></tr><tr><td>double</td><td>Double precision (IEEE754)</td></tr><tr><td>long double</td><td>Quadruple precision (IEEE754)</td></tr></table>


Table 6. ILP32 data model (base ABI types: ilp32d ilp32f ilp32s)


# ELF for the LoongArch™ Architecture

Version 20251210 

Copyright © Loongson Technology 2023, 2025. All rights reserved. 

# 1. Abstract

This document describes the use of the ELF binary file format in the Application Binary Interface (ABI) of the LoongArch Architecture. 

# 2. Keywords

LoongArch, ELF, ABI, SysV gABI, ELF header, Relocations 

# 3. Version History

<table><tr><td>Version</td><td>Description</td></tr><tr><td>20230519</td><td>initial version, derived from the original LoongArch ELF psABI document.</td></tr><tr><td>20231102</td><td>added relocation R_LARCH_CALL36, removed R_LARCH_DELETE / R_LARCH_CFA, and fixed the uleb128 relocation name.</td></tr><tr><td>20231219</td><td>added the Code Models chapter; added TLS DESC relocations; polished the description of relocations.</td></tr><tr><td>20250521</td><td>annotated assembly modifiers and range checking rules for some relocations; typo fix.</td></tr><tr><td>20251210</td><td>added 13 new relocation types (numbers 127 through 139) applicable to 32-bit LoongArch; annotated TLS relocations.</td></tr></table>

# 4. Introduction

This specification provides the processor-specific definitions required by ELF for LoongArch-based systems. 

All common ELF definitions referenced in this section can be found in the latest SysV gABI specification. 

# 5. Terms and Abbreviations

ELF 

Executable and Linking Format 

SysV gABI 

Generic System V Application Binary Interface 

PC 

Program Counter 

GOT 

Global Offset Table 

PLT 

Procedure Linkage Table 

# 6. ELF Header

# 6.1. e_machine: identifies the machine

An object file conforming to this specification must have the value EM_LOONGARCH (258, 0x102). 

# 6.2. e_flags: identifies ABI type and version


Table 7. ABI-related bits in e_flags


<table><tr><td>Bit 31 - 8</td><td>Bit 7 - 6</td><td>Bit 5 - 3</td><td>Bit 2 - 0</td></tr><tr><td>(reserved)</td><td>ABI version</td><td>ABI extension</td><td>Base ABI Modifier</td></tr></table>

The ABI type of an ELF object is uniquely identified by EI_CLASS and e_flags[7:0] in its header. 

Within this combination, EI_CLASS and e_flags[2:0] correspond to the base ABI type, where the expression of C integral and pointer types (data model) is uniquely determined by EI_CLASS value, and e_flags[2:0] represents additional properties of the base ABI type, including the FP calling convention. We refer to e_flags[2:0] as the base ABI modifier. 

As a result, programs in lp64* / ilp32* ABI should only be encoded with ELF64 / ELF32 object files, respectively. 

0x0 0x4 0x5 0x6 0x7 are reserved values for e_flags[2:0]. 


Table 8. Base ABI types


<table><tr><td>Name</td><td>EI_CLASS</td><td>Base ABI Modifier (e_flags[2:0])</td><td>Description</td></tr><tr><td>1p64s</td><td>ELFCLASS64</td><td>0x1</td><td>Uses 64-bit GPRs and the stack for parameter passing. Data model is LP64, where long and pointers are 64-bit while int is 32-bit.</td></tr><tr><td>1p64f</td><td>ELFCLASS64</td><td>0x2</td><td>Uses 64-bit GPRs, 32-bit FPRs and the stack for parameter passing. Data model is LP64, where long and pointers are 64-bit while int is 32-bit.</td></tr><tr><td>1p64d</td><td>ELFCLASS64</td><td>0x3</td><td>Uses 64-bit GPRs, 64-bit FPRs and the stack for parameter passing. Data model is LP64, where long and pointers are 64-bit while int is 32-bit.</td></tr><tr><td>ilp32s</td><td>ELFCLASS32</td><td>0x1</td><td>Uses 32-bit GPRs and the stack for parameter passing. Data model is ILP32, where int, long and pointers are 32-bit.</td></tr><tr><td>ilp32f</td><td>ELFCLASS32</td><td>0x2</td><td>Uses 32-bit GPRs, 32-bit FPRs and the stack for parameter passing. Data model is ILP32, where int, long and pointers are 32-bit.</td></tr><tr><td>ilp32d</td><td>ELFCLASS32</td><td>0x3</td><td>Uses 32-bit GPRs, 64-bit FPRs and the stack for parameter passing. Data model is ILP32, where int, long and pointers are 32-bit.</td></tr></table>

e_flags[5:3] correspond to the ABI extension type. 


Table 9. ABI extension types


<table><tr><td>Name</td><td>e_flags[5:3]</td><td>Description</td></tr><tr><td>base</td><td>0x0</td><td>No extra ABI features.</td></tr><tr><td></td><td>0x1 - 0x7</td><td>(reserved)</td></tr></table>

e_flags[7:6] marks the ABI version of an ELF object. 


Table 10. ABI version


<table><tr><td>ABI version</td><td>Value</td><td>Description</td></tr><tr><td>v0</td><td>0x0</td><td>Stack operands base relocation type.</td></tr><tr><td>v1</td><td>0x1</td><td>Supporting relocation types directly writing to immediate slots. Can be implemented separately without compatibility with v0.</td></tr><tr><td></td><td>0x2 0x3</td><td>Reserved.</td></tr></table>

# 6.3. EI_CLASS: file class


Table 11. ELF file classes


<table><tr><td>EI_CLASS</td><td>Value</td><td>Description</td></tr><tr><td>ELFCLASS32</td><td>1</td><td>ELF32 object file</td></tr><tr><td>ELFCLASS64</td><td>2</td><td>ELF64 object file</td></tr></table>

# 7. Relocations

# 7.1. Relocation types


Table 12. ELF relocation types


<table><tr><td>Enum</td><td>ELF reloc type</td><td>Usage</td><td>Detail</td></tr><tr><td>0</td><td>R_LARCH_NONE</td><td></td><td></td></tr><tr><td>1</td><td>R_LARCH_32</td><td>Runtime address resolving</td><td>*(int32_t *) PC = RtAddr + A</td></tr><tr><td>2</td><td>R_LARCH_64</td><td>Runtime address resolving</td><td>*(int64_t *) PC = RtAddr + A</td></tr><tr><td>3</td><td>R_LARCH_RELATIVE</td><td>Runtime fixup for load-address</td><td>*(void **) PC = B + A</td></tr><tr><td>4</td><td>R_LARCH_COPY</td><td>Runtime memory copy in executable</td><td>memcpy (PC, RtAddr, sizeof (sym))</td></tr><tr><td>5</td><td>R_LARCH_JUMP_SLOT</td><td>Runtime PLT supporting</td><td>implementation-defined</td></tr><tr><td>6</td><td>R_LARCH_TLS_DTPMOD32</td><td>Runtime relocation for TLS-GD</td><td>*(int32_t *) PC = ID of module defining sym</td></tr><tr><td>7</td><td>R_LARCH_TLS_DTPMOD64</td><td>Runtime relocation for TLS-GD</td><td>*(int64_t *) PC = ID of module defining sym</td></tr><tr><td>8</td><td>R_LARCH_TLS_DTPREL32</td><td>Runtime relocation for TLS-GD</td><td>*(int32_t *) PC = DTV-relative offset for sym</td></tr><tr><td>9</td><td>R_LARCH_TLS_DTPREL64</td><td>Runtime relocation for TLS-GD</td><td>*(int64_t *) PC = DTV-relative offset for sym</td></tr><tr><td>10</td><td>R_LARCH_TLS_TPREL32</td><td>Runtime relocation for TLS-IE</td><td>*(int32_t *) PC = T</td></tr><tr><td>11</td><td>R_LARCH_TLS_TPREL64</td><td>Runtime relocation for TLS-IE</td><td>*(int64_t *) PC = T</td></tr><tr><td>12</td><td>R_LARCH_IRELATIVE</td><td>Runtime local indirect function resolving</td><td>*(void **) PC = ((void (*)(*)()) (B + A)) ()</td></tr><tr><td>13</td><td>R_LARCH_TLS_DESC32</td><td>Runtime relocation for TLS descriptors</td><td>*(int32_t *) PC = resolve function pointer, +*(int32_t *) (PC+4) = TLS descriptors argument</td></tr><tr><td>14</td><td>R_LARCH_TLS_DESC64</td><td>Runtime relocation for TLS descriptors</td><td>*(int64_t *) PC = resolve function pointer, +*(int64_t *) (PC+8) = TLS descriptors argument</td></tr><tr><td colspan="4">... Reserved for dynamic linker.</td></tr><tr><td>20</td><td>R_LARCH_MARK_LA</td><td>Mark la.abs</td><td>Load absolute address for static link.</td></tr><tr><td>21</td><td>R_LARCH_MARK_PCREL</td><td>Mark external label branch</td><td>Access PC relative address for static link.</td></tr><tr><td>22</td><td>R_LARCH_SOP_PUSH_PCREL</td><td>Push PC-relative offset</td><td>push (S - PC + A)</td></tr><tr><td>23</td><td>R_LARCH_SOP_PUSH_ABSOLUTE</td><td>Push constant or absolute address</td><td>push (S + A)</td></tr><tr><td>24</td><td>R_LARCH_SOP_PUSH_DUP</td><td>Duplicate stack top</td><td>opr1 = pop (), push (opr1), push (opr1)</td></tr><tr><td>25</td><td>R_LARCH_SOP_PUSH_GPREL</td><td>Push for access GOT entry</td><td>push (G)</td></tr><tr><td>26</td><td>R_LARCH_SOP_PUSH_TLS_TPREL</td><td>Push for TLS-LE</td><td>push (T)</td></tr><tr><td>27</td><td>R_LARCH_SOP_PUSH_TLS_GOT</td><td>Push for TLS-IE</td><td>push (IE)</td></tr><tr><td>28</td><td>R_LARCH_SOP_PUSH_TLS_GD</td><td>Push for TLS-GD</td><td>push (GD)</td></tr><tr><td>29</td><td>R_LARCH_SOP_PUSH_PLT_PCREL</td><td>Push for external function calling</td><td>push (PLT - PC)</td></tr><tr><td>30</td><td>R_LARCH_SOP_ASSERT</td><td>Assert stack top</td><td>assert (pop ())</td></tr><tr><td>31</td><td>R_LARCH_SOP_NOT</td><td>Stack top operation</td><td>push (!pop ())</td></tr><tr><td>32</td><td>R_LARCH_SOP_SUB</td><td>Stack top operation</td><td>opr2 = pop (), opr1 = pop (), push (opr1 - opr2)</td></tr><tr><td>33</td><td>R_LARCH_SOP_SL</td><td>Stack top operation</td><td>opr2 = pop (), opr1 = pop (), push (opr1 &lt;&lt; opr2)</td></tr><tr><td>34</td><td>R_LARCH_SOP_SR</td><td>Stack top operation</td><td>opr2 = pop (), opr1 = pop (), push (opr1 &gt;&gt; opr2)</td></tr><tr><td>35</td><td>R_LARCH_SOP_ADD</td><td>Stack top operation</td><td>opr2 = pop (), opr1 = pop (), push (opr1 + opr2)</td></tr><tr><td>36</td><td>R_LARCH_SOP_AND</td><td>Stack top operation</td><td>opr2 = pop (), opr1 = pop (), push (opr1 &amp; opr2)</td></tr><tr><td>37</td><td>R_LARCH_SOP_IF_ELSE</td><td>Stack top operation</td><td>opr3 = pop (), opr2 = pop (), opr1 = pop (), push (opr1 ? opr2 : opr3)</td></tr><tr><td>38</td><td>R_LARCH_SOP_POP_32_S_10_5</td><td>Instruction imm-field relocation</td><td>opr1 = pop (), (*(uint32_t *) PC) [14 ... 10] = opr1 [4 ... 0]with check 5-bit signed overflow</td></tr><tr><td>39</td><td>R_LARCH_SOP_POP_32_U_10_12</td><td>Instruction imm-field relocation</td><td>opr1 = pop (), (*(uint32_t *) PC) [21 ... 10] = opr1 [11 ... 0]with check 12-bit unsigned overflow</td></tr><tr><td>40</td><td>R_LARCH_SOP_POP_32_S_10_12</td><td>Instruction imm-field relocation</td><td>opr1 = pop (), (*(uint32_t *) PC) [21 ... 10] = opr1 [11 ... 0]with check 12-bit signed overflow</td></tr><tr><td>41</td><td>R_LARCH_SOP_POP_32_S_10_16</td><td>Instruction imm-field relocation</td><td>opr1 = pop (), (*(uint32_t *) PC) [25 ... 10] = opr1 [15 ... 0]with check 16-bit signed overflow</td></tr><tr><td>42</td><td>R_LARCH_SOP_POP_32_S_10_16_S2</td><td>Instruction imm-field relocation</td><td>opr1 = pop (), (*(uint32_t *) PC) [25 ... 10] = opr1 [17 ... 2]with check 18-bit signed overflow and a multiple of 4</td></tr><tr><td>43</td><td>R_LARCH_SOP_POP_32_S_5_20</td><td>Instruction imm-field relocation</td><td>opr1 = pop (), (*(uint32_t *) PC) [24 ... 5] = opr1 [19 ... 0]with check 20-bit signed overflow</td></tr><tr><td>44</td><td>R_LARCH_SOP_POP_32_S_0_5_10_16_S2</td><td>Instruction imm-field relocation</td><td>opr1 = pop (),(*(uint32_t *) PC) [4 ... 0] = opr1 [22 ... 18],(*(uint32_t *) PC) [25 ... 10] = opr1 [17 ... 2]with check 23-bit signed overflow and a multiple of 4</td></tr><tr><td>45</td><td>R_LARCH_SOP_POP_32_S_0_10_10_16_S2</td><td>Instruction imm-field relocation</td><td>opr1 = pop (),(*(uint32_t *) PC) [9 ... 0] = opr1 [27 ... 18],(*(uint32_t *) PC) [25 ... 10] = opr1 [17 ... 2]with check 28-bit signed overflow and a multiple of 4</td></tr><tr><td>46</td><td>R_LARCH_SOP_POP_32_U</td><td>Instruction fixup</td><td>(*(uint32_t *) PC) = pop ()with check 32-bit unsigned overflow</td></tr><tr><td>47</td><td>R_LARCH_ADD8</td><td>8-bit in-place addition</td><td>*(int8_t *) PC += S + A</td></tr><tr><td>48</td><td>R_LARCH_ADD16</td><td>16-bit in-place addition</td><td>*(int16_t *) PC += S + A</td></tr><tr><td>49</td><td>R_LARCH_ADD24</td><td>24-bit in-place addition</td><td>*(int24_t *) PC += S + A</td></tr><tr><td>50</td><td>R_LARCH_ADD32</td><td>32-bit in-place addition</td><td>*(int32_t *) PC += S + A</td></tr><tr><td>51</td><td>R_LARCH_ADD64</td><td>64-bit in-place addition</td><td>*(int64_t *) PC += S + A</td></tr><tr><td>52</td><td>R_LARCH_SUB8</td><td>8-bit in-place subtraction</td><td>*(int8_t *) PC -= S + A</td></tr><tr><td>53</td><td>R_LARCH_SUB16</td><td>16-bit in-place subtraction</td><td>*(int16_t *) PC -= S + A</td></tr><tr><td>54</td><td>R_LARCH_SUB24</td><td>24-bit in-place subtraction</td><td>*(int24_t *) PC -= S + A</td></tr><tr><td>55</td><td>R_LARCH_SUB32</td><td>32-bit in-place subtraction</td><td>*(int32_t *) PC -= S + A</td></tr><tr><td>56</td><td>R_LARCH_SUB64</td><td>64-bit in-place subtraction</td><td>*(int64_t *) PC -= S + A</td></tr><tr><td>57</td><td>R_LARCH_GNU_VTINHERIT</td><td>GNU C++ vtable hierarchy</td><td></td></tr><tr><td>58</td><td>R_LARCH_GNU_VTENTRY</td><td>GNU C++ vtable member usage</td><td></td></tr><tr><td colspan="4">... Reserved</td></tr><tr><td>64</td><td>R_LARCH_B16</td><td>18-bit PC-relative jump, %b16(symbol)</td><td>(*(uint32_t *) PC) [25 ... 10] = (S+A-PC) [17 ... 2]with check 18-bit signed overflow and a multiple of 4</td></tr><tr><td>65</td><td>R_LARCH_B21</td><td>23-bit PC-relative jump,%b21(symbol)</td><td><eq>\left( {\text{ *(uint32\_t *) PC) [4\ldots 0]} \right.</eq><eq>= (S+A-PC) [22\ldots 18]</eq>,<eq>\left( {\text{ *(uint32\_t *) PC) [25\ldots }</eq><eq>10] = (S+A-PC) [17\ldots 2]}</eq>with check 23-bit signed overflow and a multiple of 4</td></tr><tr><td>66</td><td>R_LARCH_B26</td><td>28-bit PC-relative jump,%plt(symbol)</td><td><eq>\left( {\text{ *(uint32\_t *) PC) [9\ldots 0]} \right.</eq><eq>= (S+A-PC) [27\ldots 18]</eq>,<eq>\left( {\text{ *(uint32\_t *) PC) [25\ldots }</eq><eq>10] = (S+A-PC) [17\ldots 2]}</eq>with check 28-bit signed overflow and a multiple of 4</td></tr><tr><td>67</td><td>R_LARCH_ABS_HI20</td><td>[31 ... 12] bits of 32/64-bit absolute address,%abs_hi20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t *) PC) [24\ldots }</eq><eq>5] = (S+A) [31\ldots 12]}</eq></td></tr><tr><td>68</td><td>R_LARCH_ABS_L012</td><td>[11 ... 0] bits of 32/64-bit absolute address,%abs_lo12(symbol)</td><td><eq>\left( {\text{ *(uint32\_t *) PC) [21\ldots }</eq><eq>10] = (S+A) [11\ldots 0]}</eq></td></tr><tr><td>69</td><td>R_LARCH_ABS64_L020</td><td>[51 ... 32] bits of 64-bit absolute address,%abs64_lo20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t *) PC) [24\ldots }</eq><eq>5] = (S+A) [51\ldots 32]}</eq></td></tr><tr><td>70</td><td>R_LARCH_ABS64_HI12</td><td>[63 ... 52] bits of 64-bit absolute address,%abs64_hi12(symbol)</td><td><eq>\left( {\text{ *(uint32\_t *) PC) [21\ldots }</eq><eq>10] = (S+A) [63\ldots 52]}</eq></td></tr><tr><td>71</td><td>R_LARCH_PCALA_HI20</td><td>[31 ... 12] bits of 32/64-bit PC-relative offset,%pc_hi20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t *) PC) [24\ldots }</eq><eq>5] = ((S+A+0x800) \&amp; ^{0xff}) - (PC \&amp; ^{0xff})) [31\ldots 12]}</eq>SeeCode Modelsfor how it works on various code models.</td></tr><tr><td>72</td><td>R_LARCH_PCALA_L012</td><td>[11 ... 0] bits of 32/64-bit address,%pc_lo12(symbol)</td><td><eq>\left( {\text{ *(uint32\_t *) PC) [21\ldots }</eq><eq>10] = (S+A) [11\ldots 0]}</eq>SeeCode Modelsfor how it works on various code models.</td></tr><tr><td>73</td><td>R_LARCH_PCALA64_L020</td><td>[51 ... 32] bits of 64-bit PC-relative offset,%pc64_lo20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t *) PC) [24\ldots }</eq><eq>5] = ((S+A+0x8000'0000 + ((S+A) \&amp; 0x800) ? (0x1000-0x1'0000'0000) : 0)) \&amp; ^{0xff}) - (PC-8 \&amp; ^{0xff})</eq><eq>[51\ldots 32]</eq></td></tr><tr><td>74</td><td>R_LARCH_PCALA64_HI12</td><td>[63 ... 52] bits of 64-bit PC-relative offset,%pc64_hi12(symbol)</td><td><eq>\left( {\text{ *(uint32\_t *) PC) [21\ldots }</eq><eq>10] = ((S+A+0x8000'0000 + ((S+A) \&amp; 0x800) ? (0x1000-0x1'0000'0000) : 0)) \&amp; ^{0xff}) - (PC-12 \&amp; ^{0xff})</eq><eq>[63\ldots 52]</eq></td></tr><tr><td>75</td><td>R_LARCH_GOT_PC_HI20</td><td>[31 ... 12] bits of 32/64-bit PC-relative offset to GOT entry,%got_pc_hi20(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [24 ... 5] = <eq>\left( {\left( \mathrm{{GOT}} + \mathrm{G}\right) \&amp; {}^{\prime }0\mathrm{{xff}}}\right) - \left( {\mathrm{{PC}}\&amp; {}^{\prime }0\mathrm{{xff}}}\right)</eq> [31 ... 12]</td></tr><tr><td>76</td><td>R_LARCH_GOT_PC_L012</td><td>[11 ... 0] bits of 32/64-bit GOT entry address,%got_pc_lo12(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [21 ... 10] = (GOT+G) [11 ... 0]</td></tr><tr><td>77</td><td>R_LARCH_GOT64_PC_L020</td><td>[51 ... 32] bits of 64-bit PC-relative offset to GOT entry,%got64_pc_hi20(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [24 ... 5] = <eq>\left( {\left( \mathrm{{GOT}} + \mathrm{G} + 0\mathrm{x}{8000}^{\prime }{0000} + \right. \left( \left( \mathrm{{GOT}} + \mathrm{G}\right) \&amp; 0\mathrm{x}{800}) ? \right. \left( {0\mathrm{x}{1000} - 0\mathrm{x}{1}^{\prime }{0000}^{\prime }{0000}) : 0}\right)</eq> &amp; ~0xff) - (PC-8 &amp; ~0xff)) [51 ... 32]</td></tr><tr><td>78</td><td>R_LARCH_GOT64_PC_HI12</td><td>[63 ... 52] bits of 64-bit PC-relative offset to GOT entry,%got64_pc_hi12(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [21 ... 10] = <eq>\left( {\left( \mathrm{{GOT}} + \mathrm{G} + 0\mathrm{x}{8000}^{\prime }{0000} + \right. \left( \left( \mathrm{{GOT}} + \mathrm{G}\right) \&amp; 0\mathrm{x}{800}) ? \right. \left( {0\mathrm{x}{1000} - 1\mathrm{x}{1}^{\prime }{0000}^{\prime }{0000}) : 0}\right)</eq> &amp; ~0xff) - (PC-12 &amp; ~0xff)) [63 ... 52]</td></tr><tr><td>79</td><td>R_LARCH_GOT_HI20</td><td>[31 ... 12] bits of 32/64-bit GOT entry absolute address,%got_hi20(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [24 ... 5] = (GOT+G) [31 ... 12]</td></tr><tr><td>80</td><td>R_LARCH_GOT_L012</td><td>[11 ... 0] bits of 32/64-bit GOT entry absolute address,%got_lo12(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [21 ... 10] = (GOT+G) [11 ... 0]</td></tr><tr><td>81</td><td>R_LARCH_GOT64_L020</td><td>[51 ... 32] bits of 64-bit GOT entry absolute address,%got64_lo20(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [24 ... 5] = (GOT+G) [51 ... 32]</td></tr><tr><td>82</td><td>R_LARCH_GOT64_HI12</td><td>[63 ... 52] bits of 64-bit GOT entry absolute address,%got64_hi12(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [21 ... 10] = (GOT+G) [63 ... 52]</td></tr><tr><td>83</td><td>R_LARCH_TLS_LE_HI20</td><td>[31 ... 12] bits of TLS LE 32/64-bit offset from TP register,%le_hi20(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [24 ... 5] = T [31 ... 12]</td></tr><tr><td>84</td><td>R_LARCH_TLS_LE_L012</td><td>[11 ... 0] bits of TLS LE 32/64-bit offset from TP register,%le_lo12(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [21 ... 10] = T [11 ... 0]</td></tr><tr><td>85</td><td>R_LARCH_TLS_LE64_L020</td><td>[51 ... 32] bits of TLS LE 64-bit offset from TP register,%le64_lo20(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [24 ... 5] = T [51 ... 32]</td></tr><tr><td>86</td><td>R_LARCH_TLS_LE64_HI12</td><td>[63 ... 52] bits of TLS LE 64-bit offset from TP register,%le64_hi12(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [21 ... 10] = T [63 ... 52]</td></tr><tr><td>87</td><td>R_LARCH_TLS_IE_PC_HI20</td><td>[31 ... 12] bits of 32/64-bit PC-relative offset to TLS IE GOT entry,%ie_pc_hi20(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \text{PC}</eq> [24 ... 5] = <eq>\left( {\left( \mathrm{{GOT}} + \mathrm{{IE}}\right) \&amp; {}^{\prime }0\mathrm{{xff}}}\right) - \left( {\mathrm{{PC}}\&amp; {}^{\prime }0\mathrm{{xff}}}\right)</eq> [31 ... 12]</td></tr><tr><td>88</td><td>R_LARCH_TLS_IE_PC_L012</td><td>[11 ... 0] bits of 32/64-bit TLS IE GOT entry address,%ie_pc_lo12(symbol)</td><td>(*(uint32_t *) PC) [21 ... 10] = (GOT+IE) [11 ... 0]</td></tr><tr><td>89</td><td>R_LARCH_TLS_IE64_PC_L020</td><td>[51 ... 32] bits of 64-bit PC-relative offset to TLS IE GOT entry, %ie64_pc_lo20(symbol)</td><td>(*(uint32_t *) PC) [24 ... 5] = (((GOT+IE+0x8000'0000 + ((GOT+IE) &amp; 0x800) ? (0x1000-0x1'0000'0000) : 0)) &amp; ~0xff) - (PC-8 &amp; ~0xff)) [51 ... 32]</td></tr><tr><td>90</td><td>R_LARCH_TLS_IE64_PC_HI12</td><td>[63 ... 52] bits of 64-bit PC-relative offset to TLS IE GOT entry, %ie64_pc_hi12(symbol)</td><td>(*(uint32_t *) PC) [21 ... 10] = (((GOT+IE+0x8000'0000 + ((GOT+IE) &amp; 0x800) ? (0x1000-0x1'0000'0000) : 0)) &amp; ~0xff) - (PC-12 &amp; ~0xff)) [63 ... 52]</td></tr><tr><td>91</td><td>R_LARCH_TLS_IE_HI20</td><td>[31 ... 12] bits of 32/64-bit TLS IE GOT entry absolute address,%ie_hi20(symbol)</td><td>(*(uint32_t *) PC) [24 ... 5] = (GOT+IE) [31 ... 12]</td></tr><tr><td>92</td><td>R_LARCH_TLS_IE_L012</td><td>[11 ... 0] bits of 32/64-bit TLS IE GOT entry absolute address,%ie_lo12(symbol)</td><td>(*(uint32_t *) PC) [21 ... 10] = (GOT+IE) [11 ... 0]</td></tr><tr><td>93</td><td>R_LARCH_TLS_IE64_L020</td><td>[51 ... 32] bits of 64-bit TLS IE GOT entry absolute address,%ie64_lo20(symbol)</td><td>(*(uint32_t *) PC) [24 ... 5] = (GOT+IE) [51 ... 32]</td></tr><tr><td>94</td><td>R_LARCH_TLS_IE64_HI12</td><td>[63 ... 52] bits of 64-bit TLS IE GOT entry absolute address,%ie64_hi12(symbol)</td><td>(*(uint32_t *) PC) [21 ... 10] = (GOT+IE) [63 ... 52]</td></tr><tr><td>95</td><td>R_LARCH_TLS_LD_PC_HI20</td><td>[31 ... 12] bits of 32/64-bit PC-relative offset to TLS LD GOT entry, %ld_pc_hi20(symbol)</td><td>(*(uint32_t *) PC) [24 ... 5] = (((GOT+GD) &amp; ~0xff) - (PC &amp; ~0xff)) [31 ... 12]</td></tr><tr><td>96</td><td>R_LARCH_TLS_LD_HI20</td><td>[31 ... 12] bits of 32/64-bit TLS LD GOT entry absolute address,%ld_hi20(symbol)</td><td>(*(uint32_t *) PC) [24 ... 5] = (GOT+GD) [31 ... 12]</td></tr><tr><td>97</td><td>R_LARCH_TLS_GD_PC_HI20</td><td>[31 ... 12] bits of 32/64-bit PC-relative offset to TLS GD GOT entry, %gd_pc_hi20(symbol)</td><td>(*(uint32_t *) PC) [24 ... 5] = (((GOT+GD) &amp; ~0xff) - (PC &amp; ~0xff)) [31 ... 12]</td></tr><tr><td>98</td><td>R_LARCH_TLS_GD_HI20</td><td>[31 ... 12] bits of 32/64-bit TLS GD GOT entry absolute address,%gd_hi20(symbol)</td><td>(*(uint32_t *) PC) [24 ... 5] = (GOT+GD) [31 ... 12]</td></tr><tr><td>99</td><td>R_LARCH_32_PCREL</td><td>32-bit PC relative</td><td>(*(uint32_t *) PC) = (S+A-PC) [31 ... 0]with check 32-bit signed overflow</td></tr><tr><td>100</td><td>R_LARCH_RELAX</td><td>Instruction can be relaxed,paired with a normal relocation at the same address</td><td></td></tr><tr><td>101</td><td>(reserved)</td><td></td><td></td></tr><tr><td>102</td><td>R_LARCH_ALIGN</td><td>Alignment statement. If the symbol index is 0, the addend indicates the number of bytes occupied by nop instructions at the relocation offset. The alignment boundary is specified by the addend rounded up to the next power of two. If the symbol index is not 0, the addend indicates the first and third expressions of .align. The lowest 8 bits are used to represent the first expression, other bits are used to represent the third expression.</td><td></td></tr><tr><td>103</td><td>R_LARCH_PCREL20_S2</td><td>22-bit PC-relative offset,%pcrel_20(symbol)</td><td><eq>\left( *(uint32_t *) PC) [24 ... 5] = (S + A - PC) [21 ... 2] \right.</eq>with check 22-bit signed overflow and a multiple of 4</td></tr><tr><td>104</td><td>(reserved)</td><td></td><td></td></tr><tr><td>105</td><td>R_LARCH_ADD6</td><td>low 6-bit in-place addition</td><td><eq>\left( *(int8_t *) PC) += ((S + A) &amp; 0x3f) \right.</eq></td></tr><tr><td>106</td><td>R_LARCH_SUB6</td><td>low 6-bit in-place subtraction</td><td><eq>\left( *(int8_t *) PC) -= ((S + A) &amp; 0x3f) \right.</eq></td></tr><tr><td>107</td><td>R_LARCH_ADD_ULEB128</td><td>ULEB128 in-place addition</td><td><eq>\left( *(uleb128 *) PC) += S + A \right.</eq></td></tr><tr><td>108</td><td>R_LARCH_SUB_ULEB128</td><td>ULEB128 in-place subtraction</td><td><eq>\left( *(uleb128 *) PC) -= S + A \right.</eq></td></tr><tr><td>109</td><td>R_LARCH_64_PCREL</td><td>64-bit PC relative</td><td><eq>\left( *(uint64_t *) PC) = (S+A-PC) [63 ... 0] \right.</eq></td></tr><tr><td>110</td><td>R_LARCH_CALL36</td><td>Used for medium code model function call sequencepcaddu18i + jirl,%call36(symbol). The two instructions must be adjacent.</td><td><eq>\left( *(uint32_t *) PC) [24 ... 5] = (S+A-PC) [37 ... 18], \right.</eq><eq>\left( *(uint32_t *) (PC+4) \right) [25 ... 10] = (S+A-PC) [17 ... 2]</eq>with check 38-bit signed overflow and a multiple of 4</td></tr><tr><td>111</td><td>R_LARCH_TLS_DESC_PC_HI20</td><td>[31 ... 12] bits of 32/64-bit PC-relative offset to TLS DESC GOT entry,%desc_pc_hi20(symbol)</td><td><eq>\left( *(uint32_t *) PC) [24 ... 5] = ((GOT+GD+0x800) &amp; ^0xff) - (PC &amp; ^0xff) \right) [31 ... 12]</eq></td></tr><tr><td>112</td><td>R_LARCH_TLS_DESC_PC_L012</td><td>[11 ... 0] bits of 32/64-bit TLS DESC GOT entry address,%desc_pc_lo12(symbol)</td><td><eq>\left( *(uint32_t *) PC) [21 ... 10] = (GOT+GD) [11 ... 0] \right.</eq></td></tr><tr><td>113</td><td>R_LARCH_TLS_DESC64_PC_L020</td><td>[51 ... 32] bits of 64-bit PC-relative offset to TLS DESC GOT entry, %desc64_pc_lo20(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \mathrm{{PC}}\left\lbrack {{24}\ldots }\right.</eq>5] = <eq>\left( {\left( {\mathrm{{GOT}} + \mathrm{{GD}} + 0 \times {8000}^{\prime }{0000} + }\right. }\right.</eq> <eq>\left( {\left( {\mathrm{{GOT}} + \mathrm{{GD}}}\right) \&amp; 0 \times {800}}\right) ?</eq> (0x1000-0x1'0000'0000):0)) &amp; ~0xff) - (PC-8 &amp; ~0xff)) [51 ... 32]</td></tr><tr><td>114</td><td>R_LARCH_TLS_DESC64_PC_HI12</td><td>[63 ... 52] bits of 64-bit PC-relative offset to TLS DESC GOT entry, %desc64_pc_hi12(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \mathrm{{PC}}\left\lbrack {{21}\ldots }\right.</eq>10] = <eq>\left( {\left( {\mathrm{{GOT}} + \mathrm{{GD}} + 0 \times {8000}^{\prime }{0000} + }\right. }\right.</eq> <eq>\left( {\left( {\mathrm{{GOT}} + \mathrm{{GD}}}\right) \&amp; 0 \times {800}}\right) ?</eq> (0x1000-0×1'0000'0000):0)) &amp; ~0xff) - (PC-12 &amp; ~0xff)) [63 ... 52]</td></tr><tr><td>115</td><td>R_LARCH_TLS_DESC_HI20</td><td>[31 ... 12] bits of 32/64-bit TLS DESC GOT entry absolute address, %desc_hi20(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \mathrm{{PC}}\left\lbrack {{24}\ldots }\right.</eq>5] = (GOT+GD) [31 ... 12]</td></tr><tr><td>116</td><td>R_LARCH_TLS_DESC_L012</td><td>[11 ... 0] bits of 32/64-bit TLS DESC GOT entry absolute address, %desc_lo12(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \mathrm{{PC}}\left\lbrack {{21}\ldots }\right.</eq>10] = (GOT+GD) [11 ... 0]</td></tr><tr><td>117</td><td>R_LARCH_TLS_DESC64_L020</td><td>[51 ... 32] bits of 64-bit TLS DESC GOT entry absolute address, %desc64_lo20(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \mathrm{{PC}}\left\lbrack {{24}\ldots }\right.</eq>5] = (GOT+GD) [51 ... 32]</td></tr><tr><td>118</td><td>R_LARCH_TLS_DESC64_HI12</td><td>[63 ... 52] bits of 64-bit TLS DESC GOT entry absolute address, %desc64_hi12(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \mathrm{{PC}}\left\lbrack {{21}\ldots }\right.</eq>10] = (GOT+GD) [63 ... 52]</td></tr><tr><td>119</td><td>R_LARCH_TLS_DESC_LD</td><td>Used on ld.[wd] for TLS DESC to get the resolve function address from GOT entry, %desc_ld(symbol)</td><td></td></tr><tr><td>120</td><td>R_LARCH_TLS_DESC_CALL</td><td>Used on jirl for TLS DESC to call the resolve function, %desc_call(symbol)</td><td></td></tr><tr><td>121</td><td>R_LARCH_TLS_LE_HI20_R</td><td>[31 ... 12] bits of TLS LE 32/64-bit offset from TP register, can be relaxed, %le_hi20_r(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \mathrm{{PC}}\left\lbrack {{24}\ldots }\right.</eq>5] = (T+0x800) [31 ... 12]</td></tr><tr><td>122</td><td>R_LARCH_TLS_LE_ADD_R</td><td>TLS LE thread pointer usage, can be relaxed, %le_add_r(symbol)</td><td></td></tr><tr><td>123</td><td>R_LARCH_TLS_LE_L012_R</td><td>[11 ... 0] bits of TLS LE 32/64-bit offset from TP register, sign-extended, can be relaxed, %le_lo12_r(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \mathrm{{PC}}\left\lbrack {{21}\ldots }\right.</eq>10] = T [11 ... 0]</td></tr><tr><td>124</td><td>R_LARCH_TLS_LD_PCREL20_S2</td><td>22-bit PC-relative offset to TLS LD GOT entry, %ld_pcrel_20(symbol)</td><td><eq>\left( {\text{uint32}_t * }\right) \mathrm{{PC}}\left\lbrack {{24}\ldots }\right.</eq>5] = (GOT+GD) [21 ... 2]with check 22-bit signed overflow and a multiple of 4</td></tr><tr><td>125</td><td>R_LARCH_TLS_GD_PCREL20_S2</td><td>22-bit PC-relative offset to TLS GD GOT entry,%gd_pcrel_20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC }}\right) \left\lbrack {{24}\ldots }\right.</eq>5] = (GOT+GD) [21 ... 2]with check 22-bit signed overflow and a multiple of 4</td></tr><tr><td>126</td><td>R_LARCH_TLS_DESC_PCREL20_S2</td><td>22-bit PC-relative offset to TLS DESC GOT entry,%desc_pcrel_20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC }}\right) \left\lbrack {{24}\ldots }\right.</eq>5] = (GOT+GD) [21 ... 2]with check 22-bit signed overflow and a multiple of 4</td></tr><tr><td>127</td><td>R_LARCH_CALL30</td><td>Used for calling a function in medium code model on LA32 with pcaddu12i + jirl,%call30(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC }}\right) \left\lbrack {{24}\ldots }\right.</eq>5] = (S+A-PC) [31 ... 12],<eq>\left( {\text{ *(uint32\_t * ) (PC+4)}}\right) \left\lbrack {{19}}\right.</eq> ... 10] = (S+A-PC) [11 ... 2]</td></tr><tr><td>128</td><td>R_LARCH_PCADD_HI20</td><td>[31 ... 12] bits of 32 PC-relative offset, suitable for pcaddu12i instead of pcalau12i,%pcadd_hi20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC }}\right) \left\lbrack {{24}\ldots }\right.</eq>5] = (S+A-PC+0x800) [31 ... 12],</td></tr><tr><td>129</td><td>R_LARCH_PCADD_L012</td><td>[11 ... 0] bits of 32-bit PC- relative offset to the target of an R_LARCH_PCADD_HI20 reloc filling the immediate slot for a pcaddu12i instruction at S,%pcadd_lo12(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC }}\right) \left\lbrack {{21}\ldots }\right.</eq>10] = (R(S)-S) [11 ... 0],</td></tr><tr><td>130</td><td>R_LARCH_GOT_PCADD_HI20</td><td>[31 ... 12] bits of 32-bit PC- relative offset to GOT entry,suitable for pcaddu12i instead of pcalau12i,%got_pcadd_hi20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC }}\right) \left\lbrack {{24}\ldots }\right.</eq>5] = (GOT+G-PC+0x800) [31 ... 12],</td></tr><tr><td>131</td><td>R_LARCH_GOT_PCADD_L012</td><td>[11 ... 0] bits of 32-bit PC- relative offset to the target of an R_LARCH_GOT_PCADD_HI20 reloc filling the immediate slot for a pcaddu12i instruction at S,%got_pcadd_lo12(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC }}\right) \left\lbrack {{21}\ldots }\right.</eq>10] = (R(S)-S) [11 ... 0],</td></tr><tr><td>132</td><td>R_LARCH_TLS_IE_PCADD_HI20</td><td>[31 ... 12] bits of 32-bit PC- relative offset to TLS IE GOT entry, suitable for pcaddu12i instead of pcalau12i,%ie_pcadd_hi20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC }}\right) \left\lbrack {{24}\ldots }\right.</eq>5] = (GOT+IE-PC+0x800) [31 ... 12],</td></tr><tr><td>133</td><td>R_LARCH_TLS_IE_PCADD_LO12</td><td>[11 ... 0] bits of 32-bit PC-relative offset to the target of an R_LARCH_TLS_IE_PCADD_HI20reloc filling the immediate slot for a pcaddu12i instruction at S,%ie_pcadd_lo12(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC) [21\ldots }\right. }</eq><eq>{10}] = (R(S)-S) [11\ldots 0],</eq></td></tr><tr><td>134</td><td>R_LARCH_TLS_LD_PCADD_HI20</td><td>[31 ... 12] bits of 32-bit PC-relative offset to TLS LD GOT entry, suitable for pcaddu12i instead of pcalau12i,%ld_pcadd_hi20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC) [24\ldots }\right. }</eq><eq>5] = (GOT+GD-PC+0x800) [31\ldots 12],</eq></td></tr><tr><td>135</td><td>R_LARCH_TLS_LD_PCADD_LO12</td><td>[11 ... 0] bits of 32-bit PC-relative offset to the target of an R_LARCH_TLS_LD_PCADD_HI20reloc filling the immediate slot for a pcaddu12i instruction at S,%ld_pcadd_lo12(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC) [21\ldots }\right. }</eq><eq>{10}] = (R(S)-S) [11\ldots 0],</eq></td></tr><tr><td>136</td><td>R_LARCH_TLS_GD_PCADD_HI20</td><td>[31 ... 12] bits of 32-bit PC-relative offset to TLS GD GOT entry, suitable for pcaddu12i instead of pcalau12i,%gd_pcadd_hi20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC) [24\ldots }\right. }</eq><eq>5] = (GOT+GD-PC+0x800) [31\ldots 12],</eq></td></tr><tr><td>137</td><td>R_LARCH_TLS_GD_PCADD_LO12</td><td>[11 ... 0] bits of 32-bit PC-relative offset to the target of an R_LARCH_TLS_GD_PCADD_HI20reloc filling the immediate slot for a pcaddu12i instruction at S,%gd_pcadd_lo12(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC) [21\ldots }\right. }</eq><eq>{10}] = (R(S)-S) [11\ldots 0],</eq></td></tr><tr><td>138</td><td>R_LARCH_TLS_DESC_PCADD_HI20</td><td>[31 ... 12] bits of 32-bit PC-relative offset to TLS descriptor, suitable for pcaddu12i instead of pcalau12i,%desc_pcadd_hi20(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC) [24\ldots }\right. }</eq><eq>5] = (GOT+GD-PC+0x800) [31\ldots 12],</eq></td></tr><tr><td>139</td><td>R_LARCH_TLS_DESC_PCADD_LO12</td><td>[11 ... 0] bits of 32-bit PC-relative offset to the target of anR_LARCH_TLS_DESC_PCADD_HI20reloc filling the immediate slot for a pcaddu12i instruction at S,%desc_pcadd_lo12(symbol)</td><td><eq>\left( {\text{ *(uint32\_t * ) PC) [21\ldots }\right. }</eq><eq>{10}] = (R(S)-S) [11\ldots 0],</eq></td></tr></table>

![image](https://cdn-mineru.openxlab.org.cn/result/2026-06-02/d15978ae-209c-46e3-b677-366582c72d87/5535fb5cc9c770b256e2eefd0142a110f2490e938f9a67ed2a69e4786683dec5.jpg)


To support linker relaxation, symbols can’t be changed to [section + offset] format. This change can reduce the number of symbols. 

![image](https://cdn-mineru.openxlab.org.cn/result/2026-06-02/d15978ae-209c-46e3-b677-366582c72d87/f8965cb446eedc730dc07ad72e47c70b194fa5be7ff9f87211504997d8b1d9d8.jpg)


We use general dynamic relocation types to represent the local dynamic TLS model. Linkers handle TLSLD relocations as synonyms for TLSGD, except that TLSLD only requires one dynamic relocation while TLSGD requires two dynamic relocations. 

# 7.2. Variables used in relocation calculation


Table 13. Variables used in relocation calculation


<table><tr><td>Variable</td><td>Description</td></tr><tr><td>RtAddr</td><td>Runtime address of the symbol in the relocation entry</td></tr><tr><td>PC</td><td>The address of the instruction to be relocated</td></tr><tr><td>B</td><td>Base address of an object loaded into the memory</td></tr><tr><td>S</td><td>The address of the symbol in the relocation entry</td></tr><tr><td>A</td><td>Addend field in the relocation entry associated with the symbol</td></tr><tr><td>GOT</td><td>The address of GOT (Global Offset Table)</td></tr><tr><td>G</td><td>GOT-relative offset of the GOT entry of a symbol. For tls LD/GD symbols, G is always equal to GD.</td></tr><tr><td>T</td><td>TP-relative offset of a TLS LE/IE symbols</td></tr><tr><td>IE</td><td>GOT-relative offset of the GOT entry of a TLS IE symbol</td></tr><tr><td>GD</td><td>GOT-relative offset of the GOT entry of a TLS LD/GD/DESC symbol. If a symbol is referenced by IE, GD/LD and DESC simultaneously, this symbol has five GOT entries. The first two are for GD/LD; the next two are for DESC; the last one is for IE.</td></tr><tr><td>PLT</td><td>The address of PLT entry of a function symbol</td></tr><tr><td>R(S)</td><td>The target (it may be a symbol, a GOT entry for a symbol) of an R_LARCH_*PCADD_HI20 relocation filling the immediate slot for a pcaddu12i instruction at S</td></tr></table>

# 8. Code Models

As a RISC architecture, LoongArch is limited in the range of memory addresses that can be encoded and accessed with a single instruction. Several code models are defined as schemes to implement memory accesses in different circumstances with sequences of instructions of necessary addressing capabilities and performance costs. 

Generally speaking, wider addressing range requires more instructions and brings higher overhead. The performance and size of an application can benefit from a code model that does not overestimate the memory space accessed by the code. 

# 8.1. Normal code model

The normal code model allows the code to address a 4GiB PC-relative memory space [(PC & ~0xfff)-2GiB-0x800, (PC & ~0xfff)+2GiB-0x800) for data accesses and 256MiB PC-relative addressing space [PC-128MiB, PC+128MiB-4] for function calls. This is the default code model. 

The following example shows how to load value from a global 32-bit integer variable g1 in this code model: 

```asm
00: pcalau12i $t0, %pc_hi20(g1)
0: R_LARCH_PCALA_HI20 g1
04: ld.w $a0, $t0, %pc_lo12(g1)
4: R_LARCH_PCALA_LO12 g1 
```

The following example shows how to make function calls in this code model: 

```txt
00: bl %plt(puts) 
```

# 8.2. Medium code model

For data accesses, the medium code model behaves the same as the normal code model. For function calls, this code model allows the code to address a 256GiB PC-relative memory space [PC-128GiB-0x20000, PC+128GiB-0x20000-4]. 

The following example shows how to make a function call to foo in this code model: 

```perl
00: pcaddu18i $ra, %call36(foo)
0: R_LARCH_CALL36 foo
04: jirl $ra, $ra, 0 
```

# 8.3. Extreme code model

The extreme code model uses sequence pcalau12i + addi.d + lu32i.d + lu52i.d followed by {ld,st}x.[bhwd] or {add,ldx}.d + jirl to address the full 64-bit memory space for data accesses and function calls, respectively. 

![image](https://cdn-mineru.openxlab.org.cn/result/2026-06-02/d15978ae-209c-46e3-b677-366582c72d87/91d8fab8987f59198d34cdfe08fe8f31ac6701ec60326e41ce8ec74d5b1c06d2.jpg)


Instructions pcalau12i, addi.d, lu32i.d and lu52i.d must be adjacent so that the linker can infer the PC of pcalau12i to apply relocations to lu32i.d and lu52i.d. Otherwise, the results would be incorrect if these four instructions are not in the same 4KiB page. 

The following example shows how to load a value from a global 32-bit integer variable g2 in this code model: 

```asm
00: pcalau12i $t1, %pc_hi20(g2)
0: R_LARCH_PCALA_HI20 g2
04: addi.d $t0, $zero, %pc_lo12(g2)
4: R_LARCH_PCALA_L012 g2
08: lu32i.d $t0, %pc64_lo20(g2)
8: R_LARCH_PCALA64_L020 g2
0c: lu52i.d $t0, $t0, %pc64_hi12(g2)
c: R_LARCH_PCALA64_HI12 g2
10: ldx.w $a0, $t1, $t0 
```

The following example shows how to make a call to function bar in this code model: 

```asm
00: pcalau12i $t1, %pc_hi20(bar)
0: R_LARCH_PCALA_HI20 bar
04: addi.d $t0, $zero, %pc_lo12(bar)
4: R_LARCH_PCALA_L012 bar
08: lu32i.d $t0, %pc64_lo20(bar)
8: R_LARCH_PCALA64_L020 bar
0c: lu52i.d $t0, $t0, %pc64_hi12(bar)
c: R_LARCH_PCALA64_HI12 bar
10: add.d $t0, $t0, $t1
14: jirl $ra, $t0, 0 
```

# References

▪ [SysVelf] System V Application Binary Interface - DRAFT, 10 Jun. 2013, http://www.sco.com/developers/ gabi/latest/contents.html 

# DWARF for the LoongArch™ Architecture

Version 20230425 

Copyright © Loongson Technology 2023. All rights reserved. 

# 1. Abstract

This document describes the use of the DWARF debugging information format in the Application Binary Interface (ABI) for the LoongArch architecture. 

# 2. Keywords

LoongArch, DWARF, Stack frame, CFA, CIE 

# 3. Version History

<table><tr><td>Version</td><td>Description</td></tr><tr><td>20230425</td><td>initial version.</td></tr></table>

# 4. Overview

The DWARF debugging format for LoongArch uses DWARF Standard [dwarfstd]. This specification only describes LoongArch-specific definitions. 

# 5. Terms and Abbreviations

# DWARF

Debugging With Attributed Record Formats. 

# 6. LoongArch-specific DWARF Definitions

# 6.1. DWARF Register Numbers

DWARF Standard suggests that the mapping from a DWARF register name to a target register number should be defined by the ABI for the target architecture. DWARF register names are encoded as unsigned LEB128 integers. 

The table below lists the mapping from DWARF register numbers to LoongArch64 registers. 


Table 14. Mapping from DWARF register numbers to LoongArch64 registers


<table><tr><td>DWARF Register Number</td><td>LoongArch64 Register Name</td><td>Description</td></tr><tr><td>0 - 31</td><td><eq>r0 - r31</eq></td><td>General-purpose Register</td></tr><tr><td>32 - 63</td><td><eq>f0 - f31</eq></td><td>Floating-point Register</td></tr><tr><td>64 -</td><td></td><td>Reserved for future standard extensions</td></tr></table>

# 6.2. CFA (Canonical Frame Address)

The term Canonical Frame Address (CFA) is defined in DWARF Debugging Information Format Version 5 

[dwarf5], §6.4 Call Frame Information. 

This ABI adopts the typical definition of CFA given there: 

the CFA is defined to be the value of the stack pointer at the call site in the previous frame (which may be different from its value on entry to the current frame). 

The position of CFA in frame structure of LoongArch is shown below: 

![image](https://cdn-mineru.openxlab.org.cn/result/2026-06-02/d15978ae-209c-46e3-b677-366582c72d87/8ad4b5cb643a94437dfa264f4574ed141c7faa1476b3fcb1c72bb11b73fc0f59.jpg)


# 6.3. CIE (Common Information Entry)

The $r1 register is used to store the return address of the function, and the value of the return address register field in CIE structure is 1. 

The default CFA register at the function entry is $r3, and initial_instructions field in CIE structure can define 3 as the default CFA register. 

# 6.4. Call frame instructions

Using the existing definitions in DWARF Standard. 

# 6.5. DWARF expression operations

Using the existing definitions in DWARF Standard. 

# References

▪ [dwarfstd] DWARF Standard, https://dwarfstd.org/ 

▪ [dwarf5] DWARF Debugging Information Format Version 5, https://dwarfstd.org/doc/DWARF5.pdf 