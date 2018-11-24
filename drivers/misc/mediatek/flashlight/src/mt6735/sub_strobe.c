
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "kd_camera_typedef.h"
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/version.h>
#ifdef CONFIG_COMPAT
#include <linux/fs.h>
#include <linux/compat.h>
#endif
#include "kd_flashlight.h"

#include <linux/mutex.h>
#include <linux/platform_device.h>
/******************************************************************************
 * Debug configuration
******************************************************************************/
/* availible parameter */
/* ANDROID_LOG_ASSERT */
/* ANDROID_LOG_ERROR */
/* ANDROID_LOG_WARNING */
/* ANDROID_LOG_INFO */
/* ANDROID_LOG_DEBUG */
/* ANDROID_LOG_VERBOSE */
#define TAG_NAME "[sub_strobe.c]"
#define PK_DBG_NONE(fmt, arg...)    do {} while (0)
#define PK_DBG_FUNC(fmt, arg...)    pr_debug(TAG_NAME "%s: " fmt, __func__ , ##arg)
#define PK_WARN(fmt, arg...)        pr_warn(TAG_NAME "%s: " fmt, __func__ , ##arg)
#define PK_NOTICE(fmt, arg...)      pr_notice(TAG_NAME "%s: " fmt, __func__ , ##arg)
#define PK_INFO(fmt, arg...)        pr_info(TAG_NAME "%s: " fmt, __func__ , ##arg)
#define PK_TRC_FUNC(f)              pr_debug(TAG_NAME "<%s>\n", __func__)
#define PK_TRC_VERBOSE(fmt, arg...) pr_debug(TAG_NAME fmt, ##arg)
#define PK_ERROR(fmt, arg...)       pr_err(TAG_NAME "%s: " fmt, __func__ , ##arg)

#define DEBUG_LEDS_STROBE
#ifdef DEBUG_LEDS_STROBE
#define PK_DBG PK_DBG_FUNC
#define PK_VER PK_TRC_VERBOSE
#define PK_ERR PK_ERROR
#else
#define PK_DBG(a, ...)
#define PK_VER(a, ...)
#define PK_ERR(a, ...)
#endif



static DEFINE_SPINLOCK(g_strobeSMPLock); /* cotta-- SMP proection */


static u32 strobe_Res = 0;
static u32 strobe_Timeus = 0;
static BOOL g_strobe_On = 0;

static int g_duty=-1;
static int g_timeOutTimeMs=0;


static DEFINE_MUTEX(g_strobeSem);


#define STROBE_DEVICE_ID 0x60


static struct work_struct workTimeOut;

/*****************************************************************************
Functions
*****************************************************************************/
//#define GPIO_ENF GPIO_FONT_CAMERA_FLASH_EN_PIN//GPIO_FONT_CAMERA_FLASH_MODE_PIN
//#define GPIO_ENT GPIO_FONT_CAMERA_FLASH_MODE_PIN//GPIO_FONT_CAMERA_FLASH_EN_PIN 

extern int iWriteRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u16 i2cId);
extern int iReadRegI2C(u8 *a_pSendData , u16 a_sizeSendData, u8 * a_pRecvData, u16 a_sizeRecvData, u16 i2cId);
static void work_timeOutFunc(struct work_struct *data);

static struct pinctrl *pinctrl;
static struct pinctrl_state  *frontflash_en_output0;
static struct pinctrl_state  *frontflash_en_output1;

#if 1  
int sub_FL_Enable(void)
{

	pinctrl_select_state(pinctrl, frontflash_en_output1);	

	PK_DBG(" FL_Enable line=%d\n",__LINE__);

    return 0;
}
#else
int sub_FL_Enable(void)
{
	if(g_duty==1)
	{
				
		mt_set_gpio_out(GPIO_ENT,GPIO_OUT_ONE);
		mt_set_gpio_out(GPIO_ENF,GPIO_OUT_ONE);
		//mt_set_gpio_out(GPIO_ENF,GPIO_OUT_ZERO);
		PK_DBG(" FL_Enable line=%d\n",__LINE__);
	}
	else
	{
		mt_set_gpio_out(GPIO_ENT,GPIO_OUT_ZERO);
		mt_set_gpio_out(GPIO_ENF,GPIO_OUT_ONE);
		PK_DBG(" FL_Enable line=%d\n",__LINE__);
	}

    return 0;
}
#endif

