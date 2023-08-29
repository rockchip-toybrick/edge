// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2022 Southchip Semiconductor Technology(Shanghai) Co., Ltd.
*/

#define pr_fmt(fmt)    "[sc8885] %s: " fmt, __func__

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/regmap.h>

#include "sc888x_charger.h"

#define SC8885_MANUFACTURER    	"South Chip"
#define SC8885_ID            	0x68
#define SC8886_ID            	0x66

#define sc_err(fmt, ...)                                \
do {                                            \
    if (charger->chip_id == SC8885_ID)                        \
        printk(KERN_ERR "[sc8885]:%s:" fmt, __func__, ##__VA_ARGS__);    \
    else            \
        printk(KERN_ERR "[sc8886]:%s:" fmt, __func__, ##__VA_ARGS__);    \
} while(0);

#define sc_info(fmt, ...)                                \
do {                                            \
    if (charger->chip_id == SC8885_ID)                        \
        printk(KERN_INFO "[sc8885]:%s:" fmt, __func__, ##__VA_ARGS__);    \
    else                    \
        printk(KERN_INFO "[sc8886]:%s:" fmt, __func__, ##__VA_ARGS__);    \
} while(0);

#define sc_dbg(fmt, ...)                                \
do {                                            \
    if (if (charger->chip_id == SC8885_ID))                        \
        printk(KERN_DEBUG "[sc8885]:%s:" fmt, __func__, ##__VA_ARGS__);    \
    else                 \
        printk(KERN_DEBUG "[sc8886]:%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

#define DEFAULT_INPUTVOL		((5000 - 1280) * 1000)
#define MAX_INPUTVOLTAGE        19520000
#define MAX_INPUTCURRENT        6350000
#define MAX_CHARGEVOLTAGE		16800000
#define MAX_CHARGECURRETNT		8128000
#define MAX_OTGVOLTAGE			20800000
#define MAX_OTGCURRENT			6350000



enum sc888x_fields {
    EN_LWPWR, WDTWR_ADJ, IDPM_AUTO_DISABLE,
    EN_OOA, PWM_FREQ, EN_LEARN, IADP_GAIN, IBAT_GAIN,
    EN_LDO, EN_IDPM, CHRG_INHIBIT,/*reg12h*/
    CHARGE_CURRENT,/*reg14h*/
    MAX_CHARGE_VOLTAGE,/*reg15h*/

    AC_STAT, ICO_DONE, IN_VINDPM, IN_IINDPM, IN_FCHRG, IN_PCHRG, IN_OTG,
    F_ACOV, F_BATOC, F_ACOC, SYSOVP_STAT, F_LATCHOFF, F_OTG_OVP, F_OTG_OCP,
    /*reg20h*/
    STAT_COMP, STAT_ICRIT, STAT_INOM, STAT_IDCHG, STAT_VSYS, STAT_BAT_REMOV,
    STAT_ADP_REMOV,/*reg21h*/
    INPUT_CURRENT_DPM,/*reg22h*/
    OUTPUT_INPUT_VOL, OUTPUT_SYS_POWER,/*reg23h*/
    OUTPUT_DSG_CUR,    OUTPUT_CHG_CUR,/*reg24h*/
    OUTPUT_INPUT_CUR, OUTPUT_CMPIN_VOL,/*reg25h*/
    OUTPUT_SYS_VOL, OUTPUT_BAT_VOL,/*reg26h*/

    EN_IBAT, EN_PROCHOT_LPWR, EN_PSYS, RSNS_RAC, RSNS_RSR,
    PSYS_RATIO, CMP_REF,    CMP_POL, CMP_DEG, FORCE_LATCHOFF,
    EN_SHIP_DCHG, AUTO_WAKEUP_EN, /*reg30h*/
    PKPWR_TOVLD_REG, EN_PKPWR_IDPM, EN_PKPWR_VSYS, PKPWER_OVLD_STAT,
    PKPWR_RELAX_STAT, PKPWER_TMAX,    EN_EXTILIM, EN_ICHG_IDCHG, Q2_OCP,
    ACX_OCP, EN_ACOC, ACOC_VTH, EN_BATOC, BATCOC_VTH,/*reg31h*/
    EN_HIZ, RESET_REG, RESET_VINDPM, EN_OTG, EN_ICO_MODE, OTG_RANGE_LOW, BATFETOFF_HIZ,
    PSYS_OTG_IDCHG,/*reg32h*/
    ILIM2_VTH, ICRIT_DEG, VSYS_VTH, EN_PROCHOT_EXT, PROCHOT_WIDTH,
    PROCHOT_CLEAR, INOM_DEG,/*reg33h*/
    IDCHG_VTH, IDCHG_DEG, PROCHOT_PROFILE_COMP, PROCHOT_PROFILE_ICRIT,
    PROCHOT_PROFILE_INOM, PROCHOT_PROFILE_IDCHG,
    PROCHOT_PROFILE_VSYS, PROCHOT_PROFILE_BATPRES, PROCHOT_PROFILE_ACOK,
    /*reg34h*/
    ADC_CONV, ADC_START, ADC_FULLSCALE, EN_ADC_CMPIN, EN_ADC_VBUS,
    EN_ADC_PSYS, EN_ADC_IIN, EN_ADC_IDCHG, EN_ADC_ICHG, EN_ADC_VSYS,
    EN_ADC_VBAT,/*reg35h*/

    OTG_VOLTAGE,/*reg3bh*/
    OTG_CURRENT,/*reg3ch*/
    INPUT_VOLTAGE,/*reg3dh*/
    MIN_SYS_VOTAGE,/*reg3eh*/
    INPUT_CURRENT,/*reg3fh*/

    MANUFACTURE_ID,/*regfeh*/
    DEVICE_ID,/*regffh*/

    F_MAX_FIELDS
};

enum charger_t {
    USB_TYPE_UNKNOWN_CHARGER,
    USB_TYPE_NONE_CHARGER,
    USB_TYPE_USB_CHARGER,
    USB_TYPE_AC_CHARGER,
    USB_TYPE_CDP_CHARGER,
    DC_TYPE_DC_CHARGER,
    DC_TYPE_NONE_CHARGER,
};

enum usb_status_t {
    USB_STATUS_NONE,
    USB_STATUS_USB,
    USB_STATUS_AC,
    USB_STATUS_PD,
    USB_STATUS_OTG,
};

enum tpyec_port_t {
    USB_TYPEC_0,
    USB_TYPEC_1,
};

/* initial field values, converted to register values */
struct sc8885_init_data {
    u32 ichg;    /* charge current        */
    u32 max_chg_vol;    /*max charge voltage*/
    u32 input_voltage;    /*input voltage*/
    u32 input_current;    /*input current*/
    u32 input_current_sdp;
    u32 input_current_dcp;
    u32 input_current_cdp;
    u32 sys_min_voltage;    /*mininum system voltage*/
    u32 otg_voltage;    /*OTG voltage*/
    u32 otg_current;    /*OTG current*/
};

struct sc8885_state {
    u8 ac_stat;
    u8 ico_done;
    u8 in_vindpm;
    u8 in_iindpm;
    u8 in_fchrg;
    u8 in_pchrg;
    u8 in_otg;
    u8 fault_acov;
    u8 fault_batoc;
    u8 fault_acoc;
    u8 sysovp_stat;
    u8 fault_latchoff;
    u8 fault_otg_ovp;
    u8 fault_otg_ocp;
};

struct sc888x_device {
    struct i2c_client		*client;
    struct device			*dev;
    struct power_supply		*supply_charger;
    char                    model_name[I2C_NAME_SIZE];
	int 					irq_gpio;
    unsigned int            irq;
    bool                    charger_health_valid;
    bool                    battery_health_valid;
    bool                    battery_status_valid;

    struct gpio_desc		*otg_mode_en_io;

    struct regmap			*regmap;
    struct regmap_field		*rmap_fields[F_MAX_FIELDS];
    int						chip_id;
    struct sc8885_init_data	init_data;
    struct sc8885_state		state;
    int						pd_charge_only;
    unsigned int            bc_event;
    bool                    usb_bc;
};


static const struct reg_field sc8885_reg_fields[] = {
    /*REG12*/
    [EN_LWPWR] = REG_FIELD(0x12, 15, 15),
    [WDTWR_ADJ] = REG_FIELD(0x12, 13, 14),
    [IDPM_AUTO_DISABLE] = REG_FIELD(0x12, 12, 12),
    [EN_OOA] = REG_FIELD(0x12, 10, 10),
    [PWM_FREQ] = REG_FIELD(0x12, 9, 9),
    [EN_LEARN] = REG_FIELD(0x12, 5, 5),
    [IADP_GAIN] = REG_FIELD(0x12, 4, 4),
    [IBAT_GAIN] = REG_FIELD(0x12, 3, 3),
    [EN_LDO] = REG_FIELD(0x12, 2, 2),
    [EN_IDPM] = REG_FIELD(0x12, 1, 1),
    [CHRG_INHIBIT] = REG_FIELD(0x12, 0, 0),
    /*REG0x14*/
    [CHARGE_CURRENT] = REG_FIELD(0x14, 6, 12),
    /*REG0x15*/
    [MAX_CHARGE_VOLTAGE] = REG_FIELD(0x15, 4, 14),
    /*REG20*/
    [AC_STAT] = REG_FIELD(0x20, 15, 15),
    [ICO_DONE] = REG_FIELD(0x20, 14, 14),
    [IN_VINDPM] = REG_FIELD(0x20, 12, 12),
    [IN_IINDPM] = REG_FIELD(0x20, 11, 11),
    [IN_FCHRG] = REG_FIELD(0x20, 10, 10),
    [IN_PCHRG] = REG_FIELD(0x20, 9, 9),
    [IN_OTG] = REG_FIELD(0x20, 8, 8),
    [F_ACOV] = REG_FIELD(0x20, 7, 7),
    [F_BATOC] = REG_FIELD(0x20, 6, 6),
    [F_ACOC] = REG_FIELD(0x20, 5, 5),
    [SYSOVP_STAT] = REG_FIELD(0x20, 4, 4),
    [F_LATCHOFF] = REG_FIELD(0x20, 2, 2),
    [F_OTG_OVP] = REG_FIELD(0x20, 1, 1),
    [F_OTG_OCP] = REG_FIELD(0x20, 0, 0),
    /*REG21*/
    [STAT_COMP] = REG_FIELD(0x21, 6, 6),
    [STAT_ICRIT] = REG_FIELD(0x21, 5, 5),
    [STAT_INOM] = REG_FIELD(0x21, 4, 4),
    [STAT_IDCHG] = REG_FIELD(0x21, 3, 3),
    [STAT_VSYS] = REG_FIELD(0x21, 2, 2),
    [STAT_BAT_REMOV] = REG_FIELD(0x21, 1, 1),
    [STAT_ADP_REMOV] = REG_FIELD(0x21, 0, 0),
    /*REG22*/
    [INPUT_CURRENT_DPM] = REG_FIELD(0x22, 8, 14),
    /*REG23H*/
    [OUTPUT_INPUT_VOL] = REG_FIELD(0x23, 8, 15),
    [OUTPUT_SYS_POWER] = REG_FIELD(0x23, 0, 7),
    /*REG24H*/
    [OUTPUT_DSG_CUR] = REG_FIELD(0x24, 8, 14),
    [OUTPUT_CHG_CUR] = REG_FIELD(0x24, 0, 6),
    /*REG25H*/
    [OUTPUT_INPUT_CUR] = REG_FIELD(0x25, 8, 15),
    [OUTPUT_CMPIN_VOL] = REG_FIELD(0x25, 0, 7),
    /*REG26H*/
    [OUTPUT_SYS_VOL] = REG_FIELD(0x26, 8, 15),
    [OUTPUT_BAT_VOL] = REG_FIELD(0x26, 0, 6),

    /*REG30*/
    [EN_IBAT] = REG_FIELD(0x30, 15, 15),
    [EN_PROCHOT_LPWR] = REG_FIELD(0x30, 13, 14),
    [EN_PSYS] = REG_FIELD(0x30, 12, 12),
    [RSNS_RAC] = REG_FIELD(0x30, 11, 11),
    [RSNS_RSR] = REG_FIELD(0x30, 10, 10),
    [PSYS_RATIO] = REG_FIELD(0x30, 9, 9),
    [CMP_REF] = REG_FIELD(0x30, 7, 7),
    [CMP_POL] = REG_FIELD(0x30, 6, 6),
    [CMP_DEG] = REG_FIELD(0x30, 4, 5),
    [FORCE_LATCHOFF] = REG_FIELD(0x30, 3, 3),
    [EN_SHIP_DCHG] = REG_FIELD(0x30, 1, 1),
    [AUTO_WAKEUP_EN] = REG_FIELD(0x30, 0, 0),
    /*REG31*/
    [PKPWR_TOVLD_REG] = REG_FIELD(0x31, 14, 15),
    [EN_PKPWR_IDPM] = REG_FIELD(0x31, 13, 13),
    [EN_PKPWR_VSYS] = REG_FIELD(0x31, 12, 12),
    [PKPWER_OVLD_STAT] = REG_FIELD(0x31, 11, 11),
    [PKPWR_RELAX_STAT] = REG_FIELD(0x31, 10, 10),
    [PKPWER_TMAX] = REG_FIELD(0x31, 8, 9),
    [EN_EXTILIM] = REG_FIELD(0x31, 7, 7),
    [EN_ICHG_IDCHG] = REG_FIELD(0x31, 6, 6),
    [Q2_OCP] = REG_FIELD(0x31, 5, 5),
    [ACX_OCP] = REG_FIELD(0x31, 4, 4),
    [EN_ACOC] = REG_FIELD(0x31, 3, 3),
    [ACOC_VTH] = REG_FIELD(0x31, 2, 2),
    [EN_BATOC] = REG_FIELD(0x31, 1, 1),
    [BATCOC_VTH] = REG_FIELD(0x31, 0, 0),
    /*REG32*/
    [EN_HIZ] = REG_FIELD(0x32, 15, 15),
    [RESET_REG] = REG_FIELD(0x32, 14, 14),
    [RESET_VINDPM] = REG_FIELD(0x32, 13, 13),
    [EN_OTG] = REG_FIELD(0x32, 12, 12),
    [EN_ICO_MODE] = REG_FIELD(0x32, 11, 11),
    [OTG_RANGE_LOW] = REG_FIELD(0x32, 2, 2),
    [BATFETOFF_HIZ] = REG_FIELD(0x32, 1, 1),
    [PSYS_OTG_IDCHG] = REG_FIELD(0x32, 0, 0),
    /*REG33*/
    [ILIM2_VTH] = REG_FIELD(0x33, 11, 15),
    [ICRIT_DEG] = REG_FIELD(0x33, 9, 10),
    [VSYS_VTH] = REG_FIELD(0x33, 6, 7),
    [EN_PROCHOT_EXT] = REG_FIELD(0x33, 5, 5),
    [PROCHOT_WIDTH] = REG_FIELD(0x33, 3, 4),
    [PROCHOT_CLEAR] = REG_FIELD(0x33, 2, 2),
    [INOM_DEG] = REG_FIELD(0x33, 1, 1),
    /*REG34*/
    [IDCHG_VTH] = REG_FIELD(0x34, 10, 15),
    [IDCHG_DEG] = REG_FIELD(0x34, 8, 9),
    [PROCHOT_PROFILE_COMP] = REG_FIELD(0x34, 6, 6),
    [PROCHOT_PROFILE_ICRIT] = REG_FIELD(0x34, 5, 5),
    [PROCHOT_PROFILE_INOM] = REG_FIELD(0x34, 4, 4),
    [PROCHOT_PROFILE_IDCHG] = REG_FIELD(0x34, 3, 3),
    [PROCHOT_PROFILE_VSYS] = REG_FIELD(0x34, 2, 1),
    [PROCHOT_PROFILE_BATPRES] = REG_FIELD(0x34, 1, 1),
    [PROCHOT_PROFILE_ACOK] = REG_FIELD(0x34, 0, 0),
    /*REG35*/
    [ADC_CONV] = REG_FIELD(0x35, 15, 15),
    [ADC_START] = REG_FIELD(0x35, 14, 14),
    [ADC_FULLSCALE] = REG_FIELD(0x35, 13, 13),
    [EN_ADC_CMPIN] = REG_FIELD(0x35, 7, 7),
    [EN_ADC_VBUS] = REG_FIELD(0x35, 6, 6),
    [EN_ADC_PSYS] = REG_FIELD(0x35, 5, 5),
    [EN_ADC_IIN] = REG_FIELD(0x35, 4, 4),
    [EN_ADC_IDCHG] = REG_FIELD(0x35, 3, 3),
    [EN_ADC_ICHG] = REG_FIELD(0x35, 2, 2),
    [EN_ADC_VSYS] = REG_FIELD(0x35, 1, 1),
    [EN_ADC_VBAT] = REG_FIELD(0x35, 0, 0),
    /*REG3B*/
    [OTG_VOLTAGE] = REG_FIELD(0x3B, 2, 13),
    /*REG3C*/
    [OTG_CURRENT] = REG_FIELD(0x3C, 8, 14),
    /*REG3D*/
    [INPUT_VOLTAGE] = REG_FIELD(0x3D, 6, 13),
    /*REG3E*/
    [MIN_SYS_VOTAGE] = REG_FIELD(0x3E, 8, 13),
    /*REG3F*/
    [INPUT_CURRENT] = REG_FIELD(0x3F, 8, 14),

    /*REGFE*/
    [MANUFACTURE_ID] = REG_FIELD(0xFE, 0, 7),
    /*REFFF*/
    [DEVICE_ID] = REG_FIELD(0xFF, 0, 7),
};

static const struct reg_field sc8886_reg_fields[] = {
    /*REG00*/
    [EN_LWPWR] = REG_FIELD(0x00, 15, 15),
    [WDTWR_ADJ] = REG_FIELD(0x00, 13, 14),
    [IDPM_AUTO_DISABLE] = REG_FIELD(0x00, 12, 12),
    [EN_OOA] = REG_FIELD(0x00, 10, 10),
    [PWM_FREQ] = REG_FIELD(0x00, 9, 9),
    [EN_LEARN] = REG_FIELD(0x00, 5, 5),
    [IADP_GAIN] = REG_FIELD(0x00, 4, 4),
    [IBAT_GAIN] = REG_FIELD(0x00, 3, 3),
    [EN_LDO] = REG_FIELD(0x00, 2, 2),
    [EN_IDPM] = REG_FIELD(0x00, 1, 1),
    [CHRG_INHIBIT] = REG_FIELD(0x00, 0, 0),
    /*REG0x02*/
    [CHARGE_CURRENT] = REG_FIELD(0x02, 6, 12),
    /*REG0x04*/
    [MAX_CHARGE_VOLTAGE] = REG_FIELD(0x04, 3, 14),
    /*REG20*/
    [AC_STAT] = REG_FIELD(0x20, 15, 15),
    [ICO_DONE] = REG_FIELD(0x20, 14, 14),
    [IN_VINDPM] = REG_FIELD(0x20, 12, 12),
    [IN_IINDPM] = REG_FIELD(0x20, 11, 11),
    [IN_FCHRG] = REG_FIELD(0x20, 10, 10),
    [IN_PCHRG] = REG_FIELD(0x20, 9, 9),
    [IN_OTG] = REG_FIELD(0x20, 8, 8),
    [F_ACOV] = REG_FIELD(0x20, 7, 7),
    [F_BATOC] = REG_FIELD(0x20, 6, 6),
    [F_ACOC] = REG_FIELD(0x20, 5, 5),
    [SYSOVP_STAT] = REG_FIELD(0x20, 4, 4),
    [F_LATCHOFF] = REG_FIELD(0x20, 2, 2),
    [F_OTG_OVP] = REG_FIELD(0x20, 1, 1),
    [F_OTG_OCP] = REG_FIELD(0x20, 0, 0),
    /*REG22*/
    [STAT_COMP] = REG_FIELD(0x22, 6, 6),
    [STAT_ICRIT] = REG_FIELD(0x22, 5, 5),
    [STAT_INOM] = REG_FIELD(0x22, 4, 4),
    [STAT_IDCHG] = REG_FIELD(0x22, 3, 3),
    [STAT_VSYS] = REG_FIELD(0x22, 2, 2),
    [STAT_BAT_REMOV] = REG_FIELD(0x22, 1, 1),
    [STAT_ADP_REMOV] = REG_FIELD(0x22, 0, 0),
    /*REG24*/
    [INPUT_CURRENT_DPM] = REG_FIELD(0x24, 8, 14),

    /*REG26H*/
    [OUTPUT_INPUT_VOL] = REG_FIELD(0x26, 8, 15),
    [OUTPUT_SYS_POWER] = REG_FIELD(0x26, 0, 7),
    /*REG28H*/
    [OUTPUT_DSG_CUR] = REG_FIELD(0x28, 8, 14),
    [OUTPUT_CHG_CUR] = REG_FIELD(0x28, 0, 6),
    /*REG2aH*/
    [OUTPUT_INPUT_CUR] = REG_FIELD(0x2a, 8, 15),
    [OUTPUT_CMPIN_VOL] = REG_FIELD(0x2a, 0, 7),
    /*REG2cH*/
    [OUTPUT_SYS_VOL] = REG_FIELD(0x2c, 8, 15),
    [OUTPUT_BAT_VOL] = REG_FIELD(0x2c, 0, 6),

    /*REG30*/
    [EN_IBAT] = REG_FIELD(0x30, 15, 15),
    [EN_PROCHOT_LPWR] = REG_FIELD(0x30, 13, 14),
    [EN_PSYS] = REG_FIELD(0x30, 12, 12),
    [RSNS_RAC] = REG_FIELD(0x30, 11, 11),
    [RSNS_RSR] = REG_FIELD(0x30, 10, 10),
    [PSYS_RATIO] = REG_FIELD(0x30, 9, 9),
    [CMP_REF] = REG_FIELD(0x30, 7, 7),
    [CMP_POL] = REG_FIELD(0x30, 6, 6),
    [CMP_DEG] = REG_FIELD(0x30, 4, 5),
    [FORCE_LATCHOFF] = REG_FIELD(0x30, 3, 3),
    [EN_SHIP_DCHG] = REG_FIELD(0x30, 1, 1),
    [AUTO_WAKEUP_EN] = REG_FIELD(0x30, 0, 0),
    /*REG32*/
    [PKPWR_TOVLD_REG] = REG_FIELD(0x32, 14, 15),
    [EN_PKPWR_IDPM] = REG_FIELD(0x32, 13, 13),
    [EN_PKPWR_VSYS] = REG_FIELD(0x32, 12, 12),
    [PKPWER_OVLD_STAT] = REG_FIELD(0x32, 11, 11),
    [PKPWR_RELAX_STAT] = REG_FIELD(0x32, 10, 10),
    [PKPWER_TMAX] = REG_FIELD(0x32, 8, 9),
    [EN_EXTILIM] = REG_FIELD(0x32, 7, 7),
    [EN_ICHG_IDCHG] = REG_FIELD(0x32, 6, 6),
    [Q2_OCP] = REG_FIELD(0x32, 5, 5),
    [ACX_OCP] = REG_FIELD(0x32, 4, 4),
    [EN_ACOC] = REG_FIELD(0x32, 3, 3),
    [ACOC_VTH] = REG_FIELD(0x32, 2, 2),
    [EN_BATOC] = REG_FIELD(0x32, 1, 1),
    [BATCOC_VTH] = REG_FIELD(0x32, 0, 0),
    /*REG34*/
    [EN_HIZ] = REG_FIELD(0x34, 15, 15),
    [RESET_REG] = REG_FIELD(0x34, 14, 14),
    [RESET_VINDPM] = REG_FIELD(0x34, 13, 13),
    [EN_OTG] = REG_FIELD(0x34, 12, 12),
    [EN_ICO_MODE] = REG_FIELD(0x34, 11, 11),
    [OTG_RANGE_LOW] = REG_FIELD(0x32, 2, 2),
    [BATFETOFF_HIZ] = REG_FIELD(0x34, 1, 1),
    [PSYS_OTG_IDCHG] = REG_FIELD(0x34, 0, 0),
    /*REG36*/
    [ILIM2_VTH] = REG_FIELD(0x36, 11, 15),
    [ICRIT_DEG] = REG_FIELD(0x36, 9, 10),
    [VSYS_VTH] = REG_FIELD(0x36, 6, 7),
    [EN_PROCHOT_EXT] = REG_FIELD(0x36, 5, 5),
    [PROCHOT_WIDTH] = REG_FIELD(0x36, 3, 4),
    [PROCHOT_CLEAR] = REG_FIELD(0x36, 2, 2),
    [INOM_DEG] = REG_FIELD(0x36, 1, 1),
    /*REG38*/
    [IDCHG_VTH] = REG_FIELD(0x38, 10, 15),
    [IDCHG_DEG] = REG_FIELD(0x38, 8, 9),
    [PROCHOT_PROFILE_COMP] = REG_FIELD(0x38, 6, 6),
    [PROCHOT_PROFILE_ICRIT] = REG_FIELD(0x38, 5, 5),
    [PROCHOT_PROFILE_INOM] = REG_FIELD(0x38, 4, 4),
    [PROCHOT_PROFILE_IDCHG] = REG_FIELD(0x38, 3, 3),
    [PROCHOT_PROFILE_VSYS] = REG_FIELD(0x38, 2, 2),
    [PROCHOT_PROFILE_BATPRES] = REG_FIELD(0x38, 1, 1),
    [PROCHOT_PROFILE_ACOK] = REG_FIELD(0x38, 0, 0),
    /*REG3a*/
    [ADC_CONV] = REG_FIELD(0x3a, 15, 15),
    [ADC_START] = REG_FIELD(0x3a, 14, 14),
    [ADC_FULLSCALE] = REG_FIELD(0x3a, 13, 13),
    [EN_ADC_CMPIN] = REG_FIELD(0x3a, 7, 7),
    [EN_ADC_VBUS] = REG_FIELD(0x3a, 6, 6),
    [EN_ADC_PSYS] = REG_FIELD(0x3a, 5, 5),
    [EN_ADC_IIN] = REG_FIELD(0x3a, 4, 4),
    [EN_ADC_IDCHG] = REG_FIELD(0x3a, 3, 3),
    [EN_ADC_ICHG] = REG_FIELD(0x3a, 2, 2),
    [EN_ADC_VSYS] = REG_FIELD(0x3a, 1, 1),
    [EN_ADC_VBAT] = REG_FIELD(0x3a, 0, 0),

    /*REG06*/
    [OTG_VOLTAGE] = REG_FIELD(0x06, 2, 13),
    /*REG08*/
    [OTG_CURRENT] = REG_FIELD(0x08, 8, 14),
    /*REG0a*/
    [INPUT_VOLTAGE] = REG_FIELD(0x0a, 6, 13),
    /*REG0C*/
    [MIN_SYS_VOTAGE] = REG_FIELD(0x0c, 8, 13),
    /*REG0e*/
    [INPUT_CURRENT] = REG_FIELD(0x0e, 8, 14),

    /*REG2E*/
    [MANUFACTURE_ID] = REG_FIELD(0x2E, 0, 7),
    /*REF2F*/
    [DEVICE_ID] = REG_FIELD(0x2F, 0, 7),
};

/*
 * Most of the val -> idx conversions can be computed, given the minimum,
 * maximum and the step between values. For the rest of conversions, we use
 * lookup tables.
 */
enum sc888x_table_ids {
    /* range tables */
    TBL_ICHG,
    TBL_CHGMAX,
    TBL_INPUTVOL,
    TBL_INPUTCUR,
    TBL_SYSVMIN,
    TBL_OTGVOL,
    TBL_OTGCUR,
    TBL_EXTCON,
};

struct sc888x_range {
    u32 min;
    u32 max;
    u32 step;
};

struct sc888x_lookup {
    const u32 *tbl;
    u32 size;
};

static const union {
    struct sc888x_range  rt;
    struct sc888x_lookup lt;
} sc888x_tables[] = {
    /* range tables */
    [TBL_ICHG] =    { .rt = {0,      8128000, 64000} },
    /* uV */
    [TBL_CHGMAX] = { .rt = {0, 19200000, 8000} },
    /* uV max charge voltage*/
    [TBL_INPUTVOL] = { .rt = {3200000, 19520000, 64000} },
    /* uV  input charge voltage*/
    [TBL_INPUTCUR] = {.rt = {50000, 6350000, 50000} },
    /*uA input current*/
    [TBL_SYSVMIN] = { .rt = {160000, 16182000, 256000} },
    /* uV min system voltage*/
    [TBL_OTGVOL] = {.rt = {0, 20800000, 8000} },
    /*uV OTG volage*/
    [TBL_OTGCUR] = {.rt = {0, 6400000, 50000} },
};

static const struct regmap_range sc8885_readonly_reg_ranges[] = {
    regmap_reg_range(0x20, 0x26),
    regmap_reg_range(0xFE, 0xFF),
};

static const struct regmap_access_table sc8885_writeable_regs = {
    .no_ranges = sc8885_readonly_reg_ranges,
    .n_no_ranges = ARRAY_SIZE(sc8885_readonly_reg_ranges),
};

static const struct regmap_range sc8885_volatile_reg_ranges[] = {
    regmap_reg_range(0x12, 0x12),
    regmap_reg_range(0x14, 0x15),
    regmap_reg_range(0x20, 0x26),
    regmap_reg_range(0x30, 0x35),
    regmap_reg_range(0x3B, 0x3F),
    regmap_reg_range(0xFE, 0xFF),
};

static const struct regmap_access_table sc8885_volatile_regs = {
    .yes_ranges = sc8885_volatile_reg_ranges,
    .n_yes_ranges = ARRAY_SIZE(sc8885_volatile_reg_ranges),
};

static const struct regmap_config sc8885_regmap_config = {
    .reg_bits = 8,
    .val_bits = 16,

    .max_register = 0xFF,
    .cache_type = REGCACHE_RBTREE,

    .wr_table = &sc8885_writeable_regs,
    .volatile_table = &sc8885_volatile_regs,
    .val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static const struct regmap_range sc8886_readonly_reg_ranges[] = {
    regmap_reg_range(0x20, 0x2F),
};

static const struct regmap_access_table sc8886_writeable_regs = {
    .no_ranges = sc8886_readonly_reg_ranges,
    .n_no_ranges = ARRAY_SIZE(sc8886_readonly_reg_ranges),
};

static const struct regmap_range sc8886_volatile_reg_ranges[] = {
    regmap_reg_range(0x00, 0x0F),
    regmap_reg_range(0x20, 0x3B),
};

static const struct regmap_access_table sc8886_volatile_regs = {
    .yes_ranges = sc8886_volatile_reg_ranges,
    .n_yes_ranges = ARRAY_SIZE(sc8886_volatile_reg_ranges),
};

static const struct regmap_config sc8886_regmap_config = {
    .reg_bits = 8,
    .val_bits = 16,

    .max_register = 0x3B,
    .cache_type = REGCACHE_RBTREE,

    .wr_table = &sc8886_writeable_regs,
    .volatile_table = &sc8886_volatile_regs,
    .val_format_endian = REGMAP_ENDIAN_LITTLE,
};


static struct sc888x_device *sc8885_charger;

static int sc888x_field_read(struct sc888x_device *charger,
                  enum sc888x_fields field_id)
{
    int ret;
    int val;

    ret = regmap_field_read(charger->rmap_fields[field_id], &val);
    if (ret < 0)
        return ret;

    return val;
}

static int sc888x_field_write(struct sc888x_device *charger,
                   enum sc888x_fields field_id, unsigned int val)
{
    return regmap_field_write(charger->rmap_fields[field_id], val);
}

static int sc888x_get_chip_state(struct sc888x_device *charger,
                  struct sc8885_state *state)
{
    int i, ret;

    struct {
        enum sc888x_fields id;
        u8 *data;
    } state_fields[] = {
        {AC_STAT,    &state->ac_stat},
        {ICO_DONE,    &state->ico_done},
        {IN_VINDPM,    &state->in_vindpm},
        {IN_IINDPM, &state->in_iindpm},
        {IN_FCHRG,    &state->in_fchrg},
        {IN_PCHRG,    &state->in_pchrg},
        {IN_OTG,    &state->in_otg},
        {F_ACOV,    &state->fault_acov},
        {F_BATOC,    &state->fault_batoc},
        {F_ACOC,    &state->fault_acoc},
        {SYSOVP_STAT,    &state->sysovp_stat},
        {F_LATCHOFF,    &state->fault_latchoff},
        {F_OTG_OVP,    &state->fault_otg_ovp},
        {F_OTG_OCP,    &state->fault_otg_ocp},
    };

    for (i = 0; i < ARRAY_SIZE(state_fields); i++) {
        ret = sc888x_field_read(charger, state_fields[i].id);
        if (ret < 0)
            return ret;

        *state_fields[i].data = ret;
    }

    return 0;
}

static int sc8885_dump_regs(struct sc888x_device *charger, char *buf)
{
    u8 tmpbuf[500];
    int len;
    int idx = 0;
    int i;
    u32 val = 0;
    struct sc8885_state state;
    int ret = 0;

    ret = sc888x_field_write(charger, ADC_START, 1);
    if (ret < 0) {
        sc_err("error: ADC_START\n");
        return ret;
    }

    idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8885");
    regmap_read(charger->regmap, 0x12, &val);
    len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[0x12] = 0x%04x\n", val);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;
    regmap_read(charger->regmap, 0x14, &val);
    len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[0x14] = 0x%04x\n", val);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;
    regmap_read(charger->regmap, 0x15, &val);
    len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[0x15] = 0x%04x\n", val);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    for (i = 0x20; i < 0x26; i++) {
        regmap_read(charger->regmap, i, &val);
        len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[0x%02x] = 0x%04x\n", i, val);
        memcpy(&buf[idx], tmpbuf, len);
        idx += len;
    }

    for (i = 0x30; i < 0x35; i++) {
        regmap_read(charger->regmap, i, &val);
        len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[0x%02x] = 0x%04x\n", i, val);
        memcpy(&buf[idx], tmpbuf, len);
        idx += len;
    }

    for (i = 0x3b; i < 0x3f; i++) {
        regmap_read(charger->regmap, i, &val);
        len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[0x%02x] = 0x%04x\n", i, val);
        memcpy(&buf[idx], tmpbuf, len);
        idx += len;
    }

    regmap_read(charger->regmap, 0xfe, &val);
    len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[0xfe] = 0x%04x\n", val);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;
    regmap_read(charger->regmap, 0xff, &val);
    len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[0xff] = 0x%04x\n", val);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "battery charge current: %dmA\n", 
            sc888x_field_read(charger, OUTPUT_DSG_CUR) * 64);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "attery discharge current: %dmA\n", 
            sc888x_field_read(charger, OUTPUT_CHG_CUR) * 256);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "VSYS volatge: %dmV\n", 
            2880 + sc888x_field_read(charger, OUTPUT_SYS_VOL) * 64);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "BAT volatge: %dmV\n", 
            2880 + sc888x_field_read(charger, OUTPUT_BAT_VOL) * 64);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "SET CHARGE_CURRENT: %dmA\n", 
            sc888x_field_read(charger, CHARGE_CURRENT) * 64);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "MAX_CHARGE_VOLTAGE: %dmV\n", 
            sc888x_field_read(charger, MAX_CHARGE_VOLTAGE) * 8);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "INPUT_VOLTAGE: %dmV\n", 
            3200 + sc888x_field_read(charger, INPUT_VOLTAGE) * 64);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "INPUT_CURRENT: %dmA\n", 
            1024 + sc888x_field_read(charger, MIN_SYS_VOTAGE) * 256);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "MIN_SYS_VOTAGE: %dmV\n", 
            1024 + sc888x_field_read(charger, MIN_SYS_VOTAGE) * 256);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    sc888x_get_chip_state(charger, &state);
    len = snprintf(tmpbuf, PAGE_SIZE - idx, "status:\n");
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "AC_STAT:  %d\n", state.ac_stat);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "ICO_DONE:  %d\n", state.ico_done);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "IN_VINDPM:  %d\n", state.in_vindpm);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "IN_IINDPM:  %d\n", state.in_iindpm);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "IN_FCHRG:  %d\n", state.in_fchrg);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "IN_PCHRG:  %d\n", state.in_pchrg);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "IN_OTG:  %d\n", state.in_otg);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_ACOV:  %d\n", state.fault_acov);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_BATOC:  %d\n", state.fault_batoc);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_ACOC:  %d\n", state.fault_acoc);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "SYSOVP_STAT:  %d\n", state.sysovp_stat);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_LATCHOFF:  %d\n", state.fault_latchoff);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_OTGOVP:  %d\n", state.fault_otg_ovp);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_OTGOCP:  %d\n", state.fault_otg_ocp);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;


    return idx;
}

