// Declarations of class Game, included inside its body (port/game.h, docs/port.md "Adding a kind"): support routines,
// second batch (the hit effects, the per-type part damage, the looking heads, the hit notice and shake, kinds 101 /
// 102, guard action 6), first written as runtime overrides (src/game/t1_bounds.cpp, T1) and moved here for their
// proofs (docs/proof.md "Support routines (2)"). Arcade forms only: the virtual ones are instantiated for Game<Pc> for
// its vtable alone (their PC bodies do nothing; ProofPc replays them as boundaries).
    // --- the spawners and their tasks (hit_effects.cpp) ---
    virtual void blood_drip_at(float x, float y, float z);                   // 0x43510
    virtual void blood_burst_spawn(uint32_t part);                            // 0x437A0
    virtual void part_bleed_spawn(uint32_t part);                             // 0x43740
    virtual void part_bleed_start(uint32_t part);                             // 0x43770
    virtual void part_hit_spark(uint32_t part);                               // 0x437D0
    virtual void hit_effect_at(float x, float y, float z);                    // 0x43DF0
    virtual void floor_splat_spawn(uint32_t obj);                             // 0x43EC0
    virtual void floor_splat_spawn_b(uint32_t obj);                           // 0x43F80
    virtual void hop_dust_spawn(float x, float y, float z);                   // 0x44B50
    virtual void prop_spurt_spawn(uint32_t obj);                              // 0x438F0
    virtual void shot_flash_spawn(float x, float y, float z, int player, int code);   // 0x30EF0
    virtual void shot_effect(uint32_t task);                                  // 0x95270
    void wall_dust_spawn(uint32_t obj, uint32_t angle);                       // 0x44080
    // the effect tasks
    void blood_drip(uint32_t t);                                              // 0x44E70
    void blood_burst(uint32_t t);                                             // 0x44EF0
    void part_node_free(uint32_t part);                                       // mem_free of a part node (nan had blood_burst_release first; ch4proof: the original again)
    void part_bleed(uint32_t t);                                              // 0x435A0
    void hit_splash(uint32_t t);                                              // 0x43D50
    void floor_splat(uint32_t t);                                             // 0x43E50
    void floor_dust(uint32_t t);                                              // 0x43F00
    void wall_dust(uint32_t t);                                               // 0x43FB0
    void hop_dust(uint32_t t);                                                // 0x45340
    void prop_spurt(uint32_t t);                                              // 0x451A0
    void part_spark_anim(uint32_t t);                                         // 0x44FC0
    void shot_flash(uint32_t t);                                              // 0x30E50
    void shot_effect_flash(uint32_t t);                                       // 0x95140
    void shot_effect_debris(uint32_t t);                                      // 0x95630
    void hit_spark(uint32_t t);                                               // 0x83030
    virtual void muzzle_flash_spawn(int which);                               // 0x43200
    void muzzle_flash(uint32_t task);                                         // 0x44CF0
    // ch4proof (the whole-game differential, Chapter 1): the cut-scene props body_part_draw_hook spawns
    void caaat_piece_spawn(uint32_t obj, uint32_t x, uint32_t y, uint32_t z); // 0x43330 (x, y, z: camera space, float bits)
    void caaat_piece(uint32_t t);                                             // 0x44D80
    void magazine_drop(uint32_t t);                                           // 0x9CCD0
    // helpers without an original routine of their own
    void fx_to_world(const uint32_t in[3], uint32_t out[3]);                  // camera space -> world (inline)
    void floor_splat_frame(uint32_t t, bool dust);                            // floor_splat / floor_dust
    // --- the per-type part damage routines, table 0xC66A0 by hit_type (damage_types.cpp) ---
    void part_damage_type00_dkiller(uint32_t part);                           // 0x689E0
    void part_damage_type01_mon(uint32_t part);                               // 0x68B50
    void part_damage_type18_semu(uint32_t part);                              // 0x68E20
    void part_damage_type53_tetuman(uint32_t part);                           // 0x68EC0
    void part_damage_type11_zom(uint32_t part);                               // 0x68F20
    void part_damage_type16_zonbiman(uint32_t part);                          // 0x69010
    void part_damage_type12_debu(uint32_t part);                              // 0x690C0
    void part_damage_type32_gilmoa(uint32_t part);                            // 0x691E0
    void part_damage_type31_haris(uint32_t part);                             // 0x692C0
    void part_damage_type24_mummy(uint32_t part);                             // 0x693A0
    void part_damage_type57_mummy2(uint32_t part);                            // 0x69470
    void part_damage_none(uint32_t part) { (void)part; }                      // 0x69540 (ret)
    // helpers without an original routine of their own
    void part_damage_head_off(uint32_t part);                                 // the head-off types' common body (inline)
    void part_child_sever(uint32_t part, uint32_t i, bool drip);              // inline
    void part_child_sever_first(uint32_t part, uint32_t joint);               // inline
    // --- the looking heads, the hit notice and shake (body_hooks.cpp) ---
    void body_hook_eye_beam(uint32_t body);                                   // 0x9E820
    void body_hook_target_beam(uint32_t body);                                // 0x9EB30
    void player_hit_notice(uint32_t task);                                    // 0x6290
    void hit_screen_shake();                                                  // 0x6350
    virtual void hud_player_notice_show(uint32_t task, int n);                // 0x945D0 (hud_state.cpp)
    // helpers without an original routine of their own
    void body_look_at(uint32_t body, float x, float y, float z);              // the beams' common body (inline)
    // --- kinds 101 / 102, guard action 6, turn_motion_set, voice_message (scripted_extra.cpp) ---
    void kind102_enemy_wait_camera(uint32_t obj);                             // 0x55B30
    void kind102_enemy_walk_in(uint32_t obj);                                 // 0x563E0
    void enemy_act6_frame(uint32_t obj);                                      // 0x56360
    void enemy_act6_bite(uint32_t obj);                                       // 0x56320
    void kind101_corpse(uint32_t obj);                                        // 0x5BE60
    void kind118_corpse(uint32_t obj);                                        // 0x5C0A0
    void kind118_corpse_frame(uint32_t obj);                                  // 0x5BF80
    virtual void turn_motion_set(uint32_t obj, uint32_t motion, int a, int frames);   // 0x387B0
    virtual void voice_message(int n);                                        // 0x311C0
    // --- the enemy helpers (enemy_helpers.cpp) ---
    virtual void depth_list_add(uint32_t obj);                                // 0x45480
    virtual void attacker_list_add(uint32_t obj);                             // 0x32390
    virtual void enemy_target_distance(uint32_t obj);                         // 0x768F0
    void difficulty_level();                                                  // 0x264F0
    // --- the palette animation (palette.cpp) ---
    void palette_animation();                                                 // 0x25310
    virtual void palette_animation_step(int palette, int level) {}            // 0x24020
    virtual void palette_grey_build();                                        // 0x1A400
    virtual void vblank_callback_add(uint32_t routine);                       // 0x2780
    void palette_bank2_upload();                                              // 0x1A300 (proofs-5)
    virtual void palette_restore_vblank();                                    // 0x1A710 (proofs-5)
    void palette_anim_copy();                                                 // 0x24E90 (proofs-5; the runtime's renderer reads work RAM)
    void palette_bank2_from(uint32_t src);                                    // the bank-2 copy of 0x1A300 / 0x1A710 (inline)
    virtual uint32_t angle_to_camera(uint32_t obj);                           // 0x69A80
    void enemy_dying(uint32_t obj);                                           // 0x76B80
    void enemy_corpse_sink(uint32_t obj);                                     // 0x76D60
    void enemy_corpse(uint32_t obj);                                          // 0x76CB0
    void part_tree_free_children(uint32_t node);                              // 0x38F50
    virtual void body_free_parts(uint32_t obj);                               // 0x39030
    virtual void enemy_free_extras(uint32_t obj);                             // 0x32C90
    virtual void angle_turn_toward(uint32_t obj, uint32_t angle, uint32_t rate);   // 0x1CF10
    virtual uint32_t walk_to(uint32_t obj, float x, float z, uint32_t turn, float within);   // 0x1D2D0
    virtual void enemy_scripted_to_ai(uint32_t obj);                          // 0x54EB0
    virtual void turn_toward_camera(uint32_t obj, uint32_t step);             // 0x1CF90
    virtual void move_along_heading(uint32_t obj, float step);                // 0x69DD0
    // --- combat support (combat_support.cpp) ---
    virtual void enemy_slot_take(uint32_t obj);                               // 0x32320
    virtual void attack_queue_role(uint32_t obj);                             // 0x31F00
    virtual uint32_t attack_front_busy(uint32_t obj);                         // 0x831D0
    void kill_score_award(int player, int points);                            // 0x304D0
    virtual void kill_score_shooter(uint32_t obj, int points);                // 0x30490
    virtual void enemy_kill_score(uint32_t obj, int points);                  // 0x304C0
    virtual void researcher_scream_penalty(uint32_t obj, uint32_t woman, uint32_t known);   // 0x6BDF0
    virtual void researcher_hit_by_player(uint32_t obj);                      // 0x70870
    void hostage_shot_effect(uint32_t obj);                                   // 0x1DCD0
    void researcher_blood_effect(uint32_t obj);                               // 0x1DB30
    void molb_floor_stick(uint32_t obj);                                      // 0x2E120
    void molb_floor_above(uint32_t obj);                                      // 0x2E180
    void molb_floor_bounce(uint32_t obj);                                     // 0x2E200
    virtual void velocity_for_arc(uint32_t obj, uint32_t from, uint32_t to, uint32_t gravity, uint32_t frames);   // 0x6A6F0
    virtual void velocity_for_arc_speed(uint32_t obj, uint32_t from, uint32_t to, uint32_t gravity, uint32_t speed, uint32_t outFrames);   // 0x6A760
    virtual uint32_t room_bounds_bounce(uint32_t obj);                        // 0x2F990 (1 = a wall hit)
    virtual void camera_side_set(uint32_t obj);                               // 0x2F6C0
    uint32_t player_in_play(int p);                                           // 0x27470
    virtual uint32_t combat_active();                                         // 0x274C0
    virtual void pick_target_player(uint32_t obj);                            // 0x2F7B0
    virtual void pick_target_player_any(uint32_t obj);                        // 0x2F830
    virtual void pick_target_player_rand(uint32_t obj);                       // 0x6A6C0
    virtual void spline_segment_init(uint32_t buf, uint32_t points);          // 0x347B0
    void spline_eval(uint32_t buf, float t, uint32_t out);                    // 0x34880
    virtual void spline_advance(uint32_t obj, uint32_t buf, uint32_t speed, uint32_t step, uint32_t outA, uint32_t outB);   // 0x2DB70 (floats as bits)
    virtual uint32_t is_demo();                                               // 0x27510
    // --- the part tree (part_tree.cpp) ---
    void part_tree_detach_severed_rec(uint32_t node, uint32_t parent, uint32_t index);   // 0x39440 (index: the caller's loop word)
    void part_tree_detach_severed(uint32_t root);                             // 0x39550
    // a head blown off: the falling pieces (blow_off.cpp, proofs-8)
    void severed_part_init(uint32_t piece, uint32_t node);                   // 0x39830
    void severed_part_fall(uint32_t obj);                                    // 0x69730 (task)
    void severed_part_draw(uint32_t obj);                                    // 0x38B10
    void part_node_draw_rec(uint32_t node);                                  // 0x38A50
    void severed_part_free(uint32_t piece);                                  // 0x38FA0
    void part_subtree_free(uint32_t root);                                   // 0x390F0 UNVERIFIED (teaser): no non-head subtree blown off in p8_blow
    void floor_blood_splat(float x, float y, float z);                       // 0x43920
    void floor_blood_splat_task(uint32_t obj);                               // 0x45230 (task)
    void eyeball_pop(uint32_t obj);                                          // 0x43970 (task)
    uint32_t geo_polygon_ram_write(uint32_t src, uint32_t addr, int32_t polygons);   // 0x19D0
    void sophie_skin_build(uint32_t obj);                                    // 0x21920
    void shadow_projection(uint32_t plane);                                  // 0x6C490
    virtual void body_free_cut_parts(uint32_t root);                          // 0x396C0
    void body_role_model_set_rec(uint32_t part, int role, uint32_t model);    // 0x1D8A0
    virtual void body_role_model_set(uint32_t obj, int role, uint32_t model); // 0x1D910
    void body_node_model_rec(uint32_t part, int joint);                       // 0x6A3C0
    virtual uint32_t body_node_model(uint32_t obj, int node);                 // 0x6A410
    void body_part_set_model_rec(uint32_t part);                              // 0x6A470
    virtual void axe_hand_model_hide(uint32_t obj, int node, uint32_t model); // 0x6A640
    virtual void body_part_set_model(uint32_t obj, int node, uint32_t model); // 0x6A4F0
    // --- a waiting player and the start check (player_start.cpp) ---
    uint32_t player_can_start(int p);                                         // 0x273E0
    uint32_t player_start_pressed(int p);                                     // 0x27450
    void credit_lockout_reset(int p, int32_t credits);                        // 0x1FA80
    virtual uint32_t player_start_check(uint32_t task, int state);            // 0x6020 (1 = started)
    void player_st9_waiting(uint32_t task);                                   // 0x5620
    void player_waiting(uint32_t task);                                       // 0x5660
    virtual void player_waiting_frame(uint32_t task);                         // 0x5670
    virtual void player_demo_start(uint32_t task) {}                          // 0x57E0
    virtual void player_lesson_start(uint32_t task) {}                        // 0x57F0
    virtual void set_mode(int mode);                                          // 0xDE70 (game_flow.cpp)
    virtual void hud_start_prompt(int p, int which, int on);                  // 0x209A0 (hud_state.cpp)
    virtual void hud_prompt_anim(uint32_t routine, int x, int y, int p, int on) {}   // table 0xAA450 (0x20C50 / 0x20CB0 / 0x20D80)
    // --- kind 138, prop 068 (prop_groups.cpp) ---
    void kind138_prop_group(uint32_t obj);                                    // 0x3A780 UNVERIFIED (teaser): ends in task_exit (no exit record in a capture)
    void prop068_furukagi(uint32_t obj);                                      // 0x3E630
    // helpers without an original routine of their own (combat_support.cpp)
    void blood_mark_effect(uint32_t obj, bool fixed);                         // hostage_shot_effect / researcher_blood_effect
    void floor_under(uint32_t obj, float out[3]);                             // the molb floor modes' probe (inline)
    // --- the game over (game_over.cpp) --- proofs-5; first written in the runtime (src/game/flow.cpp)
    void player_st6_game_over(uint32_t task);                                 // 0x5470
    void player_game_over_wait(uint32_t task);                                // 0x5540
    void game_over_camera(uint32_t task);                                     // 0x97490
    Tgp::Out game_over_cam_point(float x, float y, float z);                  // game_over_camera's coprocessor sequence (inline)
    void game_over_walker_task(uint32_t obj);                                 // 0x5CCD0
    void game_over_route_task(uint32_t obj);                                  // 0x5CF50
    void sky_task(uint32_t task);                                             // 0x33F50 (sky.cpp)
    // --- the sprite banks (sprite_banks.cpp) --- proofs-5; first written in the runtime (src/game/sprites.cpp)
    void sprite_slots_mark();                                                 // 0x17A10
    void sprite_banks_init();                                                 // 0x17930
    void palette_text_white();                                                // 0x1B270
    int sprite_bank_size(int sheet);                                          // 0x17AA0
    uint32_t sprite_slots_free(int first, int count);                         // 0x17AF0
    void sprite_bank_alloc(uint32_t rec, int sheet);                          // 0x17B20
    virtual void sprite_bank_tiles_load(uint32_t rec, int sheet);             // 0x17CB0
    void sprite_bank_palette_load(uint32_t rec, int sheet);                   // 0x17C10
    virtual void palette_queue_add(int32_t first, int32_t count) {}           // 0x1A980
    virtual void player_nop() {}                                              // 0x94F30
    virtual void player_continue_nop(uint32_t task) {}                        // 0x94F20 (ret only; player_continue_frame's first call)
    virtual void hud_game_over_sprite(int p);                                 // 0x950E0 (hud_state.cpp)
    virtual void hud_game_over_sprite_final(int p);                           // 0x95110 (hud_state.cpp)
    void hud_sprites_load();                                                  // 0x263D0 (hud_state.cpp)
    void hud_continue_sprite(int p);                                          // 0x95010 (hud_state.cpp)
    void hud_continue_digit(int p, int digit);                                // 0x95040
    void hud_continue_label(int p);                                           // 0x95080
    void continue_screen_draw();                                              // 0x26260 (proofs-10: p10_cont2)
    void life_bar_draw(int32_t value);                                        // 0x6BAF0
    virtual void bookkeeping_play_end(int p) {}                               // 0x166D0
    virtual void game_over_timer_tick() { mem.set32(0x5433E4, mem.u32(0x5433E4) - 1); }   // 0x5D4A8 (the route task's tail jump)
    virtual void tilemap_clear_rect(uint32_t layer, int x, int y, int w, int h) {}   // 0x18110
    void game_over_start();                                                   // 0x97200 (mode 5, table 0xCA770)
    void game_over_state0_camera();                                           // 0x97740 (group 0xCA790)
    void game_over_camera_start();                                            // 0x5CCA0 (group 0xC5440)
    void game_over_camera_task();                                             // 0x5D4F0
    void game_over_route_start();                                             // 0x5D4D0
    void game_over_walkers_start();                                           // 0x5CE70
    void game_over_scene();                                                   // 0x97340
    void game_over_map_start();                                               // 0x5CB50
    void game_over_map();                                                     // 0x5CBF0
    void game_over_text();                                                    // 0x973A0
    void game_over_wait();                                                    // 0x97430
    void map_scroll_set(uint32_t pos);                                        // 0x5CF30
    virtual void heap_reset();                                                // 0x14640 (tasks.cpp)
    virtual uint32_t task_group_create(uint32_t table);                       // 0x14C10 (tasks.cpp)
    void skin_buffers_alloc();                                                // 0x35090 (tasks.cpp)
    virtual void heap_block_insert(uint32_t block, uint32_t into);            // 0x14810 (tasks.cpp) UNVERIFIED (teaser): only unused routines call it
    virtual void heap_block_free(uint32_t p);                                 // 0x14A50 (tasks.cpp)
    // --- mode 4, the game (game_flow.cpp) --- proofs-7; first written in the runtime (src/game/flow.cpp, clear.cpp)
    bool flow_player_out(int p);                                              // the state table's "out" byte (inline)
    void mode4_game();                                                        // 0x25C20
    void game_play();                                                         // 0x25F40
    void game_frame(uint32_t groupPtr);                                       // 0x262A0
    void game_chapter_end();                                                  // 0x25FF0
    virtual void game_chapter_end_wait();                                     // 0x26020
    void game_chapter_start();                                                // 0x25D40 (chstart; first written in the runtime: Runtime::chapter_continue)
    virtual void chapter_load(uint32_t groupPtr);                             // 0x25DA0 (chstart; virtual: the runtime's set-up around it)
    virtual void chapter_screen_reset() {}                                    // 0x17280 (the runtime's 2D layer)
    virtual void hud_mode_init() {}                                           // 0x205A0 (the runtime's prompts)
    void game_continue_begin();                                               // 0x26080
    void game_continue_countdown();                                           // 0x26100
    void player_continue_begin(int p);                                        // 0x5230 (proofs-10: p10_cont2, a continue)
    void player_st4_out_of_lives(uint32_t task);                              // 0x5260
    void player_continue_start(uint32_t task);                                // 0x52C0 (proofs-10: p10_cont2, 2 players, one continues)
    void player_continue_frame(uint32_t task);                                // 0x5300
    // --- the chapter's resets and per-frame tasks (chapter_tasks.cpp) --- proofs-7; first written in the runtime
    void shot_lists_clear();                                                  // 0x453C0
    void chapter_reset_53e1b0();                                              // 0x32290
    void chapter_counters_reset();                                            // 0x67480
    void attack_slots_task(uint32_t task);                                    // 0x31C00
    void depth_lists_task(uint32_t task);                                     // 0x53480
    // the chapter's start table 0xAB210 (chapter_tasks.cpp; worker chstart, first written in the runtime)
    void sky_task_start();                                                    // 0x34130
    void input_task_start();                                                  // 0x5DA0
    void InitSetPlayerTask();                                                 // 0x5D30 (the runtime adds its own form: a player started in play, 4P)
    void shake_task_start();                                                  // 0x5DD0
    void world_init();                                                        // 0x4B650
    void attack_slots_task_start();                                           // 0x31D40
    void letterbox_task_start();                                              // 0x44AF0
    void depth_lists_task_start();                                            // 0x53370
    void demo_timer_start();                                                  // 0x23CA0
    // --- kind 176, the chapter clear (chapter_clear.cpp) --- proofs-7; first written in the runtime (src/game/clear.cpp)
    void kind176_score_draw(int p, int x, int y, int dx);                     // 0x9B690
    uint32_t kind176_bonus_lives();                                           // the table 0xCD510 lookup (inline)
    void kind176_bonus_add();                                                 // the bonus line (inline in 0x9B9F0 / 0x9BC00)
    void kind176_backdrop_draw();                                             // model 0x65E (inline)
    virtual void kind176_chapter_clear(uint32_t obj);                         // 0x9B880
    virtual void kind176_results(uint32_t obj);                               // 0x9B9F0
    virtual void kind176_results_hold(uint32_t obj);                          // 0x9BC00
    virtual void kind176_results_end(uint32_t obj);                           // 0x9BDC0
    // --- kind 182, the grey scene (support_c.cpp) --- proofs-7; first written in the runtime (src/game/palette.cpp)
    void kind182_grey_scene(uint32_t obj);                                    // 0x9B600
    void kind182_wait(uint32_t obj);                                          // 0x9B620
    void kind182_grey_hold(uint32_t obj);                                     // 0x9B660
    void object_draw_body(uint32_t obj);                                      // 0x37D80 (draw mode 1; support_c.cpp)
    void body_part_draw_mat(uint32_t part);                                   // 0x37B80
    // --- the players' start states (player_join.cpp) --- proofs-7; first written in the runtime (src/game/players.cpp)
    void hud_popup_clear(uint32_t task);                                      // 0x94430 UNVERIFIED (teaser): no popup up at the continues of p10_cont2
    void player_st1_continue(uint32_t task);                                  // 0x4F80 (proofs-10: p10_cont2)
    void player_st2_rejoin(uint32_t task);                                    // 0x4FE0 (proofs-8: p8_sweep1)
    void player_st3_join(uint32_t task);                                      // 0x4FF0 (+ player_body_reinit 0x5008; proofs-9: p9_join2, capture size 4ff0:b4, the debug-mode body path not reached)
    void PlStartInit(uint32_t task, int how);                                 // 0x5870
    void player_camdr_set(uint32_t task, uint32_t bodyRoutine);               // 0x61C0 / 0x61F0 (common part)
    void player_camdr_mode1(uint32_t task);                                   // 0x61C0
    void player_camdr_mode4(uint32_t task);                                   // 0x61F0 (proofs-9: p9_camdr)
    void player_camdr_mode3(uint32_t task);                                   // 0x4B7D0
    void player_camdr2_mode4(uint32_t task);                                  // 0x4B7F0 (proofs-9: p9_camdr)
    virtual void play_time_start(int p) {}                                    // 0x16670 (operator books, backup RAM)
    virtual void play_time_continue(int p) {}                                 // 0x16690
    virtual void task_group_run(uint32_t group) {}                            // 0x14D40
    virtual void geo_frame_begin() {}                                         // 0x1CA0
    virtual void geo_view_setup() {}                                          // 0x1E10
    virtual void palette_fade_finish() {}                                     // 0x18680
    virtual void next_mode() {}                                               // 0xDDC0
    virtual void bookkeeping_game_end() {}                                    // 0x16850
