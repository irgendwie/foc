#pragma once

#define SUNXI_ASM_HARDWARE_LOCK  (0x01c18000)
#define SUNXI_ASM_CCI_ADDRESS    (0x01790000)
#define SUNXI_ASM_CCI_STATUS     (0x000c)
#define SUNXI_ASM_CCI_C1_OFFSET  (0x5000)
#define SUNXI_ASM_SNOOP_ENABLE   (0x3)

#define HAVE_MACRO_BSP_EARLY_INIT
.macro bsp_early_init tmp1, tmp2
  @ retrieve mpidr
  mrc	  p15, 0, \tmp1, c0, c0, 5
  @ extract the cluster
  ubfx  \tmp1, \tmp1, #8, #8
  @ skip when not cluster 1
  cmp   \tmp1, #1
  bne   early_init_sunxi_done

  @ cluster 1, first must init cci bus before any paging. Take hardware locks
  @ to ensure exclusion.
early_init_sunxi_lock:
  @ Debug code: Enables an infinite loop writing to uart0
  @ ldr \tmp1, =0x01C28000
  @ mov \tmp2, #(0x41)
  @ str \tmp2, [\tmp1]
  @ b early_init_sunxi_lock

  ldr \tmp1, =SUNXI_ASM_HARDWARE_LOCK
  ldr \tmp2, [\tmp1]
  cmp \tmp2, #0
  bne early_init_sunxi_lock

  @ now we hold the lock, look if we need to enable
  ldr \tmp1, =SUNXI_ASM_CCI_ADDRESS + SUNXI_ASM_CCI_C1_OFFSET
  ldr \tmp2, [\tmp1]
  and \tmp2, \tmp2, #SUNXI_ASM_SNOOP_ENABLE
  cmp \tmp2, #SUNXI_ASM_SNOOP_ENABLE
  beq  early_init_sunxi_unlock

  @ not yet enabled, we must do it
  ldr \tmp2, [\tmp1]
  orr \tmp2, #SUNXI_ASM_SNOOP_ENABLE
  str \tmp2, [\tmp1]

  @ wait for this to go through
  ldr \tmp1, =SUNXI_ASM_CCI_ADDRESS
early_init_sunxi_cci_wait:
  ldr \tmp2, [\tmp1, #SUNXI_ASM_CCI_STATUS]
  tst \tmp2, #1
  bne early_init_sunxi_cci_wait

  dsb
early_init_sunxi_unlock:
  ldr \tmp1, =SUNXI_ASM_HARDWARE_LOCK
  mov \tmp2, #0
  str \tmp2, [\tmp1]

early_init_sunxi_done:
  
.endm
