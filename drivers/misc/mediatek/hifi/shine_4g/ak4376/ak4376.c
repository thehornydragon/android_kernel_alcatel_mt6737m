/*
 * ak4376.c  --  audio driver for AK4376
 *
 * Copyright (C) 2015 Asahi Kasei Microdevices Corporation
 *  Author                Date        Revision
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                      15/06/12	    1.0
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/mfd/ak437x/pdata.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <mt-plat/mt_gpio.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#endif
#if 1//defined(CONFIG_MTK_LEGACY)
#include "mt_gpio.h"
#include <mach/gpio_const.h>
#include "mt-plat/mtgpio.h"
#include <mt-plat/mt_gpio.h>
#include <mt-plat/mt_gpio_core.h>
#include <mach/gpio_const.h>
#endif
//#include "cust_gpio_usage.h"

#include "ak4376.h"
//#error("ak4376");
//#define AK4376_DEBUG				//used at debug mode
//#define AK4376_CONTIF_DEBUG		//used at debug mode
//#define CONFIG_DEBUG_FS_CODEC		//used at debug mode
#define AK4376_DEBUG
#ifdef AK4376_DEBUG
#define akdbgprt printk
#else
#define akdbgprt(format, arg...) do {} while (0)
#endif

#define AK4376_NAME "ak4376"

typedef	uint8_t kal_uint8;
typedef	int8_t kal_int8;
typedef uint16_t kal_uint16;
typedef	uint32_t kal_uint32;
typedef	int32_t kal_int32;
typedef	uint64_t kal_uint64;
typedef	int64_t kal_int64;
typedef bool kal_bool;

#if defined(CONFIG_OF) && !defined(CONFIG_MTK_LEGACY)
static struct pinctrl *ak4376_pinctrl;

enum{
    GPIO_HIFI_DEF = 0,
	GPIO_HIFI_PDN_LOW,
	GPIO_HIFI_PDN_HIGH,
	GPIO_HIFI_LDO_ON,
	GPIO_HIFI_LDO_OFF,
	GPIO_HIFI_LDO_END
};

struct audio_gpio_attr {
	const char *name;
	bool gpio_prepare;
	struct pinctrl_state *gpioctrl;
};

static struct audio_gpio_attr ak4376_gpios[GPIO_HIFI_LDO_END] = {
    [GPIO_HIFI_DEF] = {"default", false, NULL},
	[GPIO_HIFI_PDN_LOW] = {"pdn_low", false, NULL},
	[GPIO_HIFI_PDN_HIGH] = {"pdn_high", false, NULL},
	[GPIO_HIFI_LDO_ON] = {"ldo_on", false, NULL},
	[GPIO_HIFI_LDO_OFF] = {"ldo_off", false, NULL},
};

void ak4376_GPIO_parse(void *dev)
{
	int ret;
	int i = 0;

	akdbgprt("%s(%d) start\n", __func__, __LINE__);

	ak4376_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(ak4376_pinctrl)) {
		ret = PTR_ERR(ak4376_pinctrl);
		akdbgprt("Cannot find ak4376_pinctrl!\n");
		return;
	}

	akdbgprt("%s(%d) lookup pin state\n", __func__, __LINE__);
	for (i = 0; i < ARRAY_SIZE(ak4376_gpios); i++) {
		ak4376_gpios[i].gpioctrl = pinctrl_lookup_state(ak4376_pinctrl, ak4376_gpios[i].name);
		if (IS_ERR(ak4376_gpios[i].gpioctrl)) {
			ret = PTR_ERR(ak4376_gpios[i].gpioctrl);
			akdbgprt("%s pinctrl_lookup_state %s fail %d\n", __func__, ak4376_gpios[i].name, ret);
		} else {
			akdbgprt("%s(%d)\n",__FUNCTION__,__LINE__);
			ak4376_gpios[i].gpio_prepare = true;
		}
	}

	akdbgprt("%s(%d) ok!!\n", __func__, __LINE__);
}
#endif

/* AK4376 Codec Private Data */
struct ak4376_priv {
	struct mutex mutex;
	unsigned int priv_pdn_en;			//PDN GPIO pin
	int pdn1;							//PDN control, 0:Off, 1:On, 2:No use(assume always On)
	int pdn2;							//PDN control for kcontrol
	int fs1;
	int rclk;							//Master Clock
	int nBickFreq;						//0:32fs, 1:48fs, 2:64fs
	struct ak4376_platform_data *pdata;	//platform data
	int nPllMCKI;						//0:9.6MHz, 1:11.2896MHz, 2:12.288MHz, 3:19.2MHz
	int nDeviceID;						//0:AK4375, 1:AK4375A, 2:AK4376, 3:Other IC
	int lpmode;							//0:High Performance, 1:Low power mode
	int xtalfreq;						//0:12.288MHz, 1:11.2896MHz
	int nDACOn;
	struct i2c_client *i2c;
};

static struct ak4376_priv* ak4376_codec_priv;

#ifdef CONFIG_MTK_LEGACY
//#define GPIO16_HIFI_PDN   (GPIO16 | 0x80000000)
#define GPIO16_HIFI_PDN   (16 | 0x80000000)
#define GPIO_HIFI_LDO_EN_PIN (115 | 0x80000000)

extern int mt_set_gpio_mode(unsigned long pin, unsigned long mode);
extern int mt_set_gpio_dir(unsigned long pin, unsigned long dir);
extern int mt_set_gpio_out(unsigned long pin, unsigned long output);
#endif

unsigned int ak4376_reg_read(struct snd_soc_codec *, unsigned int);
static int ak4376_write_register(struct snd_soc_codec *, unsigned int, unsigned int);
static struct snd_soc_codec *ak4376_codec;
static u8 ak4376_reg[AK4376_MAX_REGISTERS];
static int AK4376_Wlen = 1;

#ifdef CONFIG_DEBUG_FS_CODEC
static int ak4376_reg_write(struct snd_soc_codec *, u16, u16);
#endif

static inline void ak4376_update_register(struct snd_soc_codec *codec)
{
	u8 cur_cache;
	u8 cur_register;
	u8 *cache = codec->reg_cache;
	int i;

	akdbgprt("\t[AK4376] %s(%d)\n", __FUNCTION__,__LINE__);

	if(codec->reg_cache == NULL){
		akdbgprt("\t[AK4376] %s(%d)\n", __FUNCTION__,__LINE__);
		while(1);
	}

	for (i = 0; i < AK4376_16_DUMMY; i++) {
		cur_register = ak4376_reg_read(codec, i);
		cur_cache = cache[i];

		akdbgprt("\t[AK4376] %s(%d) reg:0x%x (I2C, cache)=(0x%x,0x%x)\n", __FUNCTION__,__LINE__,i,cur_register,cur_cache);

		if (cur_register != cur_cache){
			ak4376_write_register(codec, i, cur_cache);
			akdbgprt("\t[AK4376] %s(%d) write cache to register\n", __FUNCTION__,__LINE__);
		}
	}

	cur_register = ak4376_reg_read(codec, AK4376_24_MODE_CONTROL);
	cur_cache = cache[AK4376_24_MODE_CONTROL];

	akdbgprt("\t[AK4376] %s(%d) (reg:0x24)cur_register=%x, cur_cache=%x\n", __FUNCTION__,__LINE__,cur_register,cur_cache);

	if (cur_register != cur_cache)
		ak4376_write_register(codec, AK4376_24_MODE_CONTROL, cur_cache);
}

/* GPIO control for PDN */
static int ak4376_pdn_control(struct snd_soc_codec *codec, int pdn)
{
	int ret;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;
	int n;

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	if ((ak4376->pdn1 == 0) && (pdn == 1)) {
		//gpio_direction_output(ak4376->priv_pdn_en, 1);
#ifdef CONFIG_MTK_LEGACY
		mt_set_gpio_mode(GPIO16_HIFI_PDN,  GPIO_MODE_00);
		mt_set_gpio_dir(GPIO16_HIFI_PDN,       GPIO_DIR_OUT);
		mt_set_gpio_out(GPIO16_HIFI_PDN,     GPIO_OUT_ONE);
#else
		akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
		if(ak4376_gpios[GPIO_HIFI_PDN_HIGH].gpio_prepare){
			akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
			ret = pinctrl_select_state(ak4376_pinctrl, ak4376_gpios[GPIO_HIFI_PDN_HIGH].gpioctrl);
			if(ret){
				akdbgprt("[ak4376] %s(%d) failed to set PND enable\n", __FUNCTION__, __LINE__);
				return 0;
			}
			udelay(2);
		}
#endif
		akdbgprt("\t[AK4376] %s(%d) Turn on priv_pdn_en\n", __FUNCTION__,__LINE__);
		ak4376->pdn1 = 1;
		ak4376->pdn2 = 1;
		udelay(800);

		//snd_soc_write(codec, 0x07, 0x33);

		ak4376_update_register(codec);
		for(n = 0; n < 0x25; n++){
			snd_soc_write(codec, n, ak4376_reg[n]);
		}


	} else if ((ak4376->pdn1 == 1) && (pdn == 0)) {
		//gpio_direction_output(ak4376->priv_pdn_en, 0);
#ifdef CONFIG_MTK_LEGACY
		mt_set_gpio_mode(GPIO16_HIFI_PDN,  GPIO_MODE_00);
		mt_set_gpio_dir(GPIO16_HIFI_PDN,       GPIO_DIR_OUT);
		mt_set_gpio_out(GPIO16_HIFI_PDN,     GPIO_OUT_ZERO);
#else
		akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
		if(ak4376_gpios[GPIO_HIFI_PDN_LOW].gpio_prepare){
			akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
			ret = pinctrl_select_state(ak4376_pinctrl, ak4376_gpios[GPIO_HIFI_PDN_LOW].gpioctrl);
			if(ret){
				akdbgprt("[ak4376] %s(%d) failed to set pnd disa\n", __FUNCTION__, __LINE__);
				return -1;
			}
			usleep_range(900, 1000);
		}
#endif
		akdbgprt("\t[AK4376] %s(%d) Turn off priv_pdn_en\n", __FUNCTION__,__LINE__);
		ak4376->pdn1 = 0;
		ak4376->pdn2 = 0;

		snd_soc_cache_init(codec);
	}

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	return 0;
}

#ifdef CONFIG_DEBUG_FS_CODEC
static struct mutex io_lock;

