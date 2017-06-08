#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "emmcparser.h"
#include "func.h"
#include "func_ops.h"

int register_func(mmc_parser *parser, func_type type, char *log_path)
{
    mmc_req_cb *cb;
    func *f;
    func_param *param;

    f = malloc(sizeof(func) + sizeof(func_param));
    if(f == NULL){
        printf("ERR: %s malloc fail\n", __func__);
        return -1;
    }

    param = (func_param *)((void*)f + sizeof(func));
	//printf("%p %p %d equal?:%s\n",f,param,sizeof(func),((void*)param - (void*)f)== sizeof(func)?"yes":"no");

    memset((void*)f, 0, sizeof(func));

    param->log_path = strdup(log_path);
    param->has_data = parser->has_data;
    param->has_busy = parser->has_busy;

    func_set_ops(f, type);
    if(f->ops == NULL){
        //printf("ERR: %s type:%d func->ops is NULL\n", __func__, type);
        free(f);
        return -1;
    }
	if(f->ops->load_configs){
		if(f->ops->load_configs(parser,param))
			return 0;
		if(param->cfg == NULL){
		    printf("ERR: %s  param->cfg is NULL\n", __func__);
		    free(f);
		    return -1;
		}
	}



    f->param = param;
    cb = alloc_req_cb(f->ops->desc, func_init, func_request, func_destory, f);
    mmc_register_req_cb(parser, cb);

    return 0;
}

int register_funcs(mmc_parser *parser, char *log_path)
{
	int i = 0;
	for(i = 0; i< FUNC_NO; i++){
		register_func(parser, (func_type)i, log_path);
	}
	return 0;
}

int handle_large_request(func *f, mmc_request *req, int max_sectors)
{
	mmc_request curr;
	mmc_cmd sbc, cmd;
	unsigned short cmd_index;
	unsigned int addr;
	int total = req->sectors;
	void *data = req->data;
	int is_predefine = 0;

	if(req->cmd == NULL){
		error("req->cmd is NULL");
		return -1;
	}

	if(data == NULL){
		error("req->data is NULL");
		return -1;
	}

	cmd_index = req->cmd->cmd_index;
	addr = req->cmd->arg;

	if(req->sbc){
		memcpy(&sbc, req->sbc, sizeof(sbc));
		sbc.cmd_index = req->sbc->cmd_index;
		sbc.resp = req->sbc->resp;
		sbc.resp_type = req->sbc->resp_type;
		curr.sbc = &sbc;
		curr.stop = NULL;
		is_predefine = 1;
	}else{
		curr.sbc = NULL;
		if(req->stop)
			curr.stop = req->stop;
		else{
			error("req->sbc and req->stop are NULL for data request");
			return -1;
		}
	}

	cmd.cmd_index = cmd_index;
	cmd.resp = req->cmd->resp;
	cmd.resp_type = req->cmd->resp_type;
	curr.cmd = &cmd;

	do{
		if(total > max_sectors)
			curr.sectors = max_sectors;
		else
			curr.sectors = total;
		total -=curr.sectors;
		curr.data = data;
		data += req->len_per_trans * curr.sectors;
		curr.len = req->len_per_trans * curr.sectors;
		if(is_predefine == 1)
			sbc.arg = curr.sectors;

		cmd.arg = addr;
		addr += curr.sectors;

		if(f->ops->request(f, &curr)){
			printf("ERR: %s  func->ops->request fail\n", __func__);
			return -1;
		}

	}while(total > 0);

	return 0;
}

static int func_process_request(func *f, mmc_request *req)
{
	func_param *param = f->param;

	if((param->max_sectors > 0)&&((req->sectors > param->max_sectors)||(req->sbc && (req->sbc->arg&0xfff) > param->max_sectors))){
		handle_large_request(f, req, param->max_sectors);
	}else{
		if(f->ops->request(f, req)){
			printf("ERR: %s  func->ops->request fail\n", __func__);
			return -1;
		}
	}
	return 0;
}

int func_init(mmc_parser *parser, void *arg)
{
    func *f = (func *)(arg);

    if(f->ops == NULL|| f->ops->init == NULL){
        printf("ERR: %s  func->ops is NULL\n", __func__);
        return -1;
    }

    if(f->ops->init(f,f->param)){
        printf("ERR: %s  func->ops->init fail\n", __func__);
        return -1;
    }

    return 0;
};


int func_request(mmc_parser *parser, void *arg)
{
    mmc_request *req = parser->cur_req;
    func *f = (func *)(arg);
	int ret;

    if(f == NULL){
        printf("ERR: %s  func is NULL\n", __func__);
        return -1;
    }

    if(req == NULL){
        printf("ERR: %s  mmc_request is NULL\n", __func__);
        return -1;
    }

    if(f->ops == NULL || f->ops->request == NULL){
        printf("ERR: %s  func->ops or func->ops->request is NULL\n", __func__);
        return -1;
    }

	ret = func_process_request(f, req);
    return ret;
}

int func_destory(mmc_parser *parser, void *arg)
{
    func *f = (func *)arg;

    if(f == NULL){
        printf("ERR: %s  func is NULL\n", __func__);
        return -1;
    }

    if(f->ops == NULL || f->ops->destory == NULL){
        printf("ERR: %s  func->ops or func->ops->destory is NULL\n", __func__);
        return -1;
    }

    if(f->ops->destory(f)){
        printf("ERR: %s  func->ops->destory fail\n", __func__);
        return -1;
    }

	free(f->desc);
    free(f);
    return 0;
}

