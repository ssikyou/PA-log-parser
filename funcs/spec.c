#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "common.h"
#include "spec.h"

typedef union callbak{
	struct class_ops *ops;
	int (**call)(void *arg, mmc_request *req);
}callbak;

typedef struct mmc_spec{
	mmc_val		   val;
	int (*sbc)(void *arg, mmc_cmd *cmd, mmc_val *val);
	int (*stop)(void *arg, mmc_cmd *cmd);
	callbak		callbak;
	//class_ops	  *ops;
	void *priv;
}mmc_spec;

#define RSP_R1  	(1<<0)
#define RSP_R1B  	(1<<1)
#define RSP_R2  	(1<<2)
#define RSP_R3  	(1<<3)
#define RSP_R4  	(1<<4)
#define RSP_R5  	(1<<5)
#define RSP_UND		(1<<6)//undefined
#define RSP_RVD		(1<<6)//reserved

static unsigned long int mmc_rsp_table[64] = {
RSP_UND,RSP_R3,RSP_R2,RSP_R1,RSP_UND,//CMD0~4
RSP_R1B,RSP_R1B,RSP_R1|RSP_R1B,RSP_R1,RSP_R2,//CMD5~9
RSP_R2,RSP_UND,RSP_R1|RSP_R1B,RSP_R1,RSP_R1,//CMD10~14
RSP_UND,RSP_R1,RSP_R1,RSP_R1,RSP_R1,//CMD15
RSP_UND,RSP_R1,RSP_RVD,RSP_R1,RSP_R1,//CMD20
RSP_R1,RSP_R1,RSP_R1,RSP_R1B,RSP_R1B,//CMD25
RSP_R1,RSP_R1,RSP_RVD,RSP_RVD,RSP_RVD,//CMD30
RSP_R1,RSP_R1,RSP_RVD,RSP_R1B,RSP_R4,//CMD35
RSP_R5,RSP_RVD,RSP_R1,RSP_RVD,RSP_R1,//CMD40
RSP_R1,RSP_R1,RSP_R1,RSP_R1B,RSP_R1,//CMD45
RSP_RVD,RSP_RVD,RSP_RVD,RSP_R1,RSP_R1,//CMD50
RSP_R1,RSP_R1,RSP_RVD,RSP_RVD,RSP_RVD,//CMD55
RSP_RVD,RSP_RVD,RSP_RVD,RSP_RVD//CMD60~63
};

int is_cmd_rsp_r1(unsigned short cmd_index)
{
	return mmc_rsp_table[cmd_index]&RSP_R1;
}

int is_cmd_rsp_r1b(unsigned short cmd_index)
{
	return mmc_rsp_table[cmd_index]&RSP_R1B;
}

int is_cmd_rsp_r2(unsigned short cmd_index)
{
	return mmc_rsp_table[cmd_index]&RSP_R2;
}

int is_cmd_rsp_r3(unsigned short cmd_index)
{
	return mmc_rsp_table[cmd_index]&RSP_R3;
}

int is_cmd_rsp_r4(unsigned short cmd_index)
{
	return mmc_rsp_table[cmd_index]&RSP_R4;
}

int is_cmd_rsp_r5(unsigned short cmd_index)
{
	return mmc_rsp_table[cmd_index]&RSP_R5;
}

int is_cmd_rsp_und(unsigned short cmd_index)
{
	return mmc_rsp_table[cmd_index]&RSP_UND;
}

int is_cmd_rsp_rvd(unsigned short cmd_index)
{
	return mmc_rsp_table[cmd_index]&RSP_RVD;
}

int get_cmd_rsp_size(unsigned short cmd_index)
{
	int ret = 0;

	ret = mmc_rsp_table[cmd_index];
	if(ret == RSP_R2)
		return 128;
	else if((ret == RSP_RVD)||(ret == RSP_UND))
		return 0;
	else
		return 32;
}

int is_busy_cmd(unsigned short cmd_index)
{
	return mmc_rsp_table[1]&(1<<cmd_index);
}

static int undefine(void*arg, mmc_request *req)
{
	mmc_cmd *cmd = req->cmd;

	error("%s sbc cmd_index:%d not 23\n", __func__, cmd->cmd_index);
	assert(0);
	return 0;
}

