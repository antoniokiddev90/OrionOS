#include "e1000.h"
#include "pmm.h"
#include "../mm/vmm.h"
#include "paging.h"
#include <stddef.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0x0CF8
#define PCI_CONFIG_DATA 0x0CFC
#define PCI_COMMAND 0x04
#define PCI_BAR0 0x10
#define PCI_CLASS_REVISION 0x08
#define PCI_HEADER_TYPE 0x0C
#define PCI_CLASS_NETWORK 0x02
#define PCI_SUBCLASS_ETHERNET 0x00
#define PCI_VENDOR_INTEL 0x8086
#define E1000_MMIO_VA 0xFFFF800000000000ULL
#define E1000_MMIO_SIZE 0x20000ULL
#define E1000_DMA_LIMIT 0x40000000ULL
#define E1000_REG_CTRL 0x0000
#define E1000_REG_EERD 0x0014
#define E1000_REG_ICR 0x00C0
#define E1000_REG_IMS 0x00D0
#define E1000_REG_IMC 0x00D8
#define E1000_REG_RCTL 0x0100
#define E1000_REG_TCTL 0x0400
#define E1000_REG_TIPG 0x0410
#define E1000_REG_RDBAL 0x2800
#define E1000_REG_RDBAH 0x2804
#define E1000_REG_RDLEN 0x2808
#define E1000_REG_RDH 0x2810
#define E1000_REG_RDT 0x2818
#define E1000_REG_TDBAL 0x3800
#define E1000_REG_TDBAH 0x3804
#define E1000_REG_TDLEN 0x3808
#define E1000_REG_TDH 0x3810
#define E1000_REG_TDT 0x3818
#define E1000_REG_RAL 0x5400
#define E1000_REG_RAH 0x5404
#define E1000_CTRL_RST (1U << 26)
#define E1000_CTRL_SLU (1U << 6)
#define E1000_RCTL_EN (1U << 1)
#define E1000_RCTL_BAM (1U << 15)
#define E1000_RCTL_SECRC (1U << 26)
#define E1000_TCTL_EN (1U << 1)
#define E1000_TCTL_PSP (1U << 3)
#define E1000_TCTL_CT (0x10U << 4)
#define E1000_TCTL_COLD (0x40U << 12)
#define E1000_TX_CMD_EOP (1U << 0)
#define E1000_TX_CMD_IFCS (1U << 1)
#define E1000_TX_CMD_RS (1U << 3)
#define E1000_DESC_DONE (1U << 0)
#define E1000_DESC_ERROR_MASK 0xFFU

struct e1000_rx_desc {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;
    uint8_t command;
    uint8_t status;
    uint8_t checksum_start;
    uint16_t special;
} __attribute__((packed));

static volatile uint8_t *registers;
static volatile struct e1000_rx_desc *rx_ring;
static volatile struct e1000_tx_desc *tx_ring;
static uint64_t rx_ring_physical;
static uint64_t tx_ring_physical;
static uint64_t rx_buffer_physical[E1000_RING_SIZE];
static uint64_t tx_buffer_physical[E1000_RING_SIZE];
static uint8_t mac_address[6];
static uint16_t rx_tail;
static uint16_t tx_tail;
static int initialized;

static inline void io_outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t io_inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) | ((uint32_t)function << 8) |
                       ((uint32_t)offset & 0xFCU);
    io_outl(PCI_CONFIG_ADDRESS, address);
    return io_inl(PCI_CONFIG_DATA);
}

static void pci_write_command(uint8_t bus, uint8_t device, uint8_t function, uint16_t command) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) |
                       ((uint32_t)device << 11) | ((uint32_t)function << 8) |
                       PCI_COMMAND;
    io_outl(PCI_CONFIG_ADDRESS, address);
    io_outw(PCI_CONFIG_DATA, command);
}

static int supported_device(uint16_t device) {
    switch (device) {
        case 0x100E:
        case 0x100F:
        case 0x1010:
        case 0x1011:
        case 0x1012:
        case 0x101D:
        case 0x1026:
        case 0x1027:
        case 0x105E:
        case 0x105F:
        case 0x1060:
        case 0x107D:
        case 0x107E:
        case 0x107F:
        case 0x10A7:
        case 0x10A9:
        case 0x10B5:
        case 0x10BC:
        case 0x10C9:
        case 0x10D3:
        case 0x10EA:
        case 0x10F5:
        case 0x150C:
            return 1;
        default:
            return 0;
    }
}

