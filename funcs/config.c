#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "glib.h"
#include "common.h"
#include "config.h"

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
        dbg(L_INFO,"ERR: %s group:%s not exist!\n", __func__, group);
        return -1;
    }
    return 0;
}

