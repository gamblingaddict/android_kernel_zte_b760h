#include <linux/init.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/syscalls.h>

static char bcb_buf[2048] = "boot-recovery";

static int bcb_reboot_notify(struct notifier_block *nb,
                             unsigned long action, void *data)
{
    const char *cmd = data;
    struct file *f;
    mm_segment_t old_fs;
    loff_t pos = 0;
    ssize_t ret;

    pr_err("bcb: notify action=%lu cmd=%s\n", action, cmd ? cmd : "(null)");

    if (action != SYS_RESTART || !cmd || strcmp(cmd, "recovery"))
        return NOTIFY_DONE;

    f = filp_open("/dev/misc", O_WRONLY | O_SYNC, 0);
    sys_sync();
    if (IS_ERR(f)) {
        pr_err("bcb: open /dev/misc failed (%ld)\n", PTR_ERR(f));
        return NOTIFY_DONE;
    }

    old_fs = get_fs();
    set_fs(KERNEL_DS);
    ret = vfs_write(f, bcb_buf, 14, &pos);
    set_fs(old_fs);

    if (ret != sizeof(bcb_buf))
        pr_err("bcb: write returned %zd\n", ret);
    else {
        pr_err("bcb: wrote ok\n");
        vfs_fsync(f, 0);
    }

    filp_close(f, NULL);
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