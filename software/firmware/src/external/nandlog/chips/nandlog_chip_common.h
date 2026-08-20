#ifndef __NANDLOG_CHIP_COMMON_HEADER_H__
#define __NANDLOG_CHIP_COMMON_HEADER_H__

// Boilerplate common to every chip driver, whatever the part and whatever the vendor.
//
// A driver states its geometry, then includes this header, which checks the set is complete and derives the
// addressing that follows from it:
//
//     #define NANDLOG_CHIP_NAME                    "AS5F18G04SND"
//     #define NANDLOG_CHIP_PAGE_SIZE_BYTES         4096
//     #define NANDLOG_CHIP_SPARE_SIZE_BYTES        256
//     #define NANDLOG_CHIP_PAGES_PER_BLOCK         64
//     #define NANDLOG_CHIP_BLOCK_COUNT             4096
//     #define NANDLOG_CHIP_RESERVED_BLOCKS         80
//     #include "nandlog_chip_common.h"
//
// Adding a part is therefore one new .c file and one line in the build.


// Header Inclusions ---------------------------------------------------------------------------------------------------

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "nandlog_chip.h"
#include "nandlog_conf.h"
#include "nandlog_port.h"


// Required Declarations -----------------------------------------------------------------------------------------------

#if !defined(NANDLOG_CHIP_NAME)
#error "A chip driver must define NANDLOG_CHIP_NAME before including nandlog_chip_common.h"
#endif
#if !defined(NANDLOG_CHIP_PAGE_SIZE_BYTES)
#error "A chip driver must define NANDLOG_CHIP_PAGE_SIZE_BYTES before including nandlog_chip_common.h"
#endif
#if !defined(NANDLOG_CHIP_SPARE_SIZE_BYTES)
#error "A chip driver must define NANDLOG_CHIP_SPARE_SIZE_BYTES before including nandlog_chip_common.h"
#endif
#if !defined(NANDLOG_CHIP_PAGES_PER_BLOCK)
#error "A chip driver must define NANDLOG_CHIP_PAGES_PER_BLOCK before including nandlog_chip_common.h"
#endif
#if !defined(NANDLOG_CHIP_BLOCK_COUNT)
#error "A chip driver must define NANDLOG_CHIP_BLOCK_COUNT before including nandlog_chip_common.h"
#endif
#if !defined(NANDLOG_CHIP_RESERVED_BLOCKS)
#error "A chip driver must define NANDLOG_CHIP_RESERVED_BLOCKS before including nandlog_chip_common.h"
#endif


// Declaration Validity ------------------------------------------------------------------------------------------------

_Static_assert(NANDLOG_CHIP_PAGE_SIZE_BYTES <= NANDLOG_MAX_PAGE_SIZE_BYTES,
               NANDLOG_CHIP_NAME " has a larger page than NANDLOG_MAX_PAGE_SIZE_BYTES allows for");
_Static_assert(NANDLOG_CHIP_SPARE_SIZE_BYTES <= NANDLOG_MAX_SPARE_SIZE_BYTES,
               NANDLOG_CHIP_NAME " has a larger spare area than NANDLOG_MAX_SPARE_SIZE_BYTES allows for");
_Static_assert((NANDLOG_CHIP_PAGES_PER_BLOCK & (NANDLOG_CHIP_PAGES_PER_BLOCK - 1)) == 0,
               NANDLOG_CHIP_NAME " must have a power-of-two number of pages per block");
_Static_assert(NANDLOG_CHIP_RESERVED_BLOCKS < NANDLOG_CHIP_BLOCK_COUNT,
               NANDLOG_CHIP_NAME " reserves more blocks than it has");


// Derived Addressing --------------------------------------------------------------------------------------------------

#define NANDLOG_CHIP_PAGE_COUNT                     ((uint32_t)NANDLOG_CHIP_PAGES_PER_BLOCK * NANDLOG_CHIP_BLOCK_COUNT)
#define NANDLOG_CHIP_PAGE_WITH_SPARE_SIZE_BYTES     (NANDLOG_CHIP_PAGE_SIZE_BYTES + NANDLOG_CHIP_SPARE_SIZE_BYTES)
#define NANDLOG_CHIP_BLOCK_MASK                     (~(uint32_t)(NANDLOG_CHIP_PAGES_PER_BLOCK - 1))
#define NANDLOG_CHIP_RESERVED_BASE_PAGE             ((uint32_t)(NANDLOG_CHIP_BLOCK_COUNT - NANDLOG_CHIP_RESERVED_BLOCKS) * NANDLOG_CHIP_PAGES_PER_BLOCK)

// The derived geometry that every driver hands back
#define NANDLOG_CHIP_GEOMETRY_INITIALIZER                                  \
   {                                                                       \
      .page_size_bytes  = NANDLOG_CHIP_PAGE_SIZE_BYTES,                    \
      .spare_size_bytes = NANDLOG_CHIP_SPARE_SIZE_BYTES,                   \
      .pages_per_block  = NANDLOG_CHIP_PAGES_PER_BLOCK,                    \
      .block_count      = NANDLOG_CHIP_BLOCK_COUNT,                        \
      .reserved_blocks  = NANDLOG_CHIP_RESERVED_BLOCKS                     \
   }

#endif  // #ifndef __NANDLOG_CHIP_COMMON_HEADER_H__