void* mmc_spec_init(void *arg, class_ops *ops, sbc_callbak *sbc, stop_callbak *stop, class_callbak *def)
{
	int i;

	if(ops == NULL){
		error("%s ops is NULL\n", __func__);
		return NULL;
	}
	mmc_spec *spec = malloc(sizeof(mmc_spec));
	spec->callbak.ops = ops;
	if(def != NULL){
		for(i = 0; i < 12; i++){
			if(spec->callbak.call[i] == NULL)
				spec->callbak.call[i] = def;
		}

	}
	if(spec->callbak.call[12])
		spec->callbak.call[i] = undefine;//set default undefine ops

	spec->sbc = sbc;
	spec->stop = stop;
	spec->priv = arg;
	memset(&spec->val, 0, sizeof(mmc_val));
	return (void*)spec;
}

int handle_sbc_ops(void *arg, mmc_cmd *cmd)
{
	mmc_spec *spec = (mmc_spec *)arg;
	mmc_val	*val = &spec->val;
	int ret = 0;

	switch(cmd->cmd_index){
		case 23:
			val->reliable_wr = cmd->arg & 1<<31;
			val->force_prg = cmd->arg & 1<<24;
			val->block_count = cmd->arg & 0xffff;
			break;
		default:
			error("%s sbc cmd_index:%d not 23\n", __func__, cmd->cmd_index);
			assert(0);
	}
	if(spec->sbc)
		spec->sbc(spec->priv, cmd, val);
	return ret;
}

int handle_stop_ops(void *arg, mmc_cmd *cmd)
{
	mmc_spec *spec = (mmc_spec *)arg;
	mmc_val	*val = &spec->val;
	int ret = 0;

	switch(cmd->cmd_index){
		case 12:
			break;
		default:
			error("%s sbc cmd_index:%d not 23\n", __func__, cmd->cmd_index);
			assert(0);
	}
	if(spec->stop)
		ret = spec->stop(spec->priv, cmd);
	memset(val, 0 , sizeof(mmc_val));
	return ret;
}

int handle_class_ops(void *arg, mmc_request *req)
{
	mmc_spec *spec = (mmc_spec *)arg;
	callbak *callbak = &spec->callbak;
	mmc_cmd *cmd = req->cmd;
	int index;
	int ret = 0;
	int i = 0;

	assert(spec != NULL);
	assert(callbak->ops != NULL);

	if(cmd != NULL)
		index = cmd->cmd_index;
	else{
		error("%s req->cmd is NULL\n", __func__);
		return -1;
	}

	if(callbak->ops->common){
		ret = callbak->ops->common(spec->priv, req);
		if(ret){
			error("%s ops->common fail\n", __func__);
			return -1;
		}
	}
	if((index >= 0 && index <= 15) || (index == 19)){
		i = 0;//class0_1
	}else if((index >= 16 && index <= 18) || (index == 21)){
		i = 1;//class2
	}else if(index == 20){
		i = 2;//class3
	}else if((index >= 23 && index <= 27) || (index == 49)){
		i = 3;//class4
	}else if((index >= 35 && index <= 36) || (index == 38)){
		i = 4;//class5
	}else if(index >= 28 && index <= 31){
		i = 5;//class6
	}else if(index == 42){
		i = 6;//class7
	}else if((index >= 55 && index <= 56)){
		i = 7;//class8
	}else if(index >= 39 && index <= 40){
		i = 8;//class9
	}else if(index == 53 && index == 54){
		i = 9;//class10
	}else if(index >= 44 && index <= 48){
		i = 10;//class11
	}else if((index == 22)//class3 reserved
			|| (index >= 32 && index <= 34)|| (index == 37)//class5 reserved
			||(index == 43)//class7 reserved
			|| (index >= 57 && index <= 59)//class8 reserved
			||(index == 41)//class9 reserved
			|| (index >= 50 && index <= 52)){//class10 reserved
		i = 11;
	}else if(index >= 60 && index <= 63)//class8 vendor
		i = 12;
	else{
		error("%s event_id:%d index is:%d\n", __func__, cmd->event_id, index);
		assert(0);
	}

	if(spec->callbak.call[i])
		spec->callbak.call[i](spec->priv, req);
	
/*
	if(i >= 0 && i <= 10)
		CALL_CLASS_CALLBAK(i, spec->priv, req);
	else if(i == 11)
		CALL_UND_CALLBAK(i, spec->priv, req);
	else
		CALL_VND_CALLBAK(i, spec->priv, req);
*/
	return ret;
}

