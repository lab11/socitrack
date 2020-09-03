#ifndef __ATOMIC_HEADER_H__
#define __ATOMIC_HEADER_H__

#include <stdint.h>

#define ATOMIC_OP(asm_op, a, v) do { \
   uint32_t reg0; \
   __asm__ __volatile__("   cpsid i\n" \
                        "   ldr  %0, [%1]\n" \
                        #asm_op" %0, %0, %2\n" \
                        "   str  %0, [%1]\n" \
                        "   cpsie i\n" \
                        : "=&b" (reg0) \
                        : "b" (a), "r" (v) : "cc"); \
   } while (0)

static inline void atomic_clear_bits(uint32_t volatile *addr, uint32_t bits)
{
   ATOMIC_OP(bic, addr, bits);
}
static inline void atomic_or(uint32_t volatile *addr, uint32_t bits)
{
   ATOMIC_OP(orr, addr, bits);
}
static inline void atomic_add(uint32_t volatile *addr, uint32_t value)
{
   ATOMIC_OP(add, addr, value);
}
static inline void atomic_sub(uint32_t volatile *addr, uint32_t value)
{
   ATOMIC_OP(sub, addr, value);
}
static inline void atomic_clear(uint32_t volatile *addr)
{
   __asm__ __volatile__("   cpsid   i\n"
                        "   str     %1, [%0]\n"
                        "   cpsie   i\n"
                        :
                        : "b" (addr), "r" (0) : "cc");
}
static inline void atomic_set(uint32_t volatile *addr)
{
   __asm__ __volatile__("   cpsid   i\n"
                        "   str     %1, [%0]\n"
                        "   cpsie   i\n"
                        :
                        : "b" (addr), "r" (1) : "cc");
}
static inline uint32_t atomic_read(uint32_t volatile *addr)
{
   uint32_t ret;
   __asm__ __volatile__("   cpsid   i\n"
                        "   ldr     %0, [%1]\n"
                        "   cpsie   i\n"
                        : "=&b" (ret)
                        : "b" (addr) : "cc");
   return ret;
}
static inline uint32_t atomic_read_clear(uint32_t volatile *addr)
{
   uint32_t ret;
   __asm__ __volatile__("   cpsid   i\n"
                        "   ldr     %0, [%1]\n"
                        "   str     %2, [%1]\n"
                        "   cpsie   i\n"
                        : "=&b" (ret)
                        : "b" (addr), "r" (0) : "cc");
   return ret;
}
static inline uint32_t atomic_read_set(uint32_t volatile *addr)
{
   uint32_t ret;
   __asm__ __volatile__("   cpsid   i\n"
                        "   ldr     %0, [%1]\n"
                        "   str     %2, [%1]\n"
                        "   cpsie   i\n"
                        : "=&b" (ret)
                        : "b" (addr), "r" (1) : "cc");
   return ret;
}

#endif      // #ifndef __ATOMIC_HEADER_H__
