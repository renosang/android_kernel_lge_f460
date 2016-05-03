/*
 * Maxim MAX77819 Charger Driver
 *
 * Copyright (C) 2013 Maxim Integrated Product
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*#define DEBUG */
/*#define VERBOSE_DEBUG */

#define log_level  1
#define log_worker 1

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/wakelock.h>

/* for Regmap */
#include <linux/regmap.h>

/* for Device Tree */
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include <linux/power_supply.h>
#include <linux/mfd/max77819.h>
#include <linux/mfd/max77819-charger.h>

#if defined(CONFIG_LGE_PM_BATTERY_MAX17048_FUELGAUGE)
#include <linux/power/max17048_battery.h>
#endif /*                                          */
#if defined(CONFIG_LGE_PM)
#include <mach/board_lge.h>
#endif /*               */

/* Vendor specific */
#define DRIVER_DESC    "MAX77819 Charger Driver"
#define DRIVER_NAME    MAX77819_CHARGER_NAME
#define DRIVER_VERSION MAX77819_DRIVER_VERSION".3-rc modified by LGE"
#define DRIVER_AUTHOR  "Gyungoh Yoo <jack.yoo@maximintegrated.com>"

#define IRQ_WORK_DELAY              0
#define IRQ_WORK_INTERVAL           msecs_to_jiffies(5000)
#define LOG_WORK_INTERVAL           msecs_to_jiffies(30000)

#define VERIFICATION_UNLOCK         0

/* Register map */
#define CHGINT                      0x30
#define CHGINTM1                    0x31
#define CHGINT1_AICLOTG             BIT(7)
#define CHGINT1_TOPOFF              BIT(6)
#define CHGINT1_OVP                 BIT(5)
#define CHGINT1_DC_UVP              BIT(4)
#define CHGINT1_CHG                 BIT(3)
#define CHGINT1_BAT                 BIT(2)
#define CHGINT1_THM                 BIT(1)
#define CHG_STAT                    0x32
#define CHG_STAT_AICL_NOK			BIT(7)
#define CHG_STAT_DCI_NOK			BIT(6)
#define CHG_STAT_OVP_NOK			BIT(5)
#define CHG_STAT_DCUVP_NOK			BIT(4)
#define CHG_STAT_CHG_NOK			BIT(3)
#define CHG_STAT_BAT_NOK			BIT(2)
#define CHG_STAT_THM_NOK			BIT(1)
#define CHG_STAT_DC_V				BIT(0)
#define DC_BATT_DTLS                0x33
#define DC_BATT_DTLS_DC_AICL        BIT(7)
#define DC_BATT_DTLS_DC_I           BIT(6)
#define DC_BATT_DTLS_DC_OVP         BIT(5)
#define DC_BATT_DTLS_DC_UVP         BIT(4)
#define DC_BATT_DTLS_BAT_DTLS       BITS(3, 2)
#define DC_BATT_DTLS_BATDET_DTLS    BITS(1, 0)
#define CHG_DTLS                    0x34
#define CHG_DTLS_THM_DTLS           BITS(7, 5)
#define CHG_DTLS_TOPOFF_DTLS        BIT(4)
#define CHG_DTLS_CHG_DTLS           BITS(3, 0)
#define BAT2SYS_DTLS                0x35
#define BAT2SOC_CTL                 0x36
#define CHGCTL1                     0x37
#define CHGCTL1_SFO_DEBOUNCE_TMR    BITS(7, 6)
#define CHGCTL1_SFO_DEBOUNCE_EN     BIT(5)
#define CHGCTL1_THM_DIS             BIT(4)
#define CHGCTL1_JEITA_EN            BIT(3)
#define CHGCTL1_BUCK_EN             BIT(2)
#define CHGCTL1_CHGPROT             BITS(1, 0)
#define FCHGCRNT                    0x38
#define FCHGCRNT_FCHGTIME           BITS(7, 5)
#define FCHGCRNT_CHGCC              BITS(4, 0)
#define TOPOFF                      0x39
#define TOPOFF_TOPOFFTIME           BITS(7, 5)
#define TOPOFF_IFST2P8              BIT(4)
#define TOPOFF_ITOPOFF              BITS(2, 0)
#define BATREG                      0x3A
#define BATREG_REGTEMP              BITS(7, 6)
#define BATREG_CHGRSTRT             BIT(5)
#define BATREG_MBATREG              BITS(4, 1)
#define BATREG_VICHG_GAIN           BIT(0)
#define DCCRNT                      0x3B
#define DCCRNT_DCILMT               BITS(5, 0)
#define AICLCNTL                    0x3C
#define AICLCNTL_AICL_RESET         BIT(5)
#define AICLCNTL_AICL               BITS(4, 1)
#define AICLCNTL_DCMON_DIS          BIT(0)
#define RBOOST_CTL1                 0x3D
#define CHGCTL2                     0x3E
#define CHGCTL2_DCILIM_EN           BIT(7)
#define CHGCTL2_PREQCUR             BITS(6, 5)
#define CHGCTL2_CEN                 BIT(4)
#define CHGCTL2_QBATEN              BIT(3)
#define CHGCTL2_VSYSREG             BITS(2, 0)
#define BATDET                      0x3F
#define USBCHGCTL                   0x40
#define MBATREGMAX                  0x41
#define CHGCCMAX                    0x42
#define RBOOST_CTL2                 0x43
#define CHGINT2                     0x44
#define CHGINTMSK2                  0x45
#define CHGINT2_DC_V                BIT(7)
#define CHGINT2_CHG_WDT             BIT(4)
#define CHGINT2_CHG_WDT_WRN         BIT(0)
#define CHG_WDTC                    0x46
#define CHG_WDT_CTL                 0x47
#define CHG_WDT_DTLS                0x48
#define SAFEOUTCTL		            0x49

struct max77819_charger {
	struct mutex                           lock;
	struct max77819_dev                   *chip;
	struct max77819_io                    *io;
	struct device                         *dev;
	struct kobject                        *kobj;
	struct attribute_group                *attr_grp;
	struct max77819_charger_platform_data *pdata;
	int                                    irq;
	u8                                     irq1_saved;
	u8                                     irq2_saved;
	spinlock_t                             irq_lock;
	struct delayed_work                    irq_work;
	struct delayed_work                    log_work;
	/* BATT power supply */
	struct power_supply                    batt;
	/* AC   power supply */
	struct power_supply                    psy;
	struct power_supply                   *psy_this;
	/* USB  power supply */
	struct power_supply                   *psy_ext;
	/* Cooperating charger power supply */
	struct power_supply                   *psy_coop;
	bool                                   dev_enabled;
	bool                                   dev_initialized;
	int                                    current_limit_volatile;
	int                                    current_limit_permanent;
	int                                    charge_current_volatile;
	int                                    charge_current_permanent;
	int                                    present;
	int                                    ac_online;
	int                                    usb_online;
	int                                    health;
	int                                    status;
	int                                    charge_type;
	int                                    batt_vol;
	int                                    batt_soc;
	int                                    batt_tmp;
	int                                    batt_hth;
	int                                    batt_pre;
	int                                    curr_now;
	struct wake_lock                       chg_wake_lock;
	/* for runtime pm */
	atomic_t                               suspended;
};

#define __lock(_me)    mutex_lock(&(_me)->lock)
#define __unlock(_me)  mutex_unlock(&(_me)->lock)

enum {
	BATDET_DTLS_CONTACT_BREAK       = 0b00,
	BATDET_DTLS_BATTERY_DETECTED_01 = 0b01,
	BATDET_DTLS_BATTERY_DETECTED_10 = 0b10,
	BATDET_DTLS_BATTERY_REMOVED     = 0b11,
};

static char *max77819_charger_batdet_details[] = {
	[BATDET_DTLS_CONTACT_BREAK]       = "contact break",
	[BATDET_DTLS_BATTERY_DETECTED_01] = "battery detected (01)",
	[BATDET_DTLS_BATTERY_DETECTED_10] = "battery detected (10)",
	[BATDET_DTLS_BATTERY_REMOVED]     = "battery removed",
};

enum {
	DC_UVP_INVALID = 0,
	DC_UVP_VALID   = 1,
};

static char *max77819_charger_dcuvp_details[] = {
	[DC_UVP_INVALID] = "VDC is invalid; VDC < VDC_UVLO",
	[DC_UVP_VALID]   = "VDC is valid; VDC > VDC_UVLO",
};

enum {
	DC_OVP_VALID   = 0,
	DC_OVP_INVALID = 1,
};

static char *max77819_charger_dcovp_details[] = {
	[DC_OVP_VALID]   = "VDC is valid; VDC < VDC_OVLO",
	[DC_OVP_INVALID] = "VDC is invalid; VDC > VDC_OVLO",
};

enum {
	DC_I_VALID   = 0,
	DC_I_INVALID = 1,
};

static char *max77819_charger_dci_details[] = {
	[DC_I_VALID]   = "IDC is valid; IDC < DCILMT",
	[DC_I_INVALID] = "IDC is invalid; IDC > DCILMT",
};

enum {
	DC_AICL_OK  = 0,
	DC_AICL_NOK = 1,
};

static char *max77819_charger_aicl_details[] = {
	[DC_AICL_OK]  = "VDC > AICL threshold",
	[DC_AICL_NOK] = "VDC < AICL threshold",
};

enum {
	BAT_DTLS_UVP     = 0b00,
	BAT_DTLS_TIMEOUT = 0b01,
	BAT_DTLS_OK      = 0b10,
	BAT_DTLS_OVP     = 0b11,
};

extern struct pseudo_batt_info_type pseudo_batt_info;

static char *max77819_charger_bat_details[] = {
	[BAT_DTLS_UVP]     = "battery voltage < 2.1V",
	[BAT_DTLS_TIMEOUT] = "timer fault",
	[BAT_DTLS_OK]      = "battery okay",
	[BAT_DTLS_OVP]     = "battery overvoltage",
};

