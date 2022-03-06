/*
 * Simple synchronous userspace interface to SPI devices
 *
 * Copyright (C) 2006 SWAPP
 *	Andrea Paterniani <a.paterniani@swapp-eng.it>
 * Copyright (C) 2007 David Brownell (simplification, cleanup)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
*
*  et512.spi.c
*  Date: 2016/03/16
*  Version: 0.9.0.1
*  Revise Date:  2016/03/24
*  Copyright (C) 2007-2016 Egis Technology Inc.
*
*/


#include <linux/interrupt.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#endif

#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/list.h>

//#include <mach/gpio.h>
//#include <plat/gpio-cfg.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

//#include <mt_spi_hal.h>
//#include <mt_spi.h>

//#include "mtk_spi.h"

//#include <mt-plat/mt_gpio.h>
#include "et520.h"
//#include "navi_input.h"

#define EGIS_NAVI_INPUT 0 // 1:open ; 0:close

#define FP_SPI_DEBUG
#define EDGE_TRIGGER_FALLING    0x0
#define	EDGE_TRIGGER_RAISING    0x1
#define	LEVEL_TRIGGER_LOW       0x2
#define	LEVEL_TRIGGER_HIGH      0x3

#define GPIO_PIN_IRQ  126 
#define GPIO_PIN_RESET 93
#define GPIO_PIN_33V 94

extern void mt_spi_enable_master_clk(struct spi_device *spidev);
extern void mt_spi_disable_master_clk(struct spi_device *spidev);

//void mt_spi_enable_clk(struct mt_spi_t *ms);
//void mt_spi_disable_clk(struct mt_spi_t *ms);

//struct mt_spi_t *egistec_mt_spi_t;

/*
 * FPS interrupt table
 */
 
struct interrupt_desc fps_ints = {0 , 0, "BUT0" , 0};


unsigned int bufsiz = 4096;

int gpio_irq;
int request_irq_done = 0;
int egistec_platformInit_done = 0;

int fingerprint_adm = -1;

#define EDGE_TRIGGER_FALLING    0x0
#define EDGE_TRIGGER_RISING    0x1
#define LEVEL_TRIGGER_LOW       0x2
#define LEVEL_TRIGGER_HIGH      0x3

int egistec_platformInit(struct egistec_data *egistec);
int egistec_platformFree(struct egistec_data *egistec);

struct ioctl_cmd {
int int_mode;
int detect_period;
int detect_threshold;
}; 

static void delete_device_node(void);
static struct egistec_data *g_data;

DECLARE_BITMAP(minors, N_SPI_MINORS);
LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

static struct of_device_id egistec_match_table[] = {
	{ .compatible = "mediatek,finger-fp",},
//        { .compatible = "goodix,goodix-fp",},
	{},
};

#if 0 
gjxxx
static struct of_device_id et512_spi_of_match[] = {
	{ .compatible = "mediatek,finger-fp", },
	{}
};
MODULE_DEVICE_TABLE(of, et512_spi_of_match);
#endif

// add for dual sensor start
#if 0
static struct of_device_id fpswitch_match_table[] = {
	{ .compatible = "fp_id,fp_id",},
	{},
};
#endif
//add for dual sensor end
MODULE_DEVICE_TABLE(of, egistec_match_table);


/* ------------------------------ Interrupt -----------------------------*/
/*
 * Interrupt description
 */

#define FP_INT_DETECTION_PERIOD  10
#define FP_DETECTION_THRESHOLD	10

static DECLARE_WAIT_QUEUE_HEAD(interrupt_waitq);
#if 1
//static void spi_clk_enable(struct egistec_data *egistec, u8 bonoff)
static void spi_clk_enable(u8 bonoff)
{


	if (bonoff) {
		pr_err("%s line:%d enable spi clk\n", __func__,__LINE__);
		mt_spi_enable_master_clk(g_data->spi);
//		count = 1;
	} else //if ((count > 0) && (bonoff == 0)) 
	{
		pr_err("%s line:%d disable spi clk\n", __func__,__LINE__);

		mt_spi_disable_master_clk(g_data->spi);

//		count = 0;
	}


}

#endif

/*
 *	FUNCTION NAME.
 *		interrupt_timer_routine
 *
 *	FUNCTIONAL DESCRIPTION.
 *		basic interrupt timer inital routine
 *
 *	ENTRY PARAMETERS.
 *		gpio - gpio address
 *
 *	EXIT PARAMETERS.
 *		Function Return
 */

void interrupt_timer_routine(unsigned long _data)
{
	struct interrupt_desc *bdata = (struct interrupt_desc *)_data;

	DEBUG_PRINT("FPS interrupt count = %d", bdata->int_count);
	if (bdata->int_count >= bdata->detect_threshold) {
		bdata->finger_on = 1;
		DEBUG_PRINT("FPS triggered !!!!!!!\n");
	} else {
		DEBUG_PRINT("FPS not triggered !!!!!!!\n");
	}

	bdata->int_count = 0;
	__pm_wakeup_event(&g_data->ttw_wl, msecs_to_jiffies(3000));
	wake_up_interruptible(&interrupt_waitq);
}


