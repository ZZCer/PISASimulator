#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* An implementation of 5-stage classic pipeline simulation */

/* don't count instructions flag, enabled by default, disable for inst count */
#undef NO_INSN_COUNT

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "regs.h"
#include "memory.h"
#include "loader.h"
#include "syscall.h"
#include "dlite.h"
#include "sim.h"
#include "sim-pipe.h"

/* Logging file */
FILE *log_file;

/* simulated registers */
static struct regs_t regs;

/* simulated memory */
static struct mem_t *mem = NULL;

/* register simulator-specific options */
void
sim_reg_options(struct opt_odb_t *odb)
{
  opt_reg_header(odb,
"sim-pipe: This simulator implements based on sim-fast.\n"
		 );
}

/* check simulator-specific option values */
void
sim_check_options(struct opt_odb_t *odb, int argc, char **argv)
{
  if (dlite_active)
    fatal("sim-pipe does not support DLite debugging");
}

/* register simulator-specific statistics */
void
sim_reg_stats(struct stat_sdb_t *sdb)
{
#ifndef NO_INSN_COUNT
  stat_reg_counter(sdb, "sim_num_insn",
		   "total number of instructions executed",
		   &sim_num_insn, sim_num_insn, NULL);
#endif /* !NO_INSN_COUNT */
  stat_reg_int(sdb, "sim_elapsed_time",
	       "total simulation time in seconds",
	       &sim_elapsed_time, 0, NULL);
#ifndef NO_INSN_COUNT
  stat_reg_formula(sdb, "sim_inst_rate",
		   "simulation speed (in insts/sec)",
		   "sim_num_insn / sim_elapsed_time", NULL);
#endif /* !NO_INSN_COUNT */
  ld_reg_stats(sdb);
  mem_reg_stats(mem, sdb);
}

// Buf for temp use
struct ifid_buf fd;
struct idex_buf de;
struct exmem_buf em;
struct memwb_buf mw;

// Buf for simulating actual CPU
struct cpu_stages_buf buf;

// Stage signals
fetch_sgn_t fetch_sgn;
decode_sgn_t decode_sgn;

#define DNA			(0)

/* general register dependence decoders */
#define DGPR(N)			(N)
#define DGPR_D(N)		((N) &~1)

/* floating point register dependence decoders */
#define DFPR_L(N)		(((N)+32)&~1)
#define DFPR_F(N)		(((N)+32)&~1)
#define DFPR_D(N)		(((N)+32)&~1)

/* miscellaneous register dependence decoders */
#define DHI			(0+32+32)
#define DLO			(1+32+32)
#define DFCC		(2+32+32)
#define DTMP		(3+32+32)

/* initialize the simulator */
void
sim_init(void)
{
  /* allocate and initialize register file */
  regs_init(&regs);

  /* allocate and initialize memory space */
  mem = mem_create("mem");
  mem_init(mem);

  // Initiate empty stage
  EMPTY_DE_BUF.inst.a = NOP;
  EMPTY_FD_BUF.inst.a = NOP;
  buf.fd.inst.a = NOP;
  buf.de.inst.a = NOP;
  buf.em.inst.a = NOP;
  buf.mw.inst.a = NOP;

  /* Initiate log */
  log_file = fopen("trace-withstall.txt", "w+");
}

/* load program into simulated state */
void
sim_load_prog(char *fname,		/* program to load */
	      int argc, char **argv,	/* program arguments */
	      char **envp)		/* program environment */
{
  /* load program text and data, set up environment, memory, and regs */
  ld_load_prog(fname, argc, argv, envp, &regs, mem, TRUE);
}

/* print simulator-specific configuration information */
void
sim_aux_config(FILE *stream)
{
	/* nothing currently */
}

/* dump simulator-specific auxiliary simulator statistics */
void
sim_aux_stats(FILE *stream)
{  /* nada */}

/* un-initialize simulator-specific state */
void
sim_uninit(void)
{ /* nada */ }


/*
 * configure the execution engine
 */

/* next program counter */
#define SET_NPC(EXPR)		(regs.regs_NPC = (EXPR))

/* current program counter */
#define CPC			(regs.regs_PC)

/* general purpose registers */
#define GPR(N)			(regs.regs_R[N])
#define SET_GPR(N,EXPR)		(regs.regs_R[N] = (EXPR))
#define DECLARE_FAULT(EXP) 	{;}
#if defined(TARGET_PISA)

