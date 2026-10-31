
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <media/rc-core.h>
#include <linux/rtpm_prio.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/earlysuspend.h>
#include <linux/of_address.h>
#include <linux/timer.h>


#include "mtk_ir_core.h"
#include <mach/mt_boot_common.h>

static int mtk_ir_core_probe(struct platform_device *pdev);
static int mtk_ir_core_remove(struct platform_device *pdev);
static irqreturn_t mtk_ir_core_irq(int irq, void *dev_id);




#define LIRCBUF_SIZE 6
/*
record last interrupt hang_up place for debug question
*/

char last_hang_place[64] = { 0 };

static u32 _u4Curscancode = BTN_NONE;
static spinlock_t scancode_lock;


LIST_HEAD(mtk_ir_device_list);


struct mtk_rc_core mtk_rc_core = {
	.irq = 0,
	.irq_register = false,
	.rcdev = NULL,
	.dev_current = NULL,
	.dev_parent = NULL,
	.drv = NULL,
	.k_thread = NULL,
	.reg_irrx = NULL,
#if MTK_IRRX_AS_MOUSE_INPUT
	.p_devmouse = NULL,
#endif
};


void release(struct device *dev)
{
	return;
}

/*
 * Board-file replacement for the device-tree IRRX node.
 * id -1 marks the parent device (see mtk_ir_core_probe()).
 */
static struct platform_device mtk_ir_dev_parent = {
	.name = MTK_IR_DRIVER_NAME,
	.id = -1,
	.dev = {
		.release = release,
		},
};

/*
get boot_mode
*/
MTK_IR_MODE mtk_ir_core_getmode(void)
{

	BOOTMODE boot_mode = get_boot_mode();
	IR_LOG_ALWAYS("bootmode = %d\n", boot_mode);
	if ((boot_mode == NORMAL_BOOT) || (boot_mode == RECOVERY_BOOT)) {

		return MTK_IR_NORMAL;
	} else if (boot_mode == FACTORY_BOOT) {
		return MTK_IR_FACTORY;
	}
	return MTK_IR_MAX;
}


#ifdef CONFIG_PM_SLEEP

static int mtk_ir_core_suspend(struct device *dev)
{

	int ret = 0;
	struct platform_device *pdev = to_platform_device(dev);
	struct mtk_ir_core_platform_data *pdata =
	    (struct mtk_ir_core_platform_data *)(dev->platform_data);

	if (-1 == pdev->id) {	/* this is the parent device, so ignore it */
		ret = 0;
#if 0
		mt_ir_core_clock_enable(false);	/* disable clock */
		mt_ir_core_irda_clock_enable(false);
#endif
		goto end;
	}

	if (!pdata || !(pdata->suspend) || !(pdata->resume)) {
		IR_LOG_ALWAYS("%s, suspend arg wrong\n", pdata->input_name);
		ret = -1;
		goto end;
	}

	ret = pdata->suspend(NULL);
	IR_LOG_ALWAYS("ret(%d)\n", ret);

end:

	return 0;
}

static int mtk_ir_core_resume(struct device *dev)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(dev);
	struct mtk_ir_core_platform_data *pdata =
	    (struct mtk_ir_core_platform_data *)(dev->platform_data);


	if (-1 == pdev->id) {
		ret = 0;
#if 0
		mt_ir_core_clock_enable(true);	/* enable  irrx clock */
		mt_ir_core_irda_clock_enable(true);	/*enable irda clock */
#endif

		goto end;
	}

	if (!pdata || !(pdata->suspend) || !(pdata->resume)) {
		IR_LOG_ALWAYS("%s, resume arg wrong\n", pdata->input_name);
		ret = -1;
		goto end;
	}

	ret = pdata->resume(NULL);
	IR_LOG_ALWAYS("ret(%d)\n", ret);


end:

	return 0;
}
#endif

static const struct dev_pm_ops mtk_ir_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mtk_ir_core_suspend, mtk_ir_core_resume)

};

static const struct of_device_id mtk_ir_of_ids[] = {
	{.compatible = "mediatek,mtk_irrx",},
	{}

};