int sub_FL_Disable(void)
{

	pinctrl_select_state(pinctrl, frontflash_en_output0);

	PK_DBG(" FL_Disable line=%d\n",__LINE__);
    return 0;
}

int sub_FL_dim_duty(kal_uint32 duty)
{
	g_duty=duty;
	PK_DBG(" FL_dim_duty line=%d,duty=%d\n",__LINE__,duty);
    return 0;
}


int sub_FL_Init(void)
{
	
	pinctrl_select_state(pinctrl, frontflash_en_output0);

    INIT_WORK(&workTimeOut, work_timeOutFunc);
    PK_DBG(" FL_Init line=%d\n",__LINE__);
    return 0;
}


int sub_FL_Uninit(void)
{
    PK_DBG(" FL_Uninit line=%d\n",__LINE__);

	pinctrl_select_state(pinctrl, frontflash_en_output0);

    return 0;
}

void sub_FL_Flash_Mode(void)
{
    PK_DBG(" FL_Flash_Mode line=%d\n",__LINE__);

	pinctrl_select_state(pinctrl, frontflash_en_output1);

}

void sub_FL_Torch_Mode(void)
{
    PK_DBG(" FL_Torch_Mode line=%d\n",__LINE__);

	pinctrl_select_state(pinctrl, frontflash_en_output1);

}


/*****************************************************************************
User interface 
*****************************************************************************/

static void work_timeOutFunc(struct work_struct *data)
{
    sub_FL_Disable();
    PK_DBG("ledTimeOut_callback\n");
    //printk(KERN_ALERT "work handler function./n");
}



