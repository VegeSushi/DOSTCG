#ifndef TYPES_H
#define TYPES_H

// Bulletproof standard integer sizes for 16-bit DOS Watcom
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long  uint32_t;
typedef long           int32_t;

// Archive Formats
typedef struct {
    char magic[4];          // "CHAR" or "PACK"
    uint16_t version;
    uint16_t num_entries;
} ArchiveHeader;

typedef struct {
    char name[16];
    uint32_t offset;
    uint32_t size;
    uint8_t type;
    uint8_t pad[3];
} ArchiveEntry;

typedef struct {
    char name[16];
    int32_t type;
    int32_t power;
} Move;

/* Elemental character types (Pokemon-style). Values are stored in the
   Character.elem_type field below and are what drives the weakness /
   resistance multiplier table in main.c (get_type_multiplier). */
#define TYPE_NORMAL   0
#define TYPE_FIRE     1
#define TYPE_WATER    2
#define TYPE_GRASS    3
#define TYPE_EARTH    4
#define TYPE_ELECTRIC 5
#define NUM_TYPES     6

typedef struct {
    char name[16];
    int32_t max_hp;
    int32_t hp;
    int32_t base_atk;
    int32_t base_def;
    int32_t elem_type; /* one of the TYPE_* constants above. Was "pad1";
                          renamed/repurposed so the on-disk .chr layout,
                          the network wire format, and save_character()'s
                          byte order are all unchanged. */
    int32_t pad2;      /* still reserved/unused */
    Move moves[4];
    unsigned char far* sprite_data;
} Character;

typedef struct {
    char bg_name[16];
    uint8_t bg_color; // Color index for Mode 13h
} Background;

#endif
