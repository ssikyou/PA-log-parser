#ifndef _SPEC_H_
#define _SPEC_H_

#include "emmcparser.h"

//class is reference to spec table49 ~table59
//vendor is seprate from class8

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
#define CLASS_COM()     CLASS_OPS(common)

typedef struct class_ops{
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
	CLASS_VND();//vendor cmd, devided from class 8
	CLASS_UDF();//undefined// all undefined or reserved cmd
	CLASS_COM();//common command, exec before all class func.
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