static struct platform_driver mtk_ir_core_driver = {
	.probe = mtk_ir_core_probe,
	.remove = mtk_ir_core_remove,
	.driver = {
		   .name = MTK_IR_DRIVER_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = mtk_ir_of_ids,
		   .pm = &mtk_ir_pm_ops,
		   },
};

#define IR_ASSERT_DEBUG 1

/* for Assert to debug  ir driver code */
void AssertIR(const char *szExpress, const char *szFile, int i4Line)
{
	IR_LOG_ALWAYS("\nAssertion fails at:\nFile: %s, line %d\n\n", szFile, (int)(i4Line));
	IR_LOG_ALWAYS("\t%s\n\n", szExpress);

#if IR_ASSERT_DEBUG
	dump_stack();
	panic("%s", szExpress);
#endif
}




void mtk_ir_core_send_scancode(u32 scancode)
{
	unsigned long __flags;
	struct rc_dev *rcdev = mtk_rc_core.rcdev;
	struct lirc_driver *drv = mtk_rc_core.drv;

	spin_lock_irqsave(&scancode_lock, __flags);
	_u4Curscancode = scancode;
	spin_unlock_irqrestore(&scancode_lock, __flags);
	IR_LOG_KEY("scancode 0x%x\n", _u4Curscancode);


	sprintf(last_hang_place, "%s--%d\n", __func__, __LINE__);

	if (likely(mtk_rc_core.k_thread != NULL)) {	/* transfer code to /dev/input/eventx */
		wake_up_process(mtk_rc_core.k_thread);
	}


	sprintf(last_hang_place, "%s--%d\n", __func__, __LINE__);

#if MTK_IR_DEVICE_NODE
	mtk_ir_dev_put_scancode(scancode);	/* transfer code to /dev/ir_dev */
#endif

	sprintf(last_hang_place, "%s--%d\n", __func__, __LINE__);
	if (likely((drv != NULL) && (rcdev != NULL))) {	/* transfer code to     /dev/lirc */
		int index = 0;
		struct rc_map *pmap = &(rcdev->rc_map);
		struct rc_map_table *pscan = pmap->scan;
		struct mtk_ir_msg msg = { scancode, BTN_NONE };
		/* does not find any map_key, but still transfer data to lirc driver */

		for (index = 0; index < (pmap->len); index++) {
			if (pscan[index].scancode == scancode) {
				msg.scancode = scancode;
				msg.keycode = pscan[index].keycode;
				break;
			}
		}
		lirc_buffer_write(drv->rbuf, (unsigned char *)&msg);
		wake_up(&(drv->rbuf->wait_poll));
	}
	sprintf(last_hang_place, "%s--%d\n", __func__, __LINE__);

}

void mtk_ir_core_send_mapcode(u32 mapcode, int stat)
{
	struct rc_dev *rcdev = mtk_rc_core.rcdev;
	IR_LOG_KEY("mapcode 0x%x\n", mapcode);

	if (rcdev == NULL)
		IR_LOG_ALWAYS("rcdev = NULL, send fail!!!\n");

	rc_keyup(rcdev);	/* first do key up for last key; */
	input_report_key(rcdev->input_dev, mapcode, stat);
	input_sync(rcdev->input_dev);

}

void mtk_ir_core_send_mapcode_auto_up(u32 mapcode, u32 ms)
{
	struct rc_dev *rcdev = mtk_rc_core.rcdev;
	IR_LOG_KEY("mapcode 0x%x\n", mapcode);

	if (rcdev == NULL)
		IR_LOG_ALWAYS("rcdev = NULL, send fail!!!\n");

	rc_keyup(rcdev);	/* first do key up for last key; */
	input_report_key(rcdev->input_dev, mapcode, 1);
	input_sync(rcdev->input_dev);

	msleep(ms);

	input_report_key(rcdev->input_dev, mapcode, 0);
	input_sync(rcdev->input_dev);
}