/* floating point registers, L->word, F->single-prec, D->double-prec */
#define FPR_L(N)		(regs.regs_F.l[(N)])
#define SET_FPR_L(N,EXPR)	(regs.regs_F.l[(N)] = (EXPR))
#define FPR_F(N)		(regs.regs_F.f[(N)])
#define SET_FPR_F(N,EXPR)	(regs.regs_F.f[(N)] = (EXPR))
#define FPR_D(N)		(regs.regs_F.d[(N) >> 1])
#define SET_FPR_D(N,EXPR)	(regs.regs_F.d[(N) >> 1] = (EXPR))

/* miscellaneous register accessors */
#define SET_HI(EXPR)		(regs.regs_C.hi = (EXPR))
#define HI			(regs.regs_C.hi)
#define SET_LO(EXPR)		(regs.regs_C.lo = (EXPR))
#define LO			(regs.regs_C.lo)
#define FCC			(regs.regs_C.fcc)
#define SET_FCC(EXPR)		(regs.regs_C.fcc = (EXPR))

#endif

/* precise architected memory state accessor macros */
#define READ_BYTE(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_BYTE(mem, (SRC)))
#define READ_HALF(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_HALF(mem, (SRC)))
#define READ_WORD(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_WORD(mem, (SRC)))
#ifdef HOST_HAS_QWORD
#define READ_QWORD(SRC, FAULT)						\
  ((FAULT) = md_fault_none, MEM_READ_QWORD(mem, (SRC)))
#endif /* HOST_HAS_QWORD */

#define WRITE_BYTE(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_BYTE(mem, (DST), (SRC)))
#define WRITE_HALF(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_HALF(mem, (DST), (SRC)))
#define WRITE_WORD(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_WORD(mem, (DST), (SRC)))
#ifdef HOST_HAS_QWORD
#define WRITE_QWORD(SRC, DST, FAULT)					\
  ((FAULT) = md_fault_none, MEM_WRITE_QWORD(mem, (DST), (SRC)))
#endif /* HOST_HAS_QWORD */

/* system call handler macro */
#define SYSCALL(INST)	sys_syscall(&regs, mem_access, mem, INST, TRUE)

#ifndef NO_INSN_COUNT
#define INC_INSN_CTR()	sim_num_insn++
#else /* !NO_INSN_COUNT */
#define INC_INSN_CTR()	/* nada */
#endif /* NO_INSN_COUNT */


/* start simulation, program loaded, processor precise state initialized */
void
sim_main(void)
{
  fprintf(stderr, "sim: ** starting *pipe* functional simulation **\n");

  /* must have natural byte/word ordering */
  if (sim_swap_bytes || sim_swap_words)
    fatal("sim: *pipe* functional simulation cannot swap bytes or words");

  /* set up initial default next PC */
  /* maintain $r0 semantics */
  regs.regs_R[MD_REG_ZERO] = 0;
  regs.regs_PC -= sizeof(md_inst_t);
  while (TRUE)
  {
	   do_if();
     do_wb();
     do_id();
     do_ex();
     do_mem();
     print_env();
     write_buf();
     #ifndef NO_INSN_COUNT
           sim_num_insn++;
     #endif /* !NO_INSN_COUNT */
  }
}

void print_env() {
  enum md_fault_type _fault;

  fprintf(log_file,
     "[Cycle %lld]---------------------------------------", sim_num_insn);
  fprintf(log_file, "\n\t[IF]\t");
  md_print_insn(fd.inst, fd.PC, log_file);
  fprintf(log_file, "\n\t[ID]\t");
  md_print_insn(de.inst, 0, log_file);
  fprintf(log_file, "\n\t[EX]\t");
  md_print_insn(em.inst, 0, log_file);
  fprintf(log_file, "\n\t[MEM]\t");
  md_print_insn(mw.inst, 0, log_file);
  fprintf(log_file, "\n\t[WB]\t");
  md_print_insn(buf.mw.inst, 0, log_file);
  fprintf(log_file, "\n\t[REGS]\t");
  fprintf(log_file, "r[2]=%d r[3]=%d r[4]=%d r[5]=%d r[6]=%d mem=%d\n",
          GPR(2), GPR(3), GPR(4), GPR(5), GPR(6), READ_WORD(GPR(30)+16, _fault));
  if (_fault != md_fault_none)
    DECLARE_FAULT(_fault);
}