static int sc8886_dump_regs(struct sc888x_device *charger, char *buf)
{
    u8 tmpbuf[500];
    int len;
    int idx = 0;
    int i = 0;
    u32 val = 0;
    struct sc8885_state state;

    idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8886");

    for (i = 0; i < 0x10; i += 0x02) {
        regmap_read(charger->regmap, i, &val);
        len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[0x%02x] = 0x%04x\n", i, val);
        memcpy(&buf[idx], tmpbuf, len);
        idx += len;
    }
    for (i = 0x20; i < 0x3C; i += 0x02) {
        regmap_read(charger->regmap, i, &val);
        len = snprintf(tmpbuf, PAGE_SIZE - idx, "Reg[0x%02x] = 0x%04x\n", i, val);
        memcpy(&buf[idx], tmpbuf, len);
        idx += len;
    }

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "battery charge current: %dmA\n", 
            sc888x_field_read(charger, OUTPUT_DSG_CUR) * 64);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "battery discharge current: %dmA\n", 
            sc888x_field_read(charger, OUTPUT_CHG_CUR) * 256);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "VSYS volatge: %dmV\n", 
            2880 + sc888x_field_read(charger, OUTPUT_SYS_VOL) * 64);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "BAT volatge: %dmV\n", 
            2880 + sc888x_field_read(charger, OUTPUT_BAT_VOL) * 64);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "SET CHARGE_CURRENT: %dmA\n", 
            sc888x_field_read(charger, CHARGE_CURRENT) * 64);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "MAX_CHARGE_VOLTAGE: %dmV\n", 
            sc888x_field_read(charger, MAX_CHARGE_VOLTAGE) * 8);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "INPUT_VOLTAGE: %dmV\n", 
            3200 + sc888x_field_read(charger, INPUT_VOLTAGE) * 64);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "INPUT_CURRENT: %dmA\n", 
            sc888x_field_read(charger, INPUT_CURRENT) * 50);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "MIN_SYS_VOTAGE: %dmV\n", 
            1024 + sc888x_field_read(charger, MIN_SYS_VOTAGE) * 256);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    sc888x_get_chip_state(charger, &state);
    len = snprintf(tmpbuf, PAGE_SIZE - idx, "status:\n");
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "AC_STAT:  %d\n", state.ac_stat);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "ICO_DONE:  %d\n", state.ico_done);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "IN_VINDPM:  %d\n", state.in_vindpm);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "IN_IINDPM:  %d\n", state.in_iindpm);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "IN_FCHRG:  %d\n", state.in_fchrg);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "IN_PCHRG:  %d\n", state.in_pchrg);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "IN_OTG:  %d\n", state.in_otg);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_ACOV:  %d\n", state.fault_acov);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_BATOC:  %d\n", state.fault_batoc);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_ACOC:  %d\n", state.fault_acoc);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "SYSOVP_STAT:  %d\n", state.sysovp_stat);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_LATCHOFF:  %d\n", state.fault_latchoff);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_OTGOVP:  %d\n", state.fault_otg_ovp);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    len = snprintf(tmpbuf, PAGE_SIZE - idx, "F_OTGOCP:  %d\n", state.fault_otg_ocp);
    memcpy(&buf[idx], tmpbuf, len);
    idx += len;

    return idx;
}