static ssize_t reg_data_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int ret, i, fpt;
	int rx[22];

	mutex_lock(&io_lock);
	for (i = 0; i < AK4376_16_DUMMY; i++) {
		ret = ak4376_reg_read(ak4376_codec, i);

		if (ret < 0) {
			pr_err("%s: read register error.\n", __func__);
			break;

		} else {
			rx[i] = ret;
		}
	}

	rx[22] = ak4376_reg_read(ak4376_codec, AK4376_24_MODE_CONTROL);
	mutex_unlock(&io_lock);

	if (i == 22) {

		ret = fpt = 0;

		for (i = 0; i < AK4376_16_DUMMY; i++, fpt += 6) {

			ret += sprintf(buf + fpt, "%02x,%02x\n", i, rx[i]);
		}

		ret += sprintf(buf + i * 6, "24,%02x\n", rx[22]);

		return ret;

	} else {

		return sprintf(buf, "read error");
	}
}

static ssize_t reg_data_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	char	*ptr_data = (char *)buf;
	char	*p;
	int		i, pt_count = 0;
	unsigned short val[20];

	while ((p = strsep(&ptr_data, ","))) {
		if (!*p)
			break;

		if (pt_count >= 20)
			break;

		val[pt_count] = simple_strtoul(p, NULL, 16);

		pt_count++;
	}

	mutex_lock(&io_lock);
	for (i = 0; i < pt_count; i+=2) {
		ak4376_reg_write(ak4376_codec, val[i], val[i+1]);
		pr_debug("%s: write add=%d, val=%d.\n", __func__, val[i], val[i+1]);
	}
	mutex_unlock(&io_lock);

	return count;
}

static DEVICE_ATTR(reg_data, 0666, reg_data_show, reg_data_store);

#endif

/* ak4376 register cache & default register settings */
#if 0
static const u8 ak4376_reg[AK4376_MAX_REGISTERS] = {
	0x00,	/*	0x00	AK4376_00_POWER_MANAGEMENT1		*/
	0x00,	/*	0x01	AK4376_01_POWER_MANAGEMENT2		*/
	0x00,	/*	0x02	AK4376_02_POWER_MANAGEMENT3		*/
	0x00,	/*	0x03	AK4376_03_POWER_MANAGEMENT4		*/
	0x00,	/*	0x04	AK4376_04_OUTPUT_MODE_SETTING	*/
	0x00,	/*	0x05	AK4376_05_CLOCK_MODE_SELECT		*/
	0x00,	/*	0x06	AK4376_06_DIGITAL_FILTER_SELECT	*/
	0x00,	/*	0x07	AK4376_07_DAC_MONO_MIXING		*/
	0x00,	/*	0x08	AK4376_08_RESERVED				*/
	0x00,	/*	0x09	AK4376_09_RESERVED				*/
	0x00,	/*	0x0A	AK4376_0A_RESERVED				*/
	0x19,	/*	0x0B	AK4376_0B_LCH_OUTPUT_VOLUME		*/
	0x19,	/*	0x0C	AK4376_0C_RCH_OUTPUT_VOLUME		*/
	0x0B,	/*	0x0D	AK4376_0D_HP_VOLUME_CONTROL		*/
	0x00,	/*	0x0E	AK4376_0E_PLL_CLK_SOURCE_SELECT	*/
	0x00,	/*	0x0F	AK4376_0F_PLL_REF_CLK_DIVIDER1	*/
	0x00,	/*	0x10	AK4376_10_PLL_REF_CLK_DIVIDER2	*/
	0x00,	/*	0x11	AK4376_11_PLL_FB_CLK_DIVIDER1	*/
	0x00,	/*	0x12	AK4376_12_PLL_FB_CLK_DIVIDER2	*/
	0x00,	/*	0x13	AK4376_13_DAC_CLK_SOURCE		*/
	0x00,	/*	0x14	AK4376_14_DAC_CLK_DIVIDER		*/
	0x40,	/*	0x15	AK4376_15_AUDIO_IF_FORMAT		*/
	0x00,	/*	0x16	AK4376_16_DUMMY					*/
	0x00,	/*	0x17	AK4376_17_DUMMY					*/
	0x00,	/*	0x18	AK4376_18_DUMMY					*/
	0x00,	/*	0x19	AK4376_19_DUMMY					*/
	0x00,	/*	0x1A	AK4376_1A_DUMMY					*/
	0x00,	/*	0x1B	AK4376_1B_DUMMY					*/
	0x00,	/*	0x1C	AK4376_1C_DUMMY					*/
	0x00,	/*	0x1D	AK4376_1D_DUMMY					*/
	0x00,	/*	0x1E	AK4376_1E_DUMMY					*/
	0x00,	/*	0x1F	AK4376_1F_DUMMY					*/
	0x00,	/*	0x20	AK4376_20_DUMMY					*/
	0x00,	/*	0x21	AK4376_21_DUMMY					*/
	0x00,	/*	0x22	AK4376_22_DUMMY					*/
	0x00,	/*	0x23	AK4376_23_DUMMY					*/
	0x00,	/*	0x24	AK4376_24_MODE_CONTROL			*/
};
//#else
static const u8 ak4376_reg[AK4376_MAX_REGISTERS] = {
		0x01 ,
		0x33 ,
		0x01 ,
		0x03 ,
		0x00 ,
		0x09 ,
		0x00 ,
		0x21 ,
		0x00,	/*	0x08	AK4376_08_RESERVED				*/
		0x00,	/*	0x09	AK4376_09_RESERVED				*/
		0x00,	/*	0x0A*/
		0x19,
		0x19,
		0x0B,
		0x01,
		0x00,
		0x00,
		0x00,
		0x27,
		0x01,
		0x09, //14
		0xE0,
		0x00,	/*	0x16	AK4376_16_DUMMY					*/
		0x00,	/*	0x17	AK4376_17_DUMMY					*/
		0x00,	/*	0x18	AK4376_18_DUMMY					*/
		0x00,	/*	0x19	AK4376_19_DUMMY					*/
		0x00,	/*	0x1A	AK4376_1A_DUMMY					*/
		0x00,	/*	0x1B	AK4376_1B_DUMMY					*/
		0x00,	/*	0x1C	AK4376_1C_DUMMY					*/
		0x00,	/*	0x1D	AK4376_1D_DUMMY					*/
		0x00,	/*	0x1E	AK4376_1E_DUMMY					*/
		0x00,	/*	0x1F	AK4376_1F_DUMMY					*/
		0x00,	/*	0x20	AK4376_20_DUMMY					*/
		0x00,	/*	0x21	AK4376_21_DUMMY					*/
		0x00,	/*	0x22	AK4376_22_DUMMY					*/
		0x00,	/*	0x23	AK4376_23_DUMMY					*/
		0x00

};
#endif

static u8 ak4376_reg[AK4376_MAX_REGISTERS] = {
	0x01,    //0x00
	0x33,    //0x01
	0x01,    //0x02
	0x00, //0x03,    //0x03
	0x00,    //0x04
	0x0A,    //0x05
	0x00,    //0x06
	0x00, //0x21,    //0x07
	0x00,    //0x08
	0x00,    //0x09
	0x00,    //0x0A
	0x19,    //0x0B
	0x19,    //0x0C
	0x73,    //0x0D
	0x01,    //0x0E
	0x00,    //0x0F
	0x00,    //0x10
	0x00,    //0x11
	0x27,    //0x12
	0x01,    //0x13
	0x09,    //0x14
	0x00,    //0x15
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,    //0x24
};

static const struct {
	int readable;   /* Mask of readable bits */
	int writable;   /* Mask of writable bits */
} ak4376_access_masks[] = {
    { 0xFF, 0x11 },	//0x00
    { 0xFF, 0x33 },	//0x01
    { 0xFF, 0x11 },	//0x02
    { 0xFF, 0x7F },	//0x03
    { 0xFF, 0x3F },	//0x04
    { 0xFF, 0xFF },	//0x05
    { 0xFF, 0xCB },	//0x06
    { 0xFF, 0xFF },	//0x07
    { 0xFF, 0xFF },	//0x08
    { 0xFF, 0xFF },	//0x09
    { 0xFF, 0xFF },	//0x0A
    { 0xFF, 0x9F },	//0x0B
    { 0xFF, 0x1F },	//0x0C
    { 0xFF, 0x0F },	//0x0D
    { 0xFF, 0x21 },	//0x0E
    { 0xFF, 0xFF },	//0x0F
    { 0xFF, 0xFF },	//0x10
    { 0xFF, 0xFF },	//0x11
    { 0xFF, 0xFF },	//0x12
    { 0xFF, 0x01 },	//0x13
    { 0xFF, 0xFF },	//0x14
    { 0xFF, 0x1F },	//0x15
    { 0x00, 0x00 },	//0x16	//DUMMY
    { 0x00, 0x00 },	//0x17	//DUMMY
    { 0x00, 0x00 },	//0x18	//DUMMY
    { 0x00, 0x00 },	//0x19	//DUMMY
    { 0x00, 0x00 },	//0x1A	//DUMMY
    { 0x00, 0x00 },	//0x1B	//DUMMY
    { 0x00, 0x00 },	//0x1C	//DUMMY
    { 0x00, 0x00 },	//0x1D	//DUMMY
    { 0x00, 0x00 },	//0x1E	//DUMMY
    { 0x00, 0x00 },	//0x1F	//DUMMY
    { 0x00, 0x00 },	//0x20	//DUMMY
    { 0x00, 0x00 },	//0x21	//DUMMY
    { 0x00, 0x00 },	//0x22	//DUMMY
    { 0x00, 0x00 },	//0x23	//DUMMY
    { 0xFF, 0x50 },	//0x24
};

/* Output Digital volume control:
 * from -12.5 to 3 dB in 0.5 dB steps (mute instead of -12.5 dB) */
static DECLARE_TLV_DB_SCALE(ovl_tlv, -1250, 50, 0);
static DECLARE_TLV_DB_SCALE(ovr_tlv, -1250, 50, 0);

/* HP-Amp Analog volume control:
 * from -22 to 6 dB in 2 dB steps (mute instead of -42 dB) */
static DECLARE_TLV_DB_SCALE(hpg_tlv, -2200, 200, 0);

