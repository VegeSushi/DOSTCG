#ifndef SERIAL_H
#define SERIAL_H

/* Simple polled 8250/16550 UART driver for DOS null-modem multiplayer.
   No interrupts are used: everything is polled with tick-based timeouts,
   which is fine for a turn-based game trading small packets. */

int  serial_init(int port, unsigned long baud);      /* port: 1=COM1, 2=COM2 */
void serial_close(int port);

int  serial_send_byte(int port, unsigned char b);
int  serial_recv_byte(int port, unsigned char* b, long timeout_ticks);

void serial_send_buffer(int port, const void* buf, unsigned int len);
int  serial_recv_buffer(int port, void* buf, unsigned int len, long timeout_ticks);
void serial_drain_rx(int port);

void net_send_int(int port, int v);
int  net_recv_int(int port, int* v, long timeout_ticks);

/* BIOS tick counter (0000:0040:006C), ~18.2 increments/second. */
unsigned long get_ticks(void);

/* Running byte-sum checksum, used to detect a desynced/garbled transfer
   instead of silently accepting whatever bytes happen to show up.
   Only one side (send or recv) needs to be "active" at a time on a given
   machine, since sends and receives of a given payload never overlap. */
void serial_checksum_start(void);
unsigned long serial_checksum_value(void);

/* Appends a timestamped-ish line to NETLOG.TXT in the current directory.
   Safe to call a lot: each call opens/appends/closes, so a crash or reset
   never loses what was already logged. */
void net_log(const char* msg);

#endif