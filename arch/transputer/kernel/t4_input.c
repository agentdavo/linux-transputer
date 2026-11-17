/*
 * t4_input.c — T4 emulator keyboard and mouse input driver
 *
 * Polls two memory-mapped UARTs for input data:
 *   UART1 (0x11000000) — Keyboard: VT100 byte stream
 *   UART2 (0x12000000) — Mouse: 3-byte serial packets
 *
 * Registers as Linux input devices (/dev/input/event0, event1).
 * Polling is driven by a kernel timer (no interrupts on transputer).
 *
 * Keyboard UART sends VT100 escape sequences for special keys
 * and raw ASCII for printable characters.
 *
 * Mouse UART sends 3-byte packets:
 *   Byte 0: 0x80 | buttons (bit0=left, bit1=middle, bit2=right)
 *   Byte 1: dx (signed 8-bit)
 *   Byte 2: dy (signed 8-bit)
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/io.h>

/* UART register layout (same as console UART) */
#define KBD_UART_DATA       0x11000000
#define KBD_UART_STATUS     0x11000004
#define MOUSE_UART_DATA     0x12000000
#define MOUSE_UART_STATUS   0x12000004
#define UART_RXAVAIL        0x02

/* Poll interval: ~16ms (60Hz) */
#define POLL_INTERVAL       (HZ / 60)

static struct input_dev *kbd_dev;
static struct input_dev *mouse_dev;
static struct timer_list poll_timer;

/* -----------------------------------------------------------------------
 * MMIO access
 * ----------------------------------------------------------------------- */

static inline unsigned int uart_read(unsigned int addr)
{
    return *(volatile unsigned int *)addr;
}

static inline int uart_rx_ready(unsigned int status_addr)
{
    return uart_read(status_addr) & UART_RXAVAIL;
}

static inline unsigned char uart_getc(unsigned int data_addr)
{
    return (unsigned char)(uart_read(data_addr) & 0xFF);
}

/* -----------------------------------------------------------------------
 * VT100 → Linux keycode translation
 * ----------------------------------------------------------------------- */

/* Map ASCII to Linux KEY_* codes */
static unsigned int ascii_to_keycode(int ch)
{
    if (ch >= 'a' && ch <= 'z') return KEY_A + (ch - 'a');
    if (ch >= 'A' && ch <= 'Z') return KEY_A + (ch - 'A');
    if (ch >= '1' && ch <= '9') return KEY_1 + (ch - '1');
    if (ch == '0') return KEY_0;
    if (ch == ' ') return KEY_SPACE;
    if (ch == '-') return KEY_MINUS;
    if (ch == '=') return KEY_EQUAL;
    if (ch == '[') return KEY_LEFTBRACE;
    if (ch == ']') return KEY_RIGHTBRACE;
    if (ch == '\\') return KEY_BACKSLASH;
    if (ch == ';') return KEY_SEMICOLON;
    if (ch == '\'') return KEY_APOSTROPHE;
    if (ch == '`') return KEY_GRAVE;
    if (ch == ',') return KEY_COMMA;
    if (ch == '.') return KEY_DOT;
    if (ch == '/') return KEY_SLASH;
    return 0;
}

static void kbd_send_key(unsigned int code)
{
    if (!code) return;
    input_report_key(kbd_dev, code, 1);  /* press */
    input_sync(kbd_dev);
    input_report_key(kbd_dev, code, 0);  /* release */
    input_sync(kbd_dev);
}

static void process_kbd_uart(void)
{
    int limit = 32;  /* max bytes per poll to avoid stalling */

    while (uart_rx_ready(KBD_UART_STATUS) && limit-- > 0) {
        unsigned char ch = uart_getc(KBD_UART_DATA);

        if (ch == 0x1B) {
            /* ESC sequence */
            if (!uart_rx_ready(KBD_UART_STATUS)) {
                kbd_send_key(KEY_ESC);
                continue;
            }
            unsigned char ch2 = uart_getc(KBD_UART_DATA);
            if (ch2 == '[') {
                if (!uart_rx_ready(KBD_UART_STATUS)) continue;
                unsigned char ch3 = uart_getc(KBD_UART_DATA);
                switch (ch3) {
                case 'A': kbd_send_key(KEY_UP); continue;
                case 'B': kbd_send_key(KEY_DOWN); continue;
                case 'C': kbd_send_key(KEY_RIGHT); continue;
                case 'D': kbd_send_key(KEY_LEFT); continue;
                case 'H': kbd_send_key(KEY_HOME); continue;
                case 'F': kbd_send_key(KEY_END); continue;
                default:
                    /* ESC [ N ~ or ESC [ NN ~ */
                    if (ch3 >= '1' && ch3 <= '9') {
                        if (!uart_rx_ready(KBD_UART_STATUS)) continue;
                        unsigned char ch4 = uart_getc(KBD_UART_DATA);
                        if (ch4 == '~') {
                            if (ch3 == '2') kbd_send_key(KEY_INSERT);
                            else if (ch3 == '3') kbd_send_key(KEY_DELETE);
                            else if (ch3 == '5') kbd_send_key(KEY_PAGEUP);
                            else if (ch3 == '6') kbd_send_key(KEY_PAGEDOWN);
                            continue;
                        }
                        if (!uart_rx_ready(KBD_UART_STATUS)) continue;
                        uart_getc(KBD_UART_DATA); /* consume '~' */
                        int code = (ch3 - '0') * 10 + (ch4 - '0');
                        switch (code) {
                        case 15: kbd_send_key(KEY_F5); break;
                        case 17: kbd_send_key(KEY_F6); break;
                        case 18: kbd_send_key(KEY_F7); break;
                        case 19: kbd_send_key(KEY_F8); break;
                        case 20: kbd_send_key(KEY_F9); break;
                        case 21: kbd_send_key(KEY_F10); break;
                        case 23: kbd_send_key(KEY_F11); break;
                        case 24: kbd_send_key(KEY_F12); break;
                        }
                    }
                    continue;
                }
            } else if (ch2 == 'O') {
                if (!uart_rx_ready(KBD_UART_STATUS)) continue;
                unsigned char ch3 = uart_getc(KBD_UART_DATA);
                switch (ch3) {
                case 'P': kbd_send_key(KEY_F1); break;
                case 'Q': kbd_send_key(KEY_F2); break;
                case 'R': kbd_send_key(KEY_F3); break;
                case 'S': kbd_send_key(KEY_F4); break;
                }
            } else {
                kbd_send_key(KEY_ESC);
            }
            continue;
        }

        /* Control characters */
        if (ch == 0x0D) { kbd_send_key(KEY_ENTER); continue; }
        if (ch == 0x09) { kbd_send_key(KEY_TAB); continue; }
        if (ch == 0x7F) { kbd_send_key(KEY_BACKSPACE); continue; }
        if (ch == 0x03) { kbd_send_key(KEY_C); continue; } /* Ctrl+C */

        /* Printable */
        {
            unsigned int kc = ascii_to_keycode(ch);
            if (kc) kbd_send_key(kc);
        }
    }
}