/* ir irq function */
static irqreturn_t mtk_ir_core_irq(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device *)dev_id;

	u32 scancode = BTN_NONE;

	struct mtk_ir_core_platform_data *pdata;
	mtk_ir_core_disable_hwirq();


	sprintf(last_hang_place, "irq begin\n");
	ASSERT(pdev != NULL);
	pdata = pdev->dev.platform_data;
	ASSERT(pdata != NULL);
	ASSERT(pdata->ir_hw_decode != NULL);


	scancode = pdata->ir_hw_decode(NULL);	/* get the scancode */

	if (BTN_INVALID_KEY != scancode)
		mtk_ir_core_send_scancode(scancode);

	mtk_ir_core_clear_hwirq();	/* make sure irq statu is sure clear */

	sprintf(last_hang_place, "ok here\n");

	return IRQ_HANDLED;
}



int mtk_ir_core_register_swirq(int trigger_type)
{
	int ret;
	struct mtk_ir_core_platform_data *pdata = NULL;
	/* unsigned int temp; */

	ASSERT(mtk_rc_core.dev_current != NULL);
	pdata = (mtk_rc_core.dev_current)->dev.platform_data;

	if (mtk_rc_core.irq_register == true) {
		IR_LOG_ALWAYS("irq number(%d) already registered\n", mtk_rc_core.irq);
		return 0;
	}

	IR_LOG_ALWAYS("reg irq number(%d) trigger_type(%d)\n", mtk_rc_core.irq, trigger_type);

	ret = request_irq(mtk_rc_core.irq, mtk_ir_core_irq, trigger_type,
			  pdata->input_name, (void *)(mtk_rc_core.dev_current));
	if (ret) {
		IR_LOG_ALWAYS("fail to request irq(%d), ir_dev(%s) ret = %d !!!\n",
			      mtk_rc_core.irq, pdata->input_name, ret);

		return -1;
	}
	mtk_rc_core.irq_register = true;

	return 0;
}

void mtk_ir_core_free_swirq(void)
{

	if (mtk_rc_core.irq_register == false) {
		IR_LOG_ALWAYS("irq number(%d) already freed\n", mtk_rc_core.irq);
		return;
	}
	IR_LOG_ALWAYS("free irq number(%d)\n", mtk_rc_core.irq);
	free_irq(mtk_rc_core.irq, (void *)(mtk_rc_core.dev_current));	/* first free irq */
	mtk_rc_core.irq_register = false;
	return;
}

/*
here is get message from initial kemap ,not rc-dev.map
*/
void mtk_ir_core_get_msg_by_scancode(u32 scancode, struct mtk_ir_msg *msg)
{
	struct platform_device *pdev = mtk_rc_core.dev_current;
	struct mtk_ir_core_platform_data *pdata;
	struct rc_map_list *plist;
	struct rc_map *pmap;
	struct rc_map_table *pscan;
	unsigned int size;
	int index = 0;
	ASSERT(msg != NULL);
	ASSERT(pdev != NULL);
	pdata = pdev->dev.platform_data;
	ASSERT(pdata != NULL);
	plist = pdata->p_map_list;
	ASSERT(plist != NULL);
	pmap = &(plist->map);
	pscan = pmap->scan;
	ASSERT(pscan != NULL);
	size = pmap->size;

	msg->scancode = scancode;
	msg->keycode = BTN_NONE;	/* does not find any map_key, but still transfer data to userspace */
	for (index = 0; index < size; index++) {
		if (pscan[index].scancode == scancode) {
			msg->scancode = scancode;
			msg->keycode = pscan[index].keycode;
			break;
		}
	}
}



int mtk_ir_core_create_thread(int (*threadfn) (void *data),
			      void *data,
			      const char *ps_name,
			      struct task_struct **p_struct_out, unsigned int ui4_priority)
{

	struct sched_param param;
	int ret = 0;
	struct task_struct *ptask;
	ptask = kthread_create(threadfn, data, ps_name);

	if (IS_ERR(ptask)) {
		IR_LOG_ALWAYS("unable to create kernel thread %s\n", ps_name);
		*p_struct_out = NULL;
		return -1;
	}
	param.sched_priority = ui4_priority;
	ret = sched_setscheduler(ptask, SCHED_RR, &param);
	*p_struct_out = ptask;
	return 0;
}


