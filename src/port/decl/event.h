// Declarations of class Game, included inside its body (port/game.h, docs/port.md "Adding a kind"): the event script
// interpreter (event.cpp).
    // --- the event script interpreter (event.cpp) ---
    void evt_chapter_start();                                                  // 0x66A10
    void evt_task(uint32_t task);                                              // 0x66AC0
    void evt_run(uint32_t task);                                               // 0x66AD0
    uint32_t evt_event_lists();                                                // the event lists (arcade ROM / PC EVTALL.RBH)
    void evt_call(uint32_t routine);                                           // callx through evt_handlers / evt_et_handlers
    void evt_camdr_call(int32_t mode);                                         // callx through camdr_modes
    void evt_skip_block();                                                     // inline in ET1P / ET2P
    void evt_ET1P();                                                           // 0x31500
    void evt_ET2P();                                                           // 0x31560
    void evt_ENTB();                                                           // 0x315C0
    void evt_entb_record(uint32_t rec);                                        // ENTB's body for one record
    void evt_attract_restore(uint32_t pc);                                     // Attract Gore: the PC's prologue spawns
    void evt_ENTB2();                                                          // 0x31870
    void evt_ENTB3();                                                          // 0x31900
    void evt_ENTBE();                                                          // 0x319C0
    void evt_WLTB();                                                           // 0x66B60
    void evt_CLTB();                                                           // 0x66BE0
    void evt_KGTB();                                                           // 0x66C20
    void evt_BGTB();                                                           // 0x66C60
    void evt_CCTB();                                                           // 0x66C90
    void evt_WAIT_ENNUM();                                                     // 0x66D00
    void evt_WAIT_FRM();                                                       // 0x66D60
    void evt_WAIT_ITEM();                                                      // 0x66DF0
    void evt_WAIT_CAMJOB();                                                    // 0x66E60
    void evt_WAIT_CAMFRM();                                                    // 0x66EC0
    void evt_WAIT_ENALV();                                                     // 0x66F50
    void evt_BSFT();                                                           // 0x66FB0
    void evt_PYPMD();                                                          // 0x66FE0
    void evt_SHDWLT();                                                         // 0x67010
    void evt_CGMFNSH();                                                        // 0x67040
    void evt_SKYKD();                                                          // 0x67070
    void evt_SKYTB();                                                          // 0x670A0
    void evt_FKRTB();                                                          // 0x670D0
    void evt_CHGSM();                                                          // 0x67100
    void evt_NCHGCF();                                                         // 0x67130
    void evt_camdr_set(uint32_t at);                                           // the body of CAMDR / CAMDR2
    void evt_CAMDR();                                                          // 0x67160
    void evt_CAMDR2();                                                         // 0x67220
    void evt_GGCOL();                                                          // 0x67350 (PC: skips its operands)
    void evt_GGSET();                                                          // 0x67380 (PC: evt_skip1)
    void evt_TEXCHG();                                                         // 0x67450 (PC: evt_skip1)
    void evt_skip1();                                                          // PC 0x4C1514 (PC only: GGSET, TEXCHG)
    void evt_SCNINIT();                                                        // 0x675C0
    void scnin_clear_sprites_start();                                          // 0x677A0
    void scnin_reset_543530();                                                 // 0x6BA80
    void evt_REMFRM();                                                         // 0x677C0
    void evt_RNWCJ();                                                          // 0x677F0
    void evt_ITEMON();                                                         // 0x67830
    void evt_SNDON();                                                          // 0x67860
    void evt_ATCMD();                                                          // 0x67890
    void evt_GMTMF();                                                          // 0x678C0
    void evt_GMTAD();                                                          // 0x678F0
    void evt_CLRGFST();                                                        // 0x67920
    void evt_SKPETO();                                                         // 0x67950
    void evt_SKPETU();                                                         // 0x679A0
    void evt_SKIP();                                                           // 0x679F0
    void evt_EV3JP1();                                                         // 0x67A40
    void evt_WAIT();                                                           // 0x67A80
    void evt_NEXTEV();                                                         // 0x67A90
    void pc_point_transform(float x, float y, float z, float& ox, float& oy, float& oz);   // PC 0x46C6D0 (PC only)
    // boundaries of the interpreter (not ported yet)
    virtual void shadow_light_reset(uint32_t a, uint32_t b);                   // 0x6C2A0 (shadow.cpp)
    virtual void color_row_set(int32_t row, int32_t entry) {}                  // 0x13F20
    virtual void ggset_blood_row(int32_t row) {}   // (host, display only) GGSET's row whose entry differs by blood colour
    virtual void texture_area_load(uint32_t area, int32_t b) {}                // 0x34FF0
    virtual void rand_seed(uint32_t seed) {}                                   // 0x34A40
    virtual void sound_queue_reset() {}                                        // 0x15630
    virtual void sound_send3(uint32_t v) {}                                    // 0x15590
    virtual void pc_chapter_files_load(int32_t chapter, int32_t b) {}          // PC 0x418AA0 (PC only)
    virtual void pc_music_fade_ending() {}                                     // PC 0x41B248 (PC only)
    virtual void pc_music_fade_chapter() {}                                    // PC 0x41B2A8 (PC only)
