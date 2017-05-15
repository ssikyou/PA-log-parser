#ifndef _CONFIG_H_
#define _CONFIG_H_

#include<stdbool.h>

#include "emmcparser.h"

#define PATTERN_RANDOM      0
#define PATTERN_USERINPUT   1
#define PATTERN_INTINC      2
#define PATTERN_INTDEC      3
#define PATTERN_FROMLOG     4
typedef enum config_type{
    CFG_VALUE,
    CFG_STR,
    CFG_BOOL,
    CFG_INT,
    CFG_INT64,
    CFG_UINT64,
    CFG_DOUBLE,
}config_type;

typedef struct config_list{
    config_type type;
    char *key;
    union{
        char *val_char;//value,str,
        bool val_bool;
        int val_int;
        long long val_int64;
        unsigned long long val_uint64;
        double val_double;
    };
}config_list;


typedef struct config_info{
    unsigned char pattern_type;// 0 random data, 1 user pattern, 2 int increase, 3 int decrease ,4 from log data
    unsigned char *pattern_data;//save user data pattern
    int pattern_len;//user pattern data len
    unsigned int start_value;//for pattern increase or decrease, save user set int value.
    int block_size;
    void *priv;
}config_info;

const char *pattern_type_str(unsigned char pattern_type);

int config_load_list(mmc_parser *parser, char *group, config_list *list, int list_len);
config_info *config_load(mmc_parser *parser, int *ret);
int config_init(config_info *cfg, int has_data);
int config_deinit(config_info *cfg);

#endif