static int pci_find_e1000(uint8_t *bus_out, uint8_t *device_out, uint8_t *function_out) {
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint32_t device = 0; device < 32; ++device) {
            uint32_t id0 = pci_read((uint8_t)bus, (uint8_t)device, 0, 0);
            if ((id0 & 0xFFFFU) == 0xFFFFU) continue;
            uint8_t header = (uint8_t)(pci_read((uint8_t)bus, (uint8_t)device, 0,
                                                PCI_HEADER_TYPE) >> 16);
            uint8_t function_limit = (header & 0x80U) ? 8 : 1;
            for (uint8_t function = 0; function < function_limit; ++function) {
                uint32_t id = function == 0 ? id0 :
                    pci_read((uint8_t)bus, (uint8_t)device, function, 0);
                uint16_t vendor = (uint16_t)id;
                uint16_t device_id = (uint16_t)(id >> 16);
                if (vendor != PCI_VENDOR_INTEL || !supported_device(device_id)) continue;
                uint32_t class_revision = pci_read((uint8_t)bus, (uint8_t)device,
                                                   function, PCI_CLASS_REVISION);
                uint8_t class_code = (uint8_t)(class_revision >> 24);
                uint8_t subclass = (uint8_t)(class_revision >> 16);
                if (class_code != PCI_CLASS_NETWORK || subclass != PCI_SUBCLASS_ETHERNET) continue;
                *bus_out = (uint8_t)bus;
                *device_out = (uint8_t)device;
                *function_out = function;
                return 0;
            }
        }
    }
    return -1;
}

static uint32_t reg_read(uint32_t offset) {
    return *(volatile uint32_t *)(registers + offset);
}

static void reg_write(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(registers + offset) = value;
}

static void zero_memory(void *memory, size_t size) {
    uint8_t *bytes = (uint8_t *)memory;
    for (size_t i = 0; i < size; ++i) bytes[i] = 0;
}

static uint64_t dma_alloc_page(void) {
    return pmm_alloc_frame_below(E1000_DMA_LIMIT);
}

static void dma_release(void) {
    for (uint32_t i = 0; i < E1000_RING_SIZE; ++i) {
        if (rx_buffer_physical[i]) pmm_free_frame(rx_buffer_physical[i]);
        if (tx_buffer_physical[i]) pmm_free_frame(tx_buffer_physical[i]);
        rx_buffer_physical[i] = 0;
        tx_buffer_physical[i] = 0;
    }
    if (rx_ring_physical) pmm_free_frame(rx_ring_physical);
    if (tx_ring_physical) pmm_free_frame(tx_ring_physical);
    rx_ring_physical = 0;
    tx_ring_physical = 0;
    rx_ring = NULL;
    tx_ring = NULL;
}

static int dma_initialize(void) {
    rx_ring_physical = dma_alloc_page();
    tx_ring_physical = dma_alloc_page();
    if (!rx_ring_physical || !tx_ring_physical) {
        dma_release();
        return -1;
    }
    rx_ring = (struct e1000_rx_desc *)(uintptr_t)rx_ring_physical;
    tx_ring = (struct e1000_tx_desc *)(uintptr_t)tx_ring_physical;
    zero_memory((void *)(uintptr_t)rx_ring_physical, (size_t)PMM_FRAME_SIZE);
    zero_memory((void *)(uintptr_t)tx_ring_physical, (size_t)PMM_FRAME_SIZE);
    for (uint32_t i = 0; i < E1000_RING_SIZE; ++i) {
        rx_buffer_physical[i] = dma_alloc_page();
        tx_buffer_physical[i] = dma_alloc_page();
        if (!rx_buffer_physical[i] || !tx_buffer_physical[i]) {
            dma_release();
            return -1;
        }
        rx_ring[i].address = rx_buffer_physical[i];
        tx_ring[i].address = tx_buffer_physical[i];
        tx_ring[i].status = E1000_DESC_DONE;
    }
    return 0;
}

static int eeprom_read_word(uint8_t address, uint16_t *value) {
    reg_write(E1000_REG_EERD, 1U | ((uint32_t)address << 8));
    for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
        uint32_t result = reg_read(E1000_REG_EERD);
        if (result & (1U << 4)) {
            *value = (uint16_t)(result >> 16);
            return 0;
        }
    }
    return -1;
}

