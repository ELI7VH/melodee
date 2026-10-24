/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Both kit controls and event edits through actual DSP/UI/project paths. */
#define RECORDING_NO_MAIN 1
#include "recording_test.c"
static void kit(uint32_t model)
{
    uint32_t preset=model==DK_909?1u:model==DK_KIT808?0u:model==DK_CR78?4u:DRUM_PRESET_CIRCUIT;   /* (808 KIT: the Kit's) */
    midi_test_reset();set_engine_of(TSEL,ENGI_DRUM);apply_preset_to(TSEL,preset);TSEL->engine=TSEL->eng_req;panic_req=0;song.master_q12=4096;
}
static void scope(uint32_t sc)
{ui.home=0;for(uint32_t i=0;i<NPAGES;i++)if(PAGES[i].scope==sc){ui.page=(uint8_t)i;break;}ui.force=1;}
static uint32_t active_count(void)
{uint32_t n=0;for(uint32_t i=0;i<NVOICE;i++)n+=TSEL->v[i].active;return n;}
static int32_t audio(uint32_t blocks)
{
    int32_t out[2*CTL],peak=0;
    for(uint32_t b=0;b<blocks;b++){mix_block(out,CTL);for(uint32_t i=0;i<2*CTL;i++){int32_t a=out[i]<0?-out[i]:out[i];if(a>peak)peak=a;}}
    return peak;
}
static int voices909(void)
{
    int bad=0;
    for(uint32_t ins=0;ins<D9_NUM;ins++){
        kit(DK_909);trk_note_on(TSEL,DRUM_SOUND_NOTE[ins],110);
        int32_t peak=audio(FS/CTL);
        audio(FS*5/CTL);
        bad+=check("each sample-free 909 voice sounds, stays bounded and frees itself",peak>30 && peak<32768 && !active_count());
    }
    kit(DK_909);trk_note_on(TSEL,46,110);audio(FS/10/CTL);trk_note_on(TSEL,42,110);audio(FS/20/CTL);
    drum909_part_t *k=drum909_of(TSEL);
    bad+=check("909 closed hat chokes open hat without ending the closed hit",k && !k->d.smp[1].playing && k->d.smp[0].playing);
    kit(DK_909);for(uint32_t i=0;i<D9_NUM;i++)trk_note_on(TSEL,DRUM_SOUND_NOTE[i],127);
    bad+=check("909 respects the eight-voice shared budget",active_count()<=8 && audio(128)>30);
    kit(DK_909);for(uint32_t tr=0;tr<NTRK;tr++){set_engine_of(&trk[tr],ENGI_DRUM);apply_preset_to(&trk[tr],1);trk[tr].engine=trk[tr].eng_req;trk_note_on(&trk[tr],36,110);trk_note_on(&trk[tr],38,110);}panic_req=0;
    uint32_t total=0;int32_t peak=audio(128);for(uint32_t tr=0;tr<NTRK;tr++)for(uint32_t v=0;v<NVOICE;v++)total+=trk[tr].v[v].active;
    bad+=check("four independent 909 parts share the eight-voice budget and render together",total==8 && peak>30 && peak<32768);
    return bad;
}
static int controls(void)
{
    int bad=0;kit(DK_808);scope(SC_DRUM);ui.drum_sound=D9_SD;
    trk_note_on(TSEL,38,100);drum_lane_t *k=drum_kit_of(TSEL);int32_t base=k[DV_SNARE].r.f;
    drum_sound_edit(1,12);trk_note_on(TSEL,38,100);
    bad+=check("808 sound tune transposes the same snare by an octave",k[DV_SNARE].r.f>=base*2-2 && k[DV_SNARE].r.f<=base*2+2 && TSEL->v[0].note==38);
    drum_event_pitch=-12;trk_note_on(TSEL,38,100);drum_event_pitch=0;
    bad+=check("individual pitch offsets combine with sound tune without changing instrument",k[DV_SNARE].r.f==base && TSEL->v[0].note==38 && drum_patch[0].c[D9_SD][0]==12);
    kit(DK_909);drum_patch[0].c[D9_SD][1]=-32;trk_note_on(TSEL,38,100);float short_decay=drum909_of(TSEL)->d.bt[D9_SD].decay;
    drum_patch[0].c[D9_SD][1]=32;trk_note_on(TSEL,38,100);
    bad+=check("909 snare decay survives the source engine's trigger pinning",drum909_of(TSEL)->d.bt[D9_SD].decay>short_decay*3.0f);
    for(uint32_t model=DK_808;model<=DK_909;model++){
        kit(model);drum_event_length=FS/20;trk_note_on(TSEL,36,110);drum_event_length=0;audio(FS/10/CTL);
        bad+=check("edited hit length limits the sounding voice independently of key release",!active_count());
        trk_note_on(TSEL,36,110);audio(FS/10/CTL);
        bad+=check("ordinary one-shot hits retain their natural decay",active_count()==1);
    }
    kit(DK_909);scope(SC_DRUM);ui.drum_sound=D9_LT;page_over=0;
    for(uint32_t i=0;i<NPAGES;i++)if(PAGES[i].scope==SC_DRUM && PAGES[i].id[1]==4)ui.page=i;
    drum_sound_edit(1,-127);
    bad+=check("low/mid/high tom levels remain independent",drum_patch[0].c[D9_LT][3]==127 && !drum_patch[0].c[D9_MT][3] && !drum_patch[0].c[D9_HT][3]);
    return bad;
}
static int edits_and_store(void)
{
    int bad=0;kit(DK_909);scope(SC_DRUMHIT);ui.drum_sound=D9_BD;cursor_set(3);
    TSEL->step[3]=(step_t){{0},0,ST_NOTE,0,100,3,2,0};
    drum_hit_edit(2,7);uint32_t i=drum_hit_selected();recorded_note_t hit=recording[i];
    bad+=check("grid pitch edit retains the kick, snare and their velocities",i<RECORD_MAX && hit.note==36 && hit.pitch==7 && !drum_patch[0].c[D9_BD][0] && TSEL->step[3].hit==3);
    step_history_end();step_history_finish();drum_hit_edit(3,-8);step_history_end();step_history_finish();
    bad+=check("hit length is an independent half step",recording[i].length && recording[i].duration*(1u<<(recording[i].owner>>5))==RECORD_UNIT/2);
    bad+=check("hit undo restores pitch and natural length",step_history_apply(0) && recording[i].pitch==7 && !recording[i].length);
    bad+=check("hit redo restores the length override",step_history_apply(1) && recording[i].length);
    settings_preview=1;notes_preview();audition_tick(1);
    bad+=check("hit preview retains edited pitch and duration",TSEL->v[0].note==36 && TSEL->v[0].s[4]==7 && TSEL->v[0].s[5]);audition_stop();settings_preview=0;
    drum_hit_delete();step_history_end();step_history_finish();
    bad+=check("deleting one edited hit retains its neighboring manual snare",!recording[i].vel && TSEL->step[3].hit==2);
    bad+=check("hit deletion undo restores the original event and neighbors",step_history_apply(0) && recording[i].pitch==7 && recording[i].length && TSEL->step[3].hit==3);
    recording_restore_note(TSEL,1,(recorded_note_t){32000,32768,36,80,(uint8_t)recording_owner(TSEL),3,-4,1});
    drum_hit_jog(1);uint32_t second=drum_hit_selected();drum_hit_edit(2,1);
    bad+=check("SELECT can edit a repeated hit without changing the first hit",second==1 && recording[1].pitch==-3 && recording[i].pitch==7);
    step_history_end();step_history_finish();recording_remove(TSEL,1);ui.note_pick=(uint16_t)(i+1);ui.note_identity=recording[i];ui.note_generation=recording_generation;
    drum_patch[0].c[D9_BD][0]=5;drum_patch[0].c[D9_LT][3]=19;
    project_capture(&proj_scratch);project_store_t wire;project_t decoded;
    int ok=proj_pack(&wire,&proj_scratch) && proj_import(&decoded,&wire,sizeof wire);
    bad+=check("project roundtrip keeps sound settings, instrument, pitch and length",ok && decoded.drum[0].c[D9_BD][0]==5 && decoded.drum[0].c[D9_LT][3]==19 && decoded.recording[0].note==36 && decoded.recording[0].pitch==7 && decoded.recording[0].length);
    bad+=check("new bank schema fits existing atomic flash copies",BANK_STORE_SIZE<=27904u && bank_pack(proj_wire_u.raw,&proj_scratch,1) && bank_valid(proj_wire_u.raw,BANK_STORE_SIZE));
    TSEL->engine=ENGI_DRUM;TSEL->eng_req=ENGI_DRUM;song.playing=0;
    bad+=check("general drum preset stores its own sound controls",up_store(0,"OWN 909")==3 && (memset(drum_patch,0,sizeof drum_patch),!up_load(0)) && drum_patch[0].c[D9_BD][0]==5 && drum_patch[0].c[D9_LT][3]==19);
    template_save();memset(drum_patch,0,sizeof drum_patch);template_load();
    bad+=check("template restores the edited drum kit",drum_patch[0].c[D9_BD][0]==5 && drum_patch[0].c[D9_LT][3]==19);
    return bad;
}
static int order_and_lanes(void)
{
    int bad=0;
    for(uint32_t model=DK_808;model<=DK_909;model++){
        kit(model);scope(SC_DRUMHIT);recording_clear(TSEL);drum_pick(drum_sound_step(TSEL,0,-99));
        uint32_t n=drum_sound_count(TSEL),seen=0,lane=0,ok=ui.drum_sound==D9_BD;
        for(uint32_t k=0;k<n;k++){
            uint32_t s=ui.drum_sound,l=drum_lane(drum_sound_note(TSEL,s));
            ok&=!((seen>>s)&1u) && l>=lane && ui.lane==l;seen|=1u<<s;lane=l;
            drum_hit_edit(1,1);
        }
        bad+=check("KNOB 2 walks every drum once, lane by lane in the black keys' order",ok && seen==(1u<<n)-1u);
        drum_pick(D9_LT);drum_hit_edit(1,1);
        bad+=check("the toms sit together, low to high",ui.drum_sound==D9_MT && ui.lane==DV_TOM && (drum_hit_edit(1,1),ui.drum_sound==D9_HT));
        for(uint32_t k=0;k<27u;k++)if(key_black(k) && key_place(k)==DV_RIM)grid_keys(1u<<k);
        bad+=check("a lane key picks its lane's own drum and lights its lane",ui.drum_sound==D9_RS && ui.lane==DV_RIM);
    }
    kit(DK_909);scope(SC_DRUMHIT);recording_clear(TSEL);cursor_set(5);
    TSEL->step[5]=(step_t){{41,50},2,ST_NOTE,0,100,1u<<DV_TOM,0,0};
    drum_pick(D9_LT);int lt=step_sound_note(TSEL,&TSEL->step[5],D9_LT)==41;
    drum_pick(D9_MT);int mt=step_sound_note(TSEL,&TSEL->step[5],D9_MT)==DRUM_LANE_NOTE[DV_TOM];
    bad+=check("each tom of a step is told apart",lt && mt && step_sound_note(TSEL,&TSEL->step[5],D9_HT)==50 && step_sound_note(TSEL,&TSEL->step[5],D9_BD)<0);
    TSEL->step[5]=(step_t){{0},0,ST_NOTE,0,100,1u<<DV_BELL,0,0};drum_pick(D9_RD);
    bad+=check("the 909 ride's lane hit is the ride",step_sound_note(TSEL,&TSEL->step[5],D9_RD)==DRUM_LANE_NOTE[DV_BELL] && step_sound_note(TSEL,&TSEL->step[5],D9_CR)<0);
    drum_hit_key(0);
    bad+=check("a white key clears the 909 ride's grid hit",!TSEL->step[5].hit && !TSEL->step[5].n);
    step_history_end();step_history_finish();
    return bad;
}
static int corners909(void)
{
    int bad=0;kit(DK_909);TSEL->p[P_E2]=0;trk_note_on(TSEL,38,100);float dark=drum909_of(TSEL)->d.bt[D9_SD].noise_decay;TSEL->p[P_E2]=127;trk_note_on(TSEL,38,100);
    bad+=check("909 common tone changes the snare's noise tail",drum909_of(TSEL)->d.bt[D9_SD].noise_decay>dark*10.0f);
    kit(DK_909);TSEL->p[P_E5]=0;trk_note_on(TSEL,36,64);float noacc=drum909_of(TSEL)->d.bt[D9_BD].out_gain;TSEL->p[P_E5]=127;trk_note_on(TSEL,36,64);
    bad+=check("909 common accent changes velocity response",drum909_of(TSEL)->d.bt[D9_BD].out_gain!=noacc);
    kit(DK_909);trk_note_on(TSEL,36,100);float punch=drum909_of(TSEL)->d.bt[D9_BD].bd_df;TSEL->p[P_E6]=1;trk_note_on(TSEL,36,100);
    bad+=check("909 ROUND kick softens its pitch transient",drum909_of(TSEL)->d.bt[D9_BD].bd_df<punch);
    for(uint32_t corner=0;corner<2;corner++)for(uint32_t ins=0;ins<D9_NUM;ins++){
        kit(DK_909);for(uint32_t p=P_E1;p<=P_E7;p++)TSEL->p[p]=p==P_E6?(int16_t)corner:corner?127:0;
        int8_t *c=drum_patch[0].c[ins];c[0]=corner?24:-24;c[1]=c[2]=corner?63:-64;
        drum_event_pitch=corner?24:-24;trk_note_on(TSEL,DRUM_SOUND_NOTE[ins],127);drum_event_pitch=0;
        drum909_part_t *k=drum909_of(TSEL);int finite=1;
        for(uint32_t b=0;b<FS*2/CTL;b++){float f[CTL]={0};float *nz=d9_noise_block(&k->d,CTL);drum909_render_voice(&k->d,ins,nz,f,CTL);for(uint32_t j=0;j<CTL;j++)finite&=isfinite(f[j]) && f[j]>-100.0f && f[j]<100.0f;}
        bad+=check("909 native float voices remain finite at pitch/decay/character corners",finite);
    }
    return bad;
}
static int capacity_and_controls(void)
{
    int bad=0;kit(DK_808);
    project_capture(&proj_scratch);
    for(uint32_t i=0;i<RECORD_MAX;i++)proj_scratch.recording[i]=(recorded_note_t){(uint16_t)(i*61u),(uint16_t)(65535u-i),(uint8_t)(36+i%48),(uint8_t)(1+i%127),(uint8_t)((i%32)|((i%8)<<5)),(uint8_t)(i%64),(int8_t)((int32_t)(i%49)-24),(uint8_t)(i&1)};
    for(uint32_t t=0;t<NTRK;t++)for(uint32_t i=0;i<16;i++){int8_t *c=proj_scratch.drum[t].c[i];c[0]=(int8_t)((i+t)%2?24:-24);c[1]=(int8_t)((i+t)%2?63:-64);c[2]=-c[1]-1;c[3]=(int8_t)(i%2?127:0);}
    project_store_t wire;project_t decoded;
    int ok=proj_pack(&wire,&proj_scratch) && proj_import(&decoded,&wire,sizeof wire);uint32_t at=0;
    if(ok)for(uint32_t owner=0;owner<32;owner++)for(uint32_t i=0;i<RECORD_MAX;i++)if((proj_scratch.recording[i].owner&31u)==owner)ok&=!memcmp(&decoded.recording[at++],&proj_scratch.recording[i],sizeof(recorded_note_t));
    bad+=check("all 1024 hits retain full timing, instrument, velocity, owner, pitch and length",ok && at==RECORD_MAX && !memcmp(decoded.drum,proj_scratch.drum,sizeof decoded.drum));
    memset(proj_scratch.recording,0,sizeof proj_scratch.recording);
    for(uint32_t i=0;i<RECORD_MAX;i++)proj_scratch.recording[i]=(recorded_note_t){0,65535,36,127,255,63,24,1};
    bad+=check("one bank can still hold all 1024 events",proj_pack(&wire,&proj_scratch) && proj_import(&decoded,&wire,sizeof wire) && decoded.recording[1023].owner==255 && decoded.recording[1023].pitch==24);
    for(uint32_t sound=11;sound<16;sound++){
        kit(DK_808);drum_patch[0].c[sound][0]=12;trk_note_on(TSEL,drum_sound_note(TSEL,sound),100);drum_lane_t *k=drum_kit_of(TSEL);
        bad+=check("808 congas, clave and maraca have their own sound controls",drum_sound_of(TSEL,drum_sound_note(TSEL,sound))==sound && k[drum_lane(drum_sound_note(TSEL,sound))].r.pitch==192 && audio(64)>30);
    }
    return bad;
}
/* WaveLoop fork: KIT 808 and CR78 (the Kit's drum_voice.c) through the engine: every lane sounds, stays bounded and
 * frees its voice; a lane is mono; the hats choke; the corners stay under full scale; the 808's kick is lower and
 * longer than the CR78's (independent of re-baselined numbers) */
