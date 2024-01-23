/*
 * (C) Copyright 2021-2022 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#ifndef __TOYBRICK_FREQ__
#define __TOYBRICK_FREQ__

#include <linux/string.h>
#include <common.h>
#include <dm.h>
#include <errno.h>
#include <i2c.h>
#include <stdio.h>
#include <stdlib.h>
#include <debug_uart.h>
#include <serial.h>
#include <dm/device-internal.h>
#include <dm/uclass-internal.h>

#define TOYBRICK_FREQ_CONFIG_NODE "/d0-freq-info"
//7bits-addr
#define UART_INDEX_NAME "uart_index"
#define I2C_BUSNUM_PROP_NAME "i2c_bus"
#define I2C_CHIP_ADDR_PROP_NAME "i2c_addr"

#define CLUSTER0_FREQ_LIMITS_LOW_PROP_NAME "cluster0_freq_limits_low"
#define CLUSTER1_FREQ_LIMITS_LOW_PROP_NAME "cluster1_freq_limits_low"
#define CLUSTER2_FREQ_LIMITS_LOW_PROP_NAME "cluster2_freq_limits_low"
#define GPU_FREQ_LIMITS_LOW_PROP_NAME 	   "gpu_freq_limits_low"

#define CLUSTER0_FREQ_LIMITS_HIGH_PROP_NAME "cluster0_freq_limits_high"
#define CLUSTER1_FREQ_LIMITS_HIGH_PROP_NAME "cluster1_freq_limits_high"
#define CLUSTER2_FREQ_LIMITS_HIGH_PROP_NAME "cluster2_freq_limits_high"
#define GPU_FREQ_LIMITS_HIGH_PROP_NAME 	   "gpu_freq_limits_high"

#define NPU_FREQ_LIMITS_PROP_NAME	   "npu_freq_limit"

#define msleep(a)   udelay(a * 1000)
#define UART_INTERVAL_MS  1200

#define REG_AC_STAT			0x20

enum {
    LOW_PERFORMANCE = 1,
    HIGH_PERFORMANCE,
    FULL_PERFORMANCE,
};

#define LOW_MODE "low"
#define HIGH_MODE "high"
#define FULL_MODE "full"

struct limit_table
{
	char **item_array;
	int item_num;
};

static struct limit_table cluster0_freq_limits_low;
static struct limit_table cluster1_freq_limits_low;
static struct limit_table cluster2_freq_limits_low;
static struct limit_table gpu_freq_limits_low;

static struct limit_table cluster0_freq_limits_high;
static struct limit_table cluster1_freq_limits_high;
static struct limit_table cluster2_freq_limits_high;
static struct limit_table gpu_freq_limits_high;

static inline int toybrick_detect_i2c(int bus_num, unsigned int addr)
{
	struct udevice *bus, *dev;
	int ret;
	u16 val;

	ret = uclass_get_device_by_seq(UCLASS_I2C, bus_num, &bus);
	if (ret) {
		printf("** Cannot find I2C bus %d: err=%d\n", bus_num, ret);
		return ret;
	}

	if (!bus) {
        printf("** No I2C bus %d\n", bus_num);
		return -ENOENT;
	}

    ret = dm_i2c_probe(bus, addr, 0, &dev);
    if (ret) {
		printf("** dm_i2c_probe bus %d: err=%d\n", bus_num, ret);
		return ret;
	}

	ret = dm_i2c_read(dev, REG_AC_STAT, (u8 *)&val, 2);
	if (ret) {
		printf("** READ AC_STAT (0x%02x) fail\n",
		      REG_AC_STAT);
		return ret;
	} else {
		printf("## AC_STAT: %d\n", (val & 1 << 15) ? 1 : 0);
		ret = (val & 1 << 15) ? 0 : -EINVAL;
	}

	return ret;
}

static inline int toybrick_detect_uart(int uart_index)
{
	struct udevice *dev;
	int ret = 0;
    int i = 0;
    unsigned char rxbuf[4] = {0};

	ret = uclass_get_device_by_seq(UCLASS_SERIAL, uart_index, &dev);
	if (ret) {
		printf("** Cannot find uart%d: err=%d\n", uart_index, ret);
		goto end;
	}

    serial_dev_setbrg(dev, 115200);

    serial_dev_clear(dev);
    msleep(UART_INTERVAL_MS);

    while (serial_dev_tstc(dev) && i < 4) {
        rxbuf[i] = serial_dev_getc(dev);
        i++;
    }

    if (rxbuf[0] == 0x2c) {
        printf("## Run PD Protocol!\n");
        ret = 0;
    } else {
        printf("## No PD Protocol Run!\n");
        ret = -EPROTO;
    }

end:
	return ret;
}


static inline int toybrick_del_freq_node(void *fdt_blob, char *freq_limit, char *table_header)
{
	int  nodeoffset;        /* node offset from libfdt */
	int  err = 0;
	int len;
	int depth;
	const char *tmp;
	char node_name[64];

	sprintf(node_name, "/%s/%s", table_header, freq_limit);
	/*
	* Get the path.  The root node is an oddball, the offset
	* is zero and has no name.
	*/
	nodeoffset = fdt_path_offset (fdt_blob, node_name);
	if (nodeoffset < 0) {
		/*
		* Not found or something else bad happened.
		*/
		printf ("libfdt fdt_path_offset() returned %s:%s\n",
				node_name, fdt_strerror(nodeoffset));
		return 1;
	}

	int nextoffset;
	int curoffset = nodeoffset;
	int count = 0;
	for (;;)
	{
		tmp = fdt_get_name(fdt_blob, nodeoffset, &len);
		if (tmp == NULL) {
			break;
		}

		if (strncmp(tmp,"opp-",3) != 0) {
			break;
		}

		debug("## del curoffset:%d,/%s/%s\n",nodeoffset,table_header,tmp);
		err = fdt_del_node(fdt_blob, nodeoffset);
		if (err < 0) {
			printf("libfdt fdt_del_node():  %s\n", fdt_strerror(err));
			return err;
		}

		count += 1;
		nextoffset = fdt_next_node(fdt_blob, curoffset, &depth);
		curoffset = nextoffset;
	}

	return 0;
}