/*
mtk_ir_core_open()
for linux2.4 module

*/
static int mtk_ir_core_open(void *data)
{
	return 0;
}

/*
mtk_ir_core_close()
for linux2.4 module

*/
static void mtk_ir_core_close(void *data)
{
	return;
}

/*
mtk_lirc_fops's all functions is in lirc_dev.c

*/

static struct file_operations mtk_lirc_fops = {
	.owner = THIS_MODULE,
	.write = lirc_dev_fop_write,
	.unlocked_ioctl = lirc_dev_fop_ioctl,
	.read = lirc_dev_fop_read,
	.poll = lirc_dev_fop_poll,
	.open = lirc_dev_fop_open,
	.release = lirc_dev_fop_close,
	.llseek = no_llseek,
};


/* for linux input system */
static int mtk_ir_input_thread(void *pvArg)
{

	u32 u4CurKey = BTN_NONE;

	struct rc_dev *rcdev = NULL;

	unsigned long __flags;

#if MTK_IRRX_AS_MOUSE_INPUT

	MTK_IR_DEVICE_MODE dev_mode;
	struct mtk_ir_core_platform_data *pdata = NULL;
	struct mtk_ir_mouse_code *p_mousecode = NULL;
#endif

	while (!kthread_should_stop()) {

		IR_LOG_DEBUG(" mtk_ir_input_thread begin !!\n");


		set_current_state(TASK_INTERRUPTIBLE);
		if (u4CurKey == BTN_NONE) {	/* does not have key */
			schedule();	/*  */
		}
		set_current_state(TASK_RUNNING);

		spin_lock_irqsave(&scancode_lock, __flags);
		u4CurKey = _u4Curscancode;
		_u4Curscancode = BTN_NONE;
		spin_unlock_irqrestore(&scancode_lock, __flags);


		if (kthread_should_stop())	/* other place want to stop this thread; */
			continue;

		IR_LOG_DEBUG("mtk_ir_input_thread get scancode: 0x%08x\n ", u4CurKey);
		rcdev = mtk_rc_core.rcdev;

		if (unlikely(rcdev == NULL)) {
			IR_LOG_ALWAYS("rcdev = NULL!!!!!!!!!\n");
			continue;
		}
#if MTK_IRRX_AS_MOUSE_INPUT

		dev_mode = mtk_ir_mouse_get_device_mode();
		pdata = mtk_rc_core.dev_current->dev.platform_data;
		p_mousecode = &(pdata->mouse_code);

		if (u4CurKey == p_mousecode->scanswitch) {	/* this is the mode switch code */
			IR_LOG_ALWAYS("switch mode code 0x%08x\n", u4CurKey);
			if (!(rcdev->keypressed)) {
				/* last key was released, this is the first time switch code down. */
				if (dev_mode == MTK_IR_AS_IRRX) {
					mtk_ir_mouse_set_device_mode(MTK_IR_AS_MOUSE);
				} else {
					mtk_ir_mouse_set_device_mode(MTK_IR_AS_IRRX);
				}
			}
			/* this function will auto key up, when does not have key after 250ms
			   here send 0xffff, because when we hold scanswitch key,
			   we will not switch mode in every repeat
			   interrupt, we only switch when switch key down,
			   but we need to kown ,whether switch code was the
			   first time to down, so we send 0xffff.
			 */
			rc_keydown(rcdev, 0xffff, 0);
			/* this function will auto key up, when does not have key after 250m */
		} else {
			if (dev_mode == MTK_IR_AS_MOUSE) {	/* current irrx as mouse */
				if (mtk_ir_mouse_proc_key(u4CurKey, &mtk_rc_core)) {
					/* this key is mouse event, process success */
					IR_LOG_ALWAYS("process mouse key\n");
				} else {	/* this key is not mouse key ,so as ir key process */

					IR_LOG_ALWAYS("process ir key\n");
					rc_keydown(rcdev, u4CurKey, 0);
					/* this function will auto key up, when does not have key after 250ms */
				}
			} else {
				rc_keydown(rcdev, u4CurKey, 0);
				/* this function will auto key up, when does not have key after 250ms */
			}
		}

#else

		rc_keydown(rcdev, u4CurKey, 0);
		/* this function will auto key up, when does not have key after 250ms */

#endif

		u4CurKey = BTN_NONE;

	}


	IR_LOG_ALWAYS("mtk_ir_input_thread exit success\n");
	return 0;
}