void do_if()
{
  md_inst_t instruction;
  if (buf.em.needJump) {
    fd.PC = buf.em.NPC;
  } else if (!fetch_sgn.bubble_for_decode && !fetch_sgn.stall_for_execute
        && !fetch_sgn.stall_for_mem && !fetch_sgn.bubble_for_execute) {
    fd.PC = regs.regs_PC + sizeof(md_inst_t);
  }
  regs.regs_PC = fd.PC;
  fd.NPC = fd.PC + sizeof(md_inst_t);
  regs.regs_NPC = fd.NPC;
  MD_FETCH_INSTI(instruction, mem, fd.PC);
  fd.inst = instruction;

}

void do_id()
{
    de.inst = buf.fd.inst;
    MD_SET_OPCODE(de.opcode, de.inst);
    de.NPC = buf.fd.NPC;
    md_inst_t inst = de.inst;

    switch (de.opcode) {
#define DEFINST(OP,MSK,NAME,OPFORM,RES,FLAGS,O1,O2,I1,I2,I3)\
  case OP:\
    de.instFlags = FLAGS;\
    de.oprand.out1 = O1;\
    de.oprand.out2 = O2;\
    de.oprand.in1 = I1;\
    de.oprand.in2 = I2;\
    de.oprand.in3 = I3;\
    break;
#define DEFLINK(OP,MSK,NAME,MASK,SHIFT)
#define CONNECT(OP)
#include "machine.def"
}
    de.oprand.cons.imm = IMM;
    de.oprand.cons.target = TARG;
    de.oprand.cons.shamt = SHAMT;

    de.reg1_data = GPR(de.oprand.in1);
    de.reg2_data = GPR(de.oprand.in2);
    de.reg3_data = GPR(de.oprand.in3);

    // Decide execution control signals
    switch (de.opcode) {
#define DEFCONTROL(OP, ALUSRC1, ALUSRC2, ALUOP, REGDST, BRH_TYPE)  \
      case OP:                        \
      de.ex_ctrl.ALUSrc1 = ALUSRC1;   \
      de.ex_ctrl.ALUSrc2 = ALUSRC2;   \
      de.ex_ctrl.ALUOp = ALUOP;       \
      de.ex_ctrl.RegDst = REGDST;     \
      de.mem_ctrl.Branch = BRH_TYPE;  \
      break;
#include "pipe.def"
      default:
        panic("instruction not supported");
    }

    de.mem_ctrl.MemRead = (de.instFlags & F_MEM) && (de.instFlags & F_LOAD);
    de.mem_ctrl.MemWrite = (de.instFlags & F_MEM) && (de.instFlags & F_STORE);
    de.wb_ctrl.RegWrite = de.ex_ctrl.RegDst != DST_NO_DST;
    de.wb_ctrl.MemtoReg = de.mem_ctrl.MemRead && de.wb_ctrl.RegWrite;

    fetch_sgn.bubble_for_decode = de.instFlags & F_CTRL;
}

sword_t alu_source(alu_src_t src, struct idex_buf _de)
{
  switch (src) {
    case SRC_IMM:
      return _de.oprand.cons.imm;
    case SRC_SHAMT:
      return _de.oprand.cons.shamt;
    case SRC_IN1:
      return _de.reg1_data;
    case SRC_IN2:
      return _de.reg2_data;
    case SRC_ZERO:
      return 0;
  }
  return 0;
}

