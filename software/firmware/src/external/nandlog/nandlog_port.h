#ifndef __NANDLOG_PORT_HEADER_H__
#define __NANDLOG_PORT_HEADER_H__

#include <stdbool.h>
#include <stdint.h>

// Initialize the NAND peripheral and communications protocols
bool nandlog_port_init(void);
void nandlog_port_deinit(void);

// Assumes a transfer either completes or does not return, so a failure that cannot be retried must end in nandlog_port_fatal()
void nandlog_port_spi_read(uint8_t command, const void *address, uint32_t address_length, void *read_buffer, uint32_t read_length);
void nandlog_port_spi_write(uint8_t command, const void *address, uint32_t address_length, const void *write_buffer, uint32_t write_length);

// Gate the chip's write-protect pin: true permits programming and erasing
void nandlog_port_write_enable(bool enable);

// Wake the NAND peripheral or return it to its lowest-power state
void nandlog_port_power(bool awake);

// Route any diagnostic messages as needed
void nandlog_port_log(const char *format, ...);

// Platform-specific busy wait functions
void nandlog_port_delay_us(uint32_t microseconds);
void nandlog_port_delay_ms(uint32_t milliseconds);

// Unrecoverable hardware fault
void nandlog_port_fatal(const char *reason);

#endif  // #ifndef __NANDLOG_PORT_HEADER_H__