static irqreturn_t fp_eint_func(int irq, void *dev_id)
{
	if (!fps_ints.int_count)
		mod_timer(&fps_ints.timer,jiffies + msecs_to_jiffies(fps_ints.detect_period));
	fps_ints.int_count++;
//	DEBUG_PRINT("-----------   zq fp fp_eint_func  , fps_ints.int_count=%d",fps_ints.int_count);
	return IRQ_HANDLED;
}

static irqreturn_t fp_eint_func_ll(int irq , void *dev_id)
{
	DEBUG_PRINT("[egis]fp_eint_func_ll\n");
	fps_ints.finger_on = 1;
	//fps_ints.int_count = 0;
	disable_irq_nosync(gpio_irq);
	fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;
	__pm_wakeup_event(&g_data->ttw_wl, msecs_to_jiffies(3000));
	wake_up_interruptible(&interrupt_waitq);
	return IRQ_RETVAL(IRQ_HANDLED);
}


static ssize_t adm_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	if (fingerprint_adm == 0){
		 return snprintf(buf, 2, "1");
	}else{
		 return snprintf(buf, 2, "0");
	}
}

static struct device_attribute adm_on_off_attr = {
	.attr = {
		.name = "adm",
		.mode = S_IRUSR,
	},
	.show = adm_show,
};


/*
 *	FUNCTION NAME.
 *		Interrupt_Init
 *
 *	FUNCTIONAL DESCRIPTION.
 *		button initial routine
 *
 *	ENTRY PARAMETERS.
 *		int_mode - determine trigger mode
 *			EDGE_TRIGGER_FALLING    0x0
 *			EDGE_TRIGGER_RAISING    0x1
 *			LEVEL_TRIGGER_LOW        0x2
 *			LEVEL_TRIGGER_HIGH       0x3
 *
 *	EXIT PARAMETERS.
 *		Function Return int
 */

int Interrupt_Init(struct egistec_data *egistec,int int_mode,int detect_period,int detect_threshold)
{

	int err = 0;
//	int status = 0;
	struct device_node *node_eint = NULL;
DEBUG_PRINT("FP --  %s mode = %d period = %d threshold = %d\n",__func__,int_mode,detect_period,detect_threshold);
DEBUG_PRINT("FP --  %s request_irq_done = %d gpio_irq = %d  pin = %d  \n",__func__,request_irq_done,gpio_irq, egistec->irqPin);


	fps_ints.detect_period = detect_period;
	fps_ints.detect_threshold = detect_threshold;
	fps_ints.int_count = 0;
	fps_ints.finger_on = 0;


	if (request_irq_done == 0)
	{
		node_eint = of_find_compatible_node(NULL, NULL, "mediatek,fpsensor_fp_eint_n6");
		if (node_eint == NULL) {
			err = -EINVAL;
			printk("mediatek,fpsensor_fp_eint cannot find node_eint rc = %d.\n",err);
			goto done;
		}

		egistec->irqPin = of_get_named_gpio(node_eint, "int-gpios", 0);
		printk(" egis irq_gpio=%d",egistec->irqPin);
	   

		err = devm_gpio_request(&g_data->pd->dev, egistec->irqPin, "egis,gpio_irq");
		if (err) {
				printk( KERN_ERR " egis failed to request gpio %d\n",  egistec->irqPin);
				goto done;
			}
			  
		gpio_irq = gpio_to_irq(egistec->irqPin);

		
		DEBUG_PRINT("[Interrupt_Init] flag current: %d disable: %d enable: %d\n",
		fps_ints.drdy_irq_flag, DRDY_IRQ_DISABLE, DRDY_IRQ_ENABLE);
			
		if (int_mode == EDGE_TRIGGER_RISING){
		DEBUG_PRINT("%s EDGE_TRIGGER_RISING\n", __func__);
		err = request_irq(gpio_irq, fp_eint_func,IRQ_TYPE_EDGE_RISING,"fp_detect-eint", egistec);
			if (err){
				pr_err("request_irq failed==========%s,%d\n", __func__,__LINE__);
				}				
		}
		else if (int_mode == EDGE_TRIGGER_FALLING){
			DEBUG_PRINT("%s EDGE_TRIGGER_FALLING\n", __func__);
			err = request_irq(gpio_irq, fp_eint_func,IRQ_TYPE_EDGE_FALLING,"fp_detect-eint", egistec);
			if (err){
				pr_err("request_irq failed==========%s,%d\n", __func__,__LINE__);
				}	
		}
		else if (int_mode == LEVEL_TRIGGER_LOW) {
			DEBUG_PRINT("%s LEVEL_TRIGGER_LOW\n", __func__);
			err = request_irq(gpio_irq, fp_eint_func_ll,IRQ_TYPE_LEVEL_LOW,"fp_detect-eint", egistec);
			if (err){
				pr_err("request_irq failed==========%s,%d\n", __func__,__LINE__);
				}
		}
		else if (int_mode == LEVEL_TRIGGER_HIGH){
			DEBUG_PRINT("%s LEVEL_TRIGGER_HIGH\n", __func__);
			err = request_irq(gpio_irq, fp_eint_func_ll,IRQ_TYPE_LEVEL_HIGH,"fp_detect-eint", egistec);
			if (err){
				pr_err("request_irq failed==========%s,%d\n", __func__,__LINE__);
				}
		}
		DEBUG_PRINT("[Interrupt_Init]:gpio_to_irq return: %d\n", gpio_irq);
		DEBUG_PRINT("[Interrupt_Init]:request_irq return: %d\n", err);
	
		fps_ints.drdy_irq_flag = DRDY_IRQ_ENABLE;
		enable_irq_wake(gpio_irq);
		request_irq_done = 1;
	}
		
	if (fps_ints.drdy_irq_flag == DRDY_IRQ_DISABLE){
		fps_ints.drdy_irq_flag = DRDY_IRQ_ENABLE;
		enable_irq_wake(gpio_irq);
		enable_irq(gpio_irq);
	}

	fingerprint_adm = 0;

done:
	return 0;
}