static const char *ak4376_ovolcn_select_texts[] = {"Dependent", "Independent"};
static const char *ak4376_mdacl_select_texts[] = {"x1", "x1/2"};
static const char *ak4376_mdacr_select_texts[] = {"x1", "x1/2"};
static const char *ak4376_invl_select_texts[] = {"Normal", "Inverting"};
static const char *ak4376_invr_select_texts[] = {"Normal", "Inverting"};
static const char *ak4376_cpmod_select_texts[] =
		{"Automatic Switching", "+-VDD Operation", "+-1/2VDD Operation"};
static const char *ak4376_hphl_select_texts[] = {"9ohm", "Hi-Z"};
static const char *ak4376_hphr_select_texts[] = {"9ohm", "Hi-Z"};
static const char *ak4376_dacfil_select_texts[]  =
		{"Sharp Roll-Off", "Slow Roll-Off", "Short Delay Sharp Roll-Off", "Short Delay Slow Roll-Off"};
static const char *ak4376_bcko_select_texts[] = {"64fs", "32fs"};
static const char *ak4376_dfthr_select_texts[] = {"Digital Filter", "Bypass"};
static const char *ak4376_ngate_select_texts[] = {"On", "Off"};
static const char *ak4376_ngatet_select_texts[] = {"Short", "Long"};

static const struct soc_enum ak4376_dac_enum[] = {
	SOC_ENUM_SINGLE(AK4376_0B_LCH_OUTPUT_VOLUME, 7,
			ARRAY_SIZE(ak4376_ovolcn_select_texts), ak4376_ovolcn_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 2,
			ARRAY_SIZE(ak4376_mdacl_select_texts), ak4376_mdacl_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 6,
			ARRAY_SIZE(ak4376_mdacr_select_texts), ak4376_mdacr_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 3,
			ARRAY_SIZE(ak4376_invl_select_texts), ak4376_invl_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 7,
			ARRAY_SIZE(ak4376_invr_select_texts), ak4376_invr_select_texts),
	SOC_ENUM_SINGLE(AK4376_03_POWER_MANAGEMENT4, 2,
			ARRAY_SIZE(ak4376_cpmod_select_texts), ak4376_cpmod_select_texts),
	SOC_ENUM_SINGLE(AK4376_04_OUTPUT_MODE_SETTING, 0,
			ARRAY_SIZE(ak4376_hphl_select_texts), ak4376_hphl_select_texts),
	SOC_ENUM_SINGLE(AK4376_04_OUTPUT_MODE_SETTING, 1,
			ARRAY_SIZE(ak4376_hphr_select_texts), ak4376_hphr_select_texts),
    SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 6,
    		ARRAY_SIZE(ak4376_dacfil_select_texts), ak4376_dacfil_select_texts), 
	SOC_ENUM_SINGLE(AK4376_15_AUDIO_IF_FORMAT, 3,
			ARRAY_SIZE(ak4376_bcko_select_texts), ak4376_bcko_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 3,
			ARRAY_SIZE(ak4376_dfthr_select_texts), ak4376_dfthr_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 0,
			ARRAY_SIZE(ak4376_ngate_select_texts), ak4376_ngate_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 1,
			ARRAY_SIZE(ak4376_ngatet_select_texts), ak4376_ngatet_select_texts),
};

static const char *bickfreq_on_select[] = {"32fs", "48fs", "64fs"};
static const char *pllmcki_on_select[] = {"9.6MHz", "11.2896MHz", "12.288MHz", "19.2MHz"};
static const char *lpmode_on_select[] = {"High Performance", "Low Power"};
static const char *xtalfreq_on_select[] = {"12.288MHz", "11.2896MHz"};
static const char *pdn_on_select[] = {"Off", "On"};
static const char *phone_call_select[] = {"Off", "On"};
static const char *mono_stereo[] = {"Mono", "Stereo", "Neither"};
static const char *wlen[] = {"16BITS", "32BITS"};

static const struct soc_enum ak4376_bitset_enum[] = {
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(bickfreq_on_select), bickfreq_on_select), 
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pllmcki_on_select), pllmcki_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(lpmode_on_select), lpmode_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(xtalfreq_on_select), xtalfreq_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pdn_on_select), pdn_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(phone_call_select), phone_call_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mono_stereo), mono_stereo),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(wlen), wlen),
};

static int get_bickfs(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
//    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
    struct ak4376_priv *ak4376 = ak4376_codec_priv;

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

    ucontrol->value.enumerated.item[0] = ak4376->nBickFreq;

    return 0;
}

static int ak4376_set_bickfs(struct snd_soc_codec *codec)
{
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;

	akdbgprt("\t[AK4376] %s(%d) nBickFreq=%d\n",__FUNCTION__,__LINE__,ak4376->nBickFreq);

	if ( ak4376->nBickFreq == 0 ) { 	//32fs
		snd_soc_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x03, 0x01);	//DL1-0=01(16bit, >=32fs)
	}
	else if( ak4376->nBickFreq == 1 ) {	//48fs
		snd_soc_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x03, 0x00);	//DL1-0=00(24bit, >=48fs)
	}
	else {								//64fs
		snd_soc_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x03, 0x02);	//DL1-0=1x(32bit, >=64fs)
	}
	
	return 0;
}

static int set_bickfs(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
//    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_codec *codec = ak4376_codec;
    struct ak4376_priv *ak4376 = ak4376_codec_priv;

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
    
	ak4376->nBickFreq = ucontrol->value.enumerated.item[0];

	ak4376_set_bickfs(codec);
	
    return 0;
}

static int get_pllmcki(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
//    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
    struct ak4376_priv *ak4376 = ak4376_codec_priv;

    ucontrol->value.enumerated.item[0] = ak4376->nPllMCKI;

    return 0;
}

static int set_pllmcki(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
//    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
    struct ak4376_priv *ak4376 = ak4376_codec_priv;
    
	ak4376->nPllMCKI = ucontrol->value.enumerated.item[0];

    return 0;
}

static int get_lpmode(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
//    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
    struct ak4376_priv *ak4376 = ak4376_codec_priv;

    ucontrol->value.enumerated.item[0] = ak4376->lpmode;

    return 0;
}

static int ak4376_set_lpmode(struct snd_soc_codec *codec)
{
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;

	if ( ak4376->lpmode == 0 ) { 	//High Performance Mode
		snd_soc_update_bits(codec, AK4376_02_POWER_MANAGEMENT3, 0x10, 0x00);	//LPMODE=0(High Performance Mode)
			if ( ak4376->fs1 <= 12000 ) {
				snd_soc_update_bits(codec, AK4376_24_MODE_CONTROL, 0x40, 0x40);	//DSMLP=1
			}
			else {
				snd_soc_update_bits(codec, AK4376_24_MODE_CONTROL, 0x40, 0x00);	//DSMLP=0
			}
	}
	else {							//Low Power Mode
		snd_soc_update_bits(codec, AK4376_02_POWER_MANAGEMENT3, 0x10, 0x10);	//LPMODE=1(Low Power Mode)
		snd_soc_update_bits(codec, AK4376_24_MODE_CONTROL, 0x40, 0x40);			//DSMLP=1
	}
	
	return 0;
}

static int set_lpmode(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
//    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_codec *codec = ak4376_codec;
    struct ak4376_priv *ak4376 = ak4376_codec_priv;
    
	ak4376->lpmode = ucontrol->value.enumerated.item[0];

	ak4376_set_lpmode(codec);
	
    return 0;
}

static int get_xtalfreq(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
//    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
    struct ak4376_priv *ak4376 = ak4376_codec_priv;

    ucontrol->value.enumerated.item[0] = ak4376->xtalfreq;

    return 0;
}

static int set_xtalfreq(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
//    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
    struct ak4376_priv *ak4376 = ak4376_codec_priv;
    
	ak4376->xtalfreq = ucontrol->value.enumerated.item[0];

    return 0;
}

static int get_pdn(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
//    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
    struct ak4376_priv *ak4376 = ak4376_codec_priv;

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

    ucontrol->value.enumerated.item[0] = ak4376->pdn2;

    return 0;
}

static int set_pdn(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
//    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_codec* codec = ak4376_codec;
    struct ak4376_priv *ak4376 = ak4376_codec_priv;
	//int regVal = 0;
	//int n = 0;

	akdbgprt("[ak4376] %s(%d)\n",__FUNCTION__, __LINE__);

	ak4376->pdn2 = ucontrol->value.enumerated.item[0];

	akdbgprt("\t[AK4376] %s(%d) pdn2=%d\n",__FUNCTION__,__LINE__,ak4376->pdn2);
	//if (ak4376->pdn1 == 0){
		ak4376_pdn_control(codec, ak4376->pdn2);
	//}

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

    return 0;
}

static int isAK4376_InCallMode = 0;
static int get_InCallMode(struct snd_kcontrol* kcontrol, struct snd_ctl_elem_value* ucontrol){
	ucontrol->value.enumerated.item[0] = isAK4376_InCallMode;

	return 0;
}

static int set_InCallMode(struct snd_kcontrol* kcontrol, struct snd_ctl_elem_value* uncontrol){
	struct snd_soc_codec* codec = ak4376_codec;
	int item = uncontrol->value.enumerated.item[0];

	if(item){
//		if(!isAK4376_InCallMode){
			snd_soc_write(codec, 0x05, 0x24);
			snd_soc_write(codec, 0x07, 0x11);
			snd_soc_write(codec, 0x12, 0xef);
			snd_soc_write(codec, 0x14, 0x0e);
//		}
		isAK4376_InCallMode = 1;
	}else{
//		if(isAK4376_InCallMode){
			snd_soc_write(codec, 0x05, 0x0a);
			snd_soc_write(codec, 0x07, 0x21);
			snd_soc_write(codec, 0x12, 0x27);
			snd_soc_write(codec, 0x14, 0x09);
//		}
		isAK4376_InCallMode = 0;
	}
	return 0;
}

