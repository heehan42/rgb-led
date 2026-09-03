// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/spinlock_types_raw.h>
#include <linux/stddef.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/workqueue_types.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/cdev.h>
#include <linux/printk.h>
#include <linux/gpio.h>
#include <linux/irq.h>

#define LOW 0
#define HIGH 1

#define RE_CLK_IDX 0
#define RE_DT_IDX 1
#define RE_SW_IDX 2
#define RE_MAX_IDX 3

#define NR_ROT_STATES 8
#define NR_SW_STATES 4

#define SZ_ROT_SEQ 5
#define SZ_RE_ROT_SEQ_FIFO 4
#define SZ_RE_EVENT_FIFO 4
#define SZ_RE_SW_FIFO 4

enum re_rot_phase {
	RE_ROT_PHASE_LL,
	RE_ROT_PHASE_LH,
	RE_ROT_PHASE_HL,
	RE_ROT_PHASE_HH,
};

struct re_rot_seq {
	enum re_rot_phase ring_buf[SZ_ROT_SEQ];
	int tail_idx;
};

struct re_rot_state {
	enum re_rot_phase ring_buf[SZ_ROT_SEQ];
	int tail_idx;
};

struct re_rot_state_q {
	raw_spinlock_t lock;
	DECLARE_KFIFO(fifo, struct re_rot_state, SZ_RE_ROT_SEQ_FIFO);
};

struct re_sw_state {
	bool up;
};

struct re_sw_state_q {
	raw_spinlock_t lock;
	DECLARE_KFIFO(fifo, struct re_sw_state, SZ_RE_SW_FIFO);
};

enum re_event_type {
	RE_EVENT_CW,
	RE_EVENT_CCW,
	RE_EVENT_SW_DOWN,
	RE_EVENT_SW_UP,
};

struct re_event {
	enum re_event_type type;
};

struct re_event_waitq {
	struct mutex mutex;
	wait_queue_head_t head;
	DECLARE_KFIFO(fifo, struct re_event, SZ_RE_EVENT_FIFO);
};

struct re_data {
	struct cdev cdev;
	struct device *dev;

	struct gpio_desc *gpiod[RE_MAX_IDX];
	int irq[RE_MAX_IDX];

	raw_spinlock_t rrs_lock;
	struct re_rot_state rrs;
	struct re_rot_state_q rrs_q;
	struct work_struct rot_work;

	struct re_sw_state rss;
	struct re_sw_state_q rss_q;
	struct work_struct sw_work;

	struct re_event_waitq event_waitq;
};

static struct class *re_class;
const char driver_name[] = "rotary_encoder";
const char *re_pin_names[RE_MAX_IDX] = { "clk", "dt", "sw" };

static irqreturn_t re_rot_irq(int irq, void *dev_id)
{
	struct re_data *data;
	enum re_rot_phase phase;
	int clk_val, dt_val;
	int tail_idx;

	data = dev_id;
	clk_val = gpiod_get_value(data->gpiod[RE_CLK_IDX]);
	dt_val = gpiod_get_value(data->gpiod[RE_DT_IDX]);
	phase = (enum re_rot_phase)(clk_val << 1 | dt_val << 0);

	raw_spin_lock(&data->rrs_lock);
	tail_idx = data->rrs.tail_idx;
	if (phase != data->rrs.ring_buf[tail_idx]) {
		tail_idx = (tail_idx + 1) % SZ_ROT_SEQ;
		data->rrs.ring_buf[tail_idx] = phase;
		data->rrs.tail_idx = tail_idx;

		if (phase == RE_ROT_PHASE_HH) {
			unsigned int should_queue_work;

			raw_spin_lock(&data->rrs_q.lock);
			should_queue_work = kfifo_put(&data->rrs_q.fifo, data->rrs);
			raw_spin_unlock(&data->rrs_q.lock);

			if (should_queue_work) {
				queue_work(system_wq, &data->rot_work);
			}
		}
	}
	raw_spin_unlock(&data->rrs_lock);

	return IRQ_HANDLED;
};