static int32_t audio_left(int32_t *buf,uint32_t blocks)   /* blocks of the mix, its left channel into buf */
{
    int32_t out[2*CTL],peak=0;
    for(uint32_t b=0;b<blocks;b++){mix_block(out,CTL);for(uint32_t i=0;i<CTL;i++){buf[b*CTL+i]=out[2*i];int32_t a=out[2*i]<0?-out[2*i]:out[2*i];if(a>peak)peak=a;}}
    return peak;
}
static uint32_t kick_crossings(const int32_t *y,uint32_t from,uint32_t to)
{uint32_t n=0;for(uint32_t i=from+1;i<to;i++)n+=(y[i-1]<0)!=(y[i]<0);return n;}
static uint32_t ring_ms(const int32_t *y,uint32_t len)      /* until 10 ms frames stay 30 dB under the loudest */
{
    double e[400]={0},top=0;uint32_t f,frames=len/441u,last=0;
    for(f=0;f<frames && f<400u;f++){for(uint32_t i=0;i<441u;i++)e[f]+=(double)y[f*441u+i]*y[f*441u+i];if(e[f]>top)top=e[f];}
    for(f=0;f<frames && f<400u;f++)if(e[f]>top/1000.0)last=f;
    return last*10u;
}
static int kits_kit(void)
{
    static int32_t y[FS*4];
    int bad=0;
    for(uint32_t model=DK_KIT808;model<=DK_CR78;model++){
        int ok=1;
        for(uint32_t l=0;l<DV_NLANE;l++){
            kit(model);trk_note_on(TSEL,DRUM_LANE_NOTE[l],110);
            int32_t peak=audio(FS/CTL);audio(FS*5/CTL);
            ok&=peak>30 && peak<32768 && !active_count();
        }
        bad+=check(model==DK_KIT808?"KIT 808: each lane sounds, stays bounded and frees its voice":"CR78: each lane sounds, stays bounded and frees its voice",ok);
        kit(model);trk_note_on(TSEL,41,110);audio(4);trk_note_on(TSEL,43,110);audio(2);
        dvk_part_t *K=dvk_of(TSEL);
        bad+=check("a Kit lane is mono: two toms, one voice, the second hit",active_count()==1 && K && K->lane[DV_TOM].owner && TSEL->v[K->lane[DV_TOM].owner-1u].note==43);
        kit(model);trk_note_on(TSEL,46,110);audio(FS/10/CTL);int open=K && K->lane[DV_HATO].v.live;
        trk_note_on(TSEL,42,110);audio(FS/20/CTL);
        bad+=check("a Kit's closed hat chokes its open hat",open && !dvk_of(TSEL)->lane[DV_HATO].v.live);
        int32_t worst=0;
        for(uint32_t c=0;c<16u;c++){
            kit(model);for(uint32_t p=P_E1;p<=P_E4;p++)TSEL->p[p]=(c>>(p-P_E1))&1u?127:0;TSEL->p[P_E5]=127;TSEL->p[P_E7]=127;
            for(uint32_t i=0;i<16;i++){int8_t *d=drum_patch[0].c[i];d[0]=c&1u?24:-24;d[1]=d[2]=c&2u?63:-64;}
            for(uint32_t l=0;l<DV_NLANE;l++)trk_note_on(TSEL,DRUM_LANE_NOTE[l],127);
            int32_t q=audio(FS/CTL);if(q>worst)worst=q;
        }
        bad+=check("a Kit at every knob and drum corner, all lanes at velocity 127: under full scale",worst<32767);
    }
    kit(DK_CR78);
    bad+=check("CR78 names its maracas, conga and claves",!strcmp(drum_sound_name(TSEL,D9_CP),"MARAC") && !strcmp(drum_lane_name(TSEL,DV_TOM),"CONGA") &&
               !strcmp(drum_sound_abbr(TSEL,D9_RS),"CL") && drum_sound_count(TSEL)==DV_NLANE && drum_sound_norm(TSEL,D9_LT)==D9_MT);
    uint32_t hz[2],ms[2];
    for(uint32_t k=0;k<2u;k++){
        kit(k?DK_CR78:DK_KIT808);trk_note_on(TSEL,36,110);audio_left(y,FS*2/CTL);
        hz[k]=kick_crossings(y,FS*6/100,FS*15/100)*100u/18u;ms[k]=ring_ms(y,FS*2);   /* (crossings/2 per 90 ms -> Hz) */
    }
    printf("drum editor: kicks: 808 %u Hz %u ms, CR78 %u Hz %u ms\n",hz[0],ms[0],hz[1],ms[1]);
    bad+=check("the Kit's 808 kick is lower and longer than its CR78 kick",hz[0]+5u<=hz[1] && ms[0]>ms[1]*3u/2u);
    kit(DK_KIT808);
    bad+=check("808 KIT is the Kit's 808, 808 CIRCUIT the circuits; old ids keep their sound",DRUM_PRESETS[0].e[0]==DK_KIT808 && DRUM_PRESETS[DRUM_PRESET_CIRCUIT].e[0]==DK_808 &&
               !strcmp(DRUM_PRESETS[4].name,"CR78 KIT") && ENG_DRUM.edit[0].def==DK_KIT808 && !strcmp(N_DRUM_KIT[DK_808],"808 CIRC"));
    return bad;
}
/* WaveLoop fork: each drum shaped on its own (drum_control.c): picked by a hit, its knobs, its own place in the mix,
 * kept with the song */
