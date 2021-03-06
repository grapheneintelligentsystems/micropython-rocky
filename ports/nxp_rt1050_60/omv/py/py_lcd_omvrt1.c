/*
 * This file is part of the OpenMV project.
 * Copyright (c) 2013/2014 Ibrahim Abdelkader <i.abdalkader@gmail.com>
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * LCD Python module.
 *
 */
#include <mp.h>
#include <objstr.h>
#include <spi.h>
#include <systick.h>
#include "pin.h"
#include "imlib.h"
#include "fb_alloc.h"
#include "ff_wrapper.h"
#include "py_assert.h"
#include "py_helper.h"
#include "py_image.h"

extern const pin_obj_t pin_EMC_19;
#define RST_PINOBJ			pin_EMC_19
#define RST_PORT             (RST_PINOBJ.gpio)
#define RST_PIN              (RST_PINOBJ.pin)
#define RST_PIN_WRITE(bit)   HAL_GPIO_WritePin(RST_PORT, RST_PIN, bit);

extern const pin_obj_t pin_EMC_20;
#define RS_PINOBJ			pin_EMC_20
#define RS_PORT             (RS_PINOBJ.gpio)
#define RS_PIN              (RS_PINOBJ.pin)
#define RS_PIN_WRITE(bit)   HAL_GPIO_WritePin(RS_PORT, RS_PIN, bit);

extern const pin_obj_t pin_EMC_30;
#define CS_PINOBJ			pin_EMC_30
#define CS_PORT             (CS_PINOBJ.gpio)
#define CS_PIN              (CS_PINOBJ.pin)
#define CS_PIN_WRITE(bit)   HAL_GPIO_WritePin(CS_PORT, CS_PIN, bit);

extern const pin_obj_t pin_AD_B0_13;
#define LED_PINOBJ			pin_AD_B0_13
#define LED_PORT            (LED_PINOBJ.gpio)
#define LED_PIN             (LED_PINOBJ.pin)
#define LED_PIN_WRITE(bit)  HAL_GPIO_WritePin(LED_PORT, LED_PIN, bit);

extern mp_obj_t pyb_spi_send(mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args);
extern mp_obj_t pyb_spi_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args);
extern mp_obj_t pyb_spi_deinit(mp_obj_t self_in);

static mp_obj_t s_spiPort = NULL;
static int s_width = 0;
static int height = 0;
static enum { LCD_NONE, LCD_SHIELD } type = LCD_NONE;
static bool s_backlight_init = false;

__WEAK void* fb_alloc0(uint32_t size) {
	return m_malloc0(size);
}

// Send out 8-bit data using the SPI object.
static void lcd_write_command_byte(uint8_t data_byte)
{
    mp_map_t arg_map;
    arg_map.all_keys_are_qstrs = true;
    arg_map.is_fixed = true;
    arg_map.is_ordered = true;
    arg_map.used = 0;
    arg_map.alloc = 0;
    arg_map.table = NULL;
	
    CS_PIN_WRITE(false);
    RS_PIN_WRITE(false); // command
    pyb_spi_send(
        2, (mp_obj_t []) {
            s_spiPort,
            mp_obj_new_int(data_byte)
        },
        &arg_map
    );
    CS_PIN_WRITE(true);
}

// Send out 8-bit data using the SPI object.
static void lcd_write_data_byte(uint8_t data_byte)
{
    mp_map_t arg_map;
    arg_map.all_keys_are_qstrs = true;
    arg_map.is_fixed = true;
    arg_map.is_ordered = true;
    arg_map.used = 0;
    arg_map.alloc = 0;
    arg_map.table = NULL;
	strlen((char []){1,2,3,3,0});
    CS_PIN_WRITE(false);
    RS_PIN_WRITE(true); // data
    pyb_spi_send(
        2, (mp_obj_t []) {
            s_spiPort,
            mp_obj_new_int(data_byte)
        },
        &arg_map
    );
    CS_PIN_WRITE(true);
}

// Send out 8-bit data using the SPI object.
static void lcd_write_command(uint8_t data_byte, uint32_t len, uint8_t *dat)
{
    lcd_write_command_byte(data_byte);
    for (uint32_t i=0; i<len; i++) lcd_write_data_byte(dat[i]);
}