static int AK4376_Mono_Stereo = 1;
static int get_mono_stereo(struct snd_kcontrol* kcontrol, struct snd_ctl_elem_value* ucontrol){
	ucontrol->value.enumerated.item[0] = AK4376_Mono_Stereo;
	return 0;
}
static int set_mono_stereo(struct snd_kcontrol* kcontrol, struct snd_ctl_elem_value* ucontrol){
	struct snd_soc_codec* codec = ak4376_codec;
	int item = ucontrol->value.enumerated.item[0];
	switch(item){
	case 0:
		snd_soc_write(codec, 0x07, 0x11);
		AK4376_Mono_Stereo = 0;
		break;
	case 1:
		snd_soc_write(codec, 0x07, 0x21);
		AK4376_Mono_Stereo = 1;
		break;
	case 2:
		snd_soc_write(codec, 0x07, 0x00);
		AK4376_Mono_Stereo = 2;
		break;
	default:
		return -1;
	}
	return 0;
}
static int get_wlen(struct snd_kcontrol* kcontrol, struct snd_ctl_elem_value* ucontrol){
	struct snd_soc_codec* codec = ak4376_codec;
	unsigned int reg05, reg12;
	reg05 = snd_soc_read(codec, 0x05);
	reg12 = snd_soc_read(codec, 0x12);
	if(reg05== 0x09 && reg12 == 0x4f){
		ucontrol->value.enumerated.item[0] = 0;
	}else{
		ucontrol->value.enumerated.item[0] = 1;
	}
	return 0;
}
static int set_wlen(struct snd_kcontrol* kcontrol, struct snd_ctl_elem_value* ucontrol){
	struct snd_soc_codec* codec = ak4376_codec;
	int item = ucontrol->value.enumerated.item[0];
	switch(item){
	case 0:
		snd_soc_write(codec, 0x05, 0x09);
		snd_soc_write(codec, 0x12, 0x4F);
		ak4376_reg[0x05] = 0x09;
		ak4376_reg[0x12] = 0x4f;
		AK4376_Wlen = 0;
		break;
	case 1:
		ak4376_reg[0x05] = 0x0a;
		ak4376_reg[0x12] = 0x27;
		AK4376_Wlen = 1;
		break;
	default:
		break;
	}
	return 0;
}
static int set_amp_on(struct snd_kcontrol* kcontrol, struct snd_ctl_elem_value* ucontrol){
	struct snd_soc_codec* codec = ak4376_codec;
	int item = ucontrol->value.enumerated.item[0];

	switch(item){
	case 0:
		snd_soc_write(codec, 0x03, 0x00);
		break;

	case 1:
		snd_soc_write(codec, 0x03, 0x03);
		break;

	default:
		break;
	}

	return 0;
}
static int get_amp_on(struct snd_kcontrol* kcontrol, struct snd_ctl_elem_value* ucontrol){
	struct snd_soc_codec* codec = ak4376_codec;
	uint8_t value1;
	uint8_t value2;

	value1 = snd_soc_read(codec, 0x03);
	value2 = snd_soc_read(codec, 0x07);

	if(value1 && value2){
		ucontrol->value.enumerated.item[0] = 1;
	}else{
		ucontrol->value.enumerated.item[0] = 0;
	}

	return 0;
}
#ifdef AK4376_DEBUG

static const char *test_reg_select[]   = 
{
    "read AK4376 Reg 00:24",
};

static const struct soc_enum ak4376_enum[] = 
{
    SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(test_reg_select), test_reg_select),
};

static int nTestRegNo = 0;

static int get_test_reg(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
    /* Get the current output routing */
    ucontrol->value.enumerated.item[0] = nTestRegNo;

    return 0;
}

static int set_test_reg(
struct snd_kcontrol       *kcontrol,
struct snd_ctl_elem_value  *ucontrol)
{
//    struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = ak4376_codec;
    u32    currMode = ucontrol->value.enumerated.item[0];
	int    i, value;
	int	   regs, rege;

	nTestRegNo = currMode;

	regs = 0x00;
	rege = 0x15;

	for ( i = regs ; i <= rege ; i++ ){
		value = snd_soc_read(codec, i);
		akdbgprt("***AK4376 Addr,Reg=(%x, %x)\n", i, value);
	}
	value = snd_soc_read(codec, 0x24);
	akdbgprt("***AK4376 Addr,Reg=(%x, %x)\n", 0x24, value);

	return 0;
}
#endif

static const struct snd_kcontrol_new ak4376_snd_controls[] = {
	SOC_SINGLE_TLV("AK4376_Digital_Output_VolumeL",
			AK4376_0B_LCH_OUTPUT_VOLUME, 0, 0x1F, 0, ovl_tlv),
	SOC_SINGLE_TLV("AK4376_Digital_Output_VolumeR",
			AK4376_0C_RCH_OUTPUT_VOLUME, 0, 0x1F, 0, ovr_tlv),
	SOC_SINGLE_TLV("AK4376_HP-Amp_Analog_Volume",
			AK4376_0D_HP_VOLUME_CONTROL, 0, 0x0F, 0, hpg_tlv),

	SOC_ENUM("AK4376_Digital_Volume_Control", ak4376_dac_enum[0]),
	SOC_ENUM("AK4376_DACL_Signal_Level", ak4376_dac_enum[1]),
	SOC_ENUM("AK4376_DACR_Signal_Level", ak4376_dac_enum[2]),
	SOC_ENUM("AK4376_DACL_Signal_Invert", ak4376_dac_enum[3]),
	SOC_ENUM("AK4376_DACR_Signal_Invert", ak4376_dac_enum[4]),
	SOC_ENUM("AK4376_Charge_Pump_Mode", ak4376_dac_enum[5]),
	SOC_ENUM("AK4376_HPL_Power-down_Resistor", ak4376_dac_enum[6]),
	SOC_ENUM("AK4376_HPR_Power-down_Resistor", ak4376_dac_enum[7]),
	SOC_ENUM("AK4376_DAC_Digital_Filter_Mode", ak4376_dac_enum[8]),
	SOC_ENUM("AK4376_BICK_Output_Frequency", ak4376_dac_enum[9]),
	SOC_ENUM("AK4376_Digital_Filter_Mode", ak4376_dac_enum[10]),
	SOC_ENUM("AK4376_Noise_Gate", ak4376_dac_enum[11]),
	SOC_ENUM("AK4376_Noise_Gate_Time", ak4376_dac_enum[12]),

	SOC_ENUM_EXT("AK4376_BICK_Frequency_Select", ak4376_bitset_enum[0], get_bickfs, set_bickfs),
	SOC_ENUM_EXT("AK4376_PLL_MCKI_Frequency", ak4376_bitset_enum[1], get_pllmcki, set_pllmcki),
	SOC_ENUM_EXT("AK4376_Low_Power_Mode", ak4376_bitset_enum[2], get_lpmode, set_lpmode),
	SOC_ENUM_EXT("AK4376_Xtal_Frequency", ak4376_bitset_enum[3], get_xtalfreq, set_xtalfreq),
	SOC_ENUM_EXT("AK4376_PDN_Control", ak4376_bitset_enum[4], get_pdn, set_pdn),
	SOC_ENUM_EXT("AK4376_InCall_Mode", ak4376_bitset_enum[5], get_InCallMode, set_InCallMode),
	SOC_ENUM_EXT("AK4376_Mono_Stereo", ak4376_bitset_enum[6], get_mono_stereo, set_mono_stereo),
	SOC_ENUM_EXT("AK4376_Wlen", ak4376_bitset_enum[7], get_wlen, set_wlen),
	SOC_ENUM_EXT("AK4376_AMP_Control", ak4376_bitset_enum[4], get_amp_on, set_amp_on),
#ifdef AK4376_DEBUG
	SOC_ENUM_EXT("Reg Read", ak4376_enum[0], get_test_reg, set_test_reg),
#endif

};



/* DAC control */
static int ak4376_dac_event2(struct snd_soc_codec *codec, int event) 
{
	u8 MSmode;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;

	MSmode = snd_soc_read(codec, AK4376_15_AUDIO_IF_FORMAT);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:	/* before widget power up */
		ak4376->nDACOn = 1;
		snd_soc_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x01,0x01);		//PMCP1=1
		mdelay(6);																//wait 6ms
		udelay(500);															//wait 0.5ms
		snd_soc_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x30,0x30);		//PMLDO1P/N=1
		mdelay(1);																//wait 1ms
		break;
	case SND_SOC_DAPM_POST_PMU:	/* after widget power up */
		snd_soc_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x02,0x02);		//PMCP2=1
		mdelay(4);																//wait 4ms
		udelay(500);															//wait 0.5ms
		break;
	case SND_SOC_DAPM_PRE_PMD:	/* before widget power down */
		snd_soc_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x02,0x00);		//PMCP2=0
		break;
	case SND_SOC_DAPM_POST_PMD:	/* after widget power down */
		snd_soc_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x30,0x00);		//PMLDO1P/N=0
		snd_soc_update_bits(codec, AK4376_01_POWER_MANAGEMENT2, 0x01,0x00);		//PMCP1=0

	if (ak4376->pdata->nPllMode == 0) {
		if (MSmode & 0x10) {	//Master mode
			snd_soc_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x10,0x00);	//MS bit = 0
		}
	}

		ak4376->nDACOn = 0;

		break;
	}
	return 0;
}

static int ak4376_dac_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event) //CONFIG_LINF
{
	struct snd_soc_codec *codec = w->codec;
	
	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376_dac_event2(codec, event);
	
	return 0;
}

/* PLL control */
static int ak4376_pll_event2(struct snd_soc_codec *codec, int event) 
{
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:	/* before widget power up */
	case SND_SOC_DAPM_POST_PMU:	/* after widget power up */
		if ((ak4376->pdata->nPllMode == 1) || (ak4376->pdata->nPllMode == 2)) {
		snd_soc_update_bits(codec, AK4376_00_POWER_MANAGEMENT1, 0x01,0x01);	//PMPLL=1
		}
		else if (ak4376->pdata->nPllMode == 3) {
		snd_soc_update_bits(codec, AK4376_00_POWER_MANAGEMENT1, 0x10,0x10);	//PMOSC=1
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:	/* before widget power down */
	case SND_SOC_DAPM_POST_PMD:	/* after widget power down */
		if ((ak4376->pdata->nPllMode == 1) || (ak4376->pdata->nPllMode == 2)) {
		snd_soc_update_bits(codec, AK4376_00_POWER_MANAGEMENT1, 0x01,0x00);	//PMPLL=0
		}
		else if (ak4376->pdata->nPllMode == 3) {
		snd_soc_update_bits(codec, AK4376_00_POWER_MANAGEMENT1, 0x10,0x00);	//PMOSC=0
		}
		break;
	}

	return 0;
}

static int ak4376_pll_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *kcontrol, int event) //CONFIG_LINF
{
	struct snd_soc_codec *codec = w->codec;
	
	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376_pll_event2(codec, event);
	
	return 0;
}

