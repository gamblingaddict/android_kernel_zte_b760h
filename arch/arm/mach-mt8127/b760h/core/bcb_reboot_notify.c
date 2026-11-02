#include <linux/init.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/syscalls.h>

#include "partition_define.h"   /* PartInfo, Region; adjust include path */

extern int PART_NUM;
extern int eMMC_rw_x(loff_t addr, u32 *buffer, int host_num, int iswrite,
                     u32 totalsize, int transtype, Region part);

static char bcb_buf[2048] __attribute__((aligned(4))) = "boot-recovery";

static int bcb_reboot_notify(struct notifier_block *nb,
                             unsigned long action, void *data)
{
    const char *cmd = data;
    int i, err = -ENODEV;

    pr_err("bcb: notify action=%lu cmd=%s\n", action, cmd ? cmd : "(null)");

    if (action != SYS_RESTART || !cmd ||
        (strcmp(cmd, "recovery") && strcmp(cmd, "bootloader")))
        return NOTIFY_DONE;

    pr_err("bcb: PART_NUM=%d\n", PART_NUM);

    for (i = 0; i < PART_NUM; i++) {
        if (PartInfo[i].name && !strcmp(PartInfo[i].name, "misc")) {
            pr_err("bcb: misc at 0x%llx\n", (unsigned long long)PartInfo[i].start_address);
            err = eMMC_rw_x(PartInfo[i].start_address, (u32 *)bcb_buf,
                            0, 1, sizeof(bcb_buf), 1, EMMC_PART_USER);
            break;
        }
    }

    pr_err("bcb: write result %d\n", err);
    return NOTIFY_DONE;
}

static struct notifier_block bcb_reboot_nb = {
    .notifier_call = bcb_reboot_notify,
    .priority = INT_MAX,
};

static int __init bcb_init(void)
{
    pr_err("bcb: init\n");
    return register_reboot_notifier(&bcb_reboot_nb);
}
late_initcall(bcb_init);