/* register driver for /dev/lirc */

static int mtk_ir_lirc_register(struct device *dev_parent)
{
	struct lirc_driver *drv;	/* for lirc_driver */
	struct lirc_buffer *rbuf;	/* for store ir data */
	int ret = -ENOMEM;
	unsigned long features;

	drv = kzalloc(sizeof(struct lirc_driver), GFP_KERNEL);
	if (!drv) {
		IR_LOG_ALWAYS("kzalloc lirc_driver fail!!!\n ");
		return ret;
	}

	rbuf = kzalloc(sizeof(struct lirc_buffer), GFP_KERNEL);
	if (!rbuf) {
		IR_LOG_ALWAYS("kzalloc lirc_buffer fail!!!\n ");
		goto rbuf_alloc_failed;
	}

	ret = lirc_buffer_init(rbuf, MTK_IR_CHUNK_SIZE, LIRCBUF_SIZE);
	if (ret) {
		IR_LOG_ALWAYS("lirc_buffer_init fail ret(%d) !!!\n", ret);
		goto rbuf_init_failed;
	}

	features = 0;

	snprintf(drv->name, sizeof(drv->name), "mtk_ir_core_lirc");

	drv->minor = -1;
	drv->features = features;
	drv->data = NULL;
	drv->rbuf = rbuf;
	drv->set_use_inc = mtk_ir_core_open;
	drv->set_use_dec = mtk_ir_core_close;
	drv->chunk_size = MTK_IR_CHUNK_SIZE;
	drv->code_length = MTK_IR_CHUNK_SIZE;
	drv->fops = &mtk_lirc_fops;
	drv->dev = dev_parent;
	drv->owner = THIS_MODULE;

	drv->minor = lirc_register_driver(drv);
	if (drv->minor < 0) {
		ret = -ENODEV;
		IR_LOG_ALWAYS("lirc_register_driver fail ret(%d) !!!\n", ret);
		goto lirc_register_failed;
	}
	spin_lock_init(&scancode_lock);
	ret = mtk_ir_core_create_thread(mtk_ir_input_thread,
					NULL,
					"mtk_ir_inp_thread",
					&(mtk_rc_core.k_thread), RTPM_PRIO_SCRN_UPDATE);
	if (ret) {
		IR_LOG_ALWAYS("create mtk_ir_input_thread fail\n");
		goto ir_inp_thread_fail;
	}


	IR_LOG_ALWAYS("lirc_register_driver succeed drv->minor(%d)\n", drv->minor);
	mtk_rc_core.drv = drv;	/* store lirc_driver pointor to mtk_rc_core.drv */

	return 0;

ir_inp_thread_fail:
	lirc_unregister_driver(drv->minor);
lirc_register_failed:
	lirc_buffer_free(rbuf);
rbuf_init_failed:
	kfree(rbuf);
rbuf_alloc_failed:
	kfree(drv);
	return ret;
}

/* unregister driver for /dev/lirc */

static int mtk_ir_lirc_unregister(struct mtk_rc_core *pdev)
{


	struct lirc_driver *drv = pdev->drv;
	if (pdev->k_thread) {	/* first stop thread */
		kthread_stop(pdev->k_thread);
		pdev->k_thread = NULL;
	}

	if (drv) {
		lirc_unregister_driver(drv->minor);
		lirc_buffer_free(drv->rbuf);
		kfree(drv);
		pdev->drv = NULL;
	}
	return 0;
}