static inline int toybrick_parse_config_npu_freq_limit(char *npu_freq_limit)
{
	int  nodeoffset;        /* node offset from libfdt */
	int len;
	const char *tmp;

	nodeoffset = fdt_path_offset (gd->ufdt_blob, TOYBRICK_FREQ_CONFIG_NODE);
	if (nodeoffset < 0) {
		printf ("** ufdt_blob fdt_path_offset() returned %s:%s\n", TOYBRICK_FREQ_CONFIG_NODE, fdt_strerror(nodeoffset));
		return -1;
	}

	tmp = fdt_getprop(gd->ufdt_blob, nodeoffset, NPU_FREQ_LIMITS_PROP_NAME, &len);
	if (tmp == NULL) {
		printf ("parse NPU_FREQ_LIMITS_PROP_NAME failed\n");
		return -1;
	}

	memcpy(npu_freq_limit, tmp, len);

	debug("## npu_freq_limit:%s\n", npu_freq_limit);
	return 0;
}

static inline int toybrick_get_freq_limits_by_prop_name(void *fdt_blob, const char *prop_name, struct limit_table *table)
{
	int  nodeoffset;        /* node offset from libfdt */
	int len = 0;
	const char *prop_item;
	int count, i, ret = 0;

	nodeoffset = fdt_path_offset (gd->ufdt_blob, TOYBRICK_FREQ_CONFIG_NODE);
	if (nodeoffset < 0) {
		printf ("** ufdt_blob fdt_path_offset() returned %s:%s\n", TOYBRICK_FREQ_CONFIG_NODE, fdt_strerror(nodeoffset));
		return -1;
	}

	count = fdt_stringlist_count(gd->ufdt_blob, nodeoffset, prop_name);
	if (count <= 0) {
		printf("** get %s count fail returned %d\n", prop_name, count);
		return -1;
	}
	table->item_num = count;

	table->item_array = (char **)malloc(sizeof(char)*count);
	if (table->item_array  == NULL) {
		printf("** malloc freq_limits_table fail\n");
		return -1;
	}
	for(i = 0; i < count; i++) {
		prop_item = fdt_stringlist_get(gd->ufdt_blob, nodeoffset, prop_name, i, &len);
		if (!strcmp(prop_item, "")) {
			printf("skip property: %s.\n", prop_name);
			table->item_num = 0;
			table->item_array = NULL;
			break;
		}
		if (len <= 0) {
			printf("** fdt_stringlist_get @ %dth fail\n", i);
			ret = -1;
			break;
		}
		table->item_array[i] = (char *)malloc(len * sizeof(char));
		strncpy(table->item_array[i], prop_item, len);
		debug("## prop<%s>[%d] : %s\n", prop_name, i, table->item_array[i]);
	}

	return ret;
}