enum {
	CHG_DTLS_DEAD_BATTERY     = 0b0000,
	CHG_DTLS_PRECHARGE        = 0b0001,
	CHG_DTLS_FASTCHARGE_CC    = 0b0010,
	CHG_DTLS_FASTCHARGE_CV    = 0b0011,
	CHG_DTLS_TOPOFF           = 0b0100,
	CHG_DTLS_DONE             = 0b0101,
	CHG_DTLS_TIMER_FAULT      = 0b0110,
	CHG_DTLS_TEMP_SUSPEND     = 0b0111,
	CHG_DTLS_OFF              = 0b1000,
	CHG_DTLS_THM_LOOP         = 0b1001,
	CHG_DTLS_TEMP_SHUTDOWN    = 0b1010,
	CHG_DTLS_BUCK             = 0b1011,
	CHG_DTLS_OTG_OVER_CURRENT = 0b1100,
	CHG_DTLS_USB_SUSPEND      = 0b1101,
};

static char *max77819_charger_chg_details[] = {
	[CHG_DTLS_DEAD_BATTERY] =
		"charger is in dead-battery region",
	[CHG_DTLS_PRECHARGE] =
		"charger is in precharge mode",
	[CHG_DTLS_FASTCHARGE_CC] =
		"charger is in fast-charge constant current mode",
	[CHG_DTLS_FASTCHARGE_CV] =
		"charger is in fast-charge constant voltage mode",
	[CHG_DTLS_TOPOFF] =
		"charger is in top-off mode",
	[CHG_DTLS_DONE] =
		"charger is in done mode",
	[CHG_DTLS_TIMER_FAULT] =
		"charger is in timer fault mode",
	[CHG_DTLS_TEMP_SUSPEND] =
		"charger is in temperature suspend mode",
	[CHG_DTLS_OFF] =
		"buck off, charger off",
	[CHG_DTLS_THM_LOOP] =
		"charger is operating with its thermal loop active",
	[CHG_DTLS_TEMP_SHUTDOWN] =
		"charger is off and junction temperature is > TSHDN",
	[CHG_DTLS_BUCK] =
		"buck on, charger off",
	[CHG_DTLS_OTG_OVER_CURRENT] =
		"charger OTG curr limit is exceeded longer than debounce time",
	[CHG_DTLS_USB_SUSPEND] =
		"USB suspend",
};

enum {
	TOPOFF_NOT_REACHED = 0,
	TOPOFF_REACHED     = 1,
};

static char *max77819_charger_topoff_details[] = {
	[TOPOFF_NOT_REACHED] = "topoff is not reached",
	[TOPOFF_REACHED]     = "topoff is reached",
};

enum {
	THM_DTLS_LOW_TEMP_SUSPEND   = 0b001,
	THM_DTLS_LOW_TEMP_CHARGING  = 0b010,
	THM_DTLS_STD_TEMP_CHARGING  = 0b011,
	THM_DTLS_HIGH_TEMP_CHARGING = 0b100,
	THM_DTLS_HIGH_TEMP_SUSPEND  = 0b101,
};

static char *max77819_charger_thm_details[] = {
	[THM_DTLS_LOW_TEMP_SUSPEND]   = "cold; T < T1",
	[THM_DTLS_LOW_TEMP_CHARGING]  = "cool; T1 < T < T2",
	[THM_DTLS_STD_TEMP_CHARGING]  = "normal; T2 < T < T3",
	[THM_DTLS_HIGH_TEMP_CHARGING] = "warm; T3 < T < T4",
	[THM_DTLS_HIGH_TEMP_SUSPEND]  = "hot; T4 < T",
};