static int mac_is_valid(const uint8_t address[6]) {
    uint8_t all_zero = 0;
    uint8_t all_ones = 0xFF;
    for (uint32_t i = 0; i < 6; ++i) {
        all_zero |= address[i];
        all_ones &= address[i];
    }
    return all_zero != 0 && all_ones != 0xFF && (address[0] & 1U) == 0;
}

static int read_mac_address(void) {
    uint32_t low = reg_read(E1000_REG_RAL);
    uint32_t high = reg_read(E1000_REG_RAH);
    mac_address[0] = (uint8_t)low;
    mac_address[1] = (uint8_t)(low >> 8);
    mac_address[2] = (uint8_t)(low >> 16);
    mac_address[3] = (uint8_t)(low >> 24);
    mac_address[4] = (uint8_t)high;
    mac_address[5] = (uint8_t)(high >> 8);
    if (!mac_is_valid(mac_address)) {
        uint16_t words[3];
        for (uint8_t i = 0; i < 3; ++i) {
            if (eeprom_read_word(i, &words[i]) != 0) return -1;
        }
        for (uint32_t i = 0; i < 3; ++i) {
            mac_address[i * 2] = (uint8_t)words[i];
            mac_address[i * 2 + 1] = (uint8_t)(words[i] >> 8);
        }
        if (!mac_is_valid(mac_address)) return -1;
    }
    low = (uint32_t)mac_address[0] | ((uint32_t)mac_address[1] << 8) |
          ((uint32_t)mac_address[2] << 16) | ((uint32_t)mac_address[3] << 24);
    high = (uint32_t)mac_address[4] | ((uint32_t)mac_address[5] << 8) | (1U << 31);
    reg_write(E1000_REG_RAL, low);
    reg_write(E1000_REG_RAH, high);
    return 0;
}

int e1000_init(void) {
    if (initialized) return 0;

    uint8_t bus;
    uint8_t device;
    uint8_t function;
    if (pci_find_e1000(&bus, &device, &function) != 0) return -1;

    uint32_t bar_low = pci_read(bus, device, function, PCI_BAR0);
    if ((bar_low & 1U) != 0) return -1;
    uint32_t bar_type = (bar_low >> 1) & 3U;
    uint64_t bar_physical = (uint64_t)(bar_low & 0xFFFFFFF0U);
    if (bar_type == 2U) {
        bar_physical |= (uint64_t)pci_read(bus, device, function, PCI_BAR0 + 4) << 32;
    } else if (bar_type != 0U) {
        return -1;
    }
    if (!bar_physical || bar_physical > PTE_ADDR_MASK ||
        bar_physical + E1000_MMIO_SIZE - 1 > PTE_ADDR_MASK) return -1;

    uint16_t command = (uint16_t)pci_read(bus, device, function, PCI_COMMAND);
    pci_write_command(bus, device, function, (uint16_t)(command | (1U << 1) | (1U << 2)));

    uint64_t bar_page = bar_physical & PAGE_MASK;
    uint64_t bar_offset = bar_physical - bar_page;
    uint64_t map_size = (bar_offset + E1000_MMIO_SIZE + PAGE_SIZE - 1) & PAGE_MASK;
    if (!registers) {
        if (vmm_is_mapped(E1000_MMIO_VA) ||
            vmm_map(E1000_MMIO_VA, bar_page, map_size,
                    PTE_PRESENT | PTE_WRITE | PTE_PCD | PTE_PWT) != 0) return -1;
        registers = (volatile uint8_t *)(uintptr_t)(E1000_MMIO_VA + bar_offset);
    } else if ((uintptr_t)registers != (uintptr_t)(E1000_MMIO_VA + bar_offset)) {
        return -1;
    }

    reg_write(E1000_REG_IMC, 0xFFFFFFFFU);
    (void)reg_read(E1000_REG_ICR);
    reg_write(E1000_REG_CTRL, reg_read(E1000_REG_CTRL) | E1000_CTRL_RST);
    uint32_t reset_done = 0;
    for (uint32_t attempt = 0; attempt < 1000000; ++attempt) {
        if ((reg_read(E1000_REG_CTRL) & E1000_CTRL_RST) == 0) {
            reset_done = 1;
            break;
        }
    }
    if (!reset_done || read_mac_address() != 0 || dma_initialize() != 0) {
        dma_release();
        return -1;
    }

    reg_write(E1000_REG_IMC, 0xFFFFFFFFU);
    (void)reg_read(E1000_REG_ICR);
    reg_write(E1000_REG_RDBAL, (uint32_t)rx_ring_physical);
    reg_write(E1000_REG_RDBAH, (uint32_t)(rx_ring_physical >> 32));
    reg_write(E1000_REG_RDLEN, E1000_RING_SIZE * (uint32_t)sizeof(struct e1000_rx_desc));
    reg_write(E1000_REG_RDH, 0);
    reg_write(E1000_REG_RDT, E1000_RING_SIZE - 1);
    reg_write(E1000_REG_TDBAL, (uint32_t)tx_ring_physical);
    reg_write(E1000_REG_TDBAH, (uint32_t)(tx_ring_physical >> 32));
    reg_write(E1000_REG_TDLEN, E1000_RING_SIZE * (uint32_t)sizeof(struct e1000_tx_desc));
    reg_write(E1000_REG_TDH, 0);
    reg_write(E1000_REG_TDT, 0);
    reg_write(E1000_REG_TIPG, 0x0060200AU);
    reg_write(E1000_REG_CTRL, reg_read(E1000_REG_CTRL) | E1000_CTRL_SLU);
    reg_write(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
              E1000_TCTL_CT | E1000_TCTL_COLD);
    reg_write(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);
    rx_tail = 0;
    tx_tail = 0;
    initialized = 1;
    return 0;
}