static ssize_t sc8885_show_charge_info(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct sc888x_device *charger = dev_get_drvdata(dev);
    int ret = 0;

    if ((charger->chip_id & 0xff) == SC8885_ID) {
        ret = sc8885_dump_regs(charger, buf);
    } else if ((charger->chip_id & 0xff) == SC8886_ID) {
        ret = sc8886_dump_regs(charger, buf);
    }

    return ret;    
}

static DEVICE_ATTR(charge_info, 0440, sc8885_show_charge_info, NULL);

static void sc888x_init_sysfs(struct device *dev)
{
    device_create_file(dev, &dev_attr_charge_info);
}

static u32 sc888x_find_idx(struct sc888x_device *charger, u32 value, enum sc888x_table_ids id)
{
    int ret;
    u32 idx;
    u32 rtbl_size;
    const struct sc888x_range *rtbl = &sc888x_tables[id].rt;

    if (id == TBL_OTGVOL) {
        if (value <= 19520000) {
            ret = sc888x_field_write(charger, OTG_RANGE_LOW, 1);
            if (ret < 0)
                return ret;
        }
        else {
            ret = sc888x_field_write(charger, OTG_RANGE_LOW, 0);
            if (ret < 0)
                return ret;
            value = value - 1280000;
        }
    }

    rtbl_size = (rtbl->max - rtbl->min) / rtbl->step + 1;

    for (idx = 1;
         idx < rtbl_size && (idx * rtbl->step + rtbl->min <= value);
         idx++)
        ;

    return idx - 1;
}