int etspi_reset_request(struct egistec_data *etspi)
{
	int status = 0;
	DEBUG_PRINT("%s\n", __func__);
	if (etspi != NULL) {
		/* Initial Reset Pin*/
		status = gpio_request(etspi->rstPin, "reset-gpio");
		if (status < 0) {
			pr_err("%s gpio_requset etspi_Reset failed\n",
				__func__);
			goto etspi_platformInit_rst_failed;
		}
		gpio_direction_output(etspi->rstPin, 1);
		if (status < 0) {
			pr_err("%s gpio_direction_output Reset failed\n",
					__func__);
			status = -EBUSY;
			goto etspi_platformInit_rst_failed;
		}
		/* gpio_set_value(etspi->rstPin, 1); */
		pr_err("et520:  reset to high\n");
		
	}
	return status;

etspi_platformInit_rst_failed:
	gpio_free(etspi->rstPin);
	gpio_free(etspi->irqPin);
	
	pr_err("%s is failed\n", __func__);
	return status;
}

/*
 *	FUNCTION NAME.
 *		Interrupt_Free
 *
 *	FUNCTIONAL DESCRIPTION.
 *		free all interrupt resource
 *
 *	EXIT PARAMETERS.
 *		Function Return int
 */

int Interrupt_Free(struct egistec_data *egistec)
{
	DEBUG_PRINT("%s\n", __func__);
	fps_ints.finger_on = 0;
	
	if (fps_ints.drdy_irq_flag == DRDY_IRQ_ENABLE) {
		DEBUG_PRINT("%s (DISABLE IRQ)\n", __func__);
		disable_irq_nosync(gpio_irq);

		del_timer_sync(&fps_ints.timer);
		fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;
	}
	return 0;
}

/*
 *	FUNCTION NAME.
 *		fps_interrupt_re d
 *
 *	FUNCTIONAL DESCRIPTION.
 *		FPS interrupt read status
 *
 *	ENTRY PARAMETERS.
 *		wait poll table structure
 *
 *	EXIT PARAMETERS.
 *		Function Return int
 */

unsigned int fps_interrupt_poll(
struct file *file,
struct poll_table_struct *wait)
{
	unsigned int mask = 0;

	poll_wait(file, &interrupt_waitq, wait);
	if (fps_ints.finger_on) {
		mask |= POLLIN | POLLRDNORM;
	}
	return mask;
}

void fps_interrupt_abort(void)
{
	DEBUG_PRINT("%s\n", __func__);
	fps_ints.finger_on = 0;
	wake_up_interruptible(&interrupt_waitq);
}

/*-------------------------------------------------------------------------*/
static void egistec_reset(struct egistec_data *egistec)
{
	DEBUG_PRINT("%s\n", __func__);
	
	#ifdef CONFIG_OF
	pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_reset_low);
	mdelay(15);
	pinctrl_select_state(egistec->pinctrl_gpios, egistec->pins_reset_high);
	#endif	
	
}

static ssize_t egistec_read(struct file *filp,
	char __user *buf,
	size_t count,
	loff_t *f_pos)
{
	/*Implement by vendor if needed*/
	return 0;
}

static ssize_t egistec_write(struct file *filp,
	const char __user *buf,
	size_t count,
	loff_t *f_pos)
{
	/*Implement by vendor if needed*/
	return 0;
}

