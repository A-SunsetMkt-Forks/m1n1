/* SPDX-License-Identifier: MIT */

#include "adt.h"
#include "asc.h"
#include "malloc.h"
#include "utils.h"

#define ASC_CPU_CONTROL       0x44
#define ASC_CPU_CONTROL_START 0x10

#define ASC_MBOX_CONTROL_FULL  BIT(16)
#define ASC_MBOX_CONTROL_EMPTY BIT(17)

#define ASC_MBOX_A2I_CONTROL 0x110
#define ASC_MBOX_A2I_SEND0   0x800
#define ASC_MBOX_A2I_SEND1   0x808
#define ASC_MBOX_A2I_RECV0   0x810
#define ASC_MBOX_A2I_RECV1   0x818

#define ASC_MBOX_I2A_CONTROL 0x114
#define ASC_MBOX_I2A_SEND0   0x820
#define ASC_MBOX_I2A_SEND1   0x828
#define ASC_MBOX_I2A_RECV0   0x830
#define ASC_MBOX_I2A_RECV1   0x838

struct asc_dev {
    uintptr_t cpu_base;
    uintptr_t base;
};

asc_dev_t *asc_init(const char *path)
{
    int asc_path[8];
    int node = adt_path_offset_trace(adt, path, asc_path);
    if (node < 0) {
        printf("asc: Error getting ASC node %s\n", path);
        return NULL;
    }

    u64 base;
    if (adt_get_reg(adt, asc_path, "reg", 0, &base, NULL) < 0) {
        printf("asc: Error getting ASC %s base address.\n", path);
        return NULL;
    }

    asc_dev_t *asc = malloc(sizeof(*asc));
    if (!asc)
        return NULL;

    asc->cpu_base = base;
    asc->base = base + 0x4000;
    return asc;
}

void asc_free(asc_dev_t *asc)
{
    free(asc);
}

void asc_cpu_start(asc_dev_t *asc)
{
    set32(asc->cpu_base + ASC_CPU_CONTROL, ASC_CPU_CONTROL_START);
}

bool asc_can_recv(asc_dev_t *asc)
{
    return !(read32(asc->base + ASC_MBOX_I2A_CONTROL) & ASC_MBOX_CONTROL_EMPTY);
}

bool asc_recv(asc_dev_t *asc, struct asc_message *msg)
{
    if (!asc_can_recv(asc))
        return false;

    msg->msg0 = read64(asc->base + ASC_MBOX_I2A_RECV0);
    msg->msg1 = (u32)read64(asc->base + ASC_MBOX_I2A_RECV1);
    dma_rmb();

    // printf("received msg: %lx %x\n", msg->msg0, msg->msg1);

    return true;
}

bool asc_recv_timeout(asc_dev_t *asc, struct asc_message *msg, u32 delay_usec)
{
    u64 delay = ((u64)delay_usec) * mrs(CNTFRQ_EL0) / 1000000;
    u64 val = mrs(CNTPCT_EL0);
    while ((mrs(CNTPCT_EL0) - val) < delay) {
        sysop("isb");
        if (asc_recv(asc, msg))
            return true;
    }
    return false;
}

bool asc_can_send(asc_dev_t *asc)
{
    return !(read32(asc->base + ASC_MBOX_A2I_CONTROL) & ASC_MBOX_CONTROL_FULL);
}

bool asc_send(asc_dev_t *asc, const struct asc_message *msg)
{
    if (!asc_can_send(asc))
        return false;

    dma_wmb();
    write64(asc->base + ASC_MBOX_A2I_SEND0, msg->msg0);
    write64(asc->base + ASC_MBOX_A2I_SEND1, msg->msg1);

    // printf("sent msg: %lx %x\n", msg->msg0, msg->msg1);
    return true;
}