void sc888x_charger_set_current(unsigned long event,
                 int current_value)
{
    int idx;

    if (!sc8885_charger) {
        pr_err("sc8885_charger is null\n");
        return;
    }
    switch (event) {
    case CHARGER_CURRENT_EVENT:
        idx = sc888x_find_idx(sc8885_charger, current_value, TBL_ICHG);
        sc888x_field_write(sc8885_charger, CHARGE_CURRENT, idx);
        break;

    case INPUT_CURRENT_EVENT:
        idx = sc888x_find_idx(sc8885_charger, current_value, TBL_INPUTCUR);
        sc888x_field_write(sc8885_charger, INPUT_CURRENT, idx);
        break;

    default:
        return;
    }
}

static int sc888x_fw_read_u32_props(struct sc888x_device *charger)
{
    int ret;
    u32 property;
    int i;
    struct sc8885_init_data *init = &charger->init_data;
    struct {
        char *name;
        bool optional;
        enum sc888x_table_ids tbl_id;
        u32 *conv_data; /* holds converted value from given property */
    } props[] = {
        /* required properties */
        {"sc,charge-current", false, TBL_ICHG, 
        &init->ichg},
        {"sc,max-charge-voltage", false, TBL_CHGMAX, 
        &init->max_chg_vol},   
        {"sc,min-input-voltage", false, TBL_INPUTVOL,
         &init->input_voltage},
        {"sc,max-input-current", false, TBL_INPUTCUR,
         &init->input_current},
        {"sc,input-current-sdp", false, TBL_INPUTCUR,
         &init->input_current_sdp},
        {"sc,input-current-dcp", false, TBL_INPUTCUR,
         &init->input_current_dcp},
        {"sc,input-current-cdp", false, TBL_INPUTCUR,
         &init->input_current_cdp},
        {"sc,minimum-sys-voltage", false, TBL_SYSVMIN,
         &init->sys_min_voltage},
        {"sc,otg-voltage", false, TBL_OTGVOL,
         &init->otg_voltage},
        {"sc,otg-current", false, TBL_OTGCUR,
         &init->otg_current},
    };
        

    /* initialize data for optional properties */
    for (i = 0; i < ARRAY_SIZE(props); i++) {
        ret = device_property_read_u32(charger->dev, props[i].name,
                           &property);
        if (ret < 0) {
            if (props[i].optional)
                continue;

            return ret;
        }

        if ((props[i].tbl_id == TBL_ICHG) &&
            (property > MAX_CHARGECURRETNT)) {
            dev_err(charger->dev, "sc,charge-current is error\n");
            return -ENODEV;
        }
        if ((props[i].tbl_id == TBL_CHGMAX) &&
            (property > MAX_CHARGEVOLTAGE)) {
            dev_err(charger->dev, "sc,max-charge-voltage is error\n");
            return -ENODEV;
        }
        if ((props[i].tbl_id == TBL_INPUTCUR) &&
            (property > MAX_INPUTCURRENT)) {
            dev_err(charger->dev, "sc,input-current is error\n");
            return -ENODEV;
        }
        if ((props[i].tbl_id == TBL_INPUTVOL) &&
            (property > MAX_INPUTVOLTAGE)) {
            dev_err(charger->dev, "sc,max-input-voltage is error\n");
            return -ENODEV;
        }
        if ((props[i].tbl_id == TBL_OTGVOL) &&
            (property > MAX_OTGVOLTAGE)) {
            dev_err(charger->dev, "sc,sc,otg-voltage is error\n");
            return -ENODEV;
        }
        if ((props[i].tbl_id == TBL_OTGCUR) &&
            (property > MAX_OTGCURRENT)) {
            dev_err(charger->dev, "sc,otg-current is error\n");
            return -ENODEV;
        }

        *props[i].conv_data = sc888x_find_idx(charger, property,
                               props[i].tbl_id);
        sc_info ("%s, val: %d, tbl_id =%d\n", props[i].name, property,
            *props[i].conv_data);
    }

    return 0;
}

