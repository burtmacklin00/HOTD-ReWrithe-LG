# sound2 (GitHub #26, 2026-11-09): MAME's SCSP (HOTD_SCSP_DIR/scsp.cpp, mame0289, BSD-3-Clause; supplied at build time,
# not in the repo) with the YMF292's hardware behaviour where MAME differs audibly on HOTD's music. The copy with these
# edits goes to the build folder (scsp_hw/scsp.cpp) and is the one built; the original is not touched. Each edit is a
# plain text replacement on mame0289's file and the configure step stops if an anchor is missing (another MAME tag).
# The behaviour follows the SCSP's documented / measured hardware behaviour (docs/teaser_status/sound_board.md,
# "Arcade FM, part 2"); every fix is one bit of hotd_scsp_fix (src/audio/sound_board.cpp), on by default, and
# HOTD_SCSP_MAME=<bits> (or "all") turns fixes off again at run time for A/B tests:
#   1  FM: the modulation keeps 1/64-sample precision and moves the interpolation (MAME rounds it to whole samples)
#   2  EG attack: starts at -60 dB and rises exponentially in the chip's attenuation scale (MAME: linear from -8.5 dB)
#   4  key-rate scaling: clamp(KRS + octave, 0, 15) added to each rate, rate 0 = hold (MAME: octave + 2 KRS + FNS bit 9)
#   8  timers: a reload is loaded on the next prescaler tick of the chip's sample counter (MAME: counts from the write)
#  16  FM (with 1): the chip's interpolation quirk, the second point's whole part from the next slot's phase fraction