void do_ex()
{
  em.inst = buf.de.inst;
  em.mem_ctrl = buf.de.mem_ctrl;
  em.wb_ctrl = buf.de.wb_ctrl;
  em.reg1_data = buf.de.reg1_data;
  // ALU
  sword_t alu_oprand1 = alu_source(buf.de.ex_ctrl.ALUSrc1, buf.de);
  sword_t alu_oprand2 = alu_source(buf.de.ex_ctrl.ALUSrc2, buf.de);
  switch (buf.de.ex_ctrl.ALUOp) {
    case ALU_ADD:
      em.alu_res = alu_oprand1 + alu_oprand2;
      break;
    case ALU_SUB:
      em.alu_res = alu_oprand1 - alu_oprand2;
      break;
    case ALU_AND:
      em.alu_res = alu_oprand1 & alu_oprand2;
      break;
    case ALU_OR:
      em.alu_res = alu_oprand1 | alu_oprand2;
      break;
    case ALU_SLT:
      em.alu_res = alu_oprand1 < alu_oprand2;
      break;
    case ALU_SHTL:
      em.alu_res = alu_oprand1 << alu_oprand2;
      break;
    default:
      panic("ALU operation not supported");
  }

  em.alu_zero = em.alu_res == 0;

  // Decide Memory stage control signals from instruction flags
  if (buf.de.instFlags & F_CTRL) {
    // Control instruction
    if (buf.de.instFlags & F_DIRJMP) {
      // Jump from imm or NPC + imm
      if (buf.de.instFlags & F_UNCOND) {
        // J type jump
        // TODO Is it OK to use NPC?
        em.NPC = (buf.de.NPC & 036000000000) | (buf.de.oprand.cons.target << 2);
      } else {
        // B type jump
        em.NPC = (buf.de.NPC + (buf.de.oprand.cons.imm << 2));
      }
    } else {
      // Jump form a register
      if (buf.de.instFlags & F_UNCOND) {
        // TODO
      } else {
        // TODO
      }
    }
  }

  em.needJump = (buf.de.mem_ctrl.Branch == BRH_UNCOND) ||
                (buf.de.mem_ctrl.Branch == BRH_ONZERO && em.alu_zero) ||
                (buf.de.mem_ctrl.Branch == BRH_ONNOTZERO && !em.alu_zero);

  // Decide register dest
  switch (buf.de.ex_ctrl.RegDst) {
    case DST_NO_DST:
      break;
    case DST_OUT1:
      em.reg_dest = buf.de.oprand.out1;
      break;
    case DST_OUT2:
      em.reg_dest = buf.de.oprand.out2;
      break;
  }

  decode_sgn.bubble_for_execute = (em.wb_ctrl.RegWrite && em.reg_dest != 0) &&
                        (de.oprand.in1 == em.reg_dest ||
                          de.oprand.in2 == em.reg_dest ||
                            de.oprand.in3 == em.reg_dest);
  fetch_sgn.stall_for_execute = decode_sgn.bubble_for_execute;
  fetch_sgn.bubble_for_execute = em.needJump;
}

void do_mem()
{
  mw.inst = buf.em.inst;
  mw.wb_ctrl = buf.em.wb_ctrl;
  mw.reg_dest = buf.em.reg_dest;
  mw.alu_res = buf.em.alu_res;

  // Memory operation
  enum md_fault_type _fault;
  if (buf.em.mem_ctrl.MemRead) {
    mw.memread_res = READ_WORD(mw.alu_res, _fault);
    if (_fault != md_fault_none)
      DECLARE_FAULT(_fault);
  } else {
    if (buf.em.mem_ctrl.MemWrite) {
      WRITE_WORD(buf.em.reg1_data, mw.alu_res, _fault);
      if (_fault != md_fault_none)
        DECLARE_FAULT(_fault);
    }
  }

  decode_sgn.bubble_for_mem = (mw.wb_ctrl.RegWrite && mw.reg_dest != 0) &&
                          (de.oprand.in1 == mw.reg_dest ||
                            de.oprand.in2 == mw.reg_dest ||
                              de.oprand.in3 == mw.reg_dest);
  fetch_sgn.stall_for_mem = decode_sgn.bubble_for_mem;

}

void do_wb()
{
  // Special control for SYSCALL
  if (buf.mw.inst.a == SYSCALL) {
    SYSCALL(buf.mw.inst);
  }
   if (buf.mw.wb_ctrl.RegWrite) {
     if (buf.mw.wb_ctrl.MemtoReg) {
       SET_GPR(buf.mw.reg_dest, buf.mw.memread_res);
     } else {
       SET_GPR(buf.mw.reg_dest, buf.mw.alu_res);
     }
   }
   SET_GPR(MD_REG_ZERO, 0);
}

void write_buf() {
  buf.fd = (fetch_sgn.stall_for_execute || fetch_sgn.stall_for_mem) ?
            buf.fd : (fetch_sgn.bubble_for_decode || fetch_sgn.bubble_for_execute) ?
            EMPTY_FD_BUF : fd;
  buf.de = (decode_sgn.bubble_for_execute || decode_sgn.bubble_for_mem) ?
            EMPTY_DE_BUF : de;
  buf.em = em;
  buf.mw = mw;
}
