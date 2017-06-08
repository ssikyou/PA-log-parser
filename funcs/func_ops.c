#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "func.h"
#include "func_ops.h"

extern func_ops cypress_ops;
extern func_ops simulator_ops;

void func_set_ops(func *f, func_type type)
{
	char desc[25];

    switch(type){
    case FUNC_CYPRESS:
        f->ops = &cypress_ops;
		break;
    case FUNC_SIMULATE:
        f->ops = &simulator_ops;
		break;
    case FUNC_XU4:
        break;
    }
	if(f->ops){
		if(f->ops->desc)
			f->desc = strdup(f->ops->desc);
		else{
			sprintf(desc, "func%d", type);
			f->desc = strdup(desc);
		}
	}
}
