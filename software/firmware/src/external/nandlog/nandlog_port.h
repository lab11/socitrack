#ifndef __NANDLOG_PORT_HEADER_H__
#define __NANDLOG_PORT_HEADER_H__

// Everything nandlog needs from the platform it is running on. An integrator implements this file once,
// against whatever SPI peripheral, GPIO and timing facilities the target provides; nothing above this
// interface knows what the hardware is.

#include <stdbool.h>
#include <stdint.h>


// Bring up the SPI peripheral and the chip's control pins, or take them back down. Returns false if the
// peripheral could not be brought up at all
bool nandlog_port_init(void);
void nandlog_port_deinit(void);

// Issue [command][address...] and then move payload in the requested direction. Every layer above assumes
// a transfer either completes or does not return, so a failure that cannot be retried must end in
// nandlog_port_fatal() rather than a silent no-op -- a half-written page that reports success is far worse
// than a reset
void nandlog_port_spi_read(uint8_t command, const void *address, uint32_t address_length, void *read_buffer, uint32_t read_length);
void nandlog_port_spi_write(uint8_t command, const void *address, uint32_t address_length, const void *write_buffer, uint32_t write_length);

// Gate the chip's write-protect pin. True permits programming and erasing
void nandlog_port_write_enable(bool enable);

// Wake the SPI peripheral or return it to its lowest-power state. Any access made while it is asleep is a
// fault on the caller's part, not something this layer recovers from
void nandlog_port_power(bool awake);

void nandlog_port_delay_us(uint32_t microseconds);
void nandlog_port_delay_ms(uint32_t milliseconds);

// Unrecoverable hardware fault. The host decides what that means -- reset, halt, or carry on degraded --
// because a library has no business making that call for the system it is embedded in. Should not return
void nandlog_port_fatal(const char *reason);

#endif  // #ifndef __NANDLOG_PORT_HEADER_H__
