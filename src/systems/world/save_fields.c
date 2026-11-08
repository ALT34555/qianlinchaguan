#include "save_fields.h"
_Static_assert(ACT_COUNT==11 && LAYER_ID_COUNT==12 && QLCG_MAX_LOCALES==16,"storage schema counts");

void qlcg_fields_profile(QlCodec *io, PlayerProfile *s) {
    qlcg_io_string(io,s->name,256);
    s->height_cm=(int)qlcg_io_i32(io,(int32_t)s->height_cm);
    s->main_color=(uint32_t)qlcg_io_uint(io,(uint64_t)s->main_color,4);
    s->trim_color=(uint32_t)qlcg_io_uint(io,(uint64_t)s->trim_color,4);
}

void qlcg_fields_bind(QlCodec *io, QlBind *s) {
    s->key=(uint16_t)qlcg_io_uint(io,(uint64_t)s->key,2);
    s->mods=(uint8_t)qlcg_io_uint(io,(uint64_t)s->mods,1);
    s->mouse=(int8_t)qlcg_io_uint(io,(uint64_t)s->mouse,1);
    s->pad=(int8_t)qlcg_io_uint(io,(uint64_t)s->pad,1);
    s->touch=(int8_t)qlcg_io_uint(io,(uint64_t)s->touch,1);
}

void qlcg_fields_settings(QlCodec *io, GameSettings *s) {
    qlcg_fields_profile(io,&s->profile);
    qlcg_io_string(io,s->ren_ming,256);
    s->measurement_system=(MeasurementSystem)qlcg_io_i32(io,(int32_t)s->measurement_system);
    s->theme=(ThemeMode)qlcg_io_i32(io,(int32_t)s->theme);
    s->show_top_bar=qlcg_io_bool(io,s->show_top_bar);
    s->crosshair=(CrosshairStyle)qlcg_io_i32(io,(int32_t)s->crosshair);
    s->profile_palette_to_ui=qlcg_io_bool(io,s->profile_palette_to_ui);
    s->ui_scale=qlcg_io_f32(io,s->ui_scale);
    s->graphics_preset=(GraphicsPreset)qlcg_io_i32(io,(int32_t)s->graphics_preset);
    s->fov=(int)qlcg_io_i32(io,(int32_t)s->fov);
    s->render_distance=(int)qlcg_io_i32(io,(int32_t)s->render_distance);
    s->vegetation_distance=(int)qlcg_io_i32(io,(int32_t)s->vegetation_distance);
    s->vegetation_form=(VegetationForm)qlcg_io_i32(io,(int32_t)s->vegetation_form);
    s->dense_vegetation=(int)qlcg_io_i32(io,(int32_t)s->dense_vegetation);
    s->frame_rate=(int)qlcg_io_i32(io,(int32_t)s->frame_rate);
    s->antialias=qlcg_io_bool(io,s->antialias);
    s->water_reflections=qlcg_io_bool(io,s->water_reflections);
    s->water_waves=qlcg_io_bool(io,s->water_waves);
    s->particles=qlcg_io_bool(io,s->particles);
    s->weather_atmosphere=qlcg_io_bool(io,s->weather_atmosphere);
    s->decorative_clouds=qlcg_io_bool(io,s->decorative_clouds);
    s->natural_physics=qlcg_io_bool(io,s->natural_physics);
    s->status_feedback=qlcg_io_bool(io,s->status_feedback);
    s->view_mode=(ViewMode)qlcg_io_i32(io,(int32_t)s->view_mode);
    s->safe_x=qlcg_io_f32(io,s->safe_x);
    s->safe_y=qlcg_io_f32(io,s->safe_y);
    s->text_scale=qlcg_io_f32(io,s->text_scale);
    s->reduce_motion=qlcg_io_bool(io,s->reduce_motion);
    s->binds_magic=(uint32_t)qlcg_io_uint(io,(uint64_t)s->binds_magic,4);
    for (int i=0;i<ACT_COUNT;i++) {
        qlcg_fields_bind(io,&s->binds[i]);
    }
    s->interface_main_color=(uint32_t)qlcg_io_uint(io,(uint64_t)s->interface_main_color,4);
    s->interface_trim_color=(uint32_t)qlcg_io_uint(io,(uint64_t)s->interface_trim_color,4);
    qlcg_io_string(io,s->font_id,96);
    s->mirror_reflections=qlcg_io_bool(io,s->mirror_reflections);
    qlcg_io_string(io,s->resolution,16);
    qlcg_io_string(io,s->locale,32);
    s->touch_controls=qlcg_io_bool(io,s->touch_controls);
    s->joystick_style=(int)qlcg_io_i32(io,(int32_t)s->joystick_style);
    s->touch_opacity=qlcg_io_f32(io,s->touch_opacity);
    s->hidpi_scale=qlcg_io_f32(io,s->hidpi_scale);
    for (int i=0;i<4;i++) {
        for (int j=0;j<3;j++) {
            s->touch_layout[i][j]=qlcg_io_f32(io,s->touch_layout[i][j]);
        }
    }
    s->invert_look_x=qlcg_io_bool(io,s->invert_look_x);
    s->invert_look_y=qlcg_io_bool(io,s->invert_look_y);
}