# one edit: old -> new in the parent's `text` (named arguments keep the code's semicolons)
function(_hotd_scsp_edit old new)
    string(FIND "${text}" "${old}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "scsp_hw.cmake: ${HOTD_SCSP_HW_SRC} is not mame0289's scsp.cpp (anchor not found: ${old})")
    endif()
    string(REPLACE "${old}" "${new}" text "${text}")
    set(text "${text}" PARENT_SCOPE)
endfunction()

function(hotd_scsp_hw_patch src dst)
    set(HOTD_SCSP_HW_SRC "${src}")
    file(READ "${src}" text)
        # the helpers, before the device's code
        _hotd_scsp_edit([==[DEFINE_DEVICE_TYPE(SCSP, scsp_device]==]
        [==[// HOTD ReWrithe (cmake/scsp_hw.cmake): hardware behaviour, one bit each (sound_board.cpp: hotd_scsp_fix)
extern int hotd_scsp_fix;
static u32 hotd_sc = 0;      // the chip's sample counter (EG clocking, timer ticks)
static int hotd_att[32];     // the attack's attenuation per slot, 0 (loudest) .. 0x3FF, 6 dB per 0x40
// key-rate scaling: added to the 5-bit rates; KRS 0xF = off
static int hotd_kes(int krs, int oct) { return krs == 0xf ? 0 : std::clamp(krs + ((oct ^ 8) - 8), 0, 15); }
// a 5-bit rate with the scaling on MAME's 64-step tables (2 steps per rate); rate 0 does not move
static int hotd_rate(const int *table, int r, int kes) { return r ? table[2 * std::min(0x1f, r + kes)] : 0; }
// the EG's clock: rates 0x18 and up every 2nd sample; each 2 rates below halve it, odd rates add a clock in between
static bool hotd_eg_clock(int er, u32 sc)
{
	if (!sc) return false;
	int tz = 0;
	while (!((sc >> tz) & 1)) ++tz;
	const int w = (0x18 - std::min(0x18, er)) >> 1;
	if (er < 0x18 && (er & 1)) return tz == w + 1 || tz == w + 2;
	return w ? tz == w : (sc & 1) == 0;
}
// the attack's step: the attenuation drops by 1 / 2^n of itself per clock (n 4 for slow rates .. 1 for the fastest)
static int hotd_eg_shift(int er, u32 sc)
{
	const int e = std::clamp(er, 0x18, 0x1e), w = (0x18 - std::min(0x18, er)) >> 1;
	return ((0x20 - e) >> 1) + (e & 1 & int(sc >> (w + 1)));
}
// a timer (re)load: the value is loaded on the next prescaler tick, then counts up to 0xFF (one tick per 2^n samples)
static attotime hotd_timer_time(u32 clock, int pris, int value)
{
	const u32 ticks = u32(pris - int(hotd_sc % u32(pris))) + u32(255 - value) * u32(pris);
	return attotime::from_ticks(u64(ticks) * 512, clock);
}

DEFINE_DEVICE_TYPE(SCSP, scsp_device]==])

        # key-rate scaling (fix 4): the rates after MAME's Compute_EG
        _hotd_scsp_edit([==[slot->EG.EGHOLD = EGHOLD(slot);]==]
        [==[slot->EG.EGHOLD = EGHOLD(slot);
	if (hotd_scsp_fix & 4)
	{
		const int kes = hotd_kes(KRS(slot), OCT(slot));
		slot->EG.AR = hotd_rate(m_ARTABLE, AR(slot), kes);
		slot->EG.D1R = hotd_rate(m_DRTABLE, D1R(slot), kes);
		slot->EG.D2R = hotd_rate(m_DRTABLE, D2R(slot), kes);
		slot->EG.RR = hotd_rate(m_DRTABLE, RR(slot), kes);
	}]==])
        # (a rewrite of DL / RR on a sounding slot keeps the scaling)
        _hotd_scsp_edit([==[slot->EG.RR = Get_DR(0, RR(slot));]==]
        [==[slot->EG.RR = (hotd_scsp_fix & 4) ? hotd_rate(m_DRTABLE, RR(slot), hotd_kes(KRS(slot), OCT(slot))) : Get_DR(0, RR(slot));]==])

        # the attack (fix 2): key on at -60 dB (0x280), or full at once when the scaled rate passes 0x1F
        _hotd_scsp_edit([==[	slot->EG.volume = 0x17F << EG_SHIFT;
	slot->Prev = 0;]==]
        [==[	slot->EG.volume = 0x17F << EG_SHIFT;
	hotd_att[slot->slot] = AR(slot) + hotd_kes(KRS(slot), OCT(slot)) >= 0x20 ? 0 : 0x280;
	if (hotd_scsp_fix & 2)
		slot->EG.volume = (0x3ff - hotd_att[slot->slot]) << EG_SHIFT;
	slot->Prev = 0;]==])
        _hotd_scsp_edit([==[slot->EG.volume += slot->EG.AR;]==]
        [==[if (hotd_scsp_fix & 2)
			{
				int &att = hotd_att[slot->slot];
				const int er = std::min(0x1f, AR(slot) + hotd_kes(KRS(slot), OCT(slot)));
				if (AR(slot) != 0 && att > 0 && hotd_eg_clock(er, hotd_sc))
					att = std::max(0, att + ((~att) >> hotd_eg_shift(er, hotd_sc)));
				slot->EG.volume = (0x3ff - att) << EG_SHIFT;
			}
			else
				slot->EG.volume += slot->EG.AR;]==])
        # (the attack's level goes through the same dB table as the other phases)
        _hotd_scsp_edit([==[sample = (sample * EG_Update(slot)) >> SHIFT;]==]
        [==[sample = (hotd_scsp_fix & 2) ? (sample * m_EG_TABLE[EG_Update(slot) >> (SHIFT - 10)]) >> SHIFT : (sample * EG_Update(slot)) >> SHIFT;]==])

        # FM (fix 1): the modulation in 1/64 samples (MDL 0-4: none), wrapped to 11 bits of whole samples; its fraction
        # plus the phase's weights the interpolation; the second point is the next sample (not loop-wrapped)
        _hotd_scsp_edit([==[if (MDL(slot) != 0 || MDXSL(slot) != 0 || MDYSL(slot) != 0)]==]
        [==[s32 hotd_frac = -1;   // (fix 1) the interpolation weight 0..63, -1 = MAME's
	if ((hotd_scsp_fix & 1) && (MDL(slot) != 0 || MDXSL(slot) != 0 || MDYSL(slot) != 0))
	{
		s32 off = 0;
		if (MDL(slot) > 4)
		{
			// the ring holds 2x each slot's output: (X + Y) / 2 = the sum of the two outputs
			const s32 sum = (m_RINGBUF[(m_BUFPTR + MDXSL(slot)) & 63] + m_RINGBUF[(m_BUFPTR + MDYSL(slot)) & 63]) / 2;
			off = ((sum * 64) >> (0x10 - MDL(slot))) & ~1;
		}
		const s32 pos = off + s32((slot->cur_addr & ((1 << SHIFT) - 1)) >> (SHIFT - 6));
		const s32 whole = util::sext(pos >> 6, 11);
		hotd_frac = pos & 63;
		u32 whole2 = u32(whole);
		if (hotd_scsp_fix & 16)
		{
			// the chip's quirk: the second point's whole part comes from the modulation plus the NEXT slot's phase
			// fraction (that slot not yet updated this sample; slot 31's next is slot 0, already updated)
			const SCSP_SLOT &ns = m_Slots[(slot->slot + 1) & 31];
			const s32 nf = s32((ns.cur_addr & ((1 << SHIFT) - 1)) >> (SHIFT - 6)) ^ (ns.Backwards ? 0x3f : 0);
			whole2 = u32(util::sext((off + nf) >> 6, 11));
		}
		const u32 at = slot->cur_addr >> SHIFT;
		addr1 = PCM8B(slot) ? at + u32(whole) : (at + u32(whole)) * 2;
		addr2 = PCM8B(slot) ? at + whole2 + 1 : (at + whole2 + 1) * 2;
	}
	else if (!(hotd_scsp_fix & 1) && (MDL(slot) != 0 || MDXSL(slot) != 0 || MDYSL(slot) != 0))]==])
        _hotd_scsp_edit([==[s32 fpart=slot->cur_addr & ((1 << SHIFT) - 1);]==]
        [==[s32 fpart = hotd_frac >= 0 ? hotd_frac << (SHIFT - 6) : s32(slot->cur_addr & ((1 << SHIFT) - 1));]==])
        _hotd_scsp_edit([==[s32 fpart = slot->cur_addr & ((1 << SHIFT) - 1);]==]
        [==[s32 fpart = hotd_frac >= 0 ? hotd_frac << (SHIFT - 6) : s32(slot->cur_addr & ((1 << SHIFT) - 1));]==])

        # the sample counter: one per output sample
        _hotd_scsp_edit([==[s32 smpl = 0, smpr = 0;]==]
        [==[s32 smpl = 0, smpr = 0;
		++hotd_sc;]==])

        # timers (fix 8)
        _hotd_scsp_edit([==[m_timerA->adjust(attotime::from_ticks(512, time));]==]
        [==[m_timerA->adjust((hotd_scsp_fix & 8) ? hotd_timer_time(clock(), m_TimPris[0], m_udata.data[0x18/2] & 0xff) : attotime::from_ticks(512, time));]==])
        _hotd_scsp_edit([==[m_timerB->adjust(attotime::from_ticks(512, time));]==]
        [==[m_timerB->adjust((hotd_scsp_fix & 8) ? hotd_timer_time(clock(), m_TimPris[1], m_udata.data[0x1A/2] & 0xff) : attotime::from_ticks(512, time));]==])
        _hotd_scsp_edit([==[m_timerC->adjust(attotime::from_ticks(512, time));]==]
        [==[m_timerC->adjust((hotd_scsp_fix & 8) ? hotd_timer_time(clock(), m_TimPris[2], m_udata.data[0x1C/2] & 0xff) : attotime::from_ticks(512, time));]==])
    # written only when it changes (no rebuild on every configure)
    set(old_text "")
    if(EXISTS "${dst}")
        file(READ "${dst}" old_text)
    endif()
    if(NOT old_text STREQUAL text)
        file(WRITE "${dst}" "${text}")
    endif()
endfunction()
