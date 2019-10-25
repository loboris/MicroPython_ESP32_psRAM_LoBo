#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/binary.h"

#include "gpu.h"

#define DISABLED_FRAME -1

mp_obj_t sprite_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {

    enum { ARG_strip, ARG_x, ARG_y, ARG_frame };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_strip, MP_ARG_REQUIRED | MP_ARG_INT,  {.u_int = 0} },
        { MP_QSTR_x,                       MP_ARG_INT,  {.u_int = 0} },
        { MP_QSTR_y,                       MP_ARG_INT,  {.u_int = 0} },
        { MP_QSTR_frame,                   MP_ARG_INT,  {.u_int = DISABLED_FRAME} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    sprite_obj_t *self = m_new_obj(sprite_obj_t);
    self->base.type = type;
    uint8_t strip_num = args[ARG_strip].u_int;
    self->image_strip = image_stripes[strip_num];
    self->x = args[ARG_x].u_int;
    self->y = args[ARG_y].u_int;
    self->frame = args[ARG_frame].u_int;
    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t sprite_disable(mp_obj_t self_in) {
    sprite_obj_t *self = self_in;
    self->frame = DISABLED_FRAME;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sprite_disable_obj, sprite_disable);

inline uint8_t width(sprite_obj_t* sprite) {
    return sprite->image_strip->frame_width;
}

inline uint8_t height(sprite_obj_t* sprite) {
    return sprite->image_strip->frame_height;
}


STATIC mp_obj_t sprite_collision(mp_obj_t self_in, mp_obj_t iterable) {
    sprite_obj_t *self = self_in;
    mp_obj_t iter = mp_getiter(iterable, NULL);
    mp_obj_t item;
    while ((item = mp_iternext(iter)) != MP_OBJ_STOP_ITERATION) {
        sprite_obj_t *other = item;
        if (self->x < other->x + width(other) &&
            self->x + width(self) > other->x &&
            self->y < other->y + height(other) &&
            self->y + height(self) > other->y) {
            
            return other;
        }
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sprite_collision_obj, sprite_collision);


STATIC mp_obj_t sprite_x(mp_obj_t self_in) {
    sprite_obj_t *self = self_in;
    return mp_obj_new_int(self->x);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sprite_x_obj, sprite_x);

STATIC mp_obj_t sprite_set_x(mp_obj_t self_in, mp_obj_t new_x) {
    sprite_obj_t *self = self_in;
    self->x = mp_obj_get_int(new_x);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sprite_set_x_obj, sprite_set_x);

STATIC mp_obj_t sprite_y(mp_obj_t self_in) {
    sprite_obj_t *self = self_in;
    return mp_obj_new_int(self->y);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sprite_y_obj, sprite_y);

STATIC mp_obj_t sprite_set_y(mp_obj_t self_in, mp_obj_t new_y) {
    sprite_obj_t *self = self_in;
    self->y = mp_obj_get_int(new_y);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sprite_set_y_obj, sprite_set_y);

STATIC mp_obj_t sprite_width(mp_obj_t self_in) {
    sprite_obj_t *self = self_in;
    return mp_obj_new_int(width(self));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sprite_width_obj, sprite_width);

STATIC mp_obj_t sprite_height(mp_obj_t self_in) {
    sprite_obj_t *self = self_in;
    return mp_obj_new_int(height(self));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sprite_height_obj, sprite_height);

STATIC mp_obj_t sprite_frame(mp_obj_t self_in) {
    sprite_obj_t *self = self_in;
    return mp_obj_new_int(self->frame);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sprite_frame_obj, sprite_frame);

STATIC mp_obj_t sprite_set_frame(mp_obj_t self_in, mp_obj_t new_frame) {
    sprite_obj_t *self = self_in;
    self->frame = mp_obj_get_int(new_frame);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sprite_set_frame_obj, sprite_set_frame);

STATIC mp_obj_t sprite_set_strip(mp_obj_t self_in, mp_obj_t new_strip) {
    sprite_obj_t *self = self_in;
    uint8_t strip_num = mp_obj_get_int(new_strip);
    self->image_strip = image_stripes[strip_num];
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sprite_set_strip_obj, sprite_set_strip);

// Methods for Class "Sprite"
STATIC const mp_rom_map_elem_t sprite_locals_dict_table[] = {
    // METHOD EXAMPLES
    { MP_ROM_QSTR(MP_QSTR_disable),         MP_ROM_PTR(&sprite_disable_obj) },
    { MP_ROM_QSTR(MP_QSTR_x),               MP_ROM_PTR(&sprite_x_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_x),           MP_ROM_PTR(&sprite_set_x_obj) },
    { MP_ROM_QSTR(MP_QSTR_y),               MP_ROM_PTR(&sprite_y_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_y),           MP_ROM_PTR(&sprite_set_y_obj) },
    { MP_ROM_QSTR(MP_QSTR_collision),       MP_ROM_PTR(&sprite_collision_obj) },
    { MP_ROM_QSTR(MP_QSTR_width),           MP_ROM_PTR(&sprite_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_height),          MP_ROM_PTR(&sprite_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_frame),           MP_ROM_PTR(&sprite_frame_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_frame),       MP_ROM_PTR(&sprite_set_frame_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_strip),       MP_ROM_PTR(&sprite_set_strip_obj) },
};

STATIC MP_DEFINE_CONST_DICT(sprite_locals_dict, sprite_locals_dict_table);

const mp_obj_type_t sprite_type = {
    { &mp_type_type },
    .name = MP_QSTR_Sprite,
    .make_new = sprite_make_new,
    .locals_dict = (mp_obj_dict_t*)&sprite_locals_dict,
};


// Functions for the Module "sprites" and the Class "Sprite"
STATIC const mp_rom_map_elem_t sprites_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),            MP_ROM_QSTR(MP_QSTR_sprite) },
    { MP_ROM_QSTR(MP_QSTR_Sprite),               MP_ROM_PTR(&sprite_type) },
};

STATIC MP_DEFINE_CONST_DICT (
    mp_module_sprites_globals,
    sprites_globals_table
);

const mp_obj_module_t mp_module_sprites = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_sprites_globals,
};