static int mtk_ir_core_probe(struct platform_device *pdev)
{

	struct mtk_ir_core_platform_data *pdata = pdev->dev.platform_data;
	struct rc_dev *rcdev = NULL;


	int ret = 0;

	/* id = -1 is the parent IRRX platform_device. In the DT tree it came from
	   the device tree; on this board-file kernel it is registered by
	   mtk_ir_core_init(). It maps the registers, powers the block and
	   registers lirc_driver and mtk_ir_input_thread, which are shared by all
	   protocol devices. */
	if (-1 == pdev->id) {
		mtk_rc_core.dev_parent = pdev;
		mtk_rc_core.irq = MTK_IRRX_IRQ_NUMBER;

		ret = mtk_ir_hal_clock_probe();	/* ioremap the IRRX registers */
		if (ret) {
			IR_LOG_ALWAYS("irrx register map fail ret(%d)\n", ret);
			return ret;
		}
		mtk_rc_core.reg_irrx = (void __iomem *)IRRX_BASE_VIRTUAL;

		IR_LOG_ALWAYS("IRRX_BASE_PHY = %lx\n", IRRX_BASE_PHY);
		IR_LOG_ALWAYS("IRRX_BASE_VIRTUAL = %lx\n", IRRX_BASE_VIRTUAL);
		IR_LOG_ALWAYS("irq = %d\n", mtk_rc_core.irq);

		/* The stock 3.4 driver enables the clock and sets the pin at init and
		   never gates the clock again. The registers must never be touched
		   with the clock off. */
		mt_ir_core_irda_clock_enable(true);
		mt_ir_core_clock_enable(true);
		mtk_ir_core_pin_set();
		mt_ir_hal_power_probe();
		mt_ir_hal_power_enable(true);

		ret = mtk_ir_lirc_register(&(pdev->dev));
		if (ret) {
			IR_LOG_ALWAYS("mtk_ir_lirc_register fail ret(%d) !!!\n", ret);
		}
		return ret;
	}

	/* register really ir device nec or rc5 or rc6 .... */
	ASSERT(pdata != NULL);
	ASSERT(pdata->init_hw != NULL);
	ASSERT(pdata->uninit_hw != NULL);
	ASSERT(pdata->p_map_list != NULL);
	ASSERT(pdata->ir_hw_decode != NULL);

	platform_device_unregister(mtk_rc_core.dev_current);	/* first unregister old device */

	ret = pdata->init_hw();	/* init this  ir's  hw */
	if (ret) {
		IR_LOG_ALWAYS(" fail to init_hw for ir_dev(%s) ret = %d!!!\n",
			      pdata->input_name, ret);
		goto err_init_hw;
	}

	rcdev = rc_allocate_device();	/* alloc rc device and rc->input */
	if (!rcdev) {
		ret = -ENOMEM;
		IR_LOG_ALWAYS("rc_allocate_device fail\n");
		goto err_allocate_device;
	}

	rcdev->driver_type = RC_DRIVER_SCANCODE;
	rcdev->allowed_protos = pdata->p_map_list->map.rc_type;
	rcdev->input_name = pdata->input_name;	/* /proc/bus/input/devices */
	rcdev->input_id.bustype = BUS_HOST;
	rcdev->input_id.version = IR_VERSION;
	rcdev->input_id.product = IR_PRODUCT;
	rcdev->input_id.vendor = IR_VENDOR;
	rcdev->driver_name = MTK_IR_DRIVER_NAME;
	rcdev->map_name = pdata->p_map_list->map.name;

	ret = rc_register_device(rcdev);
	if (ret < 0) {

		IR_LOG_ALWAYS("failed to register rc device for ir_dev(%s) ret(%d)!!!\n",
			      pdata->input_name, ret);
		goto err_register_rc_device;
	}
	rc_set_keypress_timeout(pdata->i4_keypress_timeout);
	clear_bit(EV_MSC, rcdev->input_dev->evbit);
	clear_bit(MSC_SCAN, rcdev->input_dev->mscbit);

#if MTK_IRRX_AS_MOUSE_INPUT
	mtk_rc_core.p_devmouse = mtk_ir_mouse_register_input(pdev);
	if (NULL == mtk_rc_core.p_devmouse) {
		IR_LOG_ALWAYS("fail to register ir_mouse device(%s)\n", pdata->mousename);
		goto err_register_mousedev;
	}
#endif

	mtk_rc_core.rcdev = rcdev;
	mtk_rc_core.dev_current = pdev;
	mtk_ir_core_create_attr(&(pdev->dev));

	ret = mtk_ir_core_register_swirq(MTK_IRRX_IRQ_TRIGGER);	/* IRQF_TRIGGER_LOW, as stock */

	if (ret) {
		goto err_request_irq;

	}

	return 0;

err_request_irq:
	rc_unregister_device(rcdev);
	rcdev = NULL;
err_register_mousedev:
err_register_rc_device:
	rc_free_device(rcdev);
	rcdev = NULL;
	mtk_rc_core.rcdev = NULL;
err_allocate_device:
	pdata->uninit_hw();
err_init_hw:

	return ret;
}