/* HPL Mixer */
static const struct snd_kcontrol_new ak4376_hpl_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDACL", AK4376_07_DAC_MONO_MIXING, 0, 1, 0), 
	SOC_DAPM_SINGLE("RDACL", AK4376_07_DAC_MONO_MIXING, 1, 1, 0), 
};

/* HPR Mixer */
static const struct snd_kcontrol_new ak4376_hpr_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDACR", AK4376_07_DAC_MONO_MIXING, 4, 1, 0), 
	SOC_DAPM_SINGLE("RDACR", AK4376_07_DAC_MONO_MIXING, 5, 1, 0), 
};


/* ak4376 dapm widgets */
static const struct snd_soc_dapm_widget ak4376_dapm_widgets[] = {
// DAC
	SND_SOC_DAPM_DAC_E("AK4376_DAC", "NULL", AK4376_02_POWER_MANAGEMENT3, 0, 0,
			ak4376_dac_event, (SND_SOC_DAPM_POST_PMU |SND_SOC_DAPM_PRE_PMD 
                            |SND_SOC_DAPM_PRE_PMU |SND_SOC_DAPM_POST_PMD)),

// PLL, OSC
	SND_SOC_DAPM_SUPPLY_S("AK4376_PLL", 0, SND_SOC_NOPM, 0, 0,
			ak4376_pll_event, (SND_SOC_DAPM_POST_PMU |SND_SOC_DAPM_PRE_PMD 
                            |SND_SOC_DAPM_PRE_PMU |SND_SOC_DAPM_POST_PMD)),

	SND_SOC_DAPM_AIF_IN("AK4376_SDTI", "Playback", 0, SND_SOC_NOPM, 0, 0),

// Analog Output
	SND_SOC_DAPM_OUTPUT("AK4376_HPL"),
	SND_SOC_DAPM_OUTPUT("AK4376_HPR"),

	SND_SOC_DAPM_MIXER("AK4376_HPR_Mixer", AK4376_03_POWER_MANAGEMENT4, 1, 0,
			&ak4376_hpr_mixer_controls[0], ARRAY_SIZE(ak4376_hpr_mixer_controls)),

	SND_SOC_DAPM_MIXER("AK4376_HPL_Mixer", AK4376_03_POWER_MANAGEMENT4, 0, 0,
			&ak4376_hpl_mixer_controls[0], ARRAY_SIZE(ak4376_hpl_mixer_controls)),

};

static const struct snd_soc_dapm_route ak4376_intercon[] = 
{

//	{"AK4376_DAC", "NULL", "AK4376_PLL"},
//	{"AK4376_DAC", "NULL", "AK4376_SDTI"},
//
//	{"AK4376_HPL_Mixer", "LDACL", "AK4376_DAC"},
//	{"AK4376_HPL_Mixer", "RDACL", "AK4376_DAC"},
//	{"AK4376_HPR_Mixer", "LDACR", "AK4376_DAC"},
//	{"AK4376_HPR_Mixer", "RDACR", "AK4376_DAC"},
//
//	{"AK4376_HPL", "NULL", "AK4376_HPL_Mixer"},
//	{"AK4376_HPR", "NULL", "AK4376_HPR_Mixer"},

};

static int ak4376_set_mcki(struct snd_soc_codec *codec, int fs, int rclk)
{
	u8 mode;
	u8 mode2;
	int mcki_rate;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;

	akdbgprt("\t[AK4376] %s fs=%d rclk=%d\n",__FUNCTION__, fs, rclk);

	if ((fs != 0)&&(rclk != 0)) {
		if (rclk > 28800000) return -EINVAL;

		if (ak4376->pdata->nPllMode == 0) {	//PLL_OFF
			mcki_rate = rclk/fs;
		}
		else {		//XTAL_MODE
			if ( ak4376->xtalfreq == 0 ) {		//12.288MHz
				mcki_rate = 12288000/fs;
			}
			else {	//11.2896MHz
				mcki_rate = 11289600/fs;
			}
		}

		mode = snd_soc_read(codec, AK4376_05_CLOCK_MODE_SELECT);
		mode &= ~AK4376_CM;

		if (ak4376->lpmode == 0) {				//High Performance Mode
			switch (mcki_rate) {
			case 32:
				mode |= AK4376_CM_0;
				break;
			case 64:
				mode |= AK4376_CM_1;
				break;
			case 128:
				mode |= AK4376_CM_3;
				break;
			case 256:
				mode |= AK4376_CM_0;
				mode2 = snd_soc_read(codec, AK4376_24_MODE_CONTROL);
				if ( fs <= 12000 ) {
					mode2 |= 0x40;	//DSMLP=1
					snd_soc_write(codec, AK4376_24_MODE_CONTROL, mode2);
				}
				else {
					mode2 &= ~0x40;	//DSMLP=0
					snd_soc_write(codec, AK4376_24_MODE_CONTROL, mode2);
				}
				break;
			case 512:
				mode |= AK4376_CM_1;
				break;
			case 1024:
				mode |= AK4376_CM_2;
				break;
			default:
				return -EINVAL;
			}
		}
		else {					//Low Power Mode (LPMODE == DSMLP == 1)
			switch (mcki_rate) {
			case 32:
				mode |= AK4376_CM_0;
				break;
			case 64:
				mode |= AK4376_CM_1;
				break;
			case 128:
				mode |= AK4376_CM_3;
				break;
			case 256:
				mode |= AK4376_CM_0;
				break;
			case 512:
				mode |= AK4376_CM_1;
				break;
			case 1024:
				mode |= AK4376_CM_2;
				break;
			default:
				return -EINVAL;
			}
		}
			
//		snd_soc_write(codec, AK4376_05_CLOCK_MODE_SELECT, mode);
	}
		
	return 0;
}

#if 0
static int ak4376_set_pllblock(struct snd_soc_codec *codec, int fs)
{
	u8 mode;
	int nMClk, nPLLClk, nRefClk;
	int PLDbit, PLMbit, MDIVbit;
	int PLLMCKI;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;
	
	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	mode = snd_soc_read(codec, AK4376_05_CLOCK_MODE_SELECT);
	mode &= ~AK4376_CM;

		if ( fs <= 24000 ) {
			mode |= AK4376_CM_1;
			nMClk = 512 * fs;
		}
		else if ( fs <= 96000 ) {
			mode |= AK4376_CM_0;
			nMClk = 256 * fs;
		}
		else if ( fs <= 192000 ) {
			mode |= AK4376_CM_3;
			nMClk = 128 * fs;
		}
		else {		//fs > 192kHz
			mode |= AK4376_CM_1;
			nMClk = 64 * fs;
		}
	
	snd_soc_write(codec, AK4376_05_CLOCK_MODE_SELECT, mode);
	
	if ( (fs % 8000) == 0 ) {
		nPLLClk = 122880000;
	}
	else if ( (fs == 11025 ) && ( ak4376->nBickFreq == 1 ) && ( ak4376->pdata->nPllMode == 1 )) {
		nPLLClk = 101606400;
	}
	else {
		nPLLClk = 112896000;
	}

	if ( ak4376->pdata->nPllMode == 1 ) {		//BICK_PLL (Slave)
		if ( ak4376->nBickFreq == 0 ) {		//32fs
			if ( fs <= 96000 ) PLDbit = 1;
			else if ( fs <= 192000 ) PLDbit = 2;
			else PLDbit = 4;
			nRefClk = 32 * fs / PLDbit;
		}
		else if ( ak4376->nBickFreq == 1 ) {	//48fs
			if ( fs <= 16000 ) PLDbit = 1;
			else if ( fs <= 192000 ) PLDbit = 3;
			else PLDbit = 6;
			nRefClk = 48 * fs / PLDbit;
		}
		else {  									// 64fs
			if ( fs <= 48000 ) PLDbit = 1;
			else if ( fs <= 96000 ) PLDbit = 2;
			else if ( fs <= 192000 ) PLDbit = 4;
			else PLDbit = 8;
			nRefClk = 64 * fs / PLDbit;
		}
	}
	
		else {		//MCKI_PLL (Master)
				if ( ak4376->nPllMCKI == 0 ) { //9.6MHz
					PLLMCKI = 9600000;
					if ( (fs % 4000) == 0) nRefClk = 1920000;
					else nRefClk = 384000;
				}
				else if ( ak4376->nPllMCKI == 1 ) { //11.2896MHz
					PLLMCKI = 11289600;
					if ( (fs % 4000) == 0) return -EINVAL;
					else nRefClk = 2822400;
				}
				else if ( ak4376->nPllMCKI == 2 ) { //12.288MHz
					PLLMCKI = 12288000;
					if ( (fs % 4000) == 0) nRefClk = 3072000;
					else nRefClk = 768000;
				}
				else {								//19.2MHz
					PLLMCKI = 19200000;
					if ( (fs % 4000) == 0) nRefClk = 1920000;
					else nRefClk = 384000;
				}
				PLDbit = PLLMCKI / nRefClk;
			}

	PLMbit = nPLLClk / nRefClk;
	MDIVbit = nPLLClk / nMClk;
	
	PLDbit--;
	PLMbit--;
	MDIVbit--;
	
	//PLD15-0
	snd_soc_write(codec, AK4376_0F_PLL_REF_CLK_DIVIDER1, ((PLDbit & 0xFF00) >> 8));
	snd_soc_write(codec, AK4376_10_PLL_REF_CLK_DIVIDER2, ((PLDbit & 0x00FF) >> 0));
	//PLM15-0
	snd_soc_write(codec, AK4376_11_PLL_FB_CLK_DIVIDER1, ((PLMbit & 0xFF00) >> 8));
	snd_soc_write(codec, AK4376_12_PLL_FB_CLK_DIVIDER2, ((PLMbit & 0x00FF) >> 0));

	if (ak4376->pdata->nPllMode == 1 ) {	//BICK_PLL (Slave)
		snd_soc_update_bits(codec, AK4376_0E_PLL_CLK_SOURCE_SELECT, 0x03, 0x01);	//PLS=1(BICK)
	}
	else {										//MCKI PLL (Slave/Master)
		snd_soc_update_bits(codec, AK4376_0E_PLL_CLK_SOURCE_SELECT, 0x03, 0x00);	//PLS=0(MCKI)
	}

	//MDIV7-0
	snd_soc_write(codec, AK4376_14_DAC_CLK_DIVIDER, MDIVbit);

	return 0;
}
#endif