static inline int toybrick_parse_freq_config(void *fdt_blob, char *npu_freq_limit, int *i2c_bus, unsigned int *i2c_addr, int *uart_index)
{
	int  nodeoffset;        /* node offset from libfdt */
	int len, ret = 0;
	const char *tmp;

	nodeoffset = fdt_path_offset (gd->ufdt_blob, TOYBRICK_FREQ_CONFIG_NODE);
	if (nodeoffset < 0) {
		printf ("** ufdt_blob fdt_path_offset() returned %s:%s\n", TOYBRICK_FREQ_CONFIG_NODE, fdt_strerror(nodeoffset));
		return -1;
	}

	ret = toybrick_get_freq_limits_by_prop_name(fdt_blob, CLUSTER0_FREQ_LIMITS_LOW_PROP_NAME, &cluster0_freq_limits_low);
	if (ret) {
		printf("** get %s prop fail\n", CLUSTER0_FREQ_LIMITS_LOW_PROP_NAME);
		return -1;
	}

	ret = toybrick_get_freq_limits_by_prop_name(fdt_blob, CLUSTER1_FREQ_LIMITS_LOW_PROP_NAME, &cluster1_freq_limits_low);
	if (ret) {
		printf("** get %s prop fail\n", CLUSTER1_FREQ_LIMITS_LOW_PROP_NAME);
		return -1;
	}

	ret = toybrick_get_freq_limits_by_prop_name(fdt_blob, CLUSTER2_FREQ_LIMITS_LOW_PROP_NAME, &cluster2_freq_limits_low);
	if (ret) {
		printf("** get %s prop fail\n", CLUSTER2_FREQ_LIMITS_LOW_PROP_NAME);
		return -1;
	}

	ret = toybrick_get_freq_limits_by_prop_name(fdt_blob, GPU_FREQ_LIMITS_LOW_PROP_NAME, &gpu_freq_limits_low);
	if (ret) {
		printf("** get %s prop fail\n", GPU_FREQ_LIMITS_LOW_PROP_NAME);
		return -1;
	}

	ret = toybrick_get_freq_limits_by_prop_name(fdt_blob, CLUSTER0_FREQ_LIMITS_HIGH_PROP_NAME, &cluster0_freq_limits_high);
	if (ret) {
		printf("** get %s prop fail\n", CLUSTER0_FREQ_LIMITS_HIGH_PROP_NAME);
		return -1;
	}

	ret = toybrick_get_freq_limits_by_prop_name(fdt_blob, CLUSTER1_FREQ_LIMITS_HIGH_PROP_NAME, &cluster1_freq_limits_high);
	if (ret) {
		printf("** get %s prop fail\n", CLUSTER1_FREQ_LIMITS_HIGH_PROP_NAME);
		return -1;
	}

	ret = toybrick_get_freq_limits_by_prop_name(fdt_blob, CLUSTER2_FREQ_LIMITS_HIGH_PROP_NAME, &cluster2_freq_limits_high);
	if (ret) {
		printf("** get %s prop fail\n", CLUSTER2_FREQ_LIMITS_HIGH_PROP_NAME);
		return -1;
	}

	ret = toybrick_get_freq_limits_by_prop_name(fdt_blob, GPU_FREQ_LIMITS_HIGH_PROP_NAME, &gpu_freq_limits_high);
	if (ret) {
		printf("** get %s prop fail\n", GPU_FREQ_LIMITS_HIGH_PROP_NAME);
		return -1;
	}


	tmp = fdt_getprop(gd->ufdt_blob, nodeoffset, NPU_FREQ_LIMITS_PROP_NAME, &len);
	if (tmp == NULL) {
		printf ("parse NPU_FREQ_LIMITS_PROP_NAME failed\n");
		return -1;
	}

	memcpy(npu_freq_limit, tmp, len);

	tmp = fdt_getprop(gd->ufdt_blob, nodeoffset, I2C_BUSNUM_PROP_NAME, &len);
	if (tmp == NULL) {
		printf ("parse I2C_BUSNUM_PROP_NAME failed\n");
		return -1;
	}
	*i2c_bus = atoi(tmp);
	tmp = NULL;

	*i2c_addr = fdtdec_get_uint(gd->ufdt_blob, nodeoffset, I2C_CHIP_ADDR_PROP_NAME,0);
	if (*i2c_addr < 0) {
		printf ("parse I2C_CHIP_ADDR_PROP_NAME failed\n");
		return -1;
	}

	*uart_index = fdtdec_get_int(gd->ufdt_blob, nodeoffset, UART_INDEX_NAME,0);
	debug("## npu_freq_limit:%s, i2c_bus:%d, i2c_addr:0x%x, uart_index:%d\n", npu_freq_limit, *i2c_bus, *i2c_addr, *uart_index);

	return 0;
}

