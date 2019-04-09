/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <string.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include "os/mynewt.h"
#include <hal/hal_i2c.h>
#include <hal/hal_gpio.h>
#include <mcu/nrf52_hal.h>
#include "nrf_twim.h"

#include <nrf.h>

#define NRF52_HAL_I2C_MAX (2)

#define NRF52_SCL_PIN_CONF                                              \
    ((GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos) |          \
      (GPIO_PIN_CNF_DRIVE_S0D1    << GPIO_PIN_CNF_DRIVE_Pos) |          \
      (GPIO_PIN_CNF_PULL_Pullup   << GPIO_PIN_CNF_PULL_Pos) |           \
      (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) |          \
      (GPIO_PIN_CNF_DIR_Input     << GPIO_PIN_CNF_DIR_Pos))
#define NRF52_SDA_PIN_CONF NRF52_SCL_PIN_CONF

#define NRF52_SCL_PIN_CONF_CLR                                  \
     ((GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos) | \
      (GPIO_PIN_CNF_DRIVE_S0D1     << GPIO_PIN_CNF_DRIVE_Pos) | \
      (GPIO_PIN_CNF_PULL_Pullup    << GPIO_PIN_CNF_PULL_Pos)  | \
      (GPIO_PIN_CNF_INPUT_Connect  << GPIO_PIN_CNF_INPUT_Pos) | \
      (GPIO_PIN_CNF_DIR_Output     << GPIO_PIN_CNF_DIR_Pos))
#define NRF52_SDA_PIN_CONF_CLR    NRF52_SCL_PIN_CONF_CLR

struct nrf52_hal_i2c {
    NRF_TWI_Type *nhi_regs;
};

#if MYNEWT_VAL(I2C_0)
struct nrf52_hal_i2c hal_twi_i2c0 = {
    .nhi_regs = NRF_TWI0
};
#endif
#if MYNEWT_VAL(I2C_1)
struct nrf52_hal_i2c hal_twi_i2c1 = {
    .nhi_regs = NRF_TWI1
};
#endif

static struct nrf52_hal_i2c *nrf52_hal_i2cs[NRF52_HAL_I2C_MAX] = {
#if MYNEWT_VAL(I2C_0)
    &hal_twi_i2c0,
#else
    NULL,
#endif
#if MYNEWT_VAL(I2C_1)
    &hal_twi_i2c1
#else
    NULL
#endif
};

static void
hal_i2c_delay_us(uint32_t number_of_us)
{
register uint32_t delay __ASM ("r0") = number_of_us;
__ASM volatile (
#ifdef NRF51
        ".syntax unified\n"
#endif
    "1:\n"
    " SUBS %0, %0, #1\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
#ifdef NRF52
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
    " NOP\n"
#endif
    " BNE 1b\n"
#ifdef NRF51
    ".syntax divided\n"
#endif
    : "+r" (delay));
}

static int
hal_i2c_resolve(uint8_t i2c_num, struct nrf52_hal_i2c **out_i2c)
{
    if (i2c_num >= NRF52_HAL_I2C_MAX) {
        *out_i2c = NULL;
        return HAL_I2C_ERR_INVAL;
    }

    *out_i2c = nrf52_hal_i2cs[i2c_num];
    if (*out_i2c == NULL) {
        return HAL_I2C_ERR_INVAL;
    }

    return 0;
}

/**
 * Converts an nRF SDK I2C status to a HAL I2C error code.
 */
static int
hal_i2c_convert_status(int nrf_status)
{
    if (nrf_status == 0) {
        return 0;
    } else if (nrf_status & NRF_TWIM_ERROR_DATA_NACK) {
        return HAL_I2C_ERR_DATA_NACK;
    } else if (nrf_status & NRF_TWIM_ERROR_ADDRESS_NACK) {
        return HAL_I2C_ERR_ADDR_NACK;
    } else {
        return HAL_I2C_ERR_UNKNOWN;
    }
}

/**
 * Reads the input buffer of the specified pin regardless
 * of if it is set as output or input
 */
static int
read_gpio_inbuffer(int pin)
{
    NRF_GPIO_Type *port;
    port = HAL_GPIO_PORT(pin);

    return (port->IN >> HAL_GPIO_INDEX(pin)) & 1UL;
}

/*
 * Clear the bus after reset by clocking 9 bits manually.
 * This should reset state from (most of) the devices on the other end.
 */
