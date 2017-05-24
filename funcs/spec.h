#ifndef _SPEC_H_
#define _SPEC_H_

#include "emmcparser.h"

//class is reference to spec table49 ~table59
//vendor is seprate from class8

//0 idle, 1 ready, 2 ident, 3 stby, 4 tran, 5 data, 6 btst, 7 rcv, 8 prg, 9 dis, 10 ina, 11 slp, 12 irq
enum mmc_state{
	STATE_IDLE = 0,
	STATE_READY,
	STATE_IDENT,
	STATE_STBY,
	STATE_TRAN,
	STATE_DATA,
	STATE_BTST,
	STATE_RCV,
	STATE_PRG,
	STATE_DIS,
	STATE_INA,
	STATE_SLP,
	STATE_IRQ,
};

typedef struct mmc_val{
	int reliable_wr:1;
	int force_prg:1;
	unsigned short block_count;
}mmc_val;

typedef int (class_callbak)(void *arg, mmc_request *req);
typedef int (sbc_callbak)(void *arg, mmc_cmd *sbc, mmc_val *val);
typedef int (stop_callbak)(void *arg, mmc_cmd *stop);

#define CLASS_OPS(x)   \
    int (*x)(void*arg, mmc_request *req)

#define CLASS_DEF(x)	CLASS_OPS(class##x)
#define CLASS_VND()     CLASS_OPS(vendor)
#define CLASS_UDF()     CLASS_OPS(undefine)

#define CLASS_PRE()		CLASS_OPS(pre)
#define CLASS_POST()    CLASS_OPS(post)

typedef struct class_ops{
	CLASS_PRE();//call it before all class ops return value: 0 OK, 1 OK but no need do class ops, -1 not OK
	CLASS_DEF(1);//class0_1
	CLASS_DEF(2);//class2
	CLASS_DEF(3);//class3
	CLASS_DEF(4);//class4
	CLASS_DEF(5);//class5
	CLASS_DEF(6);//class6
	CLASS_DEF(7);//class7
	CLASS_DEF(8);//class8
	CLASS_DEF(9);//class9
	CLASS_DEF(10);//class10
	CLASS_DEF(11);//class11
	CLASS_UDF();//undefined// all undefined or reserved cmd
	CLASS_VND();//vendor cmd, devided from class 8
	CLASS_POST();//call it at laster
}class_ops;

int is_cmd_rsp_r1(unsigned short cmd_index);
int is_cmd_rsp_r1b(unsigned short cmd_index);
int is_cmd_rsp_r2(unsigned short cmd_index);
int is_cmd_rsp_r3(unsigned short cmd_index);
int is_cmd_rsp_r4(unsigned short cmd_index);
int is_cmd_rsp_r5(unsigned short cmd_index);
int is_cmd_rsp_und(unsigned short cmd_index);
int is_cmd_rsp_rvd(unsigned short cmd_index);
int get_cmd_rsp_size(unsigned short cmd_index);
int is_busy_cmd(unsigned short cmd_index);

void* mmc_spec_init(void *arg, class_ops *ops, sbc_callbak *sbc, stop_callbak *stop, class_callbak *def);
int handle_sbc_ops(void *arg, mmc_cmd *cmd);
int handle_stop_ops(void *arg, mmc_cmd *cmd);
int handle_class_ops(void *arg, mmc_request *req);
#endif