static int sc888x_hw_init(struct sc888x_device *charger)
{
    int ret;
    int i;
    struct sc8885_state state;

    const struct {
        enum sc888x_fields id;
        u32 value;
    } init_data[] = {
        {CHARGE_CURRENT,        charger->init_data.ichg},
        {MAX_CHARGE_VOLTAGE,    charger->init_data.max_chg_vol},
        {MIN_SYS_VOTAGE,        charger->init_data.sys_min_voltage},
        {OTG_VOLTAGE,           charger->init_data.otg_voltage},
        {OTG_CURRENT,           charger->init_data.otg_current},
    };

    /* disable watchdog */
    ret = sc888x_field_write(charger, WDTWR_ADJ, 0);
    if (ret < 0)
        return ret;

    /* initialize currents/voltages and other parameters */
    for (i = 0; i < ARRAY_SIZE(init_data); i++) {
        ret = sc888x_field_write(charger, init_data[i].id,
                      init_data[i].value);
        if (ret < 0)
            return ret;
    }

    sc_info("CHARGE_CURRENT: %dmA\n",
        sc888x_field_read(charger, CHARGE_CURRENT) * 64);
    sc_info("MAX_CHARGE_VOLTAGE: %dmV\n",
        sc888x_field_read(charger, MAX_CHARGE_VOLTAGE) * 8);
    sc_info("INPUT_VOLTAGE: %dmV\n",
        3200 + sc888x_field_read(charger, INPUT_VOLTAGE) * 64);
    sc_info("INPUT_CURRENT: %dmA\n",
        sc888x_field_read(charger, INPUT_CURRENT) * 50);
    sc_info("MIN_SYS_VOTAGE: %dmV\n",
        160 + sc888x_field_read(charger, MIN_SYS_VOTAGE) * 256);
    
    ret = sc888x_field_read(charger, OTG_RANGE_LOW);
    if (ret) {
        sc_info("OTG_VOTAGE: %dmV\n",
            sc888x_field_read(charger, OTG_VOLTAGE) * 8);
    }
    else {
        sc_info("OTG_VOTAGE: %dmV\n",
            1280 + sc888x_field_read(charger, OTG_VOLTAGE) * 8);
    }
    /* Configure ADC for continuous conversions. This does not enable it. */

    ret = sc888x_field_write(charger, EN_LWPWR, 0);
    if (ret < 0) {
        sc_err("error: EN_LWPWR\n");
        return ret;
    }

    ret = sc888x_field_write(charger, ADC_CONV, 1);
    if (ret < 0) {
        sc_err("error: ADC_CONV\n");
        return ret;
    }

    ret = sc888x_field_write(charger, ADC_START, 1);
    if (ret < 0) {
        sc_err("error: ADC_START\n");
        return ret;
    }

    ret = sc888x_field_write(charger, ADC_FULLSCALE, 1);
    if (ret < 0) {
        sc_err("error: ADC_FULLSCALE\n");
        return ret;
    }

    ret = sc888x_field_write(charger, EN_ADC_CMPIN, 1);
    if (ret < 0) {
        sc_err("error: EN_ADC_CMPIN\n");
        return ret;
    }

    ret = sc888x_field_write(charger, EN_ADC_VBUS, 1);
    if (ret < 0) {
        sc_err("error: EN_ADC_VBUS\n");
        return ret;
    }

    ret = sc888x_field_write(charger, EN_ADC_PSYS, 1);
    if (ret < 0) {
        sc_err("error: EN_ADC_PSYS\n");
        return ret;
    }

    ret = sc888x_field_write(charger, EN_ADC_IIN, 1);
    if (ret < 0) {
        sc_err("error: EN_ADC_IIN\n");
        return ret;
    }

    ret = sc888x_field_write(charger, EN_ADC_IDCHG, 1);
    if (ret < 0) {
        sc_err("error: EN_ADC_IDCHG\n");
        return ret;
    }

    ret = sc888x_field_write(charger, EN_ADC_ICHG, 1);
    if (ret < 0) {
        sc_err("error: EN_ADC_ICHG\n");
        return ret;
    }

    ret = sc888x_field_write(charger, EN_ADC_VSYS, 1);
    if (ret < 0) {
        sc_err("error: EN_ADC_VSYS\n");
        return ret;
    }

    ret = sc888x_field_write(charger, EN_ADC_VBAT, 1);
    if (ret < 0) {
        sc_err("error: EN_ADC_VBAT\n");
        return ret;
    }

    sc888x_get_chip_state(charger, &state);
    charger->state = state;

    return 0;
}

