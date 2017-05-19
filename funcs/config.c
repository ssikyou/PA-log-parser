#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "glib.h"
#include "common.h"
#include "config.h"

const char pattern_table[][10]={
    {"random"},
    {"usrpat"},
    {"intinc"},
    {"intdec"},
    {"logpat"},
};

static config_list list[]={
    {.type = CFG_INT, .key = "pattern_type"},
    {.type = CFG_STR, .key = "pattern_data"},
    {.type = CFG_INT, .key = "pattern_len"},
    {.type = CFG_INT, .key = "start_value"},
    {.type = CFG_INT, .key = "block_size"},
	{.type = CFG_INT, .key = "max_sectors"},
};

const char *pattern_type_str(unsigned char pattern_type)
{
    assert((pattern_type >= PATTERN_RANDOM) && (pattern_type <= PATTERN_FROMLOG));

    dbg(L_INFO,"%s:%s\n", __func__ , pattern_table[pattern_type]);
    return pattern_table[pattern_type];
}

int config_load_list(mmc_parser *parser, char *group, config_list *list, int list_len)
{
    GKeyFile* gkf = parser->gkf;
    GError *err = NULL;
    int i;

    if(g_key_file_has_group(gkf, group)){
        for(i = 0; i < list_len; i++){
            if(g_key_file_has_key(gkf, group, list[i].key, &err)){
                switch(list[i].type){
                    case CFG_VALUE:
                        list[i].val_char = g_key_file_get_value(gkf, group, list[i].key, &err);
                        break;
                    case CFG_STR:
                        list[i].val_char = g_key_file_get_string(gkf, group, list[i].key, &err);
                        break;
                    case CFG_BOOL:
                        list[i].val_bool = g_key_file_get_boolean(gkf, group, list[i].key, &err);
                        break;
                    case CFG_INT:
                        list[i].val_int = g_key_file_get_integer(gkf, group, list[i].key, &err);
                        break;
                    case CFG_INT64:
                        list[i].val_int64 = g_key_file_get_int64(gkf, group, list[i].key, &err);
                        break;
                    case CFG_UINT64:
                        list[i].val_uint64 = g_key_file_get_uint64(gkf, group, list[i].key, &err);
                        break;
                    case CFG_DOUBLE:
                        list[i].val_double = g_key_file_get_double(gkf, group, list[i].key, &err);
                }
                if(err != NULL){
                    error("ERR: %s key:%s err:%s\n", __func__, list[i].key, err->message);
                    return -1;               
                }
            }else{
                error("ERR: %s key:%s not exist!\n", __func__, list[i].key);
                return -1;
            }
    
        }
    }else{
        error("ERR: %s group:%s not exist!\n", __func__, group);
        return -1;
    }
    return 0;
}


config_info *config_load(mmc_parser *parser, int *ret)
{
    config_info *cfg;

    cfg = malloc(sizeof(config_info));
    if(cfg == NULL){
        error("ERR: %s malloc cfg fail\n", __func__);
        *ret = -1;
        return NULL;
    }

    *ret = config_load_list(parser, "Script Common", list, sizeof(list)/sizeof(list[0]));
    if(*ret){
        error("ERR: %s load common config fail\n", __func__);
        *ret = -1;
        return NULL;
    }

    cfg->pattern_type = list[0].val_int;
    cfg->pattern_data = (unsigned char *)list[1].val_char;
    cfg->pattern_len = list[2].val_int;
    cfg->start_value = list[3].val_int;
    cfg->block_size = list[4].val_int;
	cfg->max_sectors = list[5].val_int;
    return cfg;
}

int config_init(config_info *cfg, int has_data)
{
    if((cfg->pattern_type > PATTERN_FROMLOG) || (cfg->pattern_type < PATTERN_RANDOM)){
        error("ERR:%s pattern_type invalid:%d\n", __func__, cfg->pattern_type);
        return -1;
    }

    if((cfg->pattern_type == PATTERN_USERINPUT) && ((cfg->pattern_data == NULL) || (cfg->pattern_len == 0))){
        printf("ERR: %s data pattern is NULL or pattern_len == 0\n", __func__);
        return -1;
    }

    if((has_data == 0) && (cfg->pattern_type == PATTERN_FROMLOG)) {
        printf("ERR: %s data pattern is NULL or pattern_len == 0\n", __func__);
        return -1;
    }

    if(cfg->block_size != 512){
        printf("Warr: %s block_size:%d != 512\n", __func__, cfg->block_size);
        return -1;
    }

    //pattern_type 0 random data, 1 user pattern, 2 int increase, 3 int decrease ,4 from log data
    return 0;
}

int config_deinit(config_info *cfg)
{
    if(cfg->pattern_data != NULL){
        free(cfg->pattern_data);
        cfg->pattern_data = NULL;
    }
    return 0;
}