static void
hal_i2c_clear_bus(int scl_pin, int sda_pin)
{
    int i;
    NRF_GPIO_Type *scl_port, *sda_port;
    /* Resolve which GPIO port these pins belong to */
    scl_port = HAL_GPIO_PORT(scl_pin);
    sda_port = HAL_GPIO_PORT(sda_pin);

    /* Input connected, standard-low disconnected-high, pull-ups */
    scl_port->PIN_CNF[scl_pin] = NRF52_SCL_PIN_CONF;
    sda_port->PIN_CNF[sda_pin] = NRF52_SDA_PIN_CONF;

    hal_gpio_write(scl_pin, 1);
    hal_gpio_write(sda_pin, 1);

    scl_port->PIN_CNF[scl_pin] = NRF52_SCL_PIN_CONF_CLR;
    sda_port->PIN_CNF[sda_pin] = NRF52_SDA_PIN_CONF_CLR;

    hal_i2c_delay_us(4);

    for (i = 0; i < 9; i++) {
        if (read_gpio_inbuffer(sda_pin)) {
            if (i == 0) {
                /*
                 * Nothing to do here.
                 */
                goto ret;
            } else {
                break;
            }
        }
        hal_gpio_write(scl_pin, 0);
        hal_i2c_delay_us(4);
        hal_gpio_write(scl_pin, 1);
        hal_i2c_delay_us(4);
    }

    /*
     * Send STOP.
     */
    hal_gpio_write(sda_pin, 0);
    hal_i2c_delay_us(4);
    hal_gpio_write(sda_pin, 1);

ret:
    /* Restore GPIO config */
    scl_port->PIN_CNF[scl_pin] = NRF52_SCL_PIN_CONF;
    sda_port->PIN_CNF[sda_pin] = NRF52_SDA_PIN_CONF;
}

int
hal_i2c_init(uint8_t i2c_num, void *usercfg)
{
    struct nrf52_hal_i2c *i2c;
    NRF_TWI_Type *regs;
    struct nrf52_hal_i2c_cfg *cfg;
    uint32_t freq;
    int rc;
    NRF_GPIO_Type *scl_port, *sda_port;

    assert(usercfg != NULL);

    rc = hal_i2c_resolve(i2c_num, &i2c);
    if (rc != 0) {
        goto err;
    }

    cfg = (struct nrf52_hal_i2c_cfg *) usercfg;
    regs = i2c->nhi_regs;

    switch (cfg->i2c_frequency) {
    case 100:
        freq = TWI_FREQUENCY_FREQUENCY_K100;
        break;
    case 250:
        freq = TWI_FREQUENCY_FREQUENCY_K250;
        break;
    case 400:
        freq = TWI_FREQUENCY_FREQUENCY_K400;
        break;
    default:
        rc = HAL_I2C_ERR_INVAL;
        goto err;
    }

    hal_i2c_clear_bus(cfg->scl_pin, cfg->sda_pin);

    /* Resolve which GPIO port these pins belong to */
    scl_port = HAL_GPIO_PORT(cfg->scl_pin);
    sda_port = HAL_GPIO_PORT(cfg->sda_pin);

    sda_port->PIN_CNF[HAL_GPIO_INDEX(cfg->sda_pin)] = NRF52_SDA_PIN_CONF;
    scl_port->PIN_CNF[HAL_GPIO_INDEX(cfg->scl_pin)] = NRF52_SCL_PIN_CONF;


    regs->PSELSCL = cfg->scl_pin;
    regs->PSELSDA = cfg->sda_pin;
    regs->FREQUENCY = freq;
    regs->ENABLE = TWI_ENABLE_ENABLE_Enabled;

    return (0);
err:
    return (rc);
}