static int sc888x_fw_probe(struct sc888x_device *charger)
{
    int ret;

    ret = sc888x_fw_read_u32_props(charger);
    if (ret < 0)
        return ret;

    return 0;
}

static void sc888x_enable_charger(struct sc888x_device *charger,
                   u32 input_current)
{    
    sc888x_field_write(charger, INPUT_CURRENT, input_current);
    // sc_info("%s input_current=%d\n", __func__, input_current);
    sc888x_field_write(charger, CHARGE_CURRENT, charger->init_data.ichg);
    // sc_info("%s charger->init_data.ichg=%d\n", __func__, charger->init_data.ichg);
}

static enum power_supply_property sc888x_power_supply_props[] = {
    POWER_SUPPLY_PROP_MANUFACTURER,
    POWER_SUPPLY_PROP_STATUS,
    POWER_SUPPLY_PROP_ONLINE,
    POWER_SUPPLY_PROP_HEALTH,
    POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
    POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
    POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
    POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
    POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
    POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
    POWER_SUPPLY_PROP_VOLTAGE_MAX,
    POWER_SUPPLY_PROP_CURRENT_MAX,
};

static int sc888x_power_supply_get_property(struct power_supply *psy,
                         enum power_supply_property psp,
                         union power_supply_propval *val)
{
    int ret;
    struct sc888x_device *bq = power_supply_get_drvdata(psy);
    struct sc8885_state state;