// Send out 8-bit data using the SPI object.
static void lcd_write_data(uint32_t len, uint8_t *dat)
{
    mp_obj_str_t arg_str;
    arg_str.base.type = &mp_type_bytes;
    arg_str.hash = 0;
    arg_str.len = len;
    arg_str.data = dat;

    mp_map_t arg_map;
    arg_map.all_keys_are_qstrs = true;
    arg_map.is_fixed = true;
    arg_map.is_ordered = true;
    arg_map.used = 0;
    arg_map.alloc = 0;
    arg_map.table = NULL;

    CS_PIN_WRITE(false);
    RS_PIN_WRITE(true); // data
    pyb_spi_send(
        2, (mp_obj_t []) {
            s_spiPort,
            &arg_str
        },
        &arg_map
    );
    CS_PIN_WRITE(true);
}

static mp_obj_t py_lcd_deinit()
{
    switch (type) {
        case LCD_NONE:
            return mp_const_none;
        case LCD_SHIELD:
            HAL_GPIO_DeInit(RST_PORT, RST_PIN);
            HAL_GPIO_DeInit(RS_PORT, RS_PIN);
            HAL_GPIO_DeInit(CS_PORT, CS_PIN);
            pyb_spi_deinit(s_spiPort);
            s_spiPort = NULL;
            s_width = 0;
            height = 0;
            type = LCD_NONE;
            if (s_backlight_init) {
                HAL_GPIO_DeInit(LED_PORT, LED_PIN);
                s_backlight_init = false;
            }
            return mp_const_none;
    }
    return mp_const_none;
}

static mp_obj_t py_lcd_init(uint n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
    py_lcd_deinit();
    switch (py_helper_keyword_int(n_args, args, 0, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_type), LCD_SHIELD)) {
        case LCD_NONE:
            return mp_const_none;
        case LCD_SHIELD:
        {
			mp_hal_ConfigGPIO(&CS_PINOBJ, GPIO_MODE_OUTPUT_PP, 1);
			mp_hal_ConfigGPIO(&RST_PINOBJ, GPIO_MODE_OUTPUT_PP, 1);
			mp_hal_ConfigGPIO(&RS_PINOBJ, GPIO_MODE_OUTPUT_PP, 1);

            s_spiPort = pyb_spi_make_new(NULL,
                2, // n_args
                3, // n_kw
                (mp_obj_t []) {
                    MP_OBJ_NEW_SMALL_INT(1), // SPI Port
					MP_OBJ_NEW_SMALL_INT(0), // SPI mode
					MP_OBJ_NEW_QSTR(MP_QSTR_baudrate),
                    MP_OBJ_NEW_SMALL_INT(1000000000/67), // SPI speed
                    MP_OBJ_NEW_QSTR(MP_QSTR_polarity),
                    MP_OBJ_NEW_SMALL_INT(0),
                    MP_OBJ_NEW_QSTR(MP_QSTR_phase),
                    MP_OBJ_NEW_SMALL_INT(0)
                }
            );
            s_width = 128;
            height = 160;
            type = LCD_SHIELD;
            s_backlight_init = false;

            RST_PIN_WRITE(false);
            systick_sleep(100);
            RST_PIN_WRITE(true);
            systick_sleep(100);
            lcd_write_command_byte(0x11); // Sleep Exit
            systick_sleep(120);

            // Memory Data Access Control
            lcd_write_command(0x36, 1, (uint8_t []) {0xC0});

            // Interface Pixel Format
            lcd_write_command(0x3A, 1, (uint8_t []) {0x05});

            // Display on
            lcd_write_command_byte(0x29);

            return mp_const_none;
        }
    }
    return mp_const_none;
}

static mp_obj_t py_lcd_width()
{
    if (type == LCD_NONE) return mp_const_none;
    return mp_obj_new_int(s_width);
}

static mp_obj_t py_lcd_height()
{
    if (type == LCD_NONE) return mp_const_none;
    return mp_obj_new_int(height);
}

