#ifndef _FUNC_H_
#define _FUNC_H_
#include "emmcparser.h"
#include "config.h"

typedef struct func func;

typedef struct func_param{
    int has_data;
    int has_busy;
    //func_type type;
	unsigned short max_sectors;
	void *cfg;
    //config_info *cfg;//this is the config for specify module
    const char *log_path;
}func_param;

typedef struct func_ops{
    char *desc;
    int (*load_configs)(mmc_parser *parser, func_param *param);
    int (*init)(func *func, func_param *param);
    int (*request)(func *func, mmc_request *req);
    int (*destory)(func *func);
}func_ops;

typedef struct func{
    func_param *param;
    func_ops *ops;
	char *desc;
    void *priv;
}func;

int register_funcs(mmc_parser *parser, char *log_path);
int func_init(mmc_parser *parser, void *arg);
int func_request(mmc_parser *parser, void *arg);
int func_destory(mmc_parser *parser, void *arg);

#endif
