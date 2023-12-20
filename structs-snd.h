#include <stdint.h>

struct virtio_snd_config {
    le32 jacks;
    le32 streams;
    le32 chmaps;
} __attribute((scalar_storage_order("little-endian")));

enum {
    /* jack control request types */
    VIRTIO_SND_R_JACK_INFO = 1,
    VIRTIO_SND_R_JACK_REMAP,

    /* PCM control request types */
    VIRTIO_SND_R_PCM_INFO = 0x0100,
    VIRTIO_SND_R_PCM_SET_PARAMS,
    VIRTIO_SND_R_PCM_PREPARE,
    VIRTIO_SND_R_PCM_RELEASE,
    VIRTIO_SND_R_PCM_START,
    VIRTIO_SND_R_PCM_STOP,

    /* channel map control request types */
    VIRTIO_SND_R_CHMAP_INFO = 0x0200,

    /* jack event types */
    VIRTIO_SND_EVT_JACK_CONNECTED = 0x1000,
    VIRTIO_SND_EVT_JACK_DISCONNECTED,

    /* PCM event types */
    VIRTIO_SND_EVT_PCM_PERIOD_ELAPSED = 0x1100,
    VIRTIO_SND_EVT_PCM_XRUN,

    /* common status codes */
    VIRTIO_SND_S_OK = 0x8000,
    VIRTIO_SND_S_BAD_MSG,
    VIRTIO_SND_S_NOT_SUPP,
    VIRTIO_SND_S_IO_ERR
};

/* a common header */
struct virtio_snd_hdr {
    le32 code;
} __attribute((scalar_storage_order("little-endian")));

/* an event notification */
struct virtio_snd_event {
    uint32_t hdr;
    uint32_t data;
} __attribute((scalar_storage_order("little-endian")));

// Item Information Request
struct virtio_snd_query_info {
    struct virtio_snd_hdr hdr; // VIRTIO_SND_R_*_INFO
    uint32_t start_id;
    uint32_t count;
    uint32_t size; // used for backward compatibility
} __attribute((scalar_storage_order("little-endian")));

// Jack Control Messages
A jack control request has, or consists of, a common header with the following layout structure:
struct virtio_snd_jack_hdr {
    struct virtio_snd_hdr hdr;
    uint32_t jack_id;
} __attribute((scalar_storage_order("little-endian")));

// VIRTIO_SND_R_JACK_INFO
// request =
struct virtio_snd_jack_info {
    struct virtio_snd_info hdr;
    uint32_t features; // VIRTIO_SND_JACK_F_REMAP = 1<<0
    uint32_t hda_reg_defconf;
    uint32_t hda_reg_caps;
    uint8_t connected;
    uint8_t padding[7];
} __attribute((scalar_storage_order("little-endian")));