static long egistec_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

	int retval = 0;
	struct egistec_data *egistec;
	struct ioctl_cmd data;
	int status = 0;

	memset(&data, 0, sizeof(data));
	printk("%s  ---------   zq  001  ---  cmd = 0x%X \n", __func__, cmd);	
	egistec = filp->private_data;


	if(!egistec_platformInit_done)
	/* platform init */
	status = egistec_platformInit(egistec);
	if (status != 0) {
		pr_err("%s platforminit failed\n", __func__);
		//goto egistec_probe_platformInit_failed;
	}



	switch (cmd) {
	case INT_TRIGGER_INIT:

		if (copy_from_user(&data, (int __user *)arg, sizeof(data))) {
			retval = -EFAULT;
		goto done;
		}

		//DEBUG_PRINT("fp_ioctl IOCTL_cmd.int_mode %u,
		//		IOCTL_cmd.detect_period %u,
		//		IOCTL_cmd.detect_period %u (%s)\n",
		//		data.int_mode,
		//		data.detect_period,
		//		data.detect_threshold, __func__);

		DEBUG_PRINT("fp_ioctl >>> fp Trigger function init\n");
		retval = Interrupt_Init(egistec, data.int_mode,data.detect_period,data.detect_threshold);
		DEBUG_PRINT("fp_ioctl trigger init = %x\n", retval);
	break;

	case FP_SENSOR_RESET:
			//gpio_request later
			DEBUG_PRINT("fp_ioctl ioc->opcode == FP_SENSOR_RESET --");
			egistec_reset(egistec);
		goto done;
	case INT_TRIGGER_CLOSE:
			DEBUG_PRINT("fp_ioctl <<< fp Trigger function close\n");
			retval = Interrupt_Free(egistec);
			DEBUG_PRINT("fp_ioctl trigger close = %x\n", retval);
		goto done;
	case INT_TRIGGER_ABORT:
			DEBUG_PRINT("fp_ioctl <<< fp Trigger function close\n");
			fps_interrupt_abort();
		goto done;
	case FP_FREE_GPIO:
			DEBUG_PRINT("fp_ioctl <<< FP_FREE_GPIO -------  \n");
			egistec_platformFree(egistec);
		goto done;

	case FP_SPICLK_ENABLE:
			printk("fp_ioctl <<< FP_SPICLK_ENABLE -------  \n");
		        spi_clk_enable(1);
			//mt_spi_enable_clk(egistec_mt_spi_t);
		goto done;		
	case FP_SPICLK_DISABLE:
			printk("fp_ioctl <<<FP_SPICLK_DISABLE -------  \n");
		       spi_clk_enable(0);
			//mt_spi_disable_clk(egistec_mt_spi_t);
		goto done;		
	case DELETE_DEVICE_NODE:
			DEBUG_PRINT("fp_ioctl <<< DELETE_DEVICE_NODE -------  \n");
			delete_device_node();
		goto done;	
	case FP_PIN_RESOURCE_REQUEST:
			DEBUG_PRINT("fp_ioctl <<< FP_RESET_REQUEST\n");
			//retval = etspi_reset_request(egistec); 
			//egistec_reset(egistec);
		goto done;		
	default:
	retval = -ENOTTY;
	break;
	}
	
	
	
done:

	DEBUG_PRINT("%s ----------- zq done  \n", __func__);
	return (retval);
}