void qlcg_fields_options(QlCodec *io, WorldCreatorOptions *s) {
    qlcg_io_string(io,s->world_name,240);
    qlcg_io_string(io,s->ren_ming,256);
    s->height_cm=(int)qlcg_io_i32(io,(int32_t)s->height_cm);
    s->play_mode=(PlayMode)qlcg_io_i32(io,(int32_t)s->play_mode);
    s->seed=(uint32_t)qlcg_io_uint(io,(uint64_t)s->seed,4);
    qlcg_io_string(io,s->seed_str,64);
    s->preset=(WorldPresetId)qlcg_io_i32(io,(int32_t)s->preset);
    s->terrain_recipe=(MacroTerrainRecipe)qlcg_io_i32(io,(int32_t)s->terrain_recipe);
    s->climate_rule=(MacroClimateRule)qlcg_io_i32(io,(int32_t)s->climate_rule);
    s->macro_size_idx=(int)qlcg_io_i32(io,(int32_t)s->macro_size_idx);
    s->macro_scale_idx=(int)qlcg_io_i32(io,(int32_t)s->macro_scale_idx);
    s->columns=(int)qlcg_io_i32(io,(int32_t)s->columns);
    s->chunks_per_cell=(int)qlcg_io_i32(io,(int32_t)s->chunks_per_cell);
    s->equator_chunks=(int)qlcg_io_i32(io,(int32_t)s->equator_chunks);
    s->pole_temp=qlcg_io_f32(io,s->pole_temp);
    s->equator_temp=qlcg_io_f32(io,s->equator_temp);
    s->tectonic_activity=(int)qlcg_io_i32(io,(int32_t)s->tectonic_activity);
    s->land_ratio=qlcg_io_f32(io,s->land_ratio);
    s->precipitation=qlcg_io_f32(io,s->precipitation);
    s->astronomy_enabled=qlcg_io_bool(io,s->astronomy_enabled);
    s->astronomy_version=(uint32_t)qlcg_io_uint(io,(uint64_t)s->astronomy_version,4);
    s->creation_time_speed=qlcg_io_f32(io,s->creation_time_speed);
    s->calendar_mode=(CalendarMode)qlcg_io_i32(io,(int32_t)s->calendar_mode);
    s->start_year=(int)qlcg_io_i32(io,(int32_t)s->start_year);
    s->start_month=(int)qlcg_io_i32(io,(int32_t)s->start_month);
    s->start_day=(int)qlcg_io_i32(io,(int32_t)s->start_day);
    s->artificial_enabled=qlcg_io_bool(io,s->artificial_enabled);
    s->spawn_as_flag=qlcg_io_bool(io,s->spawn_as_flag);
    qlcg_io_string(io,s->faction_name,40);
    s->faction_main_color=(uint32_t)qlcg_io_uint(io,(uint64_t)s->faction_main_color,4);
    s->faction_trim_color=(uint32_t)qlcg_io_uint(io,(uint64_t)s->faction_trim_color,4);
    s->initial_civ_limit=(int)qlcg_io_i32(io,(int32_t)s->initial_civ_limit);
    s->natural_expansion=qlcg_io_f32(io,s->natural_expansion);
    s->natural_evolution=qlcg_io_f32(io,s->natural_evolution);
    s->flag_independence=qlcg_io_bool(io,s->flag_independence);
    s->monsoon_direction=(int)qlcg_io_i32(io,(int32_t)s->monsoon_direction);
    qlcg_io_string(io,s->culture,32);
    s->axial_tilt_deg=qlcg_io_f32(io,s->axial_tilt_deg);
    s->prograde=qlcg_io_bool(io,s->prograde);
    for (int i=0;i<QLCG_MAX_LOCALES;i++) {
        qlcg_io_string(io,s->culture_choices[i],32);
    }
    s->map_mode=(WorldMapMode)qlcg_io_i32(io,(int32_t)s->map_mode);
    s->world_seed=(uint32_t)qlcg_io_uint(io,(uint64_t)s->world_seed,4);
    s->plane_x=(int32_t)qlcg_io_i32(io,(int32_t)s->plane_x);
    s->plane_z=(int32_t)qlcg_io_i32(io,(int32_t)s->plane_z);
}

