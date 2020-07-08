// 0x06005A60
static const s16 moneybag_seg6_animvalue_06005A60[] = {
	 0x0000,  0x00b4, -0x00e6,  0x1fff,  0x3fff,  0x1fff, -0x7fff, -0x3fff, 
	-0x3fff, -0x7fff, -0x7fff, -0x7fff, 
};

// 0x06005A78
static const u16 moneybag_seg6_animindex_06005A78[] = {
    0x0001, 0x0000, 0x0001, 0x0001, 0x0001, 0x0002, 0x0001, 0x0003, 0x0001, 0x0004, 0x0001, 0x0005,
    0x0001, 0x0000, 0x0001, 0x0000, 0x0001, 0x000B,
    0x0001, 0x0000, 0x0001, 0x0000, 0x0001, 0x000A,
    0x0001, 0x0000, 0x0001, 0x0000, 0x0001, 0x0007,
    0x0001, 0x0000, 0x0001, 0x0000, 0x0001, 0x0009,
    0x0001, 0x0000, 0x0001, 0x0000, 0x0001, 0x0008,
    0x0001, 0x0000, 0x0001, 0x0000, 0x0001, 0x0006,
};

// 0x06005AD8
static const struct Animation moneybag_seg6_anim_06005AD8 = {
	Animation::Flags::NONE,
    0,
    0,
    0,
    0x1E,
    ANIMINDEX_NUMPARTS(moneybag_seg6_animindex_06005A78),
    moneybag_seg6_animvalue_06005A60,
    moneybag_seg6_animindex_06005A78,
    0,
};