/*
 * low : cluster0,1,2 => 1.2G && gpu => 500M && npu => 900M
 * high: cluster1,2 => 1.2G && gpu => 500M && npu => 900M
 * full: no limits
 */
static int run_mode(void *fdt_blob, int mode)
{
    int i;

    if (mode == FULL_PERFORMANCE)
        return 0;

    if (mode == HIGH_PERFORMANCE) {
		// high performance limits cluster0 => 1.6G cluster4,6 => 1.8G gpu => 0.8G
        for (i = 0; i < cluster0_freq_limits_high.item_num; i++) {
			toybrick_del_freq_node(fdt_blob, cluster0_freq_limits_high.item_array[i], "cluster0-opp-table");
		}
		for (i = 0; i < cluster1_freq_limits_high.item_num; i++) {
			toybrick_del_freq_node(fdt_blob, cluster1_freq_limits_high.item_array[i], "cluster1-opp-table");
		}
		for (i = 0; i < cluster2_freq_limits_high.item_num; i++) {
			toybrick_del_freq_node(fdt_blob, cluster2_freq_limits_high.item_array[i], "cluster2-opp-table");
		}
		for (i = 0; i < gpu_freq_limits_high.item_num; i++) {
			toybrick_del_freq_node(fdt_blob, gpu_freq_limits_high.item_array[i], "gpu-opp-table");
		}
	} else {
		// low performance limits cluster0 => 1.2G cluster4,6 => 1.6G gpu => 0.6G
		for (i = 0; i < cluster0_freq_limits_low.item_num; i++) {
			toybrick_del_freq_node(fdt_blob, cluster0_freq_limits_low.item_array[i], "cluster0-opp-table");
		}
		for (i = 0; i < cluster1_freq_limits_low.item_num; i++) {
			toybrick_del_freq_node(fdt_blob, cluster1_freq_limits_low.item_array[i], "cluster1-opp-table");
		}
		for (i = 0; i < cluster2_freq_limits_low.item_num; i++) {
			toybrick_del_freq_node(fdt_blob, cluster2_freq_limits_low.item_array[i], "cluster2-opp-table");
		}
		for (i = 0; i < gpu_freq_limits_low.item_num; i++) {
			toybrick_del_freq_node(fdt_blob, gpu_freq_limits_low.item_array[i], "gpu-opp-table");
		}
	}

	return 0;
}