static mp_obj_t py_lcd_type()
{
    if (type == LCD_NONE) return mp_const_none;
    return mp_obj_new_int(type);
}

static mp_obj_t py_lcd_set_backlight(mp_obj_t state_obj)
{
    switch (type) {
        case LCD_NONE:
            return mp_const_none;
        case LCD_SHIELD:
        {
            bool bit = !!mp_obj_get_int(state_obj);
            if (!s_backlight_init) {
				mp_hal_ConfigGPIO(&LED_PINOBJ, GPIO_MODE_OUTPUT_OD_PUP, 1);
                s_backlight_init = true;
            }
            LED_PIN_WRITE(bit);
            return mp_const_none;
        }
    }
    return mp_const_none;
}

static mp_obj_t py_lcd_get_backlight()
{
    switch (type) {
        case LCD_NONE:
            return mp_const_none;
        case LCD_SHIELD:
            if (!s_backlight_init) {
                return mp_const_none;
            }
            return mp_obj_new_int(HAL_GPIO_ReadPin(LED_PORT, LED_PIN));
    }
    return mp_const_none;
}

#if defined(OMV_MPY_ONLY)
	// just for debugging with a small project for much shorter flashing time
static mp_obj_t py_lcd_display(uint n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
	static uint8_t ls_pix;
    rectangle_t rect;
	rect.w = s_width , rect.h = height , rect.x = 0 , rect.y = 0;
    // Fit X.
    int l_pad = 0, r_pad = 0;
    if (rect.w > s_width) {
        int adjust = rect.w - s_width;
        rect.w -= adjust;
        rect.x += adjust / 2;
    } else if (rect.w < s_width) {
        int adjust = s_width - rect.w;
        l_pad = adjust / 2;
        r_pad = (adjust + 1) / 2;
    }

    // Fit Y.
    int t_pad = 0, b_pad = 0;
    if (rect.h > height) {
        int adjust = rect.h - height;
        rect.h -= adjust;
        rect.y += adjust / 2;
    } else if (rect.h < height) {
        int adjust = height - rect.h;
        t_pad = adjust / 2;
        b_pad = (adjust + 1) / 2;
    }

    switch (type) {
        case LCD_NONE:
            return mp_const_none;
        case LCD_SHIELD:
            lcd_write_command_byte(0x2C);
            uint8_t *zero = fb_alloc0(s_width*2);
            uint16_t *line = fb_alloc(s_width*2);
            for (int i=0; i<t_pad; i++) {
                lcd_write_data(s_width*2, zero);
            }
            for (int i=0; i<rect.h; i++) {
                if (l_pad) {
                    lcd_write_data(l_pad*2, zero); // l_pad < s_width
                }
                
                for (int j=0; j<rect.w; j++) {
                    uint8_t pixel = ls_pix++;
                    line[j] = pixel;
                }
                lcd_write_data(rect.w*2, (uint8_t *) line);
                 
				
                if (r_pad) {
                    lcd_write_data(r_pad*2, zero); // r_pad < s_width
                }
            }
            for (int i=0; i<b_pad; i++) {
                lcd_write_data(s_width*2, zero);
            }
            fb_free();
            fb_free();
            return mp_const_none;
    }
    return mp_const_none;
}
#else
static mp_obj_t py_lcd_display(uint n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
    image_t *arg_img = py_image_cobj(args[0]);
    PY_ASSERT_TRUE_MSG(IM_IS_MUTABLE(arg_img), "Image format is not supported.");

    rectangle_t rect;
    py_helper_keyword_rectangle_roi(arg_img, n_args, args, 1, kw_args, &rect);

    // Fit X.
    int l_pad = 0, r_pad = 0;
    if (rect.w > s_width) {
        int adjust = rect.w - s_width;
        rect.w -= adjust;
        rect.x += adjust / 2;
    } else if (rect.w < s_width) {
        int adjust = s_width - rect.w;
        l_pad = adjust / 2;
        r_pad = (adjust + 1) / 2;
    }

    // Fit Y.
    int t_pad = 0, b_pad = 0;
    if (rect.h > height) {
        int adjust = rect.h - height;
        rect.h -= adjust;
        rect.y += adjust / 2;
    } else if (rect.h < height) {
        int adjust = height - rect.h;
        t_pad = adjust / 2;
        b_pad = (adjust + 1) / 2;
    }

    switch (type) {
        case LCD_NONE:
            return mp_const_none;
        case LCD_SHIELD:
            lcd_write_command_byte(0x2C);
            uint8_t *zero = fb_alloc0(s_width*2);
            uint16_t *line = fb_alloc(s_width*2);
            for (int i=0; i<t_pad; i++) {
                lcd_write_data(s_width*2, zero);
            }
            for (int i=0; i<rect.h; i++) {
                if (l_pad) {
                    lcd_write_data(l_pad*2, zero); // l_pad < s_width
                }
                if (IM_IS_GS(arg_img)) {
                    for (int j=0; j<rect.w; j++) {
                        uint8_t pixel = IM_GET_GS_PIXEL(arg_img, (rect.x + j), (rect.y + i));
                        line[j] = IM_RGB565(IM_R825(pixel),IM_G826(pixel),IM_B825(pixel));
                    }
                    lcd_write_data(rect.w*2, (uint8_t *) line);
                } else {
                    lcd_write_data(rect.w*2, (uint8_t *)
                        (((uint16_t *) arg_img->pixels) +
                        ((rect.y + i) * arg_img->w) + rect.x));
                }
                if (r_pad) {
                    lcd_write_data(r_pad*2, zero); // r_pad < s_width
                }
            }
            for (int i=0; i<b_pad; i++) {
                lcd_write_data(s_width*2, zero);
            }
            fb_free();
            fb_free();
            return mp_const_none;
    }
    return mp_const_none;
}
\
#endif

