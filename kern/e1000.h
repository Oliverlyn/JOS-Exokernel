#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>
#include <kern/pmap.h>

// E1000 Vendor and Device ID's (82540EM in QEMU)

#define E1000_VENDORID 0x8086
#define E1000_DEVICEID 0x100E

/* Register Set. (82543, 82544)
 *
 * Registers are defined to be 32 bits and  should be accessed as 32 bit values.
 * These registers are physically located on the NIC, but are mapped into the
 * host memory address space.
 *
 * RW - register is both readable and writable
 * RO - register is read only
 * WO - register is write only
 * R/clr - register is read only and is cleared when read
 * A - register array
 */

#define E1000_STATUS   0x00008  /* Device Status - RO */
#define E1000_TCTL     0x00400  /* TX Control - RW */
#define E1000_TIPG     0x00410  /* TX Inter-packet gap -RW */
#define E1000_TDBAL    0x03800  /* TX Descriptor Base Address Low - RW */
#define E1000_TDBAH    0x03804  /* TX Descriptor Base Address High - RW */
#define E1000_TDLEN    0x03808  /* TX Descriptor Length - RW */
#define E1000_TDH      0x03810  /* TX Descriptor Head - RW */
#define E1000_TDT      0x03818  /* TX Descripotr Tail - RW */

/* Transmit Control */
#define E1000_TCTL_RST    0x00000001    /* software reset */
#define E1000_TCTL_EN     0x00000002    /* enable tx */
#define E1000_TCTL_PSP    0x00000008    /* pad short packets */
#define E1000_TCTL_CT     0x00000ff0    /* collision threshold */
#define E1000_TCTL_COLD   0x003ff000    /* collision distance */

// Descriptors
#define NUMTD 64
#define TDSTART 0xF00D0000    // Arbitrary mem address of TD array

struct tx_desc
{
   uint64_t addr;
   uint16_t length;
   uint8_t cso;
   uint8_t cmd;
   uint8_t status;
   uint8_t css;
   uint16_t special;
};

struct tx_desc tdarr[NUMTD];

// Register addresses

volatile physaddr_t bar0addr; 
volatile uint32_t *bar0;

// Convert byte offset of register to index into 32bit array
#define REG(byte) ((byte)/4)

int e1000_attach(struct pci_func *pcif);

#endif	// JOS_KERN_E1000_H