#if 0
static int ak4376_set_timer(struct snd_soc_codec *codec)
{
	int ret, curdata;
	int count, tm, nfs;
	int lvdtm, vddtm, hptm;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;

	lvdtm = 0;
	vddtm = 0;
	hptm = 0;
	
	nfs = ak4376->fs1;

	//LVDTM2-0 bits set
	ret = snd_soc_read(codec, AK4376_03_POWER_MANAGEMENT4);
	curdata = (ret & 0x70) >> 4;	//Current data Save
	ret &= ~0x70;
	do {
       count = 1000 * (64 << lvdtm);
       tm = count / nfs;
       if ( tm > LVDTM_HOLD_TIME ) break;
       lvdtm++;
    } while ( lvdtm < 7 );			//LVDTM2-0 = 0~7
	if ( curdata != lvdtm) {
			snd_soc_write(codec, AK4376_03_POWER_MANAGEMENT4, (ret | (lvdtm << 4)));
	}

	//VDDTM3-0 bits set
	ret = snd_soc_read(codec, AK4376_04_OUTPUT_MODE_SETTING);
	curdata = (ret & 0x3C) >> 2;	//Current data Save
	ret &= ~0x3C;
	do {
       count = 1000 * (1024 << vddtm);
       tm = count / nfs;
       if ( tm > VDDTM_HOLD_TIME ) break;
       vddtm++;
    } while ( vddtm < 8 );			//VDDTM3-0 = 0~8
	if ( curdata != vddtm) {
			snd_soc_write(codec, AK4376_04_OUTPUT_MODE_SETTING, (ret | (vddtm<<2)));
	}

	return 0;
}
#endif

#if 0
static int ak4376_hw_params_set(struct snd_soc_codec *codec, int nfs1)
{
	u8 fs;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
	
	fs = snd_soc_read(codec, AK4376_05_CLOCK_MODE_SELECT);
	fs &= ~AK4376_FS;

	switch (nfs1) {
	case 8000:
		fs |= AK4376_FS_8KHZ;
		break;
	case 11025:
		fs |= AK4376_FS_11_025KHZ;
		break;
	case 16000:
		fs |= AK4376_FS_16KHZ;
		break;
	case 22050:
		fs |= AK4376_FS_22_05KHZ;
		break;
	case 32000:
		fs |= AK4376_FS_32KHZ;
		break;
	case 44100:
		fs |= AK4376_FS_44_1KHZ;
		break;
	case 48000:
		fs |= AK4376_FS_48KHZ;
		break;
	case 88200:
		fs |= AK4376_FS_88_2KHZ;
		break;
	case 96000:
		fs |= AK4376_FS_96KHZ;
		break;
	case 176400:
		fs |= AK4376_FS_176_4KHZ;
		break;
	case 192000:
		fs |= AK4376_FS_192KHZ;
		break;
	case 352800:
		fs |= AK4376_FS_352_8KHZ;
		break;
	case 384000:
		fs |= AK4376_FS_384KHZ;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_write(codec, AK4376_05_CLOCK_MODE_SELECT, fs);

	if ( ak4376->pdata->nPllMode == 0 ) {		//PLL Off
		snd_soc_update_bits(codec, AK4376_13_DAC_CLK_SOURCE, 0x03, 0x00);	//DACCKS=0
		ak4376_set_mcki(codec, nfs1, ak4376->rclk);
	}
	else if ( ak4376->pdata->nPllMode == 3 ) {	//XTAL MODE
		snd_soc_update_bits(codec, AK4376_13_DAC_CLK_SOURCE, 0x03, 0x02);	//DACCKS=2
		ak4376_set_mcki(codec, nfs1, ak4376->rclk);
	}
	else {											//PLL mode
		snd_soc_update_bits(codec, AK4376_13_DAC_CLK_SOURCE, 0x03, 0x01);	//DACCKS=1
		ak4376_set_pllblock(codec, nfs1);
	}

	ak4376_set_timer(codec);

	return 0;
}
#endif

static int ak4376_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
//	struct snd_soc_codec *codec = dai->codec;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
//	struct snd_soc_codec* codec = ak4376_codec;
	struct ak4376_priv *ak4376 = ak4376_codec_priv;
	int ret;
//	int n = 0;

	ak4376->fs1 = params_rate(params);

	akdbgprt("\t[AKM test] %s dai->name=%s\n", __FUNCTION__, dai->name);	//AKM test
	
	ret = pinctrl_select_state(ak4376_pinctrl, ak4376_gpios[GPIO_HIFI_LDO_ON].gpioctrl);

//	ak4376_hw_params_set(codec, ak4376->fs1);

//	for(n = 0; n < 0x25; n++){
//		snd_soc_write(codec, n, ak4376_reg[n]);
//	}
#if 0
	for(n = 0; n < 0x25; n++){
		ret = snd_soc_read(codec, n);
		akdbgprt("%s(%d) reg.%x:%x\n", __FUNCTION__, __LINE__, n, ret);
	}
#endif
	return 0;
}

static int ak4376_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
		unsigned int freq, int dir)
{
//	struct snd_soc_codec *codec = dai->codec;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_codec *codec = ak4376_codec;
	struct ak4376_priv *ak4376 = ak4376_codec_priv;
	int ret;

	akdbgprt("\t[AK4376] %s freq=%dHz(%d)\n",__FUNCTION__,freq,__LINE__);
	
	ret = pinctrl_select_state(ak4376_pinctrl, ak4376_gpios[GPIO_HIFI_LDO_ON].gpioctrl);

	ak4376_pdn_control(codec, 1);

	ak4376->rclk = freq;

	if ((ak4376->pdata->nPllMode == 0) || (ak4376->pdata->nPllMode == 3)) {	//Not PLL mode
		ak4376_set_mcki(codec, ak4376->fs1, freq);
	}

	return 0;
}

static int ak4376_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{

//	struct snd_soc_codec *codec = dai->codec;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_codec *codec = ak4376_codec;
	struct ak4376_priv *ak4376 = ak4376_codec_priv;
	int ret;
	u8 format;

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
	
	ret = pinctrl_select_state(ak4376_pinctrl, ak4376_gpios[GPIO_HIFI_LDO_ON].gpioctrl);

	ak4376_pdn_control(codec, 1);

	/* set master/slave audio interface */
	format = snd_soc_read(codec, AK4376_15_AUDIO_IF_FORMAT);
	format &= ~AK4376_DIF;
	
    switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
        case SND_SOC_DAIFMT_CBS_CFS:
    		format |= AK4376_SLAVE_MODE;
            break;
        case SND_SOC_DAIFMT_CBM_CFM:
	    	if (ak4376->nDeviceID == 2) {
    		format |= AK4376_MASTER_MODE;
	    	}
	    	else return -EINVAL;
	    	break;
        case SND_SOC_DAIFMT_CBS_CFM:
        case SND_SOC_DAIFMT_CBM_CFS:
        default:
            dev_err(codec->dev, "Clock mode unsupported");
           return -EINVAL;
    	}
	
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format |= AK4376_DIF_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format |= AK4376_DIF_MSB_MODE;
		break;
	default:
		return -EINVAL;
	}

	/* set format */
//	snd_soc_write(codec, AK4376_15_AUDIO_IF_FORMAT, format);

	return 0;
}
#if 0
static int ak4376_volatile(struct snd_soc_codec *codec, unsigned int reg)
{
	int	ret;

	switch (reg) {
		default:
			ret = 0;
			break;
	}
	return ret;
}

static int ak4376_readable(struct snd_soc_codec *codec, unsigned int reg)
{

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	if (reg >= ARRAY_SIZE(ak4376_access_masks))
		return 0;
	return ak4376_access_masks[reg].readable != 0;
}
#endif

/* Read ak4376 register cache */
static inline u32 ak4376_read_reg_cache(struct snd_soc_codec *codec, u16 reg)
{
	u8 *cache = codec->reg_cache;
	BUG_ON(reg > ARRAY_SIZE(ak4376_reg));
	return (u32)cache[reg];
}


