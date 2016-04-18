# Instruction implemtation list
*13302010017 Guo Liangchen*

---

In my pipeline, the operation of an instruction is determind by the `instFlags` defined in `machine.def`, and ALU \& control information defined in `pipe.def`.

The instructions in `pipe.def` are illustrated below.

Instruction | ALU Source 1 | ALU Source 2 | ALU Operation | Reg destenation | Branch
------------|--------------|--------------|---------------|-----------------|--------
`nop` | 0 | 0 | add | null | null
`syscall` | 0 | 0 | add | null | null
`lui` | 0 | imm | add | out1 | null
`add` | in1 | in2 | add | out1 | null
`addu` | in1 | in2 | add | out1 | null
`addi` | in1 | imm | add | out1 | null
`addiu` | in1 | imm | add | out1 | null
`andi` | in1 | imm | and | out1 | null
`slti` | in1 | imm | slt | out1 | null
`sll` | in1 | shamt | shtl | out1 | null
`sw` | in2 | imm | add | null | null
`lw` | in2 | imm | add | out1 | null
`bne` | in1 | in2 | sub | null | on not zero
`j` | in1 | imm | add | null | unconditional
