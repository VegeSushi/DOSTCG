#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include "serial.h"

#define COM1_BASE 0x3F8
#define COM2_BASE 0x2F8
#define NET_LOG_FILE "NETLOG.TXT"

/* BIOS tick counter at 0000:0040:006C, incremented ~18.2 times/second */
#define BIOS_TICKS_PER_SEC 18L

static unsigned com_base(int port) {
    return (port == 2) ? COM2_BASE : COM1_BASE;
}

unsigned long get_ticks(void) {
    unsigned long far* clk;
    clk = (unsigned long far*)MK_FP(0x0040, 0x006C);
    return *clk;
}

void net_log(const char* msg) {
    FILE* f;
    unsigned long t;

    f = fopen(NET_LOG_FILE, "a");
    if (!f) return;
    t = get_ticks();
    fprintf(f, "[tick %5lu] %s\n", t, msg);
    fclose(f);
}

/* Standard 8250/16550 register offsets from base:
   +0 RBR/THR (DLAB=0), +1 IER (DLAB=0), +2 IIR/FCR, +3 LCR, +4 MCR, +5 LSR, +6 MSR */
int serial_init(int port, unsigned long baud) {
    unsigned base;
    unsigned long divisor;
    char buf[80];
    unsigned char lsr, msr;

    base = com_base(port);
    if (baud == 0) baud = 9600;
    divisor = 115200UL / baud;

    outp(base + 1, 0x00);                          /* disable UART interrupts */
    outp(base + 3, 0x80);                          /* set DLAB to program baud divisor */
    outp(base + 0, (unsigned char)(divisor & 0xFF));
    outp(base + 1, (unsigned char)((divisor >> 8) & 0xFF));
    outp(base + 3, 0x03);                          /* 8 data bits, 1 stop bit, no parity, DLAB off */
    outp(base + 2, 0xC7);                          /* enable + clear 16550 FIFOs, 14-byte trigger */
    outp(base + 4, 0x0B);                          /* DTR + RTS + OUT2 on (raise the line) */

    lsr = inp(base + 5);
    msr = inp(base + 6);
    sprintf(buf, "serial_init: port=%d base=0x%X baud=%lu divisor=%lu LSR=0x%02X MSR=0x%02X",
            port, base, baud, divisor, lsr, msr);
    net_log(buf);
    /* MSR bit4 (0x10) = CTS, bit5 (0x20) = DSR. If both are 0 here, DOSBox-X's
       nullmodem link almost certainly isn't actually connected to the other side yet. */
    if (!(msr & 0x30)) {
        net_log("serial_init: WARNING - CTS/DSR both low, link may not be up");
    }

    return 1;
}

void serial_close(int port) {
    unsigned base;
    char buf[48];
    base = com_base(port);
    outp(base + 4, 0x00);
    sprintf(buf, "serial_close: port=%d", port);
    net_log(buf);
}

static unsigned long g_checksum = 0;
static int g_checksum_active = 0;

void serial_checksum_start(void) {
    g_checksum = 0;
    g_checksum_active = 1;
}

unsigned long serial_checksum_value(void) {
    g_checksum_active = 0;
    return g_checksum;
}

int serial_send_byte(int port, unsigned char b) {
    unsigned base;
    unsigned long start;

    base = com_base(port);
    start = get_ticks();
    while (!(inp(base + 5) & 0x20)) {               /* wait for THR empty */
        if (get_ticks() - start > BIOS_TICKS_PER_SEC) {
            net_log("serial_send_byte: TIMEOUT waiting for THR empty");
            return 0;
        }
    }
    outp(base, b);
    if (g_checksum_active) g_checksum += b;
    return 1;
}

int serial_recv_byte(int port, unsigned char* b, long timeout_ticks) {
    unsigned base;
    unsigned long start;

    base = com_base(port);
    start = get_ticks();
    while (!(inp(base + 5) & 0x01)) {               /* wait for data ready */
        if (get_ticks() - start > (unsigned long)timeout_ticks) return 0;
    }
    *b = (unsigned char)inp(base);
    if (g_checksum_active) g_checksum += *b;
    return 1;
}

/* Reads and discards any bytes already sitting in the receive buffer.
   Important before a fresh handshake: a cancelled previous attempt (or a
   stale link that never fully dropped) can leave old bytes queued up,
   which would otherwise be misread as a fresh handshake reply. */
void serial_drain_rx(int port) {
    unsigned base;
    int discarded;
    char logbuf[48];

    base = com_base(port);
    discarded = 0;
    while (inp(base + 5) & 0x01) {
        inp(base);
        discarded++;
        if (discarded > 20000) break; /* safety valve against a stuck line */
    }
    if (discarded > 0) {
        sprintf(logbuf, "serial_drain_rx: discarded %d stale byte(s)", discarded);
        net_log(logbuf);
    }
}

void serial_send_buffer(int port, const void* buf, unsigned int len) {
    unsigned int i;
    const unsigned char* p;
    unsigned long start;
    char logbuf[64];

    p = (const unsigned char*)buf;
    for (i = 0; i < len; i++) {
        start = get_ticks();
        while (!serial_send_byte(port, p[i])) {
            /* keep retrying: the far end may just be slow to drain, but
               give up after a while instead of spinning forever if the
               link has actually jammed. */
            if (get_ticks() - start > 30L * BIOS_TICKS_PER_SEC) {
                sprintf(logbuf, "serial_send_buffer: GAVE UP at byte %u of %u", i, len);
                net_log(logbuf);
                return;
            }
        }
    }
}

int serial_recv_buffer(int port, void* buf, unsigned int len, long timeout_ticks) {
    unsigned int i;
    unsigned char* p;
    char logbuf[64];

    p = (unsigned char*)buf;
    for (i = 0; i < len; i++) {
        if (!serial_recv_byte(port, &p[i], timeout_ticks)) {
            sprintf(logbuf, "serial_recv_buffer: TIMEOUT at byte %u of %u", i, len);
            net_log(logbuf);
            return 0;
        }
    }
    return 1;
}

void net_send_int(int port, int v) {
    serial_send_buffer(port, &v, sizeof(int));
}

int net_recv_int(int port, int* v, long timeout_ticks) {
    return serial_recv_buffer(port, v, sizeof(int), timeout_ticks);
}