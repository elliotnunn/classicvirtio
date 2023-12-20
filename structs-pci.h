#pragma once

#include <stdint.h>

struct virtio_pci_common_cfg {
	// About the whole device
	uint32_t device_feature_select;	// read-write
	uint32_t device_feature;         // read-only for driver
	uint32_t driver_feature_select;  // read-write
	uint32_t driver_feature;         // read-write
	uint16_t msix_config;            // read-write
	uint16_t num_queues;             // read-only for driver
	uint8_t device_status;              // read-write
	uint8_t config_generation;          // read-only for driver

	// About a specific virtqueue
	uint16_t queue_select;           // read-write
	uint16_t queue_size;             // read-write
	uint16_t queue_msix_vector;      // read-write
	uint16_t queue_enable;           // read-write
	uint16_t queue_notify_off;       // read-only for driver
	uint32_t queue_desc;             // read-write
	uint32_t queue_desc_hi;
	uint32_t queue_driver;           // read-write
	uint32_t queue_driver_hi;
	uint32_t queue_device;           // read-write
	uint32_t queue_device_hi;
} __attribute((scalar_storage_order("little-endian")));