kal_uint32 ak4376_read_byte(kal_uint8 cmd, kal_uint8 *returnData)
{
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(ak4376_codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;
	struct i2c_client *client = ak4376->i2c;

	char cmd_buf[1] = { 0x00 };
	char readData = 0;
	int ret = 0;

	client->ext_flag =  ((client->ext_flag) & I2C_MASK_FLAG) | I2C_WR_FLAG | I2C_DIRECTION_FLAG;
	client->ext_flag &= ~(I2C_DMA_FLAG);
	cmd_buf[0] = cmd;
	ret = i2c_master_send(client, &cmd_buf[0], (1 << 8 | 1));
	if (ret < 0) {
		client->ext_flag = 0;
		return 0;
	}

	readData = cmd_buf[0];
	*returnData = readData;
	
	client->ext_flag = 0;

	return 1;
}


/* Read ak4376 IC register */
unsigned int ak4376_i2c_read(u8 *reg, int reglen, u8 *data, int datalen)
{
	struct i2c_msg xfer[2];
	int ret;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(ak4376_codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;
	struct i2c_client *client = ak4376->i2c;

	// Write register 
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = reglen;
	xfer[0].buf = reg;

	// Read data
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = datalen;
	xfer[1].buf = data;

	ret = i2c_transfer(client->adapter, xfer, 2);

	if (ret == 2)
		return 0;
	else if (ret < 0)
		return -ret;
	else 
		return -EIO;
}

unsigned int ak4376_reg_read(struct snd_soc_codec *codec, unsigned int reg)
{
	unsigned char tx[1], rx[1];
	int	wlen, rlen;
	int ret = 0;
	unsigned int rdata = 0;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;

	if (ak4376->pdn1 == 0) {
		rdata = ak4376_read_reg_cache(codec, reg);
//		akdbgprt("\t[AK4376] %s Read cache\n",__FUNCTION__);
	} else if ((ak4376->pdn1 == 1) || (ak4376->pdn1 == 2)) {
		wlen = 1;
		rlen = 1;
		tx[0] = reg;

	   ret = ak4376_read_byte(reg, rx);

		//ret = ak4376_i2c_read(tx, wlen, rx, rlen);
//		akdbgprt("\t[AK4376] %s Read IC register\n",__FUNCTION__);
	
		if (ret < 0) {
			akdbgprt("\t[AK4376] %s error ret = %d\n",__FUNCTION__,ret);
			rdata = -EIO;
		}
		else {
			rdata = (unsigned int)rx[0];
		}
	}

	return rdata;
}

/* Write AK4376 register cache */
static inline void ak4376_write_reg_cache(
struct snd_soc_codec *codec, 
u16 reg,
u16 value)
{
    u8 *cache = codec->reg_cache;
    BUG_ON(reg > ARRAY_SIZE(ak4376_reg));
    cache[reg] = (u8)value;
}

static int ak4376_write_register(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	int wlen;
	int rc = 0;
	unsigned char tx[2];
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;

//	akdbgprt("\t[AK4376] %s(%d) (%x,%x)\n",__FUNCTION__,__LINE__,reg,value);

	wlen = 2;
	tx[0] = reg; 
	tx[1] = value;

	ak4376_write_reg_cache(codec, reg, value);	

       ak4376->i2c->ext_flag = ((ak4376->i2c->ext_flag) & I2C_MASK_FLAG) | I2C_DIRECTION_FLAG;
       ak4376->i2c->ext_flag &= ~(I2C_DMA_FLAG);

	if ((ak4376->pdn1 == 1) || (ak4376->pdn1 ==2)) {
//		akdbgprt("%s(%d) i2c master send\n", __FUNCTION__, __LINE__);
		rc = i2c_master_send(ak4376->i2c, tx, wlen);
//		akdbgprt("%s(%d) i2c master send RETVAL %d\n", __FUNCTION__, __LINE__, rc);
	}

	return rc;
}


#ifdef CONFIG_DEBUG_FS_CODEC
static int ak4376_reg_write(
struct snd_soc_codec *codec,
u16 reg,
u16 value)
{
	akdbgprt("\t[AK4376] %s(%d) (%x,%x)\n",__FUNCTION__,__LINE__,reg,value);

	snd_soc_write(codec, (unsigned int)reg, (unsigned int)value);

    return 0;
}
#endif

// * for AK4376
static int ak4376_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *codec_dai)
{
	int ret = 0;

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	if(ak4376_gpios[GPIO_HIFI_LDO_ON].gpio_prepare){
		akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
		ret = pinctrl_select_state(ak4376_pinctrl, ak4376_gpios[GPIO_HIFI_LDO_ON].gpioctrl);
		if(ret){
			akdbgprt("[ak4376] %s(%d) failed to set ldo enable\n", __FUNCTION__, __LINE__);
			return -1;
		}
		udelay(2);
	}

	return ret;
}


static int ak4376_set_bias_level(struct snd_soc_codec *codec,
		enum snd_soc_bias_level level)
{
	int level1 = level;

	akdbgprt("\t[AK4376] %s(%d) level=%d\n",__FUNCTION__,__LINE__,level1);
	akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level=%d\n",__FUNCTION__,__LINE__,codec->dapm.bias_level);

	switch (level1) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		if (codec->dapm.bias_level == SND_SOC_BIAS_STANDBY)
			akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level == SND_SOC_BIAS_STANDBY\n",__FUNCTION__,__LINE__);
		if (codec->dapm.bias_level == SND_SOC_BIAS_ON)
			akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level >= SND_SOC_BIAS_ON\n",__FUNCTION__,__LINE__);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_PREPARE) {
			akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level == SND_SOC_BIAS_PREPARE\n",__FUNCTION__,__LINE__);
//			ak4376_pdn_control(codec, 0);
		} if (codec->dapm.bias_level == SND_SOC_BIAS_OFF)
			akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level == SND_SOC_BIAS_OFF\n",__FUNCTION__,__LINE__);
		break;
	case SND_SOC_BIAS_OFF:
		if (codec->dapm.bias_level == SND_SOC_BIAS_STANDBY) {
			akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level == SND_SOC_BIAS_STANDBY\n",__FUNCTION__,__LINE__);
//			ak4376_pdn_control(codec, 0);
		} break;
	}
	codec->dapm.bias_level = level;

	return 0;
}
#if 0
static int ak4376_set_dai_mute(struct snd_soc_dai *dai, int mute) 
{
	u8 MSmode;
//	struct snd_soc_codec *codec = dai->codec;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_codec *codec = ak4376_codec;
	struct ak4376_priv *ak4376 = ak4376_codec_priv;

	akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level=%d\n",__FUNCTION__,__LINE__,codec->dapm.bias_level);

	mt_set_gpio_mode(GPIO_HIFI_LDO_EN_PIN, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_HIFI_LDO_EN_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_HIFI_LDO_EN_PIN, GPIO_OUT_ONE);

	if (ak4376->pdata->nPllMode == 0) {
		if ( ak4376->nDACOn == 0 ) {
			MSmode = snd_soc_read(codec, AK4376_15_AUDIO_IF_FORMAT);
			if (MSmode & 0x10) {	//Master mode	
				snd_soc_update_bits(codec, AK4376_15_AUDIO_IF_FORMAT, 0x10,0x00);	//MS bit = 0
			}
		}
	}

	if (codec->dapm.bias_level <= SND_SOC_BIAS_STANDBY) {
		akdbgprt("\t[AK4376] %s(%d) codec->dapm.bias_level <= SND_SOC_BIAS_STANDBY\n",__FUNCTION__,__LINE__);

		ak4376_pdn_control(codec, 0);
	}

	return 0;
}
#endif
#define AK4376_RATES		(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |\
				SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |\
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
				SNDRV_PCM_RATE_192000)

#define AK4376_FORMATS		(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)


static struct snd_soc_dai_ops ak4376_dai_ops = {
	.hw_params	= ak4376_hw_params,
	.set_sysclk	= ak4376_set_dai_sysclk,
	.set_fmt	= ak4376_set_dai_fmt,
	.trigger = ak4376_trigger,
	//.digital_mute = ak4376_set_dai_mute,
};

struct snd_soc_dai_driver ak4376_dai[] = {   
	{										 
		.name = "ak4376-AIF1",
		.playback = {
		       .stream_name = "Playback",
		       .channels_min = 1,
		       .channels_max = 2,
		       .rates = AK4376_RATES,
		       .formats = AK4376_FORMATS,
		},
		.ops = &ak4376_dai_ops,
	},
};

static int ak4376_init_reg(struct snd_soc_codec *codec)
{
	u8 DeviceID;
	int n;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376_pdn_control(codec, 1);

	DeviceID = ak4376_reg_read(codec, AK4376_15_AUDIO_IF_FORMAT);

	switch (DeviceID >> 5) {
	case 0:
		ak4376->nDeviceID = 0;		//0:AK4375
		akdbgprt("AK4375 is connecting.\n");
		break;
	case 1:
		ak4376->nDeviceID = 1;		//1:AK4375A
		akdbgprt("AK4375A is connecting.\n");
		break;
	case 2:
		ak4376->nDeviceID = 2;		//2:AK4376
		akdbgprt("AK4376 is connecting.\n");
		break;
	default:
		ak4376->nDeviceID = 3;		//3:Other IC
		akdbgprt("This device are neither AK4375/A nor AK4376.\n");
	}

	ak4376_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	for(n = 0; n < 0x25; n++){
		snd_soc_write(codec, n, ak4376_reg[n]);
	}

	akdbgprt("\t[AK4376 bias] %s(%d)\n",__FUNCTION__,__LINE__);

	return 0;
}


struct ak4376_platform_data ak4376_pdata;

static int ak4376_probe(struct snd_soc_codec *codec)
{
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;
	int ret = 0;

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
#if 0
	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}
#endif
#ifdef AK4376_CONTIF_DEBUG
	codec->read = ak4376_reg_read;
#endif

//	codec->write = ak4376_write_register;

#ifdef CONFIG_DEBUG_FS_CODEC
	mutex_init(&io_lock);
#endif

	ak4376_codec = codec;
	//ak4376->pdata = dev_get_platdata(codec->dev);
	ak4376->pdata = &ak4376_pdata;

	ak4376->pdata->nPllMode = PLL_OFF;
//	ak4376->pdata->pdn_en = GPIO16_HIFI_PDN;
	
	if(ak4376_gpios[GPIO_HIFI_LDO_ON].gpio_prepare){
		akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
		ret = pinctrl_select_state(ak4376_pinctrl, ak4376_gpios[GPIO_HIFI_LDO_ON].gpioctrl);
		if(ret){
			akdbgprt("[ak4376] %s(%d) failed to set ldo enable\n", __FUNCTION__, __LINE__);
			return -1;
		}
		udelay(2);
	}
	ak4376_init_reg(codec);
//	snd_soc_cache_init(codec);

	akdbgprt("\t[AK4376 Effect] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376->fs1 = 48000;
	ak4376->rclk = 0;
	ak4376->nBickFreq = 1;		//0:32fs, 1:48fs, 2:64fs
	ak4376->nPllMCKI = 0;		//0:9.6MHz, 1:11.2896MHz, 2:12.288MHz, 3:19.2MHz
	ak4376->lpmode = 0;			//0:High Performance mode, 1:Low Power Mode
	ak4376->xtalfreq = 0;		//0:12.288MHz, 1:11.2896MHz
	ak4376->nDACOn = 0;

/*add by jiaqing.yang for task 1537712 Heavy Current in suspend mode begin*/
	ak4376_pdn_control(codec, 0);
/*add by jiaqing.yang for task 1537712 Heavy Current in suspend mode end*/

	return ret;
}