#ifdef CONFIG_COMPAT
static long egistec_compat_ioctl(struct file *filp,
	unsigned int cmd,
	unsigned long arg)
{
	return egistec_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define egistec_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

static int egistec_open(struct inode *inode, struct file *filp)
{
	struct egistec_data *egistec;
	int			status = -ENXIO;

	DEBUG_PRINT("%s\n", __func__);
	printk("%s  ---------   zq    \n", __func__);
	
	mutex_lock(&device_list_lock);

	list_for_each_entry(egistec, &device_list, device_entry)
	{
		if (egistec->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}
	if (status == 0) {
		if (egistec->buffer == NULL) {
			egistec->buffer = kmalloc(bufsiz, GFP_KERNEL);
			if (egistec->buffer == NULL) {
//				dev_dbg(&egistec->spi->dev, "open/ENOMEM\n");
				status = -ENOMEM;
			}
		}
		if (status == 0) {
			egistec->users++;
			filp->private_data = egistec;
			nonseekable_open(inode, filp);
		}
	} else {
		pr_debug("%s nothing for minor %d\n"
			, __func__, iminor(inode));
	}
	mutex_unlock(&device_list_lock);
	return status;
}

static int egistec_release(struct inode *inode, struct file *filp)
{
	struct egistec_data *egistec;

	DEBUG_PRINT("%s\n", __func__);

	mutex_lock(&device_list_lock);
	egistec = filp->private_data;
	filp->private_data = NULL;

	/* last close? */
	egistec->users--;
	if (egistec->users == 0) {
		int	dofree;

		kfree(egistec->buffer);
		egistec->buffer = NULL;

		/* ... after we unbound from the underlying device? */
		spin_lock_irq(&egistec->spi_lock);
		dofree = (egistec->pd == NULL);
		spin_unlock_irq(&egistec->spi_lock);

		if (dofree)
			kfree(egistec);		
	}
	mutex_unlock(&device_list_lock);
	return 0;

}

int egistec_platformFree(struct egistec_data *egistec)
{
	int status = 0;
	DEBUG_PRINT("%s\n", __func__);
	if (egistec_platformInit_done != 1)
	return status;
	if (egistec != NULL) {
		if (request_irq_done==1)
		{
		free_irq(gpio_irq, NULL);
		request_irq_done = 0;
		}
	gpio_free(egistec->irqPin);
	gpio_free(GPIO_PIN_RESET);
	}

	egistec_platformInit_done = 0;

	DEBUG_PRINT("%s successful status=%d\n", __func__, status);
	return status;
}


int egistec_platformInit(struct egistec_data *egistec)
{
	int status = 0;
	DEBUG_PRINT("%s\n", __func__);

	if (egistec != NULL) {

		/* Initial Reset Pin
		status = gpio_request(egistec->rstPin, "reset-gpio");
		if (status < 0) {
			pr_err("%s gpio_requset egistec_Reset failed\n",
				__func__);
			goto egistec_platformInit_rst_failed;
		}
		*/
//		gpio_direction_output(egistec->rstPin, 1);
//		if (status < 0) {
//			pr_err("%s gpio_direction_output Reset failed\n",
//					__func__);
//			status = -EBUSY;
//			goto egistec_platformInit_rst_failed;
//		}
		
		//added to initialize it as high
//		gpio_set_value(GPIO_PIN_RESET, 1);
//		msleep(30);
		
//		gpio_set_value(GPIO_PIN_RESET, 0);
//		msleep(30);
//		gpio_set_value(GPIO_PIN_RESET, 1);
//		msleep(20);

		/* initial 33V power pin */
//		gpio_direction_output(egistec->vcc_33v_Pin, 1);
//		gpio_set_value(egistec->vcc_33v_Pin, 1);

/*		status = gpio_request(egistec->vcc_33v_Pin, "33v-gpio");
		if (status < 0) {
			pr_err("%s gpio_requset egistec_Reset failed\n",
				__func__);
			goto egistec_platformInit_rst_failed;
		}
		gpio_direction_output(egistec->vcc_33v_Pin, 1);
		if (status < 0) {
			pr_err("%s gpio_direction_output Reset failed\n",
					__func__);
			status = -EBUSY;
			goto egistec_platformInit_rst_failed;
		}

		gpio_set_value(egistec->vcc_33v_Pin, 1);
*/


		/* Initial IRQ Pin
		status = gpio_request(egistec->irqPin, "irq-gpio");
		if (status < 0) {
			pr_err("%s gpio_request egistec_irq failed\n",
				__func__);
			goto egistec_platformInit_irq_failed;
		}
		*/
/*		
		status = gpio_direction_input(egistec->irqPin);
		if (status < 0) {
			pr_err("%s gpio_direction_input IRQ failed\n",
				__func__);
//			goto egistec_platformInit_gpio_init_failed;
		}
*/

	}

	egistec_platformInit_done = 1;

	DEBUG_PRINT("%s successful status=%d\n", __func__, status);
	return status;
/*
egistec_platformInit_gpio_init_failed:
	gpio_free(egistec->irqPin);
//	gpio_free(egistec->vcc_33v_Pin);
egistec_platformInit_irq_failed:
	gpio_free(egistec->rstPin);
egistec_platformInit_rst_failed:
*/
	pr_err("%s is failed\n", __func__);
	return status;
}



static int egistec_parse_dt(struct device *dev,
	struct egistec_data *data)
{
//	struct device_node *np = dev->of_node;

#ifdef CONFIG_OF
	int ret;

	struct device_node *node = NULL;
	struct platform_device *pdev = NULL;

	printk(KERN_ERR "%s, from dts pinctrl\n", __func__);

	node = of_find_compatible_node(NULL, NULL, "mediatek,finger-fp");
	if (node) {
		pdev = of_find_device_by_node(node);
		if (pdev) {
			data->pinctrl_gpios = devm_pinctrl_get(&pdev->dev);
			if (IS_ERR(data->pinctrl_gpios)) {
				ret = PTR_ERR(data->pinctrl_gpios);
				printk(KERN_ERR "%s can't find fingerprint pinctrl\n", __func__);
				return ret;
			}
/*
			data->pins_irq = pinctrl_lookup_state(data->pinctrl_gpios, "default");
			if (IS_ERR(data->pins_irq)) {
				ret = PTR_ERR(data->pins_irq);
				printk(KERN_ERR "%s can't find fingerprint pinctrl irq\n", __func__);
				return ret;
			}
			data->pins_miso_spi = pinctrl_lookup_state(data->pinctrl_gpios, "miso_spi");
			if (IS_ERR(data->pins_miso_spi)) {
				ret = PTR_ERR(data->pins_miso_spi);
				printk(KERN_ERR "%s can't find fingerprint pinctrl miso_spi\n", __func__);
				//return ret;
			}
			data->pins_miso_pullhigh = pinctrl_lookup_state(data->pinctrl_gpios, "miso_pullhigh");
			if (IS_ERR(data->pins_miso_pullhigh)) {
				ret = PTR_ERR(data->pins_miso_pullhigh);
				printk(KERN_ERR "%s can't find fingerprint pinctrl miso_pullhigh\n", __func__);
				//return ret;
			}
			data->pins_miso_pulllow = pinctrl_lookup_state(data->pinctrl_gpios, "miso_pulllow");
			if (IS_ERR(data->pins_miso_pulllow)) {
				ret = PTR_ERR(data->pins_miso_pulllow);
				printk(KERN_ERR "%s can't find fingerprint pinctrl miso_pulllow\n", __func__);
				//return ret;
			}

*/
			data->pins_reset_high = pinctrl_lookup_state(data->pinctrl_gpios, "rst-high");
			if (IS_ERR(data->pins_reset_high)) {
				ret = PTR_ERR(data->pins_reset_high);
				printk(KERN_ERR "%s can't find fingerprint pinctrl reset_high\n", __func__);
				return ret;
			}
			data->pins_reset_low = pinctrl_lookup_state(data->pinctrl_gpios, "rst-low");
			if (IS_ERR(data->pins_reset_low)) {
				ret = PTR_ERR(data->pins_reset_low);
				printk(KERN_ERR "%s can't find fingerprint pinctrl reset_low\n", __func__);
				return ret;
			}
			printk(KERN_ERR "%s, get pinctrl success!\n", __func__);
		} else {
			printk(KERN_ERR "%s platform device is null\n", __func__);
		}
	} else {
		printk(KERN_ERR "%s device node is null\n", __func__);
	}


      egistec_platformInit_done=1;
#endif
	return 0;	
}

static const struct file_operations egistec_fops = {
	.owner = THIS_MODULE,
	.write = egistec_write,
	.read = egistec_read,
	.unlocked_ioctl = egistec_ioctl,
	.compat_ioctl = egistec_compat_ioctl,
	.open = egistec_open,
	.release = egistec_release,
	.llseek = no_llseek,
	.poll = fps_interrupt_poll
};

/*-------------------------------------------------------------------------*/

static struct class *egistec_class;

/*-------------------------------------------------------------------------*/

static int egistec_probe(struct platform_device *pdev);
static int egistec_remove(struct platform_device *pdev);




typedef struct {
	struct spi_device      *spi;
	struct class           *class;
	struct device          *device;
//	struct cdev            cdev;
	dev_t                  devno;
	u8                     *huge_buffer;
	size_t                 huge_buffer_size;
	struct input_dev       *input_dev;
} et512_data_t;


static int egistec_check_chipid(void)
{
	int status;
	//struct spi_device *spi;
	struct spi_message m;
	u8 write_addr[3];
	u8 read_addr[3];
	u8 chip_id = 0;
    
	// Write and read data in one data query section 
	struct spi_transfer t_read_data = {
		.tx_buf = write_addr,
		.rx_buf = read_addr,
		.len = 3,
		.speed_hz = 1*1000000,
	};


	write_addr[0] = 0x20;
	write_addr[1] = 0x00;

	DEBUG_PRINT("---  %s -------------  \n", __func__);

	read_addr[0] = read_addr[1] = read_addr[2] = 0;

	DEBUG_PRINT("spi_message_init !!!!!!!\n");
	spi_message_init(&m);
	DEBUG_PRINT("spi_message_add_tail!!!!!!!\n");
	spi_message_add_tail(&t_read_data, &m);
	DEBUG_PRINT("spi_sync!!!!!!!\n");
	status = spi_sync(g_data->spi, &m);
//	*buf = read_addr[2];
	printk("egis chipid = 0x%x - 0x%x - 0x%x !!\n",read_addr[0],read_addr[1],read_addr[2]);
    chip_id = read_addr[2];
    if(chip_id == 0xaa){
        printk("%s egis check chipid pass");
        return 0;
    }
    printk("%s egis check chipid fail");
    return -1;

}

extern struct spi_device *spi_fingerprint;
extern int fpc_or_chipone_exist;
extern bool sunwave_fp_exist;
extern bool egis_fp_exist;
extern bool fingtech_fp_exist;//oa 6648421,gjx.wt,add,20200915,fingertech fingerprint bringup

#if 0 
gjxxx
/* -------------------------------------------------------------------- */
static int et512_spi_probe(struct spi_device *spi)
{
//	struct device *dev = &spi->dev;
	int error = 0;
	et512_data_t *et512 = NULL;

	struct class    *fingerprint_adm_class;
	struct device *fingerprint_adm_dev;
	
	/* size_t buffer_size; */

	printk(KERN_ERR "et512_spi_probe enter++++++\n");
#if 1
	et512 = kzalloc(sizeof(*et512), GFP_KERNEL);
	if (!et512) {
		/*
		dev_err(&spi->dev,
		"failed to allocate memory for struct et512_data\n");
		*/
		return -ENOMEM;
	}
	printk(KERN_INFO"%s\n", __func__);

	spi_set_drvdata(spi, et512);
#endif	
	
	g_data->spi = spi;
//	spi_clk_enable(1);
//	egistec_mt_spi_t=spi_master_get_devdata(spi->master);
	spi_fingerprint=spi;


	fingerprint_adm_class = class_create(THIS_MODULE, "fingerprint");
	if (IS_ERR(fingerprint_adm_class)){
		printk(KERN_ERR "fpc  class_create failed \n");
	}

	fingerprint_adm_dev = device_create(fingerprint_adm_class, NULL, 0, NULL, "fingerprint");

	if (device_create_file(fingerprint_adm_dev, &adm_on_off_attr) < 0){
		printk(KERN_ERR "Failed to create device file(%s)!\n", adm_on_off_attr.attr.name);
	}

	return error;
}

/* -------------------------------------------------------------------- */
static int et512_spi_remove(struct spi_device *spi)
{
	et512_data_t *et512 = spi_get_drvdata(spi);
	spi_clk_enable(0);
//	pr_debug("%s\n", __func__);

//	et512_manage_sysfs(et512, spi, false);

	//et512_sleep(et512, true);

	//cdev_del(&et512->cdev);

	//unregister_chrdev_region(et512->devno, 1);

	//et512_cleanup(et512, spi);
	kfree(et512);

	return 0;
}
#endif
#if 0 
gjxxx
static struct spi_driver spi_driver = {
	.driver = {
		.name	= "et512",
		.owner	= THIS_MODULE,
		.of_match_table = et512_spi_of_match,
        .bus	= &spi_bus_type,
	},
	.probe	= et512_spi_probe,
	.remove	= et512_spi_remove
};
#endif
static struct platform_driver egistec_driver = {
	.driver = {
		.name		= "et512",
		.owner		= THIS_MODULE,
		.of_match_table = egistec_match_table,
	},
    .probe =    egistec_probe,
    .remove =   egistec_remove,
};


static int egistec_remove(struct platform_device *pdev)
{
//	#if EGIS_NAVI_INPUT
	struct device *dev = &pdev->dev;
	struct egistec_data *egistec = dev_get_drvdata(dev);
//	#endif
	
    DEBUG_PRINT("%s(#%d)\n", __func__, __LINE__);
	free_irq(gpio_irq, NULL);
	wakeup_source_trash(&g_data->ttw_wl);
	#if EGIS_NAVI_INPUT
	uinput_egis_destroy(egistec);
	sysfs_egis_destroy(egistec);
	#endif
	
	del_timer_sync(&fps_ints.timer);
	request_irq_done = 0;
	kfree(egistec);
    return 0;
}
/*
static int egistec_check_hwid(struct spi_device *spi)
{
	int error = 0;
	u32 time_out = 0;
	u8 tmp_buf[2] = {0};
	u16 hardware_id;

	do {
		egis_spi_read_hwid(spi, tmp_buf);
		printk(KERN_INFO "%s, egis chip version is 0x%x, 0x%x\n", __func__, tmp_buf[0], tmp_buf[1]);

		time_out++;

		hardware_id = ((tmp_buf[0] << 8) | (tmp_buf[1]));
		pr_err("egis hardware_id[0]= 0x%x id[1]=0x%x\n", tmp_buf[0], tmp_buf[1]);

		if((( hardware_id & 0xFF) == 0xaa) || (( hardware_id & 0xFF00) == 0xaa))
		{
			pr_err("egis match hardware_id = 0x%x is true\n", hardware_id);
			error = 0;
		}
		else
		{
			pr_err("egis match hardware_id = 0x%x is failed\n", hardware_id);
			error = -1;
		}


		if (!error) {
			printk(KERN_INFO "egis %s, egis chip version check pass, time_out=%d\n", __func__, time_out);
			return 0;
		}
	} while(time_out < 3);

	printk(KERN_INFO "%s, egis chip version read failed, time_out=%d\n", __func__, time_out);

	return -1;
}
*/
static int egistec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct egistec_data *egistec;
	int status = 0;
	unsigned long minor;
	struct class *fingerprint_adm_class;
	struct device *fingerprint_adm_dev;
#if 0
	struct regulator *vdd_reg;
#endif
	DEBUG_PRINT("%s initial\n", __func__);

	BUILD_BUG_ON(N_SPI_MINORS > 256);
	status = register_chrdev(ET512_MAJOR, "et512", &egistec_fops);
	if (status < 0) {
			pr_err("%s register_chrdev error.\n", __func__);
			return status;
	}

	egistec_class = class_create(THIS_MODULE, "et512");
	if (IS_ERR(egistec_class)) {
		pr_err("%s class_create error.\n", __func__);
		unregister_chrdev(ET512_MAJOR, egistec_driver.driver.name);
		return PTR_ERR(egistec_class);
	}

#if 0

	vdd_reg = regulator_get(NULL, "irtx_ldo");

	 if (vdd_reg == NULL) {
			 printk("%s()  eigs regulator_get fail!\n", __func__);
	 }

	status = regulator_enable(vdd_reg);
	if (status){
		printk("%s()  eigs regulator_enable fail!\n", __func__);
	}

	status = regulator_set_voltage(vdd_reg, 3300000, 3300000);
	 if (status< 0) {
			 printk("%s() eigs regulator_set_voltage fail! ret:%d.\n", __func__,status);
	 }
#endif
	/* Allocate driver data */
	egistec = kzalloc(sizeof(*egistec), GFP_KERNEL);
	if (egistec== NULL) {
		pr_err("%s - Failed to kzalloc\n", __func__);
		return -ENOMEM;
	}

	/* device tree call */
	if (pdev->dev.of_node) {
		status = egistec_parse_dt(&pdev->dev, egistec);
		if (status) {
			pr_err("%s - Failed to parse DT\n", __func__);
			goto egistec_probe_parse_dt_failed;
		}
	}

	
//	egistec->rstPin = GPIO_PIN_RESET;
//	egistec->irqPin = GPIO_PIN_IRQ;
//	egistec->vcc_33v_Pin = GPIO_PIN_33V;
	
	
	
	/* Initialize the driver data */
	egistec->pd = pdev;
	g_data = egistec;
    g_data->spi = spi_fingerprint;

//+Mtr 3380,gjx.wt,20200414,modify,egis fp without fp can't boot
    status = egistec_check_chipid();
    if(status == -1) {
        goto egistec_probe_parse_dt_failed;
    }
    egis_fp_exist = true;
	fingerprint_adm = status;
	fingerprint_adm_class = class_create(THIS_MODULE, "fingerprint");
	if (IS_ERR(fingerprint_adm_class)){
		printk(KERN_ERR "fpc  class_create failed \n");
	}

	fingerprint_adm_dev = device_create(fingerprint_adm_class, NULL, 0, NULL, "fingerprint");

	if (device_create_file(fingerprint_adm_dev, &adm_on_off_attr) < 0){
		printk(KERN_ERR "Failed to create device file(%s)!\n", adm_on_off_attr.attr.name);
	}
//-Mtr 3380,gjx.wt,20200414,modify,egis fp without fp can't boot

	wakeup_source_init(&g_data->ttw_wl, "fpsensor_ttw_wl");
	spin_lock_init(&egistec->spi_lock);
	mutex_init(&egistec->buf_lock);
	mutex_init(&device_list_lock);

	INIT_LIST_HEAD(&egistec->device_entry);
#if 0
	/* platform init */
	status = egistec_platformInit(egistec);
	if (status != 0) {
		pr_err("%s platforminit failed\n", __func__);
		goto egistec_probe_platformInit_failed;
	}
#endif
	
#if 0 //gpio_request later
	/* platform init */
	status = egistec_platformInit(egistec);
	if (status != 0) {
		pr_err("%s platforminit failed\n", __func__);
		goto egistec_probe_platformInit_failed;
	}
#endif

	fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;

	/*
	 * If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *fdev;
		egistec->devt = MKDEV(ET512_MAJOR, minor);
		fdev = device_create(egistec_class, &pdev->dev, egistec->devt,
					egistec, "esfp0");
		status = IS_ERR(fdev) ? PTR_ERR(fdev) : 0;
	} else {
		dev_dbg(&pdev->dev, "no minor number available!\n");
		status = -ENODEV;
	}
	if (status == 0) {
		set_bit(minor, minors);
		list_add(&egistec->device_entry, &device_list);
	}

	mutex_unlock(&device_list_lock);

	if (status == 0){
		dev_set_drvdata(dev, egistec);
	}
	else {
		goto egistec_probe_failed;
	}

	////gpio_request later
	//egistec_reset(egistec);

	fps_ints.drdy_irq_flag = DRDY_IRQ_DISABLE;

	/* the timer is for ET310 */
	setup_timer(&fps_ints.timer, interrupt_timer_routine,(unsigned long)&fps_ints);
	add_timer(&fps_ints.timer);

/*	

	struct device_node *node = NULL;
	int value;

	node = of_find_compatible_node(NULL, NULL, "goodix,goodix-fp");

	mt_spi_enable_master_clk(gf_dev->spi);

*/

	#if EGIS_NAVI_INPUT
	/*
	 * William Add.
	 */
	sysfs_egis_init(egistec);
	uinput_egis_init(egistec);
	#endif

	DEBUG_PRINT("%s : initialize success %d\n", __func__, status);	

	request_irq_done = 0;
	return status;

egistec_probe_failed:
	device_destroy(egistec_class, egistec->devt);
	class_destroy(egistec_class);

//egistec_probe_platformInit_failed:
egistec_probe_parse_dt_failed:
	kfree(egistec);
	pr_err("%s is failed\n", __func__);
	return status;
}

static void delete_device_node(void)
{
	//int retval;
	DEBUG_PRINT("%s\n", __func__);
	//del_timer_sync(&fps_ints.timer);
	//spi_clk_enable(0);
	device_destroy(egistec_class, g_data->devt);
	DEBUG_PRINT("device_destroy \n");
	list_del(&g_data->device_entry);
	DEBUG_PRINT("list_del \n");
	class_destroy(egistec_class);
	DEBUG_PRINT("class_destroy\n");
       //gjxxx spi_unregister_driver(&spi_driver);
	DEBUG_PRINT("spi_unregister_driver\n");
	unregister_chrdev(ET512_MAJOR, egistec_driver.driver.name);
	DEBUG_PRINT("unregister_chrdev\n");
	g_data = NULL;
	platform_driver_unregister(&egistec_driver);
	//DEBUG_PRINT("platform_driver_unregister\n");
}


static int __init egis512_init(void)
{
	int status = 0;

	printk(KERN_INFO "[shi]%s\n", __func__);
	
    if (sunwave_fp_exist) {
        printk(KERN_ERR "%s sunwave sensor has been detected, so exit egis sensor detect.\n",__func__);
        return -EINVAL;
    }

    if(fpc_or_chipone_exist != -1){
       printk(KERN_ERR "%s, chipone or fpc sensor has been detected.\n", __func__);
        return -EINVAL;
    }

//+oa 6648421,gjx.wt,add,20200915,fingertech fingerprint bringup
    if(fingtech_fp_exist){
        printk(KERN_ERR "%s, fingtech sensor has been detected.\n", __func__);
        return -EINVAL;
    }
//-oa 6648421,gjx.wt,add,20200915,fingertech fingerprint bringup

    if(spi_fingerprint == NULL)
        printk(KERN_ERR "%s Line:%d spi device is NULL,cannot spi transfer\n",
                __func__, __LINE__);
    else {
        status = platform_driver_register(&egistec_driver);	
        if(status)
	    {
	        printk(KERN_INFO "egis register platform driver failed");
            return status;
       	}
    } 
#if 0 
gjxxx
	if (spi_register_driver(&spi_driver))
	{
		printk(KERN_ERR "register spi driver fail%s\n", __func__);
		return -EINVAL;
	}
#endif
//			spi_clk_enable(1);
//	mt_spi_enable_clk(egistec_mt_spi_t);//temp
//	printk(KERN_ERR "spi enabled----\n");

     return status;
}

static void __exit egis512_exit(void)
{

      platform_driver_unregister(&egistec_driver);
      //gjxxxspi_unregister_driver(&spi_driver);
}
late_initcall(egis512_init);
module_exit(egis512_exit);

MODULE_AUTHOR("Wang YuWei, <robert.wang@egistec.com>");
MODULE_DESCRIPTION("SPI Interface for ET512");
MODULE_LICENSE("GPL");