int
hal_i2c_master_write(uint8_t i2c_num, struct hal_i2c_master_data *pdata,
                     uint32_t timo, uint8_t last_op)
{
    struct nrf52_hal_i2c *i2c;
    NRF_TWI_Type *regs;
    int nrf_status;
    int rc;
    int i;
    uint32_t start;

    rc = hal_i2c_resolve(i2c_num, &i2c);
    if (rc != 0) {
        return rc;
    }
    regs = i2c->nhi_regs;

    regs->ADDRESS = pdata->address;

    regs->EVENTS_ERROR = 0;
    regs->EVENTS_STOPPED = 0;
    regs->EVENTS_SUSPENDED = 0;
    regs->SHORTS = 0;

    regs->TASKS_STARTTX = 1;
    regs->TASKS_RESUME = 1;

    start = os_time_get();
    for (i = 0; i < pdata->len; i++) {
        regs->EVENTS_TXDSENT = 0;
        regs->TXD = pdata->buffer[i];
        while (!regs->EVENTS_TXDSENT && !regs->EVENTS_ERROR) {
            if (os_time_get() - start > timo) {
                rc = HAL_I2C_ERR_TIMEOUT;
                goto err;
            }
        }
        if (regs->EVENTS_ERROR) {
            goto err;
        }
    }
    /* If last_op is zero it means we dont put a stop at end. */
    if (last_op) {
        regs->EVENTS_STOPPED = 0;
        regs->TASKS_STOP = 1;
        while (!regs->EVENTS_STOPPED && !regs->EVENTS_ERROR) {
            if (os_time_get() - start > timo) {
                rc = HAL_I2C_ERR_TIMEOUT;
                goto err;
            }
        }
        if (regs->EVENTS_ERROR) {
            goto err;
        }
    }

    rc = 0;

err:
    // regs->TASKS_STOP = 1;

    if (regs->EVENTS_ERROR) {
        nrf_status = regs->ERRORSRC;
        regs->ERRORSRC = nrf_status;
        rc = hal_i2c_convert_status(nrf_status);
    } else if (rc == HAL_I2C_ERR_TIMEOUT) {
        /* Some I2C slave peripherals cause a glitch on the bus when they
         * reset which puts the TWI in an unresponsive state.  Disabling and
         * re-enabling the TWI returns it to normal operation.
         * A clear operation is performed in case one of the devices on
         * the bus is in a bad state.
         */
        regs->ENABLE = TWI_ENABLE_ENABLE_Disabled;
        hal_i2c_clear_bus(regs->PSELSCL, regs->PSELSDA);
        regs->ENABLE = TWI_ENABLE_ENABLE_Enabled;
    }

    return (rc);
}

int
hal_i2c_master_read(uint8_t i2c_num, struct hal_i2c_master_data *pdata,
                    uint32_t timo, uint8_t last_op)
{
    struct nrf52_hal_i2c *i2c;
    NRF_TWI_Type *regs;
    int nrf_status;
    int rc;
    int i;
    uint32_t start;

    rc = hal_i2c_resolve(i2c_num, &i2c);
    if (rc != 0) {
        return rc;
    }
    regs = i2c->nhi_regs;

    start = os_time_get();

    if (regs->EVENTS_RXDREADY) {
        /*
         * If previous read was interrupted, flush RXD.
         */
        (void)regs->RXD;
        (void)regs->RXD;
    }
    regs->EVENTS_ERROR = 0;
    regs->EVENTS_STOPPED = 0;
    regs->EVENTS_SUSPENDED = 0;
    regs->EVENTS_RXDREADY = 0;

    regs->ADDRESS = pdata->address;

    if (pdata->len == 1 && last_op) {
        regs->SHORTS = TWI_SHORTS_BB_STOP_Msk;
    } else {
        regs->SHORTS = TWI_SHORTS_BB_SUSPEND_Msk;
    }
    regs->TASKS_STARTRX = 1;

    for (i = 0; i < pdata->len; i++) {
        regs->TASKS_RESUME = 1;
        while (!regs->EVENTS_RXDREADY && !regs->EVENTS_ERROR) {
            if (os_time_get() - start > timo) {
                rc = HAL_I2C_ERR_TIMEOUT;
                goto err;
            }
        }
        if (regs->EVENTS_ERROR) {
            goto err;
        }
        pdata->buffer[i] = regs->RXD;
        if (i == pdata->len - 2) {
            if (last_op) {
                regs->SHORTS = TWI_SHORTS_BB_STOP_Msk;
            }
        }
        regs->EVENTS_RXDREADY = 0;
    }

    return (0);

err:
    regs->TASKS_STOP = 1;
    regs->SHORTS = TWI_SHORTS_BB_STOP_Msk;

    if (regs->EVENTS_ERROR) {
        nrf_status = regs->ERRORSRC;
        regs->ERRORSRC = nrf_status;
        rc = hal_i2c_convert_status(nrf_status);
    } else if (rc == HAL_I2C_ERR_TIMEOUT) {
       /* Some I2C slave peripherals cause a glitch on the bus when they
        * reset which puts the TWI in an unresponsive state.  Disabling and
        * re-enabling the TWI returns it to normal operation.
        * A clear operation is performed in case one of the devices on
        * the bus is in a bad state.
        */
        regs->ENABLE = TWI_ENABLE_ENABLE_Disabled;
        hal_i2c_clear_bus(regs->PSELSCL, regs->PSELSDA);
        regs->ENABLE = TWI_ENABLE_ENABLE_Enabled;
    }

    return (rc);
}

int
hal_i2c_master_probe(uint8_t i2c_num, uint8_t address, uint32_t timo)
{
    struct hal_i2c_master_data rx;
    uint8_t buf;

    rx.address = address;
    rx.buffer = &buf;
    rx.len = 1;

    return hal_i2c_master_read(i2c_num, &rx, timo, 1);
}