static void re_rot_work(struct work_struct *work)
{
	unsigned long lock_flags;
	struct re_rot_state rot_state;
	unsigned int ret;
	struct re_data *data = container_of(work, struct re_data, rot_work);

	raw_spin_lock_irqsave(&data->rrs_q.lock, lock_flags);
	ret = kfifo_get(&data->rrs_q.fifo, &rot_state);
	raw_spin_unlock_irqrestore(&data->rrs_q.lock, lock_flags);

	if (ret) {
		int ret = 0;
		struct re_event event;

		if (rot_state.ring_buf[(rot_state.tail_idx + 1) % SZ_ROT_SEQ] == RE_ROT_PHASE_HH &&
		    rot_state.ring_buf[(rot_state.tail_idx + 2) % SZ_ROT_SEQ] == RE_ROT_PHASE_LH &&
		    rot_state.ring_buf[(rot_state.tail_idx + 3) % SZ_ROT_SEQ] == RE_ROT_PHASE_LL &&
		    rot_state.ring_buf[(rot_state.tail_idx + 4) % SZ_ROT_SEQ] == RE_ROT_PHASE_HL &&
		    rot_state.ring_buf[(rot_state.tail_idx + 0) % SZ_ROT_SEQ] == RE_ROT_PHASE_HH) {
			// queue cw event
			event.type = RE_EVENT_CW;

			mutex_lock(&data->event_waitq.mutex);
			ret = kfifo_put(&data->event_waitq.fifo, event);
			mutex_unlock(&data->event_waitq.mutex);
		} else if (
			rot_state.ring_buf[(rot_state.tail_idx + 1) % SZ_ROT_SEQ] == RE_ROT_PHASE_HH &&
			rot_state.ring_buf[(rot_state.tail_idx + 2) % SZ_ROT_SEQ] == RE_ROT_PHASE_HL &&
			rot_state.ring_buf[(rot_state.tail_idx + 3) % SZ_ROT_SEQ] == RE_ROT_PHASE_LL &&
			rot_state.ring_buf[(rot_state.tail_idx + 4) % SZ_ROT_SEQ] == RE_ROT_PHASE_LH &&
			rot_state.ring_buf[(rot_state.tail_idx + 0) % SZ_ROT_SEQ] == RE_ROT_PHASE_HH) {
			// queue ccw event
			event.type = RE_EVENT_CCW;

			mutex_lock(&data->event_waitq.mutex);
			ret = kfifo_put(&data->event_waitq.fifo, event);
			mutex_unlock(&data->event_waitq.mutex);
		}

		if (ret) {
			wake_up_interruptible(&data->event_waitq.head);
			dev_info(data->dev, "[%s : %d] %s event queued.", __func__, __LINE__, event.type == RE_EVENT_CW ? "CW" : "CCW");
		}
	}
}

static irqreturn_t re_sw_irq(int irq, void *dev_id)
{
	pr_info("%s\n", __func__);

	int ret = 0;
	struct re_data *data = dev_id;
	int sw_val = gpiod_get_value(data->gpiod[RE_SW_IDX]);
	struct re_sw_state sw_state = {
		.up = sw_val == 1 ? true : false,
	};

	raw_spin_lock(&data->rss_q.lock);
	ret = kfifo_put(&data->rss_q.fifo, sw_state);
	raw_spin_unlock(&data->rss_q.lock);

	if (ret) {
		queue_work(system_wq, &data->sw_work);
	}

	return IRQ_HANDLED;
};