static int per_drum(void)
{
    static int32_t a[FS/4],b[FS/4];
    int32_t out[2*CTL];
    int bad=0;
    kit(DK_KIT808);trk_note_on(TSEL,36,100);audio(1);dvk_part_t *K=dvk_of(TSEL);int16_t tune=K->lane[DV_KICK].key.tune;
    drum_field_set(TSEL,D9_BD,DCF_PITCH,12);trk_note_on(TSEL,36,100);audio(1);
    bad+=check("PITCH moves the picked Kit drum an octave, the others not",K->lane[DV_KICK].key.tune==tune+192 && drum_field_get(TSEL,D9_SD,DCF_PITCH)==0);
    drum_field_set(TSEL,D9_SD,DCF_DECAY,0);kit(DK_KIT808);
    bad+=check("loading a kit starts every drum from its design",!drum_patch[0].c[D9_SD][1] && !drum_mix[0].pan[D9_SD]);
    /* the mix: nothing set is the one bus; a pan set elsewhere leaves this drum where it was; its own pan moves it */
    for(uint32_t pass=0;pass<3u;pass++){
        kit(DK_KIT808);lim_env=LIM_T;dc_l=dc_r=dce_l=dce_r=0;   /* (the master's limiter and DC states: as new) */
        if(pass)drum_mix[0].pan[D9_BD]=40;if(pass==2u)drum_mix[0].pan[D9_SD]=-64;
        trk_note_on(TSEL,38,110);
        double l=0,r=0;int on=0;
        for(uint32_t blk=0;blk<FS/4/CTL;blk++){mix_block(out,CTL);on|=drum_bus_used[0]!=0;for(uint32_t i=0;i<CTL;i++){if(pass<2u)(pass?b:a)[blk*CTL+i]=out[2*i];l+=(double)out[2*i]*out[2*i];r+=(double)out[2*i+1]*out[2*i+1];}}
        if(pass==0)bad+=check("a kit with no drum PAN or SEND mixes as one bus",!on);
        if(pass==1){int32_t worst=0;for(uint32_t i=0;i<FS/4;i++){int32_t d=a[i]-b[i];d=d<0?-d:d;if(d>worst)worst=d;}
            printf("drum editor: one bus against a bus per voice: worst sample difference %d\n",worst);
            bad+=check("its drums on their own buses: an untouched drum sounds as on the one bus",worst<=1);}
        if(pass==2)bad+=check("a drum's own PAN places it (snare hard left)",l>r*100.0);
    }
    kit(DK_KIT808);song.g[G_FXRETURN]=100;TSEL->p[P_FXSEND]=50;
    drum_field_set(TSEL,D9_SD,DCF_SEND,90);
    bad+=check("SEND is the drum's against the track's, and follows it",drum_mix[0].send[D9_SD]==40 && drum_field_get(TSEL,D9_SD,DCF_SEND)==90 &&
               (TSEL->p[P_FXSEND]=100,drum_field_get(TSEL,D9_SD,DCF_SEND)==127) && drum_field_get(TSEL,D9_BD,DCF_SEND)==100);
    /* a hit picks; the knobs shape what is picked; the card names it */
    kit(DK_KIT808);midi_test_reset();set_engine_of(TSEL,ENGI_DRUM);apply_preset_to(TSEL,0);TSEL->engine=TSEL->eng_req;panic_req=0;
    ui.drum_sound=D9_BD;queued(0x90,38,100,0);ui_notices();
    bad+=check("a MIDI note on the drum track picks its drum on screen",drum_focus[0]==D9_SD && ui.drum_sound==D9_SD && ui.lane==DV_SNARE);
    queued(0xB0,74,127,0);queued(0xB0,10,0,0);queued(0xB0,77,0,0);
    bad+=check("knobs 1, 4, 6 set the picked drum's PITCH, PAN, LEVEL",drum_patch[0].c[D9_SD][0]==24 && drum_mix[0].pan[D9_SD]==-64 && drum_patch[0].c[D9_SD][3]==127 &&
               !drum_patch[0].c[D9_BD][0] && midi_pop_id==(MIDI_POP_DRUM|D9_SD<<4|DCF_LEVEL));
    bad+=check("the controls page names the picked drum's targets",midi_cc_id(TSEL,74)==(MIDI_POP_DRUM|DCF_PITCH) && midi_cc_id(TSEL,76)==(MIDI_POP_DRUM|DCF_SEND) &&
               midi_cc_id(TSEL,78)==P_LD_AMP);
    ui.home=1;page_over=stage_page();edit_param(1,-3);page_over=0;ui.home=0;
    bad+=check("Stage's knob 2 on a DRUM track is the picked drum's PITCH",drum_patch[0].c[D9_SD][0]==21);
    kb_note[0]=42;key_on(0,TSEL);key_off(0,TSEL);ui_notices();
    bad+=check("a key played on the drum track picks its drum",drum_focus[0]==D9_CH && ui.drum_sound==D9_CH);
    /* kept with the song: project round trip, and a record without the block */
    drum_mix[0].pan[D9_HT]=-17;drum_mix[0].send[D9_CR]=-90;drum_focus[0]=D9_OH;
    project_capture(&proj_scratch);project_store_t wire;project_t decoded;
    int ok=proj_pack(&wire,&proj_scratch) && proj_import(&decoded,&wire,sizeof wire);
    bad+=check("each drum's PAN, SEND and the picked drum survive the song's round trip",ok && !memcmp(decoded.drum_mix,drum_mix,sizeof drum_mix) && decoded.drum_focus[0]==D9_OH);
    return bad;
}
int main(void)
{
    int bad=voices909()+controls()+edits_and_store()+capacity_and_controls()+order_and_lanes()+corners909()+kits_kit()+per_drum();
    printf("DRUM EDITOR: %s\n",bad?"FAILED":"all passed");return bad!=0;
}
