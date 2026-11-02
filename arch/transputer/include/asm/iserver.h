/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer IServer output helpers
 *
 * These functions transmit characters/strings over Link 0 using the
 * IServer protocol (TEMU host services).
 */

#ifndef __ASM_TRANSPUTER_ISERVER_H
#define __ASM_TRANSPUTER_ISERVER_H

void iserver_putc(char c);
void iserver_puts(const char *str);

#endif /* __ASM_TRANSPUTER_ISERVER_H */