static int cmdline_add_run_mode(const char *mode_str)
{
    char *cmdline = env_get("bootargs");
    const char *env_val;
    char *buf;
    char mode_buf[32];

    if (cmdline && (cmdline[0] != '\0')) {
        buf = malloc(strlen(cmdline) + 1 + sizeof(mode_buf) + 1);
		if (!buf) {
			debug("%s: out of memory\n", __func__);
			return -ENOMEM;
		}

        sprintf(buf, "%s performance_mode=%s", cmdline, mode_str);
        env_val = buf;
    } else {
        buf = NULL;
        env_val = "";
    }

    env_set("bootargs", env_val);
    debug("## new cmdline: %s\n", env_val);

    free(buf);
    return 0;
}

static inline int toybrick_freq_convert_policy(void *fdt_blob)
{
	int ret = 0;
	char npu_freq_limit[64];
	int i2c_bus;
	int uart_index;
	unsigned int  i2c_addr;
	int performance_mode = 0;

	ret = toybrick_parse_freq_config(fdt_blob, npu_freq_limit, &i2c_bus, &i2c_addr, &uart_index);
	if (ret < 0) {
		/*
		* Not found or something else bad happened.
		*/
		printf ("** toybrick_parse_freq_config failed\n");
		return -EINVAL;
	}

    if (toybrick_detect_i2c(i2c_bus, i2c_addr) != 0) {
		if(toybrick_detect_uart(uart_index) != 0) {
			performance_mode = LOW_PERFORMANCE;
		} else {
			performance_mode = HIGH_PERFORMANCE;
		}
		//toybrick_del_freq_node(fdt_blob, npu_freq_limit, "npu-opp-table");
	} else {
		performance_mode = FULL_PERFORMANCE;
	}

	return performance_mode;
}

static inline int toybrick_freq_convert_policy_setmode(void *fdt_blob, int mode)
{
	int ret = 0;
	char npu_freq_limit[64];

	ret = toybrick_parse_config_npu_freq_limit(npu_freq_limit);
	if (ret < 0) {
		/*
		* Not found or something else bad happened.
		*/
		printf ("** toybrick_parse_npu_freq_limit failed\n");
		return -EINVAL;
	}

	switch(mode) {
		case LOW_PERFORMANCE:
			run_mode(fdt_blob, LOW_PERFORMANCE);
			cmdline_add_run_mode(LOW_MODE);
            toybrick_del_freq_node(fdt_blob, npu_freq_limit, "npu-opp-table");
			printf("## Run Low Performance\n");
			break;
		case HIGH_PERFORMANCE:
            run_mode(fdt_blob, HIGH_PERFORMANCE);
            cmdline_add_run_mode(HIGH_MODE);
            toybrick_del_freq_node(fdt_blob, npu_freq_limit, "npu-opp-table");
            printf("## Run High Performance\n");
			break;
		case FULL_PERFORMANCE:
			run_mode(fdt_blob, FULL_PERFORMANCE);
			cmdline_add_run_mode(FULL_MODE);
			printf("## Run Full Performance\n");
			break;
		default:
			printf("Unknown Performance Mode\n");
	}

	return 0;
}
#endif /* _TOYBRICK_BOARD_ */