static mp_obj_t py_lcd_clear()
{
    switch (type) {
        case LCD_NONE:
            return mp_const_none;
        case LCD_SHIELD:
            lcd_write_command_byte(0x2C);
            uint8_t* zero = fb_alloc0(s_width*2);
            for (int i=0; i<height; i++) {
                lcd_write_data(s_width*2, zero);
            }
            fb_free();
            return mp_const_none;
    }
    return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_KW(py_lcd_init_obj, 0, py_lcd_init);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_lcd_deinit_obj, py_lcd_deinit);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_lcd_width_obj, py_lcd_width);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_lcd_height_obj, py_lcd_height);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_lcd_type_obj, py_lcd_type);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_lcd_set_backlight_obj, py_lcd_set_backlight);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_lcd_get_backlight_obj, py_lcd_get_backlight);
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(py_lcd_display_obj, 1, py_lcd_display);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_lcd_clear_obj, py_lcd_clear);
static const mp_map_elem_t globals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__),        MP_OBJ_NEW_QSTR(MP_QSTR_lcd) },
    { MP_OBJ_NEW_QSTR(MP_QSTR_init),            (mp_obj_t)&py_lcd_init_obj          },
    { MP_OBJ_NEW_QSTR(MP_QSTR_deinit),          (mp_obj_t)&py_lcd_deinit_obj        },
    { MP_OBJ_NEW_QSTR(MP_QSTR_width),           (mp_obj_t)&py_lcd_width_obj         },
    { MP_OBJ_NEW_QSTR(MP_QSTR_height),          (mp_obj_t)&py_lcd_height_obj        },
    { MP_OBJ_NEW_QSTR(MP_QSTR_type),            (mp_obj_t)&py_lcd_type_obj          },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_backlight),   (mp_obj_t)&py_lcd_set_backlight_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_backlight),   (mp_obj_t)&py_lcd_get_backlight_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_display),         (mp_obj_t)&py_lcd_display_obj       },
    { MP_OBJ_NEW_QSTR(MP_QSTR_clear),           (mp_obj_t)&py_lcd_clear_obj         },
    { NULL, NULL },
};
STATIC MP_DEFINE_CONST_DICT(globals_dict, globals_dict_table);

const mp_obj_module_t lcd_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_t)&globals_dict,
};

void py_lcd_init0()
{
    py_lcd_deinit();
}