static int ak4376_remove(struct snd_soc_codec *codec)
{

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int ak4376_suspend(struct snd_soc_codec *codec)
{

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

	ak4376_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int ak4376_resume(struct snd_soc_codec *codec)
{

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

//	ak4376_init_reg(codec);

	return 0;
}

struct snd_soc_codec_driver soc_codec_dev_ak4376 = {
	.probe = ak4376_probe,
	.remove = ak4376_remove,
	.suspend = ak4376_suspend,
	.resume = ak4376_resume,
	.write = ak4376_write_register,
	.read = ak4376_reg_read,
	.controls = ak4376_snd_controls,
	.num_controls = ARRAY_SIZE(ak4376_snd_controls),

	.idle_bias_off = true,
	.set_bias_level = ak4376_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(ak4376_reg),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = ak4376_reg,
//	.readable_register = ak4376_readable,
//	.volatile_register = ak4376_volatile,
	.dapm_widgets = ak4376_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ak4376_dapm_widgets),
	.dapm_routes = ak4376_intercon,
	.num_dapm_routes = ARRAY_SIZE(ak4376_intercon),
};

EXPORT_SYMBOL_GPL(soc_codec_dev_ak4376);

static int ak4376_i2c_probe(struct i2c_client *i2c,
                            const struct i2c_device_id *id)
{
	struct ak4376_priv *ak4376;
	int ret=0;
//	int ret2=0;
	
	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
	ak4376 = kzalloc(sizeof(struct ak4376_priv), GFP_KERNEL);
	if (ak4376 == NULL){
		akdbgprt("\t[AK4376] %s(%d) cannot alloc ak4376\n",__FUNCTION__,__LINE__);
		return -ENOMEM;
	}
	ak4376_codec_priv = ak4376;

#ifdef  CONFIG_DEBUG_FS_CODEC
	ret = device_create_file(&i2c->dev, &dev_attr_reg_data);
	if (ret) {
		pr_err("%s: Error to create reg_data\n", __FUNCTION__);
	}
#endif

	i2c_set_clientdata(i2c, ak4376);

//	ak4376_data = ak4376;
	ak4376->i2c = i2c;
	ak4376->pdn1 = 0;
	ak4376->pdn2 = 0;
//	ak4376->priv_pdn_en = GPIO16_HIFI_PDN;

#ifdef CONFIG_MTK_LEGACY
	mt_set_gpio_mode(GPIO16_HIFI_PDN,  GPIO_MODE_00);
	mt_set_gpio_dir(GPIO16_HIFI_PDN,       GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO16_HIFI_PDN,     GPIO_OUT_ONE);
#else
	if(ak4376_gpios[GPIO_HIFI_PDN_HIGH].gpio_prepare){
		akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
		ret = pinctrl_select_state(ak4376_pinctrl, ak4376_gpios[GPIO_HIFI_PDN_HIGH].gpioctrl);
		if(ret){
			akdbgprt("[ak4376] %s(%d) failed to set pnd enable\n", __FUNCTION__, __LINE__);
			return -1;
		}
		udelay(2);
	}
#endif

	akdbgprt("\t[AK4376] ++ %s(%d) pre register codec\n", __FUNCTION__,__LINE__);
	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_ak4376, &ak4376_dai[0], ARRAY_SIZE(ak4376_dai));
	if (ret < 0){
		kfree(ak4376);
		akdbgprt("[AK4376 Error!] %s(%d)\n",__FUNCTION__,__LINE__);
		return -1;
	}

	akdbgprt("[AK4376] %s(%d) ok pdn1=%d\n", __FUNCTION__,__LINE__,ak4376->pdn1);

	return ret;
}

static int ak4376_i2c_remove(struct i2c_client *client)
{

	akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);

#ifdef CONFIG_DEBUG_FS_CODEC
	device_remove_file(&client->dev, &dev_attr_reg_data);
#endif

	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));

	return 0;
}

static int ak4376_i2c_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	akdbgprt("%s(%d)\n", __FUNCTION__, __LINE__);
	strcpy(info->type, AK4376_NAME);
	return 0;
}
#ifdef CONFIG_OF
static struct of_device_id ak4376_match_tbl[] = {
	{ .compatible = "mediatek,AUDIO_HIFI", },
	{ },
};

//MODULE_DEVICE_TABLE(of, ak4376_match_tbl);
#endif

#if 1//def CONFIG_MTK_LEGACY
static const struct i2c_device_id ak4376_i2c_id[] = {
	{ AK4376_NAME, 0 },
	{ }
};
#endif

//MODULE_DEVICE_TABLE(i2c, ak4376_i2c_id);
//const unsigned short ak4376_i2c_addr[] = {0x10, 0x00};
static struct i2c_driver ak4376_i2c_driver = {
	.driver = {
		.name = AK4376_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = ak4376_match_tbl,
#endif
	},
	.probe = ak4376_i2c_probe,
	.remove = ak4376_i2c_remove,
	.detect = ak4376_i2c_detect,
#if 1//def CONFIG_MTK_LEGACY
	.id_table = ak4376_i2c_id,
#endif
};

//module_i2c_driver(ak4376_i2c_driver);

#ifdef CONFIG_MTK_LEGACY
static struct i2c_board_info __initdata ak4376_i2c_info = {I2C_BOARD_INFO(AK4376_NAME, 0x10)};

struct platform_device ak4376_pdevice = {
	.name	       = AK4376_NAME,
	.id            = -1,
};
#endif

extern int mt_set_gpio_mode(unsigned long pin, unsigned long mode);
extern int mt_set_gpio_dir(unsigned long pin, unsigned long dir);
extern int mt_set_gpio_out(unsigned long pin, unsigned long output);

//#define GPIO_HIFI_LDO_EN_PIN (GPIO115 | 0x80000000)


static int ak4376_platform_probe(struct platform_device *pdev)
{
	int ret ;
	akdbgprt("[ak4376] %s(%d) start\n", __FUNCTION__, __LINE__);
#if defined(CONFIG_OF) && !defined(CONFIG_MTK_LEGACY)
	ak4376_GPIO_parse(&pdev->dev);
	if(ak4376_gpios[GPIO_HIFI_LDO_ON].gpio_prepare){
		akdbgprt("\t[AK4376] %s(%d)\n",__FUNCTION__,__LINE__);
		ret = pinctrl_select_state(ak4376_pinctrl, ak4376_gpios[GPIO_HIFI_LDO_ON].gpioctrl);
		if(ret){
			akdbgprt("[ak4376] %s(%d) failed to set ldo enable\n", __FUNCTION__, __LINE__);
			return -1;
		}
		udelay(2);
	}
#else
	mt_set_gpio_mode(GPIO_HIFI_LDO_EN_PIN, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_HIFI_LDO_EN_PIN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_HIFI_LDO_EN_PIN, GPIO_OUT_ONE);
#endif

	akdbgprt("%s(%d) add i2c driver\n", __func__, __LINE__);
	ret = i2c_add_driver(&ak4376_i2c_driver);
	if(ret !=0)
	{
		akdbgprt("[ak4376] %s(%d) probe failed\n", __FUNCTION__, __LINE__);
		return -1;
	}

	akdbgprt("%s(%d) ok!!\n", __func__, __LINE__);
	return 0;
}



static int ak4376_platform_remove(struct platform_device *pdev)
{
	akdbgprt("[ak4376] %s(%d)\n", __FUNCTION__, __LINE__);
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id ak4376_of_match_table[] = {
	{.compatible = "AKM,ak4376",},
	{},
};

//MODULE_DEVICE_TABLE(of, ak4376_of_match_table);
#endif

static struct platform_driver ak4376_platform_driver = {
	.probe      = ak4376_platform_probe,
	.remove     = ak4376_platform_remove,
	.driver     =
	{
		.name  = AK4376_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = ak4376_of_match_table,
#endif
	}
};

static unsigned char attr_reg;
static unsigned char attr_val;
static ssize_t ak4376_show(struct class* class, struct class_attribute* atrr, char *buf){
	int n;
	int retval = 0;
	int pos = 0;
	if(buf == NULL){
		return -1;
	}

	for(n = 0; n < 0x25; n++){
		ak4376_read_byte(n, &attr_val);
		retval += sprintf(buf+pos, "%02x:%02x\n", n, attr_val);
		pos += 6;
	}
	return retval;
}

static ssize_t ak4376_store(struct class *dev,
				struct class_attribute *attr, const char *buf, size_t size){

	unsigned int reg, val;
//	struct ak4376_priv *ak4376 = snd_soc_codec_get_drvdata(ak4376_codec);
	struct ak4376_priv *ak4376 = ak4376_codec_priv;
	struct i2c_client *client = ak4376->i2c;
	unsigned char write[2];

	if(buf == NULL || !size){
		return -1;
	}

	akdbgprt("%s(%d) buf:%s\n", __FUNCTION__, __LINE__, buf);
	if (sscanf(buf, "%x %x", &reg, &val) >= 1){
		write[0] = reg;
		write[1] = val;
		i2c_master_send(client, write, 2);
		akdbgprt("%s(%d) reg:%x, val:%x\n", __FUNCTION__, __LINE__, reg, val);
		attr_reg = reg;
		return strlen(buf);
	}

	attr_reg = reg;

	return strlen(buf);
}
static struct class* ak4376_class;
CLASS_ATTR(ak4376, 0644, ak4376_show, ak4376_store);

static int __init ak4376_modinit(void)
{
#ifdef CONFIG_MTK_LEGACY
	int ret;

	akdbgprt("[ak4376] %s(%d) pre register platform device\n", __FUNCTION__, __LINE__);
	ret = platform_device_register(&ak4376_pdevice);
	if( ret){
		akdbgprt("[ak4376] %s(%d) platform_device_register failed!\n", __FUNCTION__, __LINE__);
		return ret;
	}

	akdbgprt("\t[AK4376] %s(%d)\n", __FUNCTION__,__LINE__);
	i2c_register_board_info(0, &ak4376_i2c_info, 1);
#endif

	akdbgprt("[ak4376] %s(%d) pre register platform driver\n", __FUNCTION__, __LINE__);
	if(platform_driver_register(&ak4376_platform_driver)){
		akdbgprt("[ak4376] %s(%d) platform_driver_register failed\n", __FUNCTION__, __LINE__);
		return -ENODEV;
	}

	akdbgprt("[ak4376] %s(%d) classs create\n", __FUNCTION__, __LINE__);
	ak4376_class = class_create(THIS_MODULE, "ak4376");
	if(IS_ERR(ak4376_class)){
		akdbgprt("[ak4376] %s(%d) classs create failed\n", __FUNCTION__, __LINE__);
		return -1;
	}

	if(class_create_file(ak4376_class, &class_attr_ak4376)){
		akdbgprt("[ak4376] %s(%d) classs create file failed\n", __FUNCTION__, __LINE__);
		return -1;
	}

	return 0;
}

module_init(ak4376_modinit);

static void __exit ak4376_exit(void)
{
	akdbgprt("[ak4376] %s(%d)\n", __FUNCTION__, __LINE__);
	i2c_del_driver(&ak4376_i2c_driver);
}
module_exit(ak4376_exit);

MODULE_DESCRIPTION("ASoC ak4376 codec driver");
MODULE_LICENSE("GPL");
