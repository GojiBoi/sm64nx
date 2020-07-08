// 0x0E0007CC
const GeoLayout ssl_pyramid_level_geo[] = {
   GEO_NODE_SCREEN_AREA(10, SCREEN_WIDTH/2, SCREEN_HEIGHT/2, SCREEN_WIDTH/2, SCREEN_HEIGHT/2),
   GEO_OPEN_NODE(),
      GEO_ZBUFFER(0),
      GEO_OPEN_NODE(),
         GEO_NODE_ORTHO(100),
         GEO_OPEN_NODE(),
            GEO_BACKGROUND_COLOR(0x0001),
         GEO_CLOSE_NODE(),
      GEO_CLOSE_NODE(),
      GEO_ZBUFFER(1),
      GEO_OPEN_NODE(),
         GEO_CAMERA_FRUSTUM_WITH_FUNC(45, 100, 12800, geo_camera_fov),
         GEO_OPEN_NODE(),
            GEO_CAMERA(4, 0, 2000, 6000, 0, 0, 0, geo_camera_main),
            GEO_OPEN_NODE(),
               GEO_DISPLAY_LIST(LAYER_OPAQUE, ssl_seg7_dl_0701EE80),
               GEO_DISPLAY_LIST(LAYER_ALPHA, ssl_seg7_dl_0701F920),
               GEO_DISPLAY_LIST(LAYER_TRANSPARENT_DECAL, ssl_seg7_dl_0701FCE0),
               GEO_ASM(0x802, geo_movtex_update_horizontal),
               GEO_ASM(   0, geo_movtex_pause_control),
               GEO_ASM(0x801, geo_movtex_draw_nocolor),
               GEO_ASM(0x802, geo_movtex_draw_nocolor),
               GEO_ASM(0x803, geo_movtex_draw_nocolor),
               GEO_RENDER_OBJ(),
               GEO_ASM(   0, geo_enfvx_main),
            GEO_CLOSE_NODE(),
         GEO_CLOSE_NODE(),
      GEO_CLOSE_NODE(),
   GEO_CLOSE_NODE(),
   GEO_END(),
};