static void re_sw_work(struct work_struct *work)
{
	struct re_data *data = container_of(work, struct re_data, sw_work);
	unsigned long lock_flags;
	int ret;
	struct re_sw_state sw_state;

	raw_spin_lock_irqsave(&data->rss_q.lock, lock_flags);
	ret = kfifo_get(&data->rss_q.fifo, &sw_state);
	raw_spin_unlock_irqrestore(&data->rss_q.lock, lock_flags);

	if (ret) {
		struct re_event event;

		if (sw_state.up)
			event.type = RE_EVENT_SW_UP;
		else
			event.type = RE_EVENT_SW_DOWN;

		mutex_lock(&data->event_waitq.mutex);
		ret = kfifo_put(&data->event_waitq.fifo, event);
		mutex_unlock(&data->event_waitq.mutex);

		if (ret) {
			pr_info("[%s:%d] %d event queued.\n", __func__, __LINE__, event.type);
			wake_up_interruptible(&data->event_waitq.head);
		}
	}
}

static ssize_t re_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	pr_info("%s\n", __func__);

	int ret;
	struct re_event event;
	char *event_type;
	int len;
	struct re_data *data = filp->private_data;

	while (true) {
		ret = wait_event_interruptible(data->event_waitq.head, !kfifo_is_empty(&data->event_waitq.fifo));

		if (ret)
			return ret;

		mutex_lock(&data->event_waitq.mutex);

		if (kfifo_get(&data->event_waitq.fifo, &event)) {
			mutex_unlock(&data->event_waitq.mutex);
			break;
		}

		mutex_unlock(&data->event_waitq.mutex);
	}

	switch (event.type) {
	case RE_EVENT_CW:
		event_type = "CW";
		len = 2;
		break;

	case RE_EVENT_CCW:
		event_type = "CCW";
		len = 3;
		break;

	case RE_EVENT_SW_DOWN:
		event_type = "DOWN";
		len = 4;
		break;
	case RE_EVENT_SW_UP:
		event_type = "UP";
		len = 2;
		break;
	}

	if (len < count) {
		ret = copy_to_user(buf, event_type, len);
		if (ret) {
			dev_err(data->dev, "[%s : %d]\n", __func__, __LINE__);
			return -EINVAL;
		}
	}

	return len;
}

static ssize_t re_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	pr_info("%s\n", __func__);

	struct re_data *data = filp->private_data;
	return count;
}

static int re_open(struct inode *inode, struct file *filp)
{
	pr_info("%s\n", __func__);

	struct re_data *data = container_of(inode->i_cdev, struct re_data, cdev);
	filp->private_data = data;
	return 0;
}

static int re_release(struct inode *inode, struct file *filp)
{
	pr_info("%s\n", __func__);

	struct re_data *data = container_of(inode->i_cdev, struct re_data, cdev);
	return 0;
}

static struct file_operations re_fops = {
	.read = re_read,
	.write = re_write,
	.open = re_open,
	.release = re_release,
};

static int init_re_data(struct re_data *data, struct platform_device *pdev)
{
	int ret;

	data->dev = &pdev->dev;

	for (int i = 0; i < RE_MAX_IDX; ++i) {
		// get gpiod
		data->gpiod[i] = devm_gpiod_get(data->dev, re_pin_names[i], GPIOD_IN);
		if (IS_ERR(data->gpiod[i]))
			return dev_err_probe(data->dev, PTR_ERR(data->gpiod[i]), "failed to get %s gpiod.\n", re_pin_names[i]);

		// get irq
		data->irq[i] = platform_get_irq_byname(pdev, re_pin_names[i]);
		if (data->irq[i] <= 0)
			return dev_err_probe(data->dev, data->irq[i], "failed to %s irq.\n", re_pin_names[i]);

		// request irq
		irq_handler_t handler;
		char *handler_name;
		switch (i) {
		case RE_CLK_IDX:
		case RE_DT_IDX:
			handler = re_rot_irq;
			handler_name = "rotary-encoder-rot";
			break;
		case RE_SW_IDX:
			handler = re_sw_irq;
			handler_name = "rotary-encoder-sw";
			break;
		}
		ret = devm_request_irq(data->dev, data->irq[i], handler, 0, handler_name, data);
		if (ret)
			return dev_err_probe(data->dev, ret, "failed to request %s irq.\n", re_pin_names[i]);
	}

	// work
	INIT_WORK(&data->rot_work, re_rot_work);
	INIT_WORK(&data->sw_work, re_sw_work);

	// rrs
	raw_spin_lock_init(&data->rrs_lock);
	memset(&data->rrs.ring_buf, 0, sizeof(data->rrs.ring_buf));
	data->rrs.tail_idx = 0;

	// rrs_q
	raw_spin_lock_init(&data->rrs_q.lock);
	INIT_KFIFO(data->rrs_q.fifo);

	// rss
	raw_spin_lock_init(&data->rss_q.lock);
	INIT_KFIFO(data->rss_q.fifo);

	// event wait queue
	mutex_init(&data->event_waitq.mutex);
	init_waitqueue_head(&data->event_waitq.head);
	INIT_KFIFO(data->event_waitq.fifo);

	return 0;
}

