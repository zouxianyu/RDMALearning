/*
 * Definition of errors used in example
 */

#ifndef ERRORS_H_
#define ERRORS_H_

typedef enum {
    OK                         =   0,

    /* Failure codes */
    ERR_NO_MEMORY              =  -1,
    ERR_BIND_MEM_ERROR         =  -2,
    ERR_NOT_IMPLEMENTED        =  -3,
    ERR_UNSUPPORTED            =  -4,
    ERR_INVALID_FREE_ATTEMPT   =  -5,

    ERR_LAST                   = -100
} errors_t ;

#endif

