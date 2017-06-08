#ifndef _FUNC_OPS_H_
#define _FUNC_OPS_H_

#define FUNC_NO (3)
typedef enum func_type{
    FUNC_CYPRESS,
    FUNC_SIMULATE,
    FUNC_XU4
}func_type;

void func_set_ops(func *f, func_type type);
#endif
