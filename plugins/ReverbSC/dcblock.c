/*
 * DCblock
 *
 * This code has been extracted from the Csound opcode "dcblock".
 * It has been modified to work as a Soundpipe module.
 *
 * Original Author(s): Perry R. Cook
 * Year: 1995
 * Location: Opcodes/biquad.c
 *
 */

#include <math.h>
#include <stdlib.h>
#include "base.h"
#include "dcblock.h"

int sp_dcblock_create(sp_dcblock **p)
{
    *p = malloc(sizeof(sp_dcblock));
    if (*p == NULL)
    {
        return SP_NOT_OK;
    }
    return SP_OK;
}

int sp_dcblock_destroy(sp_dcblock **p)
{
    free(*p);
    *p = NULL;
    return SP_OK;
}

int sp_dcblock_init(sp_data *sp, sp_dcblock *p, int oversampling )
{
    p->outputs = 0.0;
    p->inputs = 0.0;
    p->gain = pow( 0.99, 1.0f / oversampling );
    if (p->gain == 0.0 || p->gain>=1.0 || p->gain<=-1.0)
      p->gain = 0.99f;
    return SP_OK;
}

int sp_dcblock_create_and_init_pair(sp_data *sp, sp_dcblock **p, int oversampling)
{
    sp_dcblock *created[2] = {NULL, NULL};
    p[0] = NULL;
    p[1] = NULL;

    if (sp_dcblock_create(&created[0]) == SP_NOT_OK ||
        sp_dcblock_create(&created[1]) == SP_NOT_OK ||
        sp_dcblock_init(sp, created[0], oversampling) == SP_NOT_OK ||
        sp_dcblock_init(sp, created[1], oversampling) == SP_NOT_OK)
    {
        sp_dcblock_destroy(&created[0]);
        sp_dcblock_destroy(&created[1]);
        return SP_NOT_OK;
    }

    p[0] = created[0];
    p[1] = created[1];
    return SP_OK;
}

int sp_dcblock_compute(sp_data *sp, sp_dcblock *p, SPFLOAT *in, SPFLOAT *out)
{
    SPFLOAT gain = p->gain;
    SPFLOAT outputs = p->outputs;
    SPFLOAT inputs = p->inputs;

    SPFLOAT sample = *in;
    outputs = sample - inputs + (gain * outputs);
    inputs = sample;
    *out = outputs;
    p->outputs = outputs;
    p->inputs = inputs;
    return SP_OK;
}
