// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Southchip Semiconductor Technology(Shanghai) Co., Ltd.
 */
#ifndef __CHARGER_SC888X_H_
#define __CHARGER_SC888X_H_

#define CHARGER_CURRENT_EVENT	0x01
#define INPUT_CURRENT_EVENT		0x02

void sc8885_charger_set_current(unsigned long event, int current_value);

#endif /* __CHARGER_SC888X_H_ */
