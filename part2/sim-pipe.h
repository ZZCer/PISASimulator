#include "machine.h"

/* define values related to operands, all possible combinations are included */

typedef enum {
  SRC_ZERO = 0,
  SRC_IN1,
  SRC_IN2,
  SRC_IMM,
  SRC_SHAMT
} alu_src_t;

typedef enum {
  DST_NO_DST = 0,
  DST_OUT1,
  DST_OUT2
} reg_dst_t;

typedef enum {
  ALU_AND = 0,
  ALU_OR,
  ALU_ADD,
  ALU_SUB,
  ALU_SLT,
  ALU_SHTL,
  ALU_OP_NUM
} alu_op_t;

typedef enum {
  BRH_NOBRANCH = 0,
  BRH_UNCOND,
  BRH_ONZERO,
  BRH_ONNOTZERO,
} branch_type_t;

typedef struct {
    int imm;
    int target;
    int shamt;
} inst_cons_t;

typedef struct {
  int in1;			/* input 1 register number */
  int in2;			/* input 2 register number */
  int in3;			/* input 3 register number */
  int out1;			/* output 1 register number */
  int out2;			/* output 2 register number */
  inst_cons_t cons;  /* constants */
} oprand_t;

typedef struct {
  reg_dst_t RegDst;
  alu_src_t ALUSrc1;
  alu_src_t ALUSrc2;
  alu_op_t ALUOp;
} ex_ctrl_t;

typedef struct {
  bool_t MemRead;
  bool_t MemWrite;
  branch_type_t Branch;
} mem_ctrl_t;

typedef struct {
  bool_t RegWrite;
  bool_t MemtoReg;
} wb_ctrl_t;

typedef struct {
  bool_t bubble_for_decode;
  bool_t stall_for_execute;
  bool_t bubble_for_execute;
  bool_t stall_for_mem;
} fetch_sgn_t;

typedef struct {
  bool_t bubble_for_execute;
  bool_t bubble_for_mem;
} decode_sgn_t;

/*define buffer between fetch and decode stage*/
struct ifid_buf {
  md_inst_t inst;	    /* instruction that has been fetched */
  md_addr_t PC;	        /* pc value of current instruction */
  md_addr_t NPC;		/* the next instruction to fetch */
};

static struct ifid_buf EMPTY_FD_BUF;

/*define buffer between decode and execute stage*/
struct idex_buf {
  md_inst_t inst;		/* instruction in ID stage */
  int opcode;			/* operation number */ // TODO Is not the opcode provided in machine.def
  oprand_t oprand;		/* operand */
  int instFlags;
  // Control signals
  ex_ctrl_t ex_ctrl;
  mem_ctrl_t mem_ctrl;
  wb_ctrl_t wb_ctrl;
  // Next PC
  md_addr_t NPC;
  // Output
  sword_t reg1_data;
  sword_t reg2_data;
  sword_t reg3_data;
};

static struct idex_buf EMPTY_DE_BUF;

/*define buffer between execute and memory stage*/
struct exmem_buf{
  md_inst_t inst;		/* instruction in EX stage */
  mem_ctrl_t mem_ctrl;
  wb_ctrl_t wb_ctrl;
  md_addr_t NPC;
  bool_t needJump;
  sword_t alu_res;
  bool_t alu_zero;
  sword_t reg1_data;
  int reg_dest;
};

/*define buffer between memory and writeback stage*/
struct memwb_buf{
  md_inst_t inst;		/* instruction in MEM stage */
  wb_ctrl_t wb_ctrl;
  sword_t alu_res;
  sword_t memread_res;
  int reg_dest;
};

struct cpu_stages_buf {
  struct ifid_buf fd;
  struct idex_buf de;
  struct exmem_buf em;
  struct memwb_buf mw;
};

/*do fetch stage*/
void do_if();

/*do decode stage*/
void do_id();

/*do execute stage*/
void do_ex();

/*do memory stage*/
void do_mem();

/*do write_back to register*/
void do_wb();

/*write stage buf*/
void write_buf();

void print_env();

#define MD_FETCH_INSTI(INST, MEM, PC)					\
  { INST.a = MEM_READ_WORD(mem, (PC));					\
    INST.b = MEM_READ_WORD(mem, (PC) + sizeof(word_t)); }

#define SET_OPCODE(OP, INST) ((OP) = ((INST).a & 0xff))

#define RSI(INST)		(INST.b >> 24& 0xff)		/* reg source #1 */
#define RTI(INST)		((INST.b >> 16) & 0xff)		/* reg source #2 */
#define RDI(INST)		((INST.b >> 8) & 0xff)		/* reg dest */

#define IMMI(INST)	((int)((/* signed */short)(INST.b & 0xffff)))	/*get immediate value*/
#define TARGI(INST)	(INST.b & 0x3ffffff)		/*jump target*/