#define CHGINT1 CHGINT
#define max77819_charger_read_irq_status(_me, _irq_reg) \
	({	\
	 u8 __irq_current = 0;	\
	 int __rc = max77819_read((_me)->io, _irq_reg, &__irq_current);\
	 if (unlikely(IS_ERR_VALUE(__rc))) {	\
	 log_err(#_irq_reg" read error [%d]\n", __rc);	\
	 __irq_current = 0;	\
	 }	\
	 __irq_current;	\
	 })

enum {
	CFG_CHGPROT = 0,
	CFG_SFO_DEBOUNCE_TMR,
	CFG_SFO_DEBOUNCE_EN,
	CFG_THM_DIS,
	CFG_JEITA_EN,
	CFG_BUCK_EN,
	CFG_DCILIM_EN,
	CFG_PREQCUR,
	CFG_CEN,
	CFG_QBATEN,
	CFG_VSYSREG,
	CFG_DCILMT,
	CFG_FCHGTIME,
	CFG_CHGCC,
	CFG_AICL_RESET,
	CFG_AICL,
	CFG_DCMON_DIS,
	CFG_MBATREG,
	CFG_CHGRSTRT,
	CFG_TOPOFFTIME,
	CFG_ITOPOFF,
};

static struct max77819_bitdesc max77819_charger_cfg_bitdesc[] = {
#define CFG_BITDESC(_cfg_bit, _cfg_reg) \
	[CFG_##_cfg_bit] = MAX77819_BITDESC(_cfg_reg, _cfg_reg##_##_cfg_bit)
	CFG_BITDESC(CHGPROT,	CHGCTL1),
	CFG_BITDESC(SFO_DEBOUNCE_TMR,	CHGCTL1),
	CFG_BITDESC(SFO_DEBOUNCE_EN,	CHGCTL1),
	CFG_BITDESC(THM_DIS,	CHGCTL1),
	CFG_BITDESC(JEITA_EN,	CHGCTL1),
	CFG_BITDESC(BUCK_EN,	CHGCTL1),
	CFG_BITDESC(DCILIM_EN,	CHGCTL2),
	CFG_BITDESC(PREQCUR,	CHGCTL2),
	CFG_BITDESC(CEN,		CHGCTL2),
	CFG_BITDESC(QBATEN,		CHGCTL2),
	CFG_BITDESC(VSYSREG,	CHGCTL2),
	CFG_BITDESC(DCILMT,		DCCRNT),
	CFG_BITDESC(FCHGTIME,	FCHGCRNT),
	CFG_BITDESC(CHGCC,		FCHGCRNT),
	CFG_BITDESC(AICL_RESET,	AICLCNTL),
	CFG_BITDESC(AICL,		AICLCNTL),
	CFG_BITDESC(DCMON_DIS,	AICLCNTL),
	CFG_BITDESC(MBATREG,	BATREG),
	CFG_BITDESC(CHGRSTRT,	BATREG),
	CFG_BITDESC(TOPOFFTIME,	TOPOFF),
	CFG_BITDESC(ITOPOFF,	TOPOFF),
};
#define __cfg_bitdesc(_cfg) (&max77819_charger_cfg_bitdesc[CFG_##_cfg])
#define DEVICE_ATTRIB_SIZE      8

static ssize_t max77819_show_chgint(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHGINT, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_show_chgintm1(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHGINTM1, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_chgintm1(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, CHGINTM1, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_chg_stat(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHG_STAT, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_show_dc_batt_dtls(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, DC_BATT_DTLS, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_show_chg_dtls(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHG_DTLS, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_show_bat2sys_dtls(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, BAT2SYS_DTLS, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_show_bat2soc_ctl(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, BAT2SOC_CTL, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_bat2soc_ctl(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, BAT2SOC_CTL, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_chgctl1(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHGCTL1, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_chgctl1(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, CHGCTL1, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_fchgcrnt(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, FCHGCRNT, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_fchgcrnt(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, FCHGCRNT, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_topoff(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, TOPOFF, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_topoff(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, TOPOFF, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_batreg(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, BATREG, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_batreg(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, BATREG, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_dccrnt(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, DCCRNT, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_dccrnt(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, DCCRNT, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_aiclcntl(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, AICLCNTL, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_aiclcntl(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, AICLCNTL, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_rboost_ctl1(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, RBOOST_CTL1, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_rboost_ctl1(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, RBOOST_CTL1, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_chgctl2(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHGCTL2, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_chgctl2(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, CHGCTL2, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_batdet(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, BATDET, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_batdet(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, BATDET, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_usbchgctl(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, USBCHGCTL, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_usbchgctl(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, USBCHGCTL, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_mbatregmax(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, MBATREGMAX, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_mbatregmax(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, MBATREGMAX, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_chgccmax(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHGCCMAX, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_chgccmax(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, CHGCCMAX, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_rboost_ctl2(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, RBOOST_CTL2, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_rboost_ctl2(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, RBOOST_CTL2, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_chgint2(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHGINT2, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_show_chgintmsk2(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHGINTMSK2, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_chgintmsk2(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, CHGINTMSK2, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_chg_wdtc(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHG_WDTC, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_chg_wdtc(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, CHG_WDTC, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_chg_wdt_ctl(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHG_WDT_CTL, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_chg_wdt_ctl(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, CHG_WDT_CTL, data);
	if (ret)
		return ret;

	return len;
}

static ssize_t max77819_show_chg_wdt_dtls(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, CHG_WDT_DTLS, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}


static ssize_t max77819_show_safeoutctl(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;

	max77819_read(me->io, SAFEOUTCTL, &data);

	return snprintf(buf, DEVICE_ATTRIB_SIZE, "0x%2X\n", data);
}

static ssize_t max77819_store_safeoutctl(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct max77819_charger *me = dev_get_drvdata(dev);
	u8 data;
	int ret;

	if (kstrtou8(buf, 0, &data))
		return -EINVAL;

	ret = max77819_write(me->io, SAFEOUTCTL, data);
	if (ret)
		return ret;

	return len;
}

#define MAX77819_CHARGER_ATTR_RW(_name) \
	DEVICE_ATTR(_name, S_IRUGO | S_IWUSR, max77819_show_##_name, \
	max77819_store_##_name)

#define MAX77819_CHARGER_ATTR_RO(_name) \
	DEVICE_ATTR(_name, S_IRUGO | S_IWUSR, max77819_show_##_name, NULL)

static MAX77819_CHARGER_ATTR_RO(chgint);
static MAX77819_CHARGER_ATTR_RW(chgintm1);
static MAX77819_CHARGER_ATTR_RO(chg_stat);
static MAX77819_CHARGER_ATTR_RO(dc_batt_dtls);
static MAX77819_CHARGER_ATTR_RO(chg_dtls);
static MAX77819_CHARGER_ATTR_RO(bat2sys_dtls);
static MAX77819_CHARGER_ATTR_RW(bat2soc_ctl);
static MAX77819_CHARGER_ATTR_RW(chgctl1);
static MAX77819_CHARGER_ATTR_RW(fchgcrnt);
static MAX77819_CHARGER_ATTR_RW(topoff);
static MAX77819_CHARGER_ATTR_RW(batreg);
static MAX77819_CHARGER_ATTR_RW(dccrnt);
static MAX77819_CHARGER_ATTR_RW(aiclcntl);
static MAX77819_CHARGER_ATTR_RW(rboost_ctl1);
static MAX77819_CHARGER_ATTR_RW(chgctl2);
static MAX77819_CHARGER_ATTR_RW(batdet);
static MAX77819_CHARGER_ATTR_RW(usbchgctl);
static MAX77819_CHARGER_ATTR_RW(mbatregmax);
static MAX77819_CHARGER_ATTR_RW(chgccmax);
static MAX77819_CHARGER_ATTR_RW(rboost_ctl2);
static MAX77819_CHARGER_ATTR_RO(chgint2);
static MAX77819_CHARGER_ATTR_RW(chgintmsk2);
static MAX77819_CHARGER_ATTR_RW(chg_wdtc);
static MAX77819_CHARGER_ATTR_RW(chg_wdt_ctl);
static MAX77819_CHARGER_ATTR_RO(chg_wdt_dtls);
static MAX77819_CHARGER_ATTR_RW(safeoutctl);

static struct attribute *max77819_charger_attributes[] = {
	&dev_attr_chgint.attr,
	&dev_attr_chgintm1.attr,
	&dev_attr_chg_stat.attr,
	&dev_attr_dc_batt_dtls.attr,
	&dev_attr_chg_dtls.attr,
	&dev_attr_bat2sys_dtls.attr,
	&dev_attr_bat2soc_ctl.attr,
	&dev_attr_chgctl1.attr,
	&dev_attr_fchgcrnt.attr,
	&dev_attr_topoff.attr,
	&dev_attr_batreg.attr,
	&dev_attr_dccrnt.attr,
	&dev_attr_aiclcntl.attr,
	&dev_attr_rboost_ctl1.attr,
	&dev_attr_chgctl2.attr,
	&dev_attr_batdet.attr,
	&dev_attr_usbchgctl.attr,
	&dev_attr_mbatregmax.attr,
	&dev_attr_chgccmax.attr,
	&dev_attr_rboost_ctl2.attr,
	&dev_attr_chgint2.attr,
	&dev_attr_chgintmsk2.attr,
	&dev_attr_chg_wdtc.attr,
	&dev_attr_chg_wdt_ctl.attr,
	&dev_attr_chg_wdt_dtls.attr,
	&dev_attr_safeoutctl.attr,
	NULL,
};

static struct attribute_group max77819_charger_attribute_group = {
	.attrs	= max77819_charger_attributes,
};

#define PROTCMD_UNLOCK  3
#define PROTCMD_LOCK    0
/* Unlock Charger Property */
static __always_inline int max77819_charger_unlock(struct max77819_charger *me)
{
	int rc;

	rc = max77819_write_bitdesc(me->io, __cfg_bitdesc(CHGPROT),
			PROTCMD_UNLOCK);
	if (unlikely(IS_ERR_VALUE(rc))) {
		log_err("failed to unlock [%d]\n", rc);
		goto out;
	}

#if VERIFICATION_UNLOCK
	do {
		u8 chgprot = 0;

		rc = max77819_read_bitdesc(me->io, __cfg_bitdesc(CHGPROT),
				&chgprot);
		if (unlikely(IS_ERR_VALUE(rc) || chgprot != PROTCMD_UNLOCK)) {
			log_err("access denied - CHGPROT %X [%d]\n",
					chgprot, rc);
			rc = -EACCES;
			goto out;
		}
	} while (0);
#endif /* VERIFICATION_UNLOCK */

out:
	return rc;
}

/* Lock Charger property */
static __always_inline int max77819_charger_lock(struct max77819_charger *me)
{
	int rc;

	rc = max77819_write_bitdesc(me->io,
			__cfg_bitdesc(CHGPROT), PROTCMD_LOCK);
	if (unlikely(IS_ERR_VALUE(rc)))
		log_err("failed to lock [%d]\n", rc);

	return rc;
}

#define max77819_charger_read_config(_me, _cfg, _val_ptr) \
	({	\
	 int __rc = max77819_read_bitdesc((_me)->io, __cfg_bitdesc(_cfg), \
		 _val_ptr); \
	 if (unlikely(IS_ERR_VALUE(__rc))) \
	 log_err("read config "#_cfg" error [%d]\n", __rc); \
	 else \
	 log_vdbg("read config "#_cfg": %Xh\n", *(_val_ptr)); \
	 __rc; \
	 })
#define max77819_charger_write_config(_me, _cfg, _val) \
	({ \
	 int __rc = max77819_charger_unlock(_me); \
	 if (likely(!IS_ERR_VALUE(__rc))) { \
	 __rc = max77819_write_bitdesc((_me)->io, __cfg_bitdesc(_cfg),\
		 _val); \
	 if (unlikely(IS_ERR_VALUE(__rc))) \
	 log_err("write config "#_cfg" error [%d]\n", __rc); \
	 else \
	 log_vdbg("write config "#_cfg": %Xh\n", _val); \
	 max77819_charger_lock(_me); \
	 } \
	 __rc; \
	 })

/* Set DC input current limit */
static inline int max77819_charger_get_dcilmt(struct max77819_charger *me,
		int *ua)
{
	int rc;
	u8 dcilmt = 0;

	rc = max77819_charger_read_config(me, DCILMT, &dcilmt);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	if (unlikely(dcilmt >= 0x3F)) {
		*ua = MAX77819_CHARGER_CURRENT_UNLIMIT;
		log_vdbg("<get_dcilmt> no limit\n");
		goto out;
	}
	*ua = dcilmt < 0x03 ? 100000 :
		dcilmt < 0x35 ? (int)(dcilmt - 0x03) * 25000 +  275000 :
		dcilmt == 0x35 ? 1709000 :
		dcilmt < 0x39 ? (int)(dcilmt - 0x36) * 42000 + 1750000 :
		dcilmt == 0x39 ? 1875000 :
		dcilmt < 0x3C ? (int)(dcilmt - 0x39) * 42000 + 1875000 :
		dcilmt == 0x3C ? 2000000 :
		(int)(dcilmt - 0x3C) * 42000 + 2000000;

	log_vdbg("<get_dcilmt> %Xh -> %dua\n", dcilmt, *ua);

out:
	return rc;
}

/* Set DC input current limit */
static int max77819_charger_set_dcilmt(struct max77819_charger *me, int ua)
{
	u8 dcilmt;

	if (unlikely(ua == MAX77819_CHARGER_CURRENT_UNLIMIT)) {
		dcilmt = 0x3F;
		log_vdbg("<set_dcilmt> no limit\n");
		goto out;
	}

	dcilmt = ua <  275000 ? 0x00 :
		ua < 1500000 ? (ua -  275000)/25000 + 0x03 :
		ua < 1709000 ? 0x34 :
		ua < 1750000 ? 0x35 :
		ua < 1792000 ? 0x36 :
		ua < 1834000 ? 0x37 :
		ua < 1875000 ? 0x38 :
		ua < 1917000 ? 0x39 :
		ua < 1959000 ? 0x3A :
		ua < 2000000 ? 0x3B :
		ua < 2042000 ? 0x3C :
		ua < 2084000 ? 0x3D : 0x3E;

	log_vdbg("<set_dcilmt> %dua -> %Xh\n", ua, dcilmt);

out:
	return max77819_charger_write_config(me, DCILMT, dcilmt);
}
/* Get Charging enable, disable */
static inline int max77819_charger_get_enable(struct max77819_charger *me,
		int *en)
{
	int rc;
	u8 cen = 0;

	rc = max77819_charger_read_config(me, CEN, &cen);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	*en = !!cen;
	log_dbg("<get_enable> %s\n", en ? "enabled" : "disabled");

out:
	return rc;
}

/* Set Charging enable, disable */
static int max77819_charger_set_enable(struct max77819_charger *me, int en)
{
	log_err("<set_enable> %s\n", en ? "enabling" : "disabling");
	return max77819_charger_write_config(me, CEN, !!en);
}

static inline int max77819_charger_get_chgcc(struct max77819_charger *me,
		int *ua)
{
	int rc;
	u8 chgcc = 0;

	rc = max77819_charger_read_config(me, CHGCC, &chgcc);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	*ua = chgcc < 0x01 ? 0 :
		chgcc < 0x1C ? (int)(chgcc - 0x01) * 50000 +  250000 :
		chgcc == 0x1C ? 1800000 :
		chgcc == 0x1D ? 1867000 :
		chgcc == 0x1E ? 1933000 : 2000000;

	log_vdbg("<get_chgcc> %Xh -> %dua\n", chgcc, *ua);
out:
	return rc;
}

/* Get Fast-charging current limit */
static int max77819_charger_set_chgcc(struct max77819_charger *me, int ua)
{
	u8 chgcc;

	chgcc = ua <  250000 ? 0x00 :
		ua < 1550000 ? (ua -  250000)/50000 + 0x01 :
		ua < 1800000 ? 0x1B :
		ua < 1867000 ? 0x1C :
		ua < 1933000 ? 0x1D :
		ua < 2000000 ? 0x1E : 0x1F;

	log_vdbg("<set_chgcc> %dua -> %Xh\n", ua, chgcc);

	return max77819_charger_write_config(me, CHGCC, chgcc);
}

#define DCILMT_MIN        100000
#define DCILMT_MAX        1875000
#define CHGCC_MIN         0
#define CHGCC_MAX         1800000
/* Set Fast-charging current limit */
static int max77819_charger_set_charge_current(struct max77819_charger *me,
		int limit_ua, int charge_ua)
{
	int rc;
	/*max77819_charger_set_chgcc(me, CHGCC_MIN);*/

	if (limit_ua == MAX77819_CHARGER_CURRENT_MAXIMUM)
		limit_ua = DCILMT_MAX;
	else if (limit_ua == MAX77819_CHARGER_CURRENT_MINIMUM)
		limit_ua = DCILMT_MIN;
	else if (limit_ua != MAX77819_CHARGER_CURRENT_UNLIMIT)
		limit_ua  = max(DCILMT_MIN, limit_ua);
	else
		;

	if (charge_ua == MAX77819_CHARGER_CURRENT_UNLIMIT ||
			charge_ua == MAX77819_CHARGER_CURRENT_MAXIMUM)
		charge_ua = CHGCC_MAX;
	else if (limit_ua == MAX77819_CHARGER_CURRENT_MINIMUM)
		charge_ua = CHGCC_MIN;
	else
		charge_ua = max(CHGCC_MIN, charge_ua);

	if (likely(limit_ua == MAX77819_CHARGER_CURRENT_UNLIMIT ||
				limit_ua >= charge_ua)) {
		rc = max77819_charger_set_dcilmt(me, limit_ua);
		if (unlikely(IS_ERR_VALUE(rc)))
			goto out;
		if (likely(me->dev_enabled))
			rc = max77819_charger_set_chgcc(me, charge_ua);

		goto out;
	}

	if (likely(me->dev_enabled)) {
		log_dbg("setting current %dua but limited up to %dua\n",
				charge_ua, limit_ua);

		if (likely(limit_ua >= CHGCC_MIN)) {
			rc = max77819_charger_set_chgcc(me, limit_ua);
		} else {
			log_err("disabling charger ; charging current < %ua\n",
					CHGCC_MIN);
			rc = max77819_charger_set_enable(me, false);
		}

		if (unlikely(IS_ERR_VALUE(rc)))
			goto out;
	}

	rc = max77819_charger_set_dcilmt(me, limit_ua);

out:
	return rc;
}

/* Get Charger Present */
static bool max77819_charger_present_input(struct max77819_charger *me)
{
	u8 dc_uvp = 0;
	int rc;

	rc = max77819_read_reg_bit(me->io, DC_BATT_DTLS, DC_UVP, &dc_uvp);
	if (unlikely(IS_ERR_VALUE(rc)))
		return false;

	return (dc_uvp == DC_UVP_VALID);
}

/* Cable Unplugged registor set */
static int max77819_charger_exit_dev(struct max77819_charger *me)
{
	struct max77819_charger_platform_data *pdata = me->pdata;

	/* input current limit set, fast charging current - 0 */
	max77819_charger_set_charge_current(me, me->current_limit_permanent, 0);

	/* charging disable */
	max77819_charger_set_enable(me, false);

	/* volatile data reset to permanent data
	 * - input current limit
	 * - fast-charging current limit
	 */
	me->current_limit_volatile  = me->current_limit_permanent;
	me->charge_current_volatile = me->charge_current_permanent;

	me->dev_enabled     = (!pdata->enable_coop || pdata->coop_psy_name);
	me->dev_initialized = false;
	return 0;
}

/* Cable Plugged registor set */
static int max77819_charger_init_dev(struct max77819_charger *me)
{
	struct max77819_charger_platform_data *pdata = me->pdata;
	unsigned long irq_flags;
	int rc;
	u8 irq1_current, irq2_current, val;

	/* Enable IRQ_1 */
	val  = 0;
	val |= CHGINT1_AICLOTG;
	val |= CHGINT1_TOPOFF;
	/*  val |= CHGINT1_OVP; */
	val |= CHGINT1_DC_UVP;
	val |= CHGINT1_CHG;
	/*  val |= CHGINT1_BAT; */
	/*  val |= CHGINT1_THM; */

	rc = max77819_write(me->io, CHGINTM1, ~val);
	if (unlikely(IS_ERR_VALUE(rc))) {
		log_err("CHGINTM1 write error [%d]\n", rc);
		goto out;
	}

	/* Enable IRQ_2 */
	val  = 0;
	/*  val |= CHGINT2_DC_V;*/
	/*  val |= CHGINT2_CHG_WDT;*/
	/*  val |= CHGINT2_CHG_WDT_WRN;*/

	rc = max77819_write(me->io, CHGINTMSK2, ~val);
	if (unlikely(IS_ERR_VALUE(rc))) {
		log_err("CHGINTMSK2 write error [%d]\n", rc);
		goto out;
	}

	/* Store current irq status */
	irq1_current = max77819_charger_read_irq_status(me, CHGINT1);
	irq2_current = max77819_charger_read_irq_status(me, CHGINT2);

	spin_lock_irqsave(&me->irq_lock, irq_flags);
	me->irq1_saved |= irq1_current;
	me->irq2_saved |= irq2_current;
	spin_unlock_irqrestore(&me->irq_lock, irq_flags);

	log_dbg("CHGINT1 CURR %02Xh SAVED %02Xh\n",
			irq1_current, me->irq1_saved);
	log_dbg("CHGINT2 CURR %02Xh SAVED %02Xh\n",
			irq2_current, me->irq2_saved);

	/* charge current */
	rc = max77819_charger_set_charge_current(me, me->current_limit_volatile,
			me->charge_current_volatile);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;
	/* JEITA DISABLE */
	rc = max77819_charger_write_config(me , JEITA_EN , true);

	/* topoff timer */
	val = pdata->topoff_timer <=  0 ? 0x00 :
		pdata->topoff_timer <= 60 ?
		(int)DIV_ROUND_UP(pdata->topoff_timer, 10) : 0x07;
	rc = max77819_charger_write_config(me, TOPOFFTIME, val);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	/* topoff current */
	val = pdata->topoff_current <  50000 ? 0x00 :
		pdata->topoff_current < 400000 ?
		(int)DIV_ROUND_UP(pdata->topoff_current - 50000, 50000) : 0x07;
	rc = max77819_charger_write_config(me, ITOPOFF, val);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	/* charge restart threshold */
	val = (pdata->charge_restart_threshold > 150000);
	rc = max77819_charger_write_config(me, CHGRSTRT, val);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	/* charge termination voltage */
	val = pdata->charge_termination_voltage < 3700000 ? 0x00 :
		(int)DIV_ROUND_UP(pdata->charge_termination_voltage - 3700000,
				50000) + 0x01;
	rc = max77819_charger_write_config(me, MBATREG, val);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	/* thermistor control */
	val = (pdata->enable_thermistor == false);
	rc = max77819_charger_write_config(me, THM_DIS, val);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	/* AICL control */
	val = (pdata->enable_aicl == false);
	rc = max77819_charger_write_config(me, DCMON_DIS, val);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	if (likely(pdata->enable_aicl)) {
		int uv;

		/* AICL detection voltage selection */

		uv = pdata->aicl_detection_voltage;
		val = uv < 3900000 ? 0x00 : uv < 4800000 ?
			(int)DIV_ROUND_UP(uv - 3900000, 100000) : 0x09;
		log_dbg("AICL detection voltage %uv (%Xh)\n", uv, val);

		rc = max77819_charger_write_config(me, AICL, val);
		if (unlikely(IS_ERR_VALUE(rc)))
			goto out;

		/* AICL reset threshold */

		uv = (int)pdata->aicl_reset_threshold;
		val = (uv > 100000);
		log_dbg("AICL reset threshold %uv (%Xh)\n", uv, val);

		rc = max77819_charger_write_config(me, AICL_RESET, val);
		if (unlikely(IS_ERR_VALUE(rc)))
			goto out;
	}

	/* DCILMT enable */
	rc = max77819_charger_write_config(me, DCILIM_EN, true);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	/* charging enable */
	rc = max77819_charger_set_enable(me, me->dev_enabled);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	me->dev_initialized = true;
	log_dbg("device initialized\n");

out:
	return rc;
}

#define max77819_charger_psy_setprop(_me, _psy, _psp, _val) \
	({ \
	 struct power_supply *__psy = _me->_psy; \
	 union power_supply_propval __propval = { .intval = _val }; \
	 int __rc = -ENXIO; \
	 if (likely(__psy && __psy->set_property)) { \
	 __rc = __psy->set_property(__psy, POWER_SUPPLY_PROP_##_psp,\
		 &__propval); \
	 } \
	 __rc; \
	 })

/* Charger wake lock */
static void max77819_charger_wake_lock(struct max77819_charger *me, bool enable)
{
	/* TODO: EOC unlock chg wake lock needed */
	if (!me)
		log_dbg("called before init\n");

	if (enable) {
		if (!wake_lock_active(&me->chg_wake_lock)) {
			log_err("charging wake lock enable\n");
			wake_lock(&me->chg_wake_lock);
		}
	} else {
		if (wake_lock_active(&me->chg_wake_lock)) {
			log_err("charging wake lock disable\n");
			wake_unlock(&me->chg_wake_lock);
		}
	}
}

static void max77819_charger_psy_init(struct max77819_charger *me)
{
	if (unlikely(!me->psy_this))
		me->psy_this = &me->psy;

	if (unlikely(!me->psy_ext && me->pdata->ext_psy_name)) {
		me->psy_ext = power_supply_get_by_name(me->pdata->ext_psy_name);
		if (likely(me->psy_ext)) {
			log_dbg("psy %s found\n", me->pdata->ext_psy_name);
			max77819_charger_psy_setprop(me, psy_ext, PRESENT,
					false);
		}
	}

	/* Get cooperate power supply */
	if (unlikely(!me->psy_coop && me->pdata->coop_psy_name)) {
		me->psy_coop =
			power_supply_get_by_name(me->pdata->coop_psy_name);
		if (likely(me->psy_coop))
			log_dbg("psy %s found\n", me->pdata->coop_psy_name);
	}
}

static void max77819_charger_psy_changed(struct max77819_charger *me)
{
	max77819_charger_psy_init(me);

	if (likely(me->psy_this))
		power_supply_changed(me->psy_this);

	if (likely(me->psy_ext))
		power_supply_changed(me->psy_ext);

	if (likely(me->psy_coop))
		power_supply_changed(me->psy_coop);
}

struct max77819_charger_status_map {
	int health, status, charge_type;
};

static struct max77819_charger_status_map max77819_charger_status_map[] = {
#define STATUS_MAP(_chg_dtls, _health, _status, _charge_type) \
	[CHG_DTLS_##_chg_dtls] = {\
		.health = POWER_SUPPLY_HEALTH_##_health,\
		.status = POWER_SUPPLY_STATUS_##_status,\
		.charge_type = POWER_SUPPLY_CHARGE_TYPE_##_charge_type,\
	}
	/* health               status        charge_type */
	STATUS_MAP(DEAD_BATTERY,     DEAD,           NOT_CHARGING, NONE),
	STATUS_MAP(PRECHARGE,        UNKNOWN,        CHARGING,     TRICKLE),
	STATUS_MAP(FASTCHARGE_CC,    UNKNOWN,        CHARGING,     FAST),
	STATUS_MAP(FASTCHARGE_CV,    UNKNOWN,        CHARGING,     FAST),
	STATUS_MAP(TOPOFF,           UNKNOWN,        CHARGING,     FAST),
	STATUS_MAP(DONE,             UNKNOWN,        FULL,         NONE),
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	STATUS_MAP(TIMER_FAULT,      SAFETY_TIMER_EXPIRE, NOT_CHARGING, NONE),
#else /* LINUX_VERSION_CODE ... */
	STATUS_MAP(TIMER_FAULT,      UNKNOWN,        NOT_CHARGING, NONE),
#endif /* LINUX_VERSION_CODE ... */
	STATUS_MAP(TEMP_SUSPEND,     UNKNOWN,        NOT_CHARGING, NONE),
	STATUS_MAP(OFF,              UNKNOWN,        NOT_CHARGING, NONE),
	STATUS_MAP(THM_LOOP,         UNKNOWN,        CHARGING,     NONE),
	STATUS_MAP(TEMP_SHUTDOWN,    OVERHEAT,       NOT_CHARGING, NONE),
	STATUS_MAP(BUCK,             UNKNOWN,        NOT_CHARGING, UNKNOWN),
	STATUS_MAP(OTG_OVER_CURRENT, UNKNOWN,        NOT_CHARGING, UNKNOWN),
	STATUS_MAP(USB_SUSPEND,      UNKNOWN,        NOT_CHARGING, NONE),
};

static int max77819_charger_update(struct max77819_charger *me)
{
	int rc;
	u8 dc_batt_dtls, chg_dtls;
	u8 batdet, bat, dcuvp, dcovp, dci, aicl;
	u8 chg, topoff, thm;

	me->health      = POWER_SUPPLY_HEALTH_UNKNOWN;
	me->status      = POWER_SUPPLY_STATUS_UNKNOWN;
	me->charge_type = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;

#if defined(CONFIG_LGE_PM_BATTERY_MAX17048_FUELGAUGE)
	me->batt_vol = max17048_get_voltage();
	me->batt_soc = max17048_get_capacity();
	/* TODO: get battery temperature by PMIC ADC */
	me->batt_tmp = 24 * 10;
	/*                                                   */
	me->batt_hth = POWER_SUPPLY_HEALTH_GOOD;
	/* TODO: get battery current now by PMIC VBAT RSENSE */
	me->curr_now = 200 * 100;
#else /* if have no infomation, capacity value is fixed. */
	/* TODO: get current voltage by fuel gauge IF */
	me->batt_vol = 4000 * 1000;
	/* TODO: get current capacity by fuel gauge IF */
	me->batt_soc = 80;
	/* TODO: get battery temperature by PMIC ADC */
	me->batt_tmp = 24 * 10;
	/* TODO: get battery health by vendor charging scenario */
	me->batt_hth = POWER_SUPPLY_HEALTH_GOOD;
	/* TODO: get battery current now by PMIC VBAT RSENSE */
	me->curr_now = 200 * 100;
#endif /*                                          */

	rc = max77819_read(me->io, DC_BATT_DTLS, &dc_batt_dtls);
	if (unlikely(IS_ERR_VALUE(rc))) {
		log_err("DC_BATT_DTLS read error [%d]\n", rc);
		goto out;
	}

	rc = max77819_read(me->io, CHG_DTLS, &chg_dtls);
	if (unlikely(IS_ERR_VALUE(rc))) {
		log_err("CHG_DTLS read error [%d]\n", rc);
		goto out;
	}

	log_vdbg("DC_BATT_DTLS %Xh CHG_DTLS %Xh\n", dc_batt_dtls, chg_dtls);

	batdet = BITS_GET(dc_batt_dtls, DC_BATT_DTLS_BATDET_DTLS);
	log_vdbg("*** BATDET %s\n", max77819_charger_batdet_details[batdet]);

	bat = BITS_GET(dc_batt_dtls, DC_BATT_DTLS_BAT_DTLS);
	log_vdbg("*** BAT    %s\n", max77819_charger_bat_details[bat]);

	dcuvp = BITS_GET(dc_batt_dtls, DC_BATT_DTLS_DC_UVP);
	log_vdbg("*** DC_UVP %s\n", max77819_charger_dcuvp_details[dcuvp]);

	dcovp = BITS_GET(dc_batt_dtls, DC_BATT_DTLS_DC_OVP);
	log_vdbg("*** DC_OVP %s\n", max77819_charger_dcovp_details[dcovp]);

	dci = BITS_GET(dc_batt_dtls, DC_BATT_DTLS_DC_I);
	log_vdbg("*** DC_I   %s\n", max77819_charger_dci_details[dci]);

	aicl = BITS_GET(dc_batt_dtls, DC_BATT_DTLS_DC_AICL);
	log_vdbg("*** AICL   %s\n", max77819_charger_aicl_details[aicl]);

	chg = BITS_GET(chg_dtls, CHG_DTLS_CHG_DTLS);
	log_vdbg("*** CHG    %s\n", max77819_charger_chg_details[chg]);

	topoff = BITS_GET(chg_dtls, CHG_DTLS_TOPOFF_DTLS);
	log_vdbg("*** TOPOFF %s\n", max77819_charger_topoff_details[topoff]);

	thm = BITS_GET(chg_dtls, CHG_DTLS_THM_DTLS);
	log_vdbg("*** THM    %s\n", max77819_charger_thm_details[thm]);

	me->present = (dcuvp == DC_UVP_VALID);

	/* charger not present */
	if (unlikely(!me->present)) {
		me->health      = POWER_SUPPLY_HEALTH_UNKNOWN;
		me->status      = POWER_SUPPLY_STATUS_DISCHARGING;
		me->charge_type = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
		goto out;
	}

	/* charger present, !vaild */
	if (unlikely(dcovp != DC_OVP_VALID)) {
		me->health      = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		me->status      = POWER_SUPPLY_STATUS_NOT_CHARGING;
		me->charge_type = POWER_SUPPLY_CHARGE_TYPE_NONE;
		goto out;
	}

	me->health      = max77819_charger_status_map[chg].health;
	me->status      = max77819_charger_status_map[chg].status;
	me->charge_type = max77819_charger_status_map[chg].charge_type;

	if (likely(me->health != POWER_SUPPLY_HEALTH_UNKNOWN))
		goto out;

	/* override health by THM_DTLS */
	switch (thm) {
		case THM_DTLS_LOW_TEMP_SUSPEND:
			me->health = POWER_SUPPLY_HEALTH_COLD;
			break;
		case THM_DTLS_HIGH_TEMP_SUSPEND:
			me->health = POWER_SUPPLY_HEALTH_OVERHEAT;
			break;
		default:
			me->health = POWER_SUPPLY_HEALTH_GOOD;
			break;
	}

out:
	log_vdbg("PRESENT %d HEALTH %d STATUS %d CHARGE_TYPE %d\n",
			me->present, me->health, me->status, me->charge_type);
	return rc;
}

#define max77819_charger_resume_log_work(_me) \
	do { \
		if (likely(log_worker)) { \
			if (likely(!delayed_work_pending(&(_me)->log_work))) {\
				schedule_delayed_work(&(_me)->log_work, \
						LOG_WORK_INTERVAL); \
			} \
		} \
	} while (0)

#define max77819_charger_suspend_log_work(_me) \
	cancel_delayed_work_sync(&(_me)->log_work)

static void max77819_charger_log_work(struct work_struct *work)
{
	struct max77819_charger *me =
		container_of(work, struct max77819_charger, log_work.work);
#if defined(CONFIG_LGE_PM)
	char on[] = "O";
	char off[] = "X";
	u8 regval;

	max77819_read(me->io, CHG_STAT, &regval);
	log_info("CHG_STAT=0x%2x, DC_V:%s, CHG_NOK:%s, AICL_NOK:%s\n",
		(int)regval,
		regval & CHG_STAT_DC_V ? on : off,
		regval & CHG_STAT_CHG_NOK ? on : off,
		/* regval & CHG_STAT_DC_UVP_NOK ? on: off,*/
		regval & CHG_STAT_AICL_NOK ? on : off
		);
	max77819_read(me->io, DC_BATT_DTLS, &regval);
	log_info("DC_BATT_DTLS=0x%2x, DC_AICL:%s, DC_I:%s, DC_OVP:%s, DC_UVP:%s, BAT_DTLS:%d, BATDET_DTLS:%d\n",
		(int)regval,
		regval & DC_BATT_DTLS_DC_AICL ? on : off,
		regval & DC_BATT_DTLS_DC_I ? on : off,
		regval & DC_BATT_DTLS_DC_OVP ? on : off,
		regval & DC_BATT_DTLS_DC_UVP ? on : off,
		(int)BITS_GET(regval, DC_BATT_DTLS_BAT_DTLS),
		(int)BITS_GET(regval, DC_BATT_DTLS_BATDET_DTLS)
		);
	max77819_read(me->io, CHG_DTLS, &regval);
	log_info("CHG_DTLS=0x%2x, THM_DTLS:%d, TOPOFF_DTLS:%s, CHG_DTLS:%d\n",
		(int)regval,
		(int)BITS_GET(regval, CHG_DTLS_THM_DTLS),
		regval & CHG_DTLS_TOPOFF_DTLS ? on : off,
		(int)BITS_GET(regval, CHG_DTLS_CHG_DTLS)
		);
	max77819_read(me->io, CHGCTL1, &regval);
	log_info("CHGCTL1=0x%2x, DEBOUNCE_TMR:%d, DEBOUNCE_EN:%s, THM_DIS:%s, JEITA_EN:%s, BUCK_EN:%s, CHGPROT:%s\n",
		(int)regval,
		(int)BITS_GET(regval, CHGCTL1_SFO_DEBOUNCE_TMR),
		regval & CHGCTL1_SFO_DEBOUNCE_EN ? on : off,
		regval & CHGCTL1_THM_DIS ? on : off,
		regval & CHGCTL1_JEITA_EN ? on : off,
		regval & CHGCTL1_BUCK_EN ? on : off,
		regval & CHGCTL1_CHGPROT ? on : off
		);
	max77819_read(me->io, FCHGCRNT, &regval);
	log_info("FCHGCRNT=0x%2x,  CHGCC:%d\n",
		(int)regval,
		BITS_GET(regval, FCHGCRNT_CHGCC) < 0x01 ? 0 :
		BITS_GET(regval, FCHGCRNT_CHGCC) < 0x1C ?
		(int)(BITS_GET(regval, FCHGCRNT_CHGCC) - 0x01) * 50000 +  250000 :
		BITS_GET(regval, FCHGCRNT_CHGCC) == 0x1C ? 1800000 :
		BITS_GET(regval, FCHGCRNT_CHGCC) == 0x1D ? 1867000 :
		BITS_GET(regval, FCHGCRNT_CHGCC) == 0x1E ? 1933000 : 2000000
		);
	max77819_read(me->io, TOPOFF, &regval);
	log_info("TOPOFF=0x%2x, TOPOFFTIME:%d, IFST2P8:%s, ITOPOFF:%d\n",
		(int)regval,
		(int)BITS_GET(regval, TOPOFF_TOPOFFTIME),
		regval & TOPOFF_IFST2P8 ? on : off,
		(int)BITS_GET(regval, TOPOFF_ITOPOFF)
		);
	max77819_read(me->io, BATREG, &regval);
	log_info("BATREG=0x%2x, REGTEMP:%d, CHGRSTRT:%s, MBATREG:%d\n",
		(int)regval,
		(int)BITS_GET(regval, BATREG_REGTEMP),
		regval & BATREG_CHGRSTRT ? on : off,
		(int)BITS_GET(regval, BATREG_MBATREG)
		);
	max77819_read(me->io, DCCRNT, &regval);
	log_info("DCCRNT=0x%2x, DCILMT:%d\n",
		(int)regval,
		BITS_GET(regval, DCCRNT_DCILMT) < 0x03 ? 100000 :
		BITS_GET(regval, DCCRNT_DCILMT) < 0x35 ?
		(int)(BITS_GET(regval, DCCRNT_DCILMT) - 0x03) * 25000 +  275000 :
		BITS_GET(regval, DCCRNT_DCILMT) == 0x35 ? 1709000 :
		BITS_GET(regval, DCCRNT_DCILMT) < 0x39 ?
		(int)(BITS_GET(regval, DCCRNT_DCILMT) - 0x36) * 42000 + 1750000 :
		BITS_GET(regval, DCCRNT_DCILMT) == 0x39 ? 1875000 :
		BITS_GET(regval, DCCRNT_DCILMT) < 0x3C ?
		(int)(BITS_GET(regval, DCCRNT_DCILMT) - 0x39) * 42000 + 1875000 :
		BITS_GET(regval, DCCRNT_DCILMT) == 0x3C ? 2000000 :
		(int)(BITS_GET(regval, DCCRNT_DCILMT) - 0x3C) * 42000 + 2000000
		);
	max77819_read(me->io, AICLCNTL, &regval);
	log_info("AICLCNTL=0x%2x, AICL_RESET:%s, AICL:%d, DCMON_DIS:%s\n",
		(int)regval,
		regval & AICLCNTL_AICL_RESET ? on : off,
		(int)BITS_GET(regval, AICLCNTL_AICL),
		regval & AICLCNTL_DCMON_DIS ? on : off
		);
	max77819_read(me->io, CHGCTL2, &regval);
	log_info("CHGCTL2=0x%2x, DCILIM_EN:%s, PREQCUR:%d, CEN:%s, QBATEN:%s, VSYSREG:%d\n",
		(int)regval,
		regval & CHGCTL2_DCILIM_EN ? on : off,
		(int)BITS_GET(regval, CHGCTL2_PREQCUR),
		regval & CHGCTL2_CEN ? on : off,
		regval & CHGCTL2_QBATEN ? on : off,
		(int)BITS_GET(regval, CHGCTL2_VSYSREG)
		);
	max77819_charger_resume_log_work(me);
#else
	int val = 0;
	u8 regval = 0;

	__lock(me);

	max77819_charger_update(me);

	max77819_charger_get_enable(me, &val);
	log_info("charger = %s\n", val ? "on" : "off");

	max77819_charger_get_dcilmt(me, &val);

	if (me->current_limit_volatile == MAX77819_CHARGER_CURRENT_UNLIMIT) {
		log_info("current limit expected = unlimited\n");
	} else {
		log_info("current limit expected = %duA\n",
				me->current_limit_volatile);
	}

	if (val == MAX77819_CHARGER_CURRENT_UNLIMIT) {
		log_info("current limit set      = unlimited\n");
	} else {
		log_info("current limit set      = %duA\n", val);
	}

	max77819_charger_get_chgcc(me, &val);

	log_info("charge current expected = %duA\n",
			me->charge_current_volatile);
	log_info("charge current set      = %duA\n", val);

	max77819_read(me->io, TOPOFF, &regval);
	log_info("TOPOFF register %02Xh\n", regval);

	max77819_read(me->io, BATREG, &regval);
	log_info("BATREG register %02Xh\n", regval);

	max77819_charger_resume_log_work(me);

	__unlock(me);
#endif
}

static void max77819_charger_irq_work(struct work_struct *work)
{
	struct max77819_charger *me =
		container_of(work, struct max77819_charger, irq_work.work);
	unsigned long irq_flags;
	u8 irq1_current, irq2_current;
	bool present_input;

	__lock(me);

	/* Read current IRQ status */
	irq1_current = max77819_charger_read_irq_status(me, CHGINT1);
	irq2_current = max77819_charger_read_irq_status(me, CHGINT2);

	spin_lock_irqsave(&me->irq_lock, irq_flags);
	irq1_current |= me->irq1_saved;
	me->irq1_saved = 0;
	irq2_current |= me->irq2_saved;
	me->irq2_saved = 0;
	spin_unlock_irqrestore(&me->irq_lock, irq_flags);

	log_dbg("<IRQ_WORK> CHGINT1 %02Xh CHGINT2 %02Xh\n", irq1_current,
			irq2_current);

	if (unlikely(!irq1_current && !irq2_current))
		goto done;

	/* Check Charger Present */
	present_input = max77819_charger_present_input(me);
	if (present_input ^ me->dev_initialized) {
		max77819_charger_psy_init(me);

		/* TODO: maybe it should be replaced external power change */
		if (likely(present_input)) {
			max77819_charger_wake_lock(me, true);
			max77819_charger_init_dev(me);
		} else {
			max77819_charger_wake_lock(me, false);
			max77819_charger_exit_dev(me);
		}

		/* TODO: dual charging implementation */
		/*max77819_charger_psy_setprop(me, psy_coop, CHARGING_ENABLED,
		  present_input); */

		/* Notify Charger insertion to USB power supply
		 * for AC/USB recognization
		 */
		max77819_charger_psy_setprop(me, psy_ext, PRESENT,
				present_input);

		schedule_delayed_work(&me->irq_work, IRQ_WORK_DELAY);
		log_err("DC input %s\n", present_input ? "inserted" :
				"removed");
		goto out;
	}

	/* notify psy changed */
	max77819_charger_psy_changed(me);

done:
	if (unlikely(me->irq <= 0)) {
		log_err("IRQ not initialized, force working\n");
		if (likely(!delayed_work_pending(&me->irq_work)))
			schedule_delayed_work(&me->irq_work, IRQ_WORK_INTERVAL);
	}

out:
	__unlock(me);
	return;
}

static irqreturn_t max77819_charger_isr(int irq, void *data)
{
	struct max77819_charger *me = data;
	u8 irq_current;

	irq_current = max77819_charger_read_irq_status(me, CHGINT1);
	log_dbg("<ISR> CHGINT1 CURR %02Xh SAVED %02Xh\n",
			irq_current, me->irq1_saved);
	me->irq1_saved |= irq_current;

	irq_current = max77819_charger_read_irq_status(me, CHGINT2);
	log_dbg("<ISR> CHGINT2 CURR %02Xh SAVED %02Xh\n",
			irq_current, me->irq2_saved);
	me->irq2_saved |= irq_current;

	schedule_delayed_work(&me->irq_work, IRQ_WORK_DELAY);
	return IRQ_HANDLED;
}

static int max77819_charger_batt_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct max77819_charger *me =
		container_of(psy, struct max77819_charger, batt);
	int rc = 0;

	__lock(me);

	rc = max77819_charger_update(me);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = me->status;
			break;
		case POWER_SUPPLY_PROP_CHARGE_TYPE:
			val->intval = me->charge_type;
			break;
		case POWER_SUPPLY_PROP_HEALTH:
			val->intval = me->batt_hth;
			break;
		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = 1;
			break;
		case POWER_SUPPLY_PROP_TECHNOLOGY:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
			val->intval = 4350 * 1000;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
			val->intval = 4350 * 1000;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val->intval = me->batt_vol;
			break;
		case POWER_SUPPLY_PROP_TEMP:
			val->intval = me->batt_tmp;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			if (pseudo_batt_info.mode) {
				val->intval = pseudo_batt_info.capacity;
				break;
			} else
				val->intval = me->batt_soc;
			break;
		case POWER_SUPPLY_PROP_CURRENT_NOW:
			val->intval = me->curr_now;
			break;
		case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
			val->intval = 3000;
			break;
		case POWER_SUPPLY_PROP_CHARGING_ENABLED:
			val->intval = me->dev_enabled;
			break;
		case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
			val->intval = 0;
			break;
		case POWER_SUPPLY_PROP_PSEUDO_BATT:
			val->intval = pseudo_batt_info.mode;
			break;
		default:
			rc = -EINVAL;
			goto out;
	}
out:
	log_vdbg("<get_property> psp %d val %d [%d]\n", psp, val->intval, rc);
	__unlock(me);
	return rc;
}

	static int
max77819_charger_batt_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct max77819_charger *me =
		container_of(psy, struct max77819_charger, batt);
	int rc = 0;

	__lock(me);

	switch (psp) {
		case POWER_SUPPLY_PROP_CHARGING_ENABLED:
			rc = max77819_charger_set_enable(me, val->intval);
			if (unlikely(IS_ERR_VALUE(rc)))
				goto out;

			me->dev_enabled = val->intval;

			/* apply charge current */
			rc = max77819_charger_set_charge_current(me,
					me->current_limit_volatile,
					me->charge_current_volatile);
			break;
		default:
			rc = -EINVAL;
			goto out;
	}
out:
	log_vdbg("<get_property> psp %d val %d [%d]\n", psp, val->intval, rc);
	__unlock(me);

	power_supply_changed(&me->batt);
	return rc;
}

static void
max77819_charger_batt_external_power_changed (struct power_supply *psy)
{
	struct max77819_charger *me =
		container_of(psy, struct max77819_charger, batt);

	union power_supply_propval ret = {0,};
	int rc = 0;

	me->psy_ext->get_property(me->psy_ext,
			POWER_SUPPLY_PROP_ONLINE, &ret);
	me->usb_online = ret.intval;

	if (max77819_charger_present_input(me)) {
		if (me->usb_online) {
			/* USB connect state */
			log_dbg("usb online\n");
			me->current_limit_volatile =
				me->pdata->current_limit_usb;
		} else if (me->ac_online) {
			/* AC connect state */
			log_dbg("ac online\n");
			me->current_limit_volatile =
				me->pdata->current_limit_ac;
		}

		rc = max77819_charger_set_charge_current(me,
				me->current_limit_volatile,
				me->charge_current_volatile);
	} else {
		log_dbg("nothing online\n");
		rc = max77819_charger_exit_dev(me);
	}
	power_supply_changed(&me->batt);
}


static int max77819_charger_batt_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
		case POWER_SUPPLY_PROP_CHARGING_ENABLED:
			return 1;
		default:
			break;
	}
	return -EINVAL;
}

static enum power_supply_property max77819_charger_batt_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL,
	POWER_SUPPLY_PROP_PSEUDO_BATT,
};

static int max77819_charger_ac_get_property(struct power_supply *psy,
		enum power_supply_property psp, union power_supply_propval *val)
{
	struct max77819_charger *me =
		container_of(psy, struct max77819_charger, psy);
	int rc = 0;

	__lock(me);

	rc = max77819_charger_update(me);
	if (unlikely(IS_ERR_VALUE(rc)))
		goto out;

	switch (psp) {
		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = me->present;
			break;
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = me->ac_online;
			break;
		case POWER_SUPPLY_PROP_HEALTH:
			val->intval = me->health;
			break;
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = me->status;
			break;
		case POWER_SUPPLY_PROP_CHARGE_TYPE:
			val->intval = me->charge_type;
			break;
		case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
			val->intval = me->charge_current_volatile;
			break;
		case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
			val->intval = me->current_limit_volatile;
			break;
		default:
			rc = -EINVAL;
			goto out;
	}
out:
	log_vdbg("<get_property> psp %d val %d [%d]\n", psp, val->intval, rc);
	__unlock(me);
	return rc;
}

static int max77819_charger_ac_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct max77819_charger *me =
		container_of(psy, struct max77819_charger, psy);
	int rc = 0;

	__lock(me);

	switch (psp) {
		case POWER_SUPPLY_PROP_ONLINE:
			me->ac_online = val->intval;
			break;
		case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
			break;
		default:
			rc = -EINVAL;
			goto out;
	}
out:
	log_vdbg("<set_property> psp %d val %d [%d]\n", psp, val->intval, rc);
	__unlock(me);

	power_supply_changed(&me->psy);
	return rc;
}

static int max77819_charger_ac_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
		case POWER_SUPPLY_PROP_ONLINE:
			return 1;
		default:
			break;
	}
	return -EINVAL;
}

static enum power_supply_property max77819_charger_ac_psy_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,     /* charging current */
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, /* input current limit */
};

static int max77819_charger_init_batt_psy(struct max77819_charger *me)
{
	int rc = 0;
	struct device *dev = me->dev;

	/* BATT power supply initialization */
	me->batt.name                   = "battery";
	me->batt.type                   = POWER_SUPPLY_TYPE_BATTERY;
	me->batt.properties             = max77819_charger_batt_props;
	me->batt.num_properties         =
		ARRAY_SIZE(max77819_charger_batt_props);
	me->batt.get_property           = max77819_charger_batt_get_property;
	me->batt.set_property           = max77819_charger_batt_set_property;
	me->batt.property_is_writeable  =
		max77819_charger_batt_property_is_writeable;
	me->batt.external_power_changed =
		max77819_charger_batt_external_power_changed;

	/* BATT power supply registration   */
	rc = power_supply_register(dev, &me->batt);
	if (IS_ERR_VALUE(rc))
		log_err("Failed to register batt power supply [%d]\n", rc);
	return rc;
}

static int max77819_charger_init_ac_psy(struct max77819_charger *me)
{
	int rc = 0;
	struct device *dev = me->dev;

	if (unlikely(!me->psy_this))
		me->psy_this = &me->psy;

	/* AC power supply initialization  */
	me->psy.name                    = me->pdata->psy_name;
	me->psy.type                    = POWER_SUPPLY_TYPE_MAINS;
	me->psy.supplied_to             = me->pdata->supplied_to;
	me->psy.num_supplicants         = me->pdata->num_supplicants;
	me->psy.properties              = max77819_charger_ac_psy_props;
	me->psy.num_properties          =
		ARRAY_SIZE(max77819_charger_ac_psy_props);
	me->psy.get_property            = max77819_charger_ac_get_property;
	me->psy.set_property            = max77819_charger_ac_set_property;
	me->psy.property_is_writeable   =
		max77819_charger_ac_property_is_writeable;

	/* AC power supply registration    */
	rc = power_supply_register(dev, me->psy_this);
	if (unlikely(IS_ERR_VALUE(rc))) {
		log_err("failed to register ac power_supply [%d]\n", rc);
		me->psy_this = NULL;
	}
	return rc;
}

/* Get max77819 charger device tree parsed data */
static void *max77819_charger_get_platdata(struct max77819_charger *me)
{
#ifdef CONFIG_OF
	struct device *dev = me->dev;
	struct device_node *np = dev->of_node;
	struct max77819_charger_platform_data *pdata;
	size_t sz;
	int num_supplicants, i;

	num_supplicants = of_property_count_strings(np, "supplied_to");
	num_supplicants = max(0, num_supplicants);

	sz = sizeof(*pdata) + num_supplicants * sizeof(char *);
	pdata = devm_kzalloc(dev, sz, GFP_KERNEL);
	if (unlikely(!pdata)) {
		log_err("out of memory (%uB requested)\n", sz);
		pdata = ERR_PTR(-ENOMEM);
		goto out;
	}

#ifdef CONFIG_LGE_PM
	get_cable_data_from_dt(np);
#endif

	pdata->irq = irq_of_parse_and_map(np, 0);
	log_dbg("property:IRQ             %d\n", pdata->irq);

	pdata->psy_name = "ac";
	of_property_read_string(np, "psy_name",
			(char const **)&pdata->psy_name);
	log_dbg("property:PSY NAME        %s\n", pdata->psy_name);

	of_property_read_string(np, "ext_psy_name",
			(char const **)&pdata->ext_psy_name);
	log_dbg("property:EXT PSY         %s\n",
			pdata->ext_psy_name ? pdata->ext_psy_name : "null");

	if (unlikely(num_supplicants <= 0)) {
		pdata->supplied_to     = NULL;
		pdata->num_supplicants = 0;
		log_dbg("property:SUPPLICANTS     null\n");
	} else {
		pdata->num_supplicants = (size_t)num_supplicants;
		log_dbg("property:SUPPLICANTS     %d\n", num_supplicants);
		pdata->supplied_to = (char **)(pdata + 1);
		for (i = 0; i < num_supplicants; i++) {
			of_property_read_string_index(np, "supplied_to", i,
					(char const **)&pdata->supplied_to[i]);
			log_dbg("property:SUPPLICANTS     %s\n",
					pdata->supplied_to[i]);
		}
	}
	pdata->current_limit_usb = 500000;
	of_property_read_u32(np, "current_limit_usb",
			&pdata->current_limit_usb);
	log_dbg("property:DCILMT_USB      %uuA\n", pdata->current_limit_usb);

	pdata->current_limit_ac = 1500000;
	of_property_read_u32(np, "current_limit_ac",
			&pdata->current_limit_ac);
	log_dbg("property:DCILMT_AC       %uuA\n", pdata->current_limit_ac);

	pdata->fast_charge_current = 500000;
	of_property_read_u32(np, "fast_charge_current",
			&pdata->fast_charge_current);
	log_dbg("property:CHGCC           %uuA\n", pdata->fast_charge_current);

	pdata->charge_termination_voltage = 4350000;
	of_property_read_u32(np, "charge_termination_voltage",
			&pdata->charge_termination_voltage);
	log_dbg("property:MBATREG         %uuV\n",
			pdata->charge_termination_voltage);

	pdata->topoff_timer = 0;
	of_property_read_u32(np, "topoff_timer", &pdata->topoff_timer);
	log_dbg("property:TOPOFFTIME      %umin\n", pdata->topoff_timer);

	pdata->topoff_current = 200000;
	of_property_read_u32(np, "topoff_current", &pdata->topoff_current);
	log_dbg("property:ITOPOFF         %uuA\n", pdata->topoff_current);

	pdata->charge_restart_threshold = 150000;
	of_property_read_u32(np, "charge_restart_threshold",
			&pdata->charge_restart_threshold);
	log_dbg("property:CHGRSTRT        %uuV\n",
			pdata->charge_restart_threshold);

	pdata->enable_coop = of_property_read_bool(np, "enable_coop");
	log_dbg("property:COOP CHG        %s\n",
			pdata->enable_coop ? "enabled" : "disabled");

	if (likely(pdata->enable_coop)) {
		of_property_read_string(np, "coop_psy_name",
				(char const **)&pdata->coop_psy_name);
		log_dbg("property:COOP CHG        %s\n", pdata->coop_psy_name ?
				pdata->coop_psy_name : "null");
	}

	pdata->enable_thermistor = of_property_read_bool(np,
			"enable_thermistor");
	log_dbg("property:THERMISTOR      %s\n",
			pdata->enable_thermistor ? "enabled" : "disabled");

	pdata->enable_aicl = of_property_read_bool(np, "enable_aicl");
	log_dbg("property:AICL            %s\n",
			pdata->enable_aicl ? "enabled" : "disabled");

	pdata->aicl_detection_voltage = 4500000;
	of_property_read_u32(np, "aicl_detection_voltage",
			&pdata->aicl_detection_voltage);
	log_dbg("property:AICL DETECTION  %uuV\n",
			pdata->aicl_detection_voltage);

	pdata->aicl_reset_threshold = 100000;
	of_property_read_u32(np, "aicl_reset_threshold",
			&pdata->aicl_reset_threshold);
	log_dbg("property:AICL RESET      %uuV\n", pdata->aicl_reset_threshold);

out:
	return pdata;
#else /* CONFIG_OF */
	return dev_get_platdata(me->dev) ?
		dev_get_platdata(me->dev) : ERR_PTR(-EINVAL);
#endif /* CONFIG_OF */
}

	static __always_inline
void max77819_charger_destroy(struct max77819_charger *me)
{
	struct device *dev = me->dev;

	cancel_delayed_work_sync(&me->log_work);

	if (likely(me->irq > 0))
		devm_free_irq(dev, me->irq, me);

	cancel_delayed_work_sync(&me->irq_work);

	if (likely(me->attr_grp))
		sysfs_remove_group(me->kobj, me->attr_grp);

	if (likely(me->psy_this))
		power_supply_unregister(me->psy_this);

#ifdef CONFIG_OF
	if (likely(me->pdata))
		devm_kfree(dev, me->pdata);
#endif /* CONFIG_OF */

	mutex_destroy(&me->lock);
	/*  spin_lock_destroy(&me->irq_lock); */

	devm_kfree(dev, me);
}

#ifdef CONFIG_OF
static struct of_device_id max77819_charger_of_ids[] = {
	{ .compatible = "maxim,"MAX77819_CHARGER_NAME },
	{ },
};
MODULE_DEVICE_TABLE(of, max77819_charger_of_ids);
#endif /* CONFIG_OF */

static int max77819_charger_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max77819_dev *chip = dev_get_drvdata(dev->parent);
	struct max77819_charger *me;
	int rc = 0;

	log_err("max77819 charger attached\n");

	me = devm_kzalloc(dev, sizeof(*me), GFP_KERNEL);
	if (unlikely(!me)) {
		log_err("out of memory (%uB requested)\n", sizeof(*me));
		return -ENOMEM;
	}

	dev_set_drvdata(dev, me);

	spin_lock_init(&me->irq_lock);
	mutex_init(&me->lock);
	me->io   = max77819_get_io(chip);
	me->dev  = dev;
	me->kobj = &dev->kobj;
	me->irq  = -1;

	wake_lock_init(&me->chg_wake_lock, WAKE_LOCK_SUSPEND, "chg_wakelock");

	INIT_DELAYED_WORK(&me->irq_work, max77819_charger_irq_work);
	INIT_DELAYED_WORK(&me->log_work, max77819_charger_log_work);

	me->pdata = max77819_charger_get_platdata(me);
	if (unlikely(IS_ERR(me->pdata))) {
		rc = PTR_ERR(me->pdata);
		me->pdata = NULL;

		log_err("failed to get platform data [%d]\n", rc);
		goto abort;
	}

	/* Get USB Power Supply */
	if (unlikely(!me->psy_ext && me->pdata->ext_psy_name)) {
		me->psy_ext = power_supply_get_by_name(me->pdata->ext_psy_name);
		if (likely(me->psy_ext)) {
			log_err("psy %s found\n", me->pdata->ext_psy_name);
			max77819_charger_psy_setprop(me, psy_ext, PRESENT,
					false);
		} else {
			log_err("psy %s not found, deferring probe\n",
					me->pdata->ext_psy_name);
			rc = -EPROBE_DEFER;
			goto usb_psy_fail;
		}
	}

	/* Disable all IRQ */
	max77819_write(me->io, CHGINTM1,   0xFF);
	max77819_write(me->io, CHGINTMSK2, 0xFF);

	me->dev_enabled               =
		(!me->pdata->enable_coop || me->pdata->coop_psy_name);

	me->current_limit_permanent   = MAX77819_CHARGER_CURRENT_UNLIMIT;
	me->current_limit_volatile    = me->current_limit_permanent;

	me->charge_current_permanent  = me->pdata->fast_charge_current;
	me->charge_current_volatile   = me->charge_current_permanent;

	/* Battery power supply initialize and regist */
	rc = max77819_charger_init_batt_psy(me);
	if (unlikely(IS_ERR_VALUE(rc))) {
		log_err("batt_psy init fail: %d\n", rc);
		goto batt_psy_fail;
	}

	/* AC power supply initialize and regist */
	rc = max77819_charger_init_ac_psy(me);
	if (unlikely(IS_ERR_VALUE(rc))) {
		log_err("ac_psy init fail: %d\n", rc);
		goto ac_psy_fail;
	}

	/* Get External power supply, USB and ETC */
	max77819_charger_psy_init(me);

	me->irq = me->pdata->irq;

	/* If IRQ disabled or not initialized, use irq_work polling */
	if (unlikely(me->irq <= 0)) {
		log_err("interrupt disabled\n");
		schedule_delayed_work(&me->irq_work, IRQ_WORK_INTERVAL);
	} else {
		rc = devm_request_threaded_irq(dev, me->irq, NULL,
				max77819_charger_isr, IRQF_ONESHOT,
				DRIVER_NAME, me);
		if (unlikely(IS_ERR_VALUE(rc))) {
			log_err("failed to request IRQ %d [%d]\n", me->irq, rc);
			me->irq = -1;
			goto irq_reg_fail;
		}
	}

	/* enable IRQ we want    */
	/* TODO: enable irq more */
	max77819_write(me->io, CHGINTM1, (u8)~CHGINT1_DC_UVP);

	log_err("driver "DRIVER_VERSION" installed\n");

	/* Default onetime call for irq_work */
	schedule_delayed_work(&me->irq_work, IRQ_WORK_DELAY);
	max77819_charger_resume_log_work(me);

	rc = sysfs_create_group(&dev->kobj, &max77819_charger_attribute_group);
	if (rc < 0) {
		log_err("failed to create sysfs attributes\n");
		goto abort;
	}

	return 0;

irq_reg_fail:
	/* TODO: unregist cooperated charger */
	/*coop_psy_fail:
	  power_supply_unregister(&me->coop_psy_name); */
	power_supply_unregister(&me->psy);
ac_psy_fail:
	power_supply_unregister(&me->batt);
batt_psy_fail:
usb_psy_fail:
abort:
	wake_lock_destroy(&me->chg_wake_lock);
	dev_set_drvdata(dev, NULL);
	max77819_charger_destroy(me);
	return rc;
}

static int max77819_charger_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max77819_charger *me = dev_get_drvdata(dev);

	sysfs_remove_group(&dev->kobj, &max77819_charger_attribute_group);
	wake_lock_destroy(&me->chg_wake_lock);

	dev_set_drvdata(dev, NULL);
	max77819_charger_destroy(me);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77819_charger_suspend(struct device *dev)
{
	struct max77819_charger *me = dev_get_drvdata(dev);

	__lock(me);

	log_vdbg("suspending\n");

	max77819_charger_suspend_log_work(me);
	flush_delayed_work(&me->irq_work);
	atomic_set(&me->suspended, 1);

	__unlock(me);
	return 0;
}

static int max77819_charger_resume(struct device *dev)
{
	struct max77819_charger *me = dev_get_drvdata(dev);

	__lock(me);

	log_vdbg("resuming\n");
	atomic_set(&me->suspended, 0);
	schedule_delayed_work(&me->irq_work, IRQ_WORK_DELAY);
	max77819_charger_resume_log_work(me);

	__unlock(me);
	return 0;
}

static int max77819_charger_pm_suspend_runtime(struct device *dev)
{
	max77819_charger_suspend(dev);
	return 0;
}
static int max77819_charger_pm_resume_runtime(struct device *dev)
{
	max77819_charger_resume(dev);
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops max77819_charger_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(
			max77819_charger_suspend,
			max77819_charger_resume
			)
		SET_RUNTIME_PM_OPS(
				max77819_charger_pm_suspend_runtime,
				max77819_charger_pm_resume_runtime,
				NULL
				)
};

static struct platform_driver max77819_charger_driver = {
	.driver.name            = DRIVER_NAME,
	.driver.owner           = THIS_MODULE,
	.driver.pm              = &max77819_charger_pm,
#ifdef CONFIG_OF
	.driver.of_match_table  = max77819_charger_of_ids,
#endif /* CONFIG_OF */
	.probe                  = max77819_charger_probe,
	.remove                 = max77819_charger_remove,
};

static __init int max77819_charger_init(void)
{
	return platform_driver_register(&max77819_charger_driver);
}
late_initcall(max77819_charger_init);

static __exit void max77819_charger_exit(void)
{
	platform_driver_unregister(&max77819_charger_driver);
}
module_exit(max77819_charger_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_VERSION(DRIVER_VERSION);
