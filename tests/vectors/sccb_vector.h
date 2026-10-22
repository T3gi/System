#ifndef SCCB_VECTOR_H
#define SCCB_VECTOR_H

#include <stdint.h>

/* One SCCB transaction as decoded from a logic-analyzer capture. */
typedef enum {
    SCCB_W3,    /* 3-phase write: address, register, value          */
    SCCB_W2,    /* 2-phase write: address, register (read, phase 1) */
    SCCB_R      /* 2-phase read:  address, value    (read, phase 2) */
} sccb_vec_op_t;

typedef struct {
    sccb_vec_op_t op;
    uint8_t       reg;      /* W3, W2 */
    uint8_t       val;      /* W3: written value, R: value the camera returned */
} sccb_vec_t;

#endif