int e1000_get_mac(uint8_t address[6]) {
    if (!initialized || !address) return -1;
    for (uint32_t i = 0; i < 6; ++i) address[i] = mac_address[i];
    return 0;
}

int e1000_send(const void *frame, uint16_t length) {
    if (!initialized || !frame || length == 0 || length > E1000_MAX_FRAME_SIZE) return -1;
    uint16_t index = tx_tail;
    uint32_t attempts = 1000000;
    int done = 0;
    while (attempts) {
        __asm__ volatile("" : : : "memory");
        if (tx_ring[index].status & E1000_DESC_DONE) {
            done = 1;
            break;
        }
        --attempts;
    }
    if (!done) return -1;

    const uint8_t *source = (const uint8_t *)frame;
    uint8_t *destination = (uint8_t *)(uintptr_t)tx_buffer_physical[index];
    for (uint16_t i = 0; i < length; ++i) destination[i] = source[i];
    tx_ring[index].length = length;
    tx_ring[index].checksum_offset = 0;
    tx_ring[index].command = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    tx_ring[index].status = 0;
    tx_ring[index].checksum_start = 0;
    tx_ring[index].special = 0;
    __asm__ volatile("" : : : "memory");
    tx_tail = (uint16_t)((index + 1) % E1000_RING_SIZE);
    reg_write(E1000_REG_TDT, tx_tail);

    attempts = 1000000;
    done = 0;
    while (attempts) {
        __asm__ volatile("" : : : "memory");
        if (tx_ring[index].status & E1000_DESC_DONE) {
            done = 1;
            break;
        }
        --attempts;
    }
    return done ? 0 : -1;
}

int e1000_receive(void *buffer, uint16_t capacity, uint16_t *length) {
    if (!initialized || !buffer || !length) return E1000_RECEIVE_ERROR;
    uint16_t index = rx_tail;
    volatile struct e1000_rx_desc *descriptor = &rx_ring[index];
    if ((descriptor->status & E1000_DESC_DONE) == 0) return E1000_RECEIVE_EMPTY;
    __asm__ volatile("" : : : "memory");

    uint16_t frame_length = descriptor->length;
    uint8_t errors = descriptor->errors;
    int result = E1000_RECEIVE_FRAME;
    if (frame_length == 0 || frame_length > E1000_MAX_FRAME_SIZE || errors != 0) {
        result = E1000_RECEIVE_ERROR;
        *length = 0;
    } else {
        *length = frame_length;
        if (capacity < frame_length) {
            result = E1000_RECEIVE_ERROR;
        } else {
            const uint8_t *source = (const uint8_t *)(uintptr_t)rx_buffer_physical[index];
            uint8_t *destination = (uint8_t *)buffer;
            for (uint16_t i = 0; i < frame_length; ++i) destination[i] = source[i];
        }
    }

    descriptor->length = 0;
    descriptor->checksum = 0;
    descriptor->errors = 0;
    descriptor->special = 0;
    descriptor->status = 0;
    __asm__ volatile("" : : : "memory");
    reg_write(E1000_REG_RDT, index);
    rx_tail = (uint16_t)((index + 1) % E1000_RING_SIZE);
    return result;
}