    state = bq->state;

    switch (psp) {
    case POWER_SUPPLY_PROP_STATUS:
        if (!state.ac_stat)
            val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
        else if (state.in_fchrg == 1 ||
             state.in_pchrg == 1)
            val->intval = POWER_SUPPLY_STATUS_CHARGING;
        else
            val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
        break;

    case POWER_SUPPLY_PROP_MANUFACTURER:
        val->strval = SC8885_MANUFACTURER;
        break;

    case POWER_SUPPLY_PROP_ONLINE:
        val->intval = state.ac_stat;
        break;

    case POWER_SUPPLY_PROP_HEALTH:
        if (!state.fault_acoc &&
            !state.fault_acov && !state.fault_batoc)
            val->intval = POWER_SUPPLY_HEALTH_GOOD;
        else if (state.fault_batoc)
            val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
        break;

    case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
        /* read measured value */
        ret = sc888x_field_read(bq, OUTPUT_CHG_CUR);
        if (ret < 0)
            return ret;

        /* converted_val = ADC_val * 64mA  */
        val->intval = ret * 64000;
        break;

    case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
        val->intval = sc888x_tables[TBL_ICHG].rt.max;
        break;

    case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
        if (!state.ac_stat) {
            val->intval = 0;
            break;
        }

        /* read measured value */
        ret = sc888x_field_read(bq, OUTPUT_BAT_VOL);
        if (ret < 0)
            return ret;

        /* converted_val = 2.88V + ADC_val * 64mV */
        val->intval = 2880000 + ret * 64000;
        break;

    case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
        val->intval = sc888x_tables[TBL_CHGMAX].rt.max;
        break;

    case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
        val->intval = sc888x_tables[TBL_INPUTVOL].rt.max;
        break;

    case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
        val->intval = sc888x_tables[TBL_INPUTCUR].rt.max;
        break;

    case POWER_SUPPLY_PROP_VOLTAGE_MAX:
        ret = sc888x_field_read(bq, MAX_CHARGE_VOLTAGE);
        val->intval = ret * 16;
        break;

    case POWER_SUPPLY_PROP_CURRENT_MAX:
        ret = sc888x_field_read(bq, CHARGE_CURRENT);
        val->intval = ret * 64;
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

static char *sc888x_charger_supplied_to[] = {
    "charger",
};

static const struct power_supply_desc sc888x_power_supply_desc = {
    .name = "sc888x-charger",
    .type = POWER_SUPPLY_TYPE_USB,
    .properties = sc888x_power_supply_props,
    .num_properties = ARRAY_SIZE(sc888x_power_supply_props),
    .get_property = sc888x_power_supply_get_property,
};

static int sc888x_power_supply_init(struct sc888x_device *charger)
{
    struct power_supply_config psy_cfg = { .drv_data = charger, };

    psy_cfg.supplied_to = sc888x_charger_supplied_to;
    psy_cfg.num_supplicants = ARRAY_SIZE(sc888x_charger_supplied_to);

    charger->supply_charger =
        power_supply_register(charger->dev,
                      &sc888x_power_supply_desc,
                      &psy_cfg);
    sc_info(" power_supply init successful\n");

    return PTR_ERR_OR_ZERO(charger->supply_charger);
}

static irqreturn_t sc8885_irq_handler_thread(int irq, void *private)
{
    struct sc888x_device *charger = private;
    int irq_flag;
    struct sc8885_state state;

    if (sc888x_field_read(charger, AC_STAT)) {
		sc888x_enable_charger(charger, charger->init_data.input_current_dcp);
        msleep(100);
        sc888x_field_write(charger, INPUT_VOLTAGE,
                charger->init_data.input_voltage);
        irq_flag = IRQF_TRIGGER_LOW;
    } else {
        irq_flag = IRQF_TRIGGER_HIGH;
        sc888x_field_write(charger, INPUT_CURRENT,
                    charger->init_data.input_current_sdp);
        sc888x_get_chip_state(charger, &state);
        charger->state = state;
        power_supply_changed(charger->supply_charger);
    }
    irq_set_irq_type(irq, irq_flag | IRQF_ONESHOT);

    return IRQ_HANDLED;
}

static int sc888x_irq_register(struct sc888x_device *charger)
{
	int ret;
	int irq_flag;
	struct device_node *node = charger->dev->of_node;
	
	if (!node) {
        sc_err("device tree node missing\n");
        return -EINVAL;
    }

	charger->irq_gpio = of_get_named_gpio(node, "sc,sc888x-intr-gpio", 0);
    if (!gpio_is_valid(charger->irq_gpio)) {
        sc_err("fail to valid gpio : %d\n", charger->irq_gpio);
        return -EINVAL;
    }

    ret = gpio_request_one(charger->irq_gpio, GPIOF_DIR_IN, "sc888x_irq");
    if (ret) {
        sc_err("fail to request sc888x irq\n");
        return EINVAL;
    }

    charger->irq = gpio_to_irq(charger->irq_gpio);
    if (charger->irq < 0) {
        sc_err("fail to gpio to irq\n");
        return EINVAL;
    }

	if (sc888x_field_read(charger, AC_STAT))
        irq_flag = IRQF_TRIGGER_LOW;
    else
        irq_flag = IRQF_TRIGGER_HIGH;

    

    ret = devm_request_threaded_irq(&charger->client->dev, charger->irq, NULL,
                    sc8885_irq_handler_thread,
                    irq_flag | IRQF_ONESHOT,
                    "sc888x_irq", charger);
    if (ret) {
		return EINVAL;
	}

    enable_irq_wake(charger->irq);

	return ret;
}

static int sc888x_probe(struct i2c_client *client,
             const struct i2c_device_id *id)
{
    struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
    struct device *dev = &client->dev;
    struct sc888x_device *charger;
    struct device_node *charger_np;
    int ret = 0;
    u32 i = 0;
    

    if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA))
        return -EIO;