enum hrtimer_restart sub_ledTimeOutCallback(struct hrtimer *timer)
{
    schedule_work(&workTimeOut);
    return HRTIMER_NORESTART;
}
static struct hrtimer g_timeOutTimer;
void sub_timerInit(void)
{
	g_timeOutTimeMs=1000; //1s
	hrtimer_init( &g_timeOutTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
	g_timeOutTimer.function=sub_ledTimeOutCallback;

}


static int sub_strobe_ioctl(unsigned int cmd, unsigned long arg)
{
	int i4RetValue = 0;
	int ior_shift;
	int iow_shift;
	int iowr_shift;
	ior_shift = cmd - (_IOR(FLASHLIGHT_MAGIC,0, int));
	iow_shift = cmd - (_IOW(FLASHLIGHT_MAGIC,0, int));
	iowr_shift = cmd - (_IOWR(FLASHLIGHT_MAGIC,0, int));
	PK_DBG("sub_flashlight_ioctl() line=%d ior_shift=%d, iow_shift=%d iowr_shift=%d arg=%lu\n",__LINE__, ior_shift, iow_shift, iowr_shift, arg);
    switch(cmd)
    {

		case FLASH_IOC_SET_TIME_OUT_TIME_MS:
			PK_DBG("FLASH_IOC_SET_TIME_OUT_TIME_MS: %lu\n",arg);
			g_timeOutTimeMs=arg;
		break;


    	case FLASH_IOC_SET_DUTY :
    		PK_DBG("FLASHLIGHT_DUTY: %lu\n",arg);
			//if (arg > 1)
			//	sub_FL_Flash_Mode();
			//else
			//	sub_FL_Torch_Mode();
			
    		sub_FL_dim_duty(arg);
    		break;


    	case FLASH_IOC_SET_STEP:
    		PK_DBG("FLASH_IOC_SET_STEP: %lu\n",arg);

    		break;

    	case FLASH_IOC_SET_ONOFF :
    		PK_DBG("FLASHLIGHT_ONOFF: %lu\n",arg);
    		if(arg==1)
    		{
				if(g_timeOutTimeMs!=0)
	            {
	            	ktime_t ktime;
					ktime = ktime_set( 0, g_timeOutTimeMs*1000000 );
					hrtimer_start( &g_timeOutTimer, ktime, HRTIMER_MODE_REL );
	            }
    			sub_FL_Enable();
    		}
    		else
    		{
    			sub_FL_Disable();
				hrtimer_cancel( &g_timeOutTimer );
    		}
    		break;
		default :
    		PK_DBG(" No such command \n");
    		i4RetValue = -EPERM;
    		break;
    }
    return i4RetValue;
}

static int sub_strobe_open(void *pArg)
{
    int i4RetValue = 0;
    PK_DBG("sub_flashlight_open line=%d\n", __LINE__);

	if (0 == strobe_Res)
	{
	    sub_FL_Init();
		sub_timerInit();
	}
	PK_DBG("sub_flashlight_open line=%d\n", __LINE__);
	spin_lock_irq(&g_strobeSMPLock);


    if(strobe_Res)
    {
        PK_ERR(" busy!\n");
        i4RetValue = -EBUSY;
    }
    else
    {
        strobe_Res += 1;
    }


    spin_unlock_irq(&g_strobeSMPLock);
    PK_DBG("sub_flashlight_open line=%d\n", __LINE__);

    return i4RetValue;

}

static int sub_strobe_release(void *pArg)
{
    PK_DBG(" sub_flashlight_release\n");

    if (strobe_Res)
    {
        spin_lock_irq(&g_strobeSMPLock);

        strobe_Res = 0;
        strobe_Timeus = 0;

        /* LED On Status */
        g_strobe_On = FALSE;

        spin_unlock_irq(&g_strobeSMPLock);

    	sub_FL_Uninit();
    }

    PK_DBG(" Done\n");

    return 0;

}

FLASHLIGHT_FUNCTION_STRUCT subStrobeFunc = {
	sub_strobe_open,
	sub_strobe_release,
	sub_strobe_ioctl
};


MUINT32 subStrobeInit(PFLASHLIGHT_FUNCTION_STRUCT *pfFunc)
{
	if (pfFunc != NULL)
		*pfFunc = &subStrobeFunc;
	return 0;
}


static int frontflash_probe(struct platform_device *pdev)
{
	int err = 0;
	pinctrl = devm_pinctrl_get(&pdev->dev);	
	if (IS_ERR(pinctrl)) 
	{
		err = PTR_ERR(pinctrl);
		dev_err(&pdev->dev, "fwq Cannot find rgb pinctrl!\n");
		return err;
    }

	frontflash_en_output0 = pinctrl_lookup_state(pinctrl, "front_flash_en_output0");
    if (IS_ERR(frontflash_en_output0))
	{
		err = PTR_ERR(frontflash_en_output0);
		dev_err(&pdev->dev, "fwq Cannot find pinctrl front_flash_en_output0!\n");
   	}

	frontflash_en_output1 = pinctrl_lookup_state(pinctrl, "front_flash_en_output1");
    if (IS_ERR(frontflash_en_output1))
	{
		err = PTR_ERR(frontflash_en_output1);
		dev_err(&pdev->dev, "fwq Cannot find pinctrl front_flash_en_output1!\n");
   	}

	pinctrl_select_state(pinctrl, frontflash_en_output0);
	
	return err;	
}

static int frontflash_remove(struct platform_device *pdev)
{
	return 0;
}

static struct of_device_id frontflash_of_match[] = {
        { .compatible = "mediatek,strobe_sub", },
        {},
};

static struct platform_driver frontflash_driver = {
        .remove = frontflash_remove,
        .shutdown = NULL,
        .probe = frontflash_probe,
        .driver = {
		    .name = "strobe_sub",
		    //.pm = &tpd_pm_ops,
		    .owner = THIS_MODULE,
		    .of_match_table = frontflash_of_match,
    	},
};

static int __init frontflash_init(void)
{
	PK_DBG("%s\n",__func__);

    if (platform_driver_register(&frontflash_driver) != 0) {
		PK_DBG("%s error!\n",__func__);
        return -1;
    }

    return 0;
}

static void __exit frontflash_exit(void)
{
	PK_DBG("%s\n",__func__);
	//i2c_del_driver(&ktd20xx_driver);
}

module_init(frontflash_init);
module_exit(frontflash_exit);

MODULE_LICENSE("GPL");