/* -----------------------------------------------------------------------
 * Mouse: 3-byte serial protocol → input events
 * ----------------------------------------------------------------------- */

static void process_mouse_uart(void)
{
    int limit = 16;

    while (uart_rx_ready(MOUSE_UART_STATUS) && limit-- > 0) {
        unsigned char b0 = uart_getc(MOUSE_UART_DATA);

        /* Sync byte must have bit 7 set */
        if (!(b0 & 0x80)) continue;

        /* Need 2 more bytes */
        if (!uart_rx_ready(MOUSE_UART_STATUS)) break;
        signed char dx = (signed char)uart_getc(MOUSE_UART_DATA);

        if (!uart_rx_ready(MOUSE_UART_STATUS)) break;
        signed char dy = (signed char)uart_getc(MOUSE_UART_DATA);

        int buttons = b0 & 0x07;

        input_report_key(mouse_dev, BTN_LEFT,   buttons & 1);
        input_report_key(mouse_dev, BTN_MIDDLE, (buttons >> 1) & 1);
        input_report_key(mouse_dev, BTN_RIGHT,  (buttons >> 2) & 1);
        input_report_rel(mouse_dev, REL_X, dx);
        input_report_rel(mouse_dev, REL_Y, dy);
        input_sync(mouse_dev);
    }
}

/* -----------------------------------------------------------------------
 * Timer callback — polls both UARTs
 * ----------------------------------------------------------------------- */

static void t4_input_poll(struct timer_list *timer)
{
    process_kbd_uart();
    process_mouse_uart();
    mod_timer(&poll_timer, jiffies + POLL_INTERVAL);
}

/* -----------------------------------------------------------------------
 * Init / Exit
 * ----------------------------------------------------------------------- */

static int __init t4_input_init(void)
{
    int err;
    int i;

    /* Allocate keyboard input device */
    kbd_dev = input_allocate_device();
    if (!kbd_dev)
        return -ENOMEM;

    kbd_dev->name = "T4 Keyboard (UART1)";
    kbd_dev->phys = "t4/uart1";
    kbd_dev->id.bustype = BUS_HOST;
    kbd_dev->id.vendor  = 0x0001;
    kbd_dev->id.product = 0x0001;
    kbd_dev->id.version = 0x0100;

    set_bit(EV_KEY, kbd_dev->evbit);
    /* Register all possible keycodes */
    for (i = 0; i < KEY_CNT; i++)
        set_bit(i, kbd_dev->keybit);

    err = input_register_device(kbd_dev);
    if (err) {
        input_free_device(kbd_dev);
        return err;
    }

    /* Allocate mouse input device */
    mouse_dev = input_allocate_device();
    if (!mouse_dev) {
        input_unregister_device(kbd_dev);
        return -ENOMEM;
    }

    mouse_dev->name = "T4 Mouse (UART2)";
    mouse_dev->phys = "t4/uart2";
    mouse_dev->id.bustype = BUS_HOST;
    mouse_dev->id.vendor  = 0x0001;
    mouse_dev->id.product = 0x0002;
    mouse_dev->id.version = 0x0100;

    set_bit(EV_KEY, mouse_dev->evbit);
    set_bit(EV_REL, mouse_dev->evbit);
    set_bit(BTN_LEFT, mouse_dev->keybit);
    set_bit(BTN_MIDDLE, mouse_dev->keybit);
    set_bit(BTN_RIGHT, mouse_dev->keybit);
    set_bit(REL_X, mouse_dev->relbit);
    set_bit(REL_Y, mouse_dev->relbit);

    err = input_register_device(mouse_dev);
    if (err) {
        input_free_device(mouse_dev);
        input_unregister_device(kbd_dev);
        return err;
    }

    /* Start polling timer */
    timer_setup(&poll_timer, t4_input_poll, 0);
    poll_timer.expires = jiffies + POLL_INTERVAL;
    add_timer(&poll_timer);

    printk("t4_input: keyboard (UART1 @ 0x%x) and mouse (UART2 @ 0x%x) registered\n",
           KBD_UART_DATA, MOUSE_UART_DATA);

    return 0;
}

device_initcall(t4_input_init);