static void uninit_re_data(struct re_data *data)
{
	// cancel work <- prevent UAF
	cancel_work_sync(&data->rot_work);
	cancel_work_sync(&data->sw_work);
}

static int re_probe(struct platform_device *pdev)
{
	pr_info("%s\n", __func__);

	int ret;
	dev_t devno;

	// per-device data
	struct re_data *data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return dev_err_probe(&pdev->dev, -ENOMEM, "failed to alloc data memory.\n");

	ret = init_re_data(data, pdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, data);

	ret = alloc_chrdev_region(&devno, 0, 1, driver_name);
	if (ret)
		return dev_err_probe(data->dev, ret, "failed to alloc chardev region.\n");

	// cdev
	cdev_init(&data->cdev, &re_fops);
	data->cdev.owner = THIS_MODULE;
	ret = cdev_add(&data->cdev, devno, 1);
	if (ret) {
		unregister_chrdev_region(devno, 1);
		return dev_err_probe(data->dev, ret, "failed to add cdev.\n");
	}

	// device node
	struct device *dev = device_create(re_class, &pdev->dev, devno, data, driver_name);
	if (IS_ERR(dev)) {
		cdev_del(&data->cdev);
		unregister_chrdev_region(devno, 1);
		return dev_err_probe(data->dev, PTR_ERR(dev), "failed to create device node.\n");
	}

	pr_info("%s end\n", __func__);

	return 0;
}

static void re_remove(struct platform_device *pdev)
{
	pr_info("%s\n", __func__);

	struct re_data *data = platform_get_drvdata(pdev);
	dev_t devno = data->cdev.dev;

	uninit_re_data(data);

	device_destroy(re_class, devno);
	cdev_del(&data->cdev);
	unregister_chrdev_region(devno, 1);
}

static struct of_device_id re_match_table[] = {
	{ .compatible = "heehan,rotary-encoder" },
	{}
};
MODULE_DEVICE_TABLE(of, re_match_table);

static struct platform_driver re_driver = {
	.driver = {
		.name = driver_name,
		.of_match_table = re_match_table,
	},
	.probe = re_probe,
	.remove = re_remove,
};

static int __init re_init(void)
{
	int ret;

	pr_info("%s\n", __func__);

	// class
	re_class = class_create("encoder");
	if (IS_ERR(re_class)) {
		pr_err("failed to create class.\n");
		return PTR_ERR(re_class);
	}

	// platform driver
	ret = platform_driver_register(&re_driver);
	if (ret) {
		class_destroy(re_class);
		pr_err("failed to create platform driver of rotary encoder.\n");
		return ret;
	}

	return 0;
}

static void __exit re_exit(void)
{
	pr_info("%s\n", __func__);

	platform_driver_unregister(&re_driver); // remove 호출
	class_destroy(re_class);
}

module_init(re_init);
module_exit(re_exit);
MODULE_LICENSE("GPL");