static int mtk_ir_core_remove(struct platform_device *pdev)
{
	struct mtk_ir_core_platform_data *pdata = pdev->dev.platform_data;
	struct rc_dev *rcdev = mtk_rc_core.rcdev;

	IR_LOG_ALWAYS("mtk_ir_core_remove device name(%s)\n", kobject_name(&(pdev->dev).kobj));

	if (-1 == pdev->id) {	/* it is mtk_ir_dev_parent, it must be the last paltform device to unregister */
		mtk_ir_lirc_unregister(&mtk_rc_core);	/* unregister lirc driver */

		/* registers were mapped by the HAL and stay mapped */
		return 0;
	}

	if (mtk_rc_core.dev_current != pdev) {	/* it is not the current active device, do nothing ,only prompt */
		IR_LOG_ALWAYS("mtk_ir_core_remove device name(%s) key_map_name(%s)\n",
			      kobject_name(&(pdev->dev).kobj), pdata->p_map_list->map.name);
		return 0;
	}

	if (rcdev != NULL) {	/* ok, it is the current active device */
		IR_LOG_ALWAYS("unregister current active device: name(%s),key_map_name(%s)",
			      kobject_name(&(pdev->dev).kobj), pdata->p_map_list->map.name);

		mtk_ir_core_remove_attr(&(pdev->dev));
		mtk_ir_core_free_swirq();
		pdata->uninit_hw();	/* first uinit this type ir device */
#if MTK_IRRX_AS_MOUSE_INPUT
		if (mtk_rc_core.p_devmouse != NULL) {
			mtk_ir_mouse_unregister_input(mtk_rc_core.p_devmouse);
			mtk_rc_core.p_devmouse = NULL;
		}
#endif
		rc_unregister_device(rcdev);	/* unregister rcdev */
		mtk_rc_core.rcdev = NULL;
		mtk_rc_core.dev_current = NULL;
	}

	return 0;
}


void mtk_ir_core_log_always(const char *fmt, ...)
{
	va_list args;

#if  MTK_IR_DEVICE_NODE
	int size_data = 0;
	char buff[IR_NETLINK_MSG_SIZE] = { 0 };
	char *p = buff;
	struct message_head head = { MESSAGE_NORMAL, 0 };

	if (0 == mtk_ir_get_log_to()) {	/* end key to kernel */
		va_start(args, fmt);
		vprintk(fmt, args);
		va_end(args);
	} else {		/* here is log to userspace */


		p += IR_NETLINK_MESSAGE_HEADER;
		va_start(args, fmt);
		size_data =
		    vsnprintf(p, IR_NETLINK_MSG_SIZE - IR_NETLINK_MESSAGE_HEADER - 1, fmt, args);
		va_end(args);
		p[size_data] = 0;
		head.message_size = size_data + 1;
		memcpy(buff, &head, IR_NETLINK_MESSAGE_HEADER);

		mtk_ir_netlink_msg_q_send(buff, IR_NETLINK_MESSAGE_HEADER + head.message_size);

	}
#else

	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);
#endif


}


#ifdef CONFIG_HAS_EARLYSUSPEND

static void mtk_ir_core_early_suspend(struct early_suspend *h)
{

	struct mtk_ir_core_platform_data *pdata = NULL;
	IR_LOG_ALWAYS("\n");


	if (likely(mtk_rc_core.dev_current != NULL)) {
		pdata = mtk_rc_core.dev_current->dev.platform_data;
		ASSERT(pdata != NULL);
		if (pdata->early_suspend) {
			pdata->early_suspend(NULL);
		}
	}

}

static void mtk_ir_core_late_resume(struct early_suspend *h)
{

	struct mtk_ir_core_platform_data *pdata = NULL;
	IR_LOG_ALWAYS("\n");

	if (likely(mtk_rc_core.dev_current != NULL)) {
		pdata = mtk_rc_core.dev_current->dev.platform_data;
		ASSERT(pdata != NULL);
		if (pdata->late_resume) {
			pdata->late_resume(NULL);
		}
	}
}

static struct early_suspend mtk_ir_early_suspend_desc = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1,
	.suspend = mtk_ir_core_early_suspend,
	.resume = mtk_ir_core_late_resume,
};
#endif


static int __init mtk_ir_core_init(void)
{
	int ret;

#ifdef MTK_IRRX_SUPPORT_ENABLE
	IR_LOG_ALWAYS("MTK_IRRX_SUPPORT_ENABLE ok\n");
#endif
	ret = platform_driver_register(&mtk_ir_core_driver);	/* first register driver */

	if (ret) {
		IR_LOG_ALWAYS("mtk_ir_core_driver register fail\n");
		goto fail;
	}
	/* the parent (id -1) must exist before any protocol device probes */
	ret = platform_device_register(&mtk_ir_dev_parent);
	if (ret) {
		IR_LOG_ALWAYS("mtk_ir_dev_parent register fail ret(%d)\n", ret);
		goto fail;
	}
#if (MTK_IRRX_PROTOCOL == MTK_IR_ID_NEC)
	ret = platform_device_register(&(mtk_ir_dev_nec.dev_platform));	/* register  nec device */
#elif (MTK_IRRX_PROTOCOL == MTK_IR_ID_RC6)
	ret = platform_device_register(&(mtk_ir_dev_rc6.dev_platform));	/* register  rc6 device */
#elif (MTK_IRRX_PROTOCOL == MTK_IR_ID_RC5)
	ret = platform_device_register(&(mtk_ir_dev_rc5.dev_platform));	/* register  rc5 device */
#endif


	/* mt_ir_core_clock_enable(true); */
	if (ret) {
		IR_LOG_ALWAYS("mtk_ir_dev register fail!!!\n");
		goto fail;
	}
#if MTK_IR_DEVICE_NODE
	mtk_ir_dev_init();
#endif

	INIT_LIST_HEAD(&(mtk_ir_dev_nec.list));
	list_add(&(mtk_ir_dev_nec.list), &mtk_ir_device_list);

	INIT_LIST_HEAD(&(mtk_ir_dev_rc6.list));
	list_add(&(mtk_ir_dev_rc6.list), &mtk_ir_device_list);

	INIT_LIST_HEAD(&(mtk_ir_dev_rc5.list));
	list_add(&(mtk_ir_dev_rc5.list), &mtk_ir_device_list);



#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&mtk_ir_early_suspend_desc);
#endif

	IR_LOG_ALWAYS("mtk_ir_core_init success\n");

fail:
	return ret;
}

static void __exit mtk_ir_core_exit(void)
{

#if MTK_IR_DEVICE_NODE
	mtk_ir_dev_exit();
#endif

	if (mtk_rc_core.dev_current) {
		platform_device_unregister(mtk_rc_core.dev_current);
		mtk_rc_core.dev_current = NULL;
	}

	platform_driver_unregister(&mtk_ir_core_driver);
	platform_device_unregister(&mtk_ir_dev_parent);	/* for this ,all device will be unregister */

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&mtk_ir_early_suspend_desc);
#endif


}
module_init(mtk_ir_core_init);
module_exit(mtk_ir_core_exit);

/*****************************************************************************/
MODULE_AUTHOR("ji.wang <ji.wang@mediatek.com>");
MODULE_DESCRIPTION(IRRX_TAG);
MODULE_LICENSE("GPL");
