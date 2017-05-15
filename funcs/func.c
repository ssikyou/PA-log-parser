#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "emmcparser.h"
#include "func.h"
#include "func_ops.h"

static void func_set_ops(func *f, func_type type)
{
    switch(type){
    case FUNC_CYPRESS:
        f->ops = &cypress_ops;
    case FUNC_SIMULATE:
    case FUNC_XU4:
        break; 
    }
}

/*
mmc_req_cb cb = {"func cypress", func_init, func_request, func_destory, NULL};
int register_func(mmc_parser *parser, func_type type, char *log_path)
{
    mmc_register_req_cb(parser, &cb);
    return 0;
}
*/

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

    param = (func_param *)(f + sizeof(func));


    memset((void*)f, 0, sizeof(func));

    func_set_ops(f, type);
    if(f->ops == NULL|| f->ops->load_configs == NULL){
        printf("ERR: %s  func->ops is NULL\n", __func__);
        free(f);
        return -1;
    }

    f->ops->load_configs(parser,param);
    if(param->cfg == NULL){
        printf("ERR: %s  param->cfg is NULL\n", __func__);
        free(f);
        return -1;
    }

    param->log_path = strdup(log_path);
    param->has_data = parser->has_data;
    param->has_busy = parser->has_busy;

    f->param = param;
    cb = alloc_req_cb(f->ops->desc, func_init, func_request, func_destory, f);
    mmc_register_req_cb(parser, cb);
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

    if(f->ops->request(f, req)){
        printf("ERR: %s  func->ops->request fail\n", __func__);
        return -1;
    }
    return 0;
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

    free(f);
    return 0;
}