    charger = devm_kzalloc(&client->dev, sizeof(*charger), GFP_KERNEL);
    if (!charger)
        return -EINVAL;
    
    charger->client = client;
    charger->dev = dev;

    charger_np = of_find_compatible_node(NULL, NULL, "sc,sc8885");
    if (charger_np) {
        charger->regmap = devm_regmap_init_i2c(client,
                               &sc8885_regmap_config);
        if (IS_ERR(charger->regmap)) {
            dev_err(&client->dev, "Failed to initialize regmap\n");
            return -EINVAL;
        }

        for (i = 0; i < ARRAY_SIZE(sc8885_reg_fields); i++) {
            const struct reg_field *reg_fields = sc8885_reg_fields;

            charger->rmap_fields[i] =
                devm_regmap_field_alloc(dev,
                            charger->regmap,
                            reg_fields[i]);
            if (IS_ERR(charger->rmap_fields[i])) {
                dev_err(dev, "cannot allocate regmap field\n");
                return PTR_ERR(charger->rmap_fields[i]);
            }
        }
    } else {
        charger->regmap = devm_regmap_init_i2c(client,
                               &sc8886_regmap_config);

        if (IS_ERR(charger->regmap)) {
            dev_err(&client->dev, "Failed to initialize regmap\n");
            return -EINVAL;
        }

        for (i = 0; i < ARRAY_SIZE(sc8886_reg_fields); i++) {
            const struct reg_field *reg_fields = sc8886_reg_fields;

            charger->rmap_fields[i] =
                devm_regmap_field_alloc(dev,
                            charger->regmap,
                            reg_fields[i]);
            if (IS_ERR(charger->rmap_fields[i])) {
                dev_err(dev, "cannot allocate regmap field\n");
                return PTR_ERR(charger->rmap_fields[i]);
            }
        }
    }
    i2c_set_clientdata(client, charger);

    /*read chip id. Confirm whether to support the chip*/
    charger->chip_id = sc888x_field_read(charger, DEVICE_ID);
    sc_info("charger->chip_id=%x\n", charger->chip_id);
    if (charger->chip_id < 0) {
        sc_err("Cannot read chip ID.\n");
        return charger->chip_id;
    }

    if (!dev->platform_data) {
        ret = sc888x_fw_probe(charger);
        if (ret < 0) {
            sc_err("Cannot read device properties.\n");
            return ret;
        }
    } else {
        return -ENODEV;
    }

    ret = sc888x_hw_init(charger);
    if (ret < 0) {
        sc_err("Cannot initialize the chip.\n");
        return ret;
    }

    sc888x_init_sysfs(&(charger->client->dev));

    ret = sc888x_power_supply_init(charger);
	if (ret) {
		goto err;
	}

	ret = sc888x_irq_register(charger);
	if (ret) {
		goto err;
	}

    device_init_wakeup(dev, 1);

    sc_info("------------sc888x init successful!-------------\n");

    return 0;
err:
	power_supply_unregister(charger->supply_charger);
	
	return ret;
}

void sc888x_shutdown(struct i2c_client *client)
{
#ifdef SC888X_SHUTDOWN_OPS
    int vol_idx;
    struct sc888x_device *charger = i2c_get_clientdata(client);

    vol_idx = sc888x_find_idx(charger, DEFAULT_INPUTVOL, TBL_INPUTVOL);
    sc888x_field_write(charger, INPUT_VOLTAGE, vol_idx);
    sc888x_field_write(charger, INPUT_CURRENT,
                charger->init_data.input_current_sdp);
    sc888x_field_write(charger, EN_LWPWR, 1);
#endif
}

static int sc888x_pm_suspend(struct device *dev)
{
    return 0;
}

static int sc888x_pm_resume(struct device *dev)
{
    return 0;
}

static SIMPLE_DEV_PM_OPS(sc888x_pm_ops, sc888x_pm_suspend, sc888x_pm_resume);

static const struct i2c_device_id sc888x_i2c_ids[] = {
    { "sc888x"},
    { },
};
MODULE_DEVICE_TABLE(i2c, sc888x_i2c_ids);

static const struct of_device_id sc888x_of_match[] = {
    { .compatible = "sc,sc8885", },
    { .compatible = "sc,sc8886", },
    { },
};
MODULE_DEVICE_TABLE(of, sc8885_of_match);

static struct i2c_driver sc888x_driver = {
    .probe			= sc888x_probe,
    .shutdown    	= sc888x_shutdown,
    .id_table    	= sc888x_i2c_ids,
    .driver = {
        .name      	= "sc888x-charger",
        .pm        	= &sc888x_pm_ops,
        .of_match_table	= of_match_ptr(sc888x_of_match),
    },
};

module_i2c_driver(sc888x_driver);

MODULE_DESCRIPTION("SC SC888X Charger Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aiden-yu@southchip.com");