void qlcg_fields_delta(QlCodec *io, SparseDeltaRecord *s) {
    s->type=(EngToolType)qlcg_io_i32(io,(int32_t)s->type);
    s->wx=qlcg_io_f32(io,s->wx);
    s->wz=qlcg_io_f32(io,s->wz);
    s->ex=qlcg_io_f32(io,s->ex);
    s->ez=qlcg_io_f32(io,s->ez);
    s->radius=qlcg_io_f32(io,s->radius);
    s->delta_h=qlcg_io_f32(io,s->delta_h);
    s->width_m=qlcg_io_f32(io,s->width_m);
}

void qlcg_fields_faction(QlCodec *io, TerritoryFaction *s) {
    s->active=qlcg_io_bool(io,s->active);
    s->id=(int)qlcg_io_i32(io,(int32_t)s->id);
    s->is_player=qlcg_io_bool(io,s->is_player);
    qlcg_io_string(io,s->uid,17);
    qlcg_io_string(io,s->name,40);
    s->main_color=(uint32_t)qlcg_io_uint(io,(uint64_t)s->main_color,4);
    s->trim_color=(uint32_t)qlcg_io_uint(io,(uint64_t)s->trim_color,4);
    s->flag_cell=(int)qlcg_io_i32(io,(int32_t)s->flag_cell);
    s->cx=(int)qlcg_io_i32(io,(int32_t)s->cx);
    s->cz=(int)qlcg_io_i32(io,(int32_t)s->cz);
    s->wx=qlcg_io_f32(io,s->wx);
    s->wz=qlcg_io_f32(io,s->wz);
    s->level=(int)qlcg_io_i32(io,(int32_t)s->level);
    s->claim_radius=(int)qlcg_io_i32(io,(int32_t)s->claim_radius);
    s->born_year=(int)qlcg_io_i32(io,(int32_t)s->born_year);
    s->origin_type_id=(int)qlcg_io_i32(io,(int32_t)s->origin_type_id);
    qlcg_io_string(io,s->culture,32);
    qlcg_io_string(io,s->auto_name,96);
}

void qlcg_fields_layer(QlCodec *io, AtlasLayerSetting *s) {
    s->id=(AtlasLayerId)qlcg_io_i32(io,(int32_t)s->id);
    s->on=qlcg_io_bool(io,s->on);
    s->opacity=qlcg_io_f32(io,s->opacity);
}

void qlcg_fields_atlas(QlCodec *io, AtlasDisplayState *s) {
    for (int i=0;i<LAYER_ID_COUNT;i++) {
        qlcg_fields_layer(io,&s->layers[i]);
    }
    s->contour_density=(int)qlcg_io_i32(io,(int32_t)s->contour_density);
}

void qlcg_fields_slot(QlCodec *io, SaveSlotInfo *s) {
    qlcg_io_string(io,s->world_name,240);
    qlcg_io_string(io,s->ren_ming,256);
    s->main_color=(uint32_t)qlcg_io_uint(io,(uint64_t)s->main_color,4);
    s->play_mode=(PlayMode)qlcg_io_i32(io,(int32_t)s->play_mode);
    s->seed=(uint32_t)qlcg_io_uint(io,(uint64_t)s->seed,4);
    s->calendar_mode=(CalendarMode)qlcg_io_i32(io,(int32_t)s->calendar_mode);
    s->unix_ms=qlcg_io_f64(io,s->unix_ms);
    s->year=(int)qlcg_io_i32(io,(int32_t)s->year);
    s->month=(int)qlcg_io_i32(io,(int32_t)s->month);
    s->day=(int)qlcg_io_i32(io,(int32_t)s->day);
    qlcg_io_string(io,s->saved_at_str,32);
    s->player_x=qlcg_io_f32(io,s->player_x);
    s->player_y=qlcg_io_f32(io,s->player_y);
    s->player_z=qlcg_io_f32(io,s->player_z);
    s->player_yaw=qlcg_io_f32(io,s->player_yaw);
    s->player_pitch=qlcg_io_f32(io,s->player_pitch);
    s->flying=qlcg_io_bool(io,s->flying);
    qlcg_fields_options(io,&s->opt);
    qlcg_fields_atlas(io,&s->atlas_display);
    s->mystery_time_speed=qlcg_io_f32(io,s->mystery_time_speed);
    s->mystery_menu_auto_pause=qlcg_io_bool(io,s->mystery_menu_auto_pause);
    s->delta_count=(int)qlcg_io_i32(io,(int32_t)s->delta_count);
    for (int i=0;i<64;i++) {
        qlcg_fields_delta(io,&s->deltas[i]);
    }
}
