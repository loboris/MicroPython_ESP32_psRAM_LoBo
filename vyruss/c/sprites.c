#include "gpu.h"

// #define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
// #include "esp_log.h"
//static const char* TAG = "Sprites";

const mp_obj_type_t sprite_type;

#define DISABLED_FRAME -1

uint8_t sprite_num = 1;

sprite_obj_t* to_sprite(mp_obj_t self_in) {
    mp_obj_instance_t *self = MP_OBJ_TO_PTR(self_in);
    for(;;) {
        mp_obj_type_t* type = mp_obj_get_type(self);
        if (type == &sprite_type) {
            return (sprite_obj_t*)self;
        } else {
            self = self->subobj[0];
            if (self == NULL) {
                break;
            }
        }
    }
    return mp_const_none;
}

uint8_t add_sprite(sprite_obj_t* sprite) {
    if (sprite_num < NUM_SPRITES) {
        sprites[sprite_num] = sprite;
        return sprite_num++;
    }
    return 255;
}

uint8_t replace_sprite(sprite_obj_t* sprite, mp_obj_t replacing) {
    sprite_obj_t* replacing_sprite = to_sprite(replacing);
    uint8_t existing_sprite_num = replacing_sprite->sprite_id;
    if (existing_sprite_num < NUM_SPRITES) {
        sprites[existing_sprite_num] = sprite;
    }
    return existing_sprite_num;
}

STATIC mp_obj_t reset_sprites() {
    for (int i = 0; i < NUM_SPRITES; i++) {
        sprites[i] = NULL;
    }
    sprite_num = 1;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(reset_sprites_obj, reset_sprites);


STATIC mp_obj_t set_imagestrip(mp_obj_t strip_number, mp_obj_t strip_data) {
    int strip_nr = mp_obj_get_int(strip_number);
    const char* strip_data_ptr = mp_obj_str_get_str(strip_data);
    image_stripes[strip_nr] = (const ImageStrip*) strip_data_ptr;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(set_imagestrip_obj, set_imagestrip);

STATIC void sprite_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    sprite_obj_t *self = self_in;
    mp_printf(print, "<Sprite %p strip=%p>", self, self->image_strip);
}

mp_obj_t sprite_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_replacing };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_replacing, MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    mp_obj_t replacing = args[ARG_replacing].u_obj;

    sprite_obj_t *self = m_new_obj(sprite_obj_t);
    self->base.type = type;
    self->image_strip = image_stripes[0];
    self->perspective = 1;
    self->x = 0;
    self->y = 0;
    self->frame = DISABLED_FRAME;
    if (replacing == MP_OBJ_NULL) {
        self->sprite_id = add_sprite(self);
    } else {
        self->sprite_id = replace_sprite(self, replacing);
    }
    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t sprite_disable(mp_obj_t self_in) {
    sprite_obj_t *self = self_in;
    self->frame = DISABLED_FRAME;
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sprite_disable_obj, sprite_disable);

uint8_t width(sprite_obj_t* sprite) {
    return sprite->image_strip->frame_width;
}

uint8_t height(sprite_obj_t* sprite) {
    return sprite->image_strip->frame_height;
}

STATIC mp_obj_t sprite_collision(mp_obj_t self_in, mp_obj_t iterable) {
    sprite_obj_t *self = MP_OBJ_TO_PTR(self_in);
    // ESP_LOGD(TAG, "Collision, self=%p", self);
    // ESP_LOGD(TAG, "           strip=%p", self->image_strip);
    // mp_obj_type_t *type = mp_obj_get_type(self_in);
    // ESP_LOGD(TAG, "           type=%s", qstr_str(type->name));
    // ESP_LOGD(TAG, "Again");
    
    mp_obj_t iter = mp_getiter(iterable, NULL);
    mp_obj_t item;

    while ((item = mp_iternext(iter)) != MP_OBJ_STOP_ITERATION) {
        sprite_obj_t *other_sprite = to_sprite(item);
        if (other_sprite == mp_const_none || other_sprite->frame == DISABLED_FRAME) {
            continue;
        }
        // ESP_LOGI(TAG, "           item=%p", item);
        // ESP_LOGI(TAG, "           other=%p", other);
        // ESP_LOGI(TAG, "           strip=%p", other->image_strip);
        // type = mp_obj_get_type(other);
        // ESP_LOGD(TAG, "           type=%s", qstr_str(type->name));
        if (self->x < other_sprite->x + width(other_sprite) &&
            self->x + width(self) > other_sprite->x &&
            self->y < other_sprite->y + height(other_sprite) &&
            self->y + height(self) > other_sprite->y) {
            
            return item;
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

STATIC mp_obj_t sprite_set_perspective(mp_obj_t self_in, mp_obj_t value) {
    sprite_obj_t *self = self_in;
    self->perspective = mp_obj_get_int(value);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sprite_set_perspective_obj, sprite_set_perspective);

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
    { MP_ROM_QSTR(MP_QSTR_set_perspective), MP_ROM_PTR(&sprite_set_perspective_obj) },
};

STATIC MP_DEFINE_CONST_DICT(sprite_locals_dict, sprite_locals_dict_table);

const mp_obj_type_t sprite_type = {
    { &mp_type_type },
    .name = MP_QSTR_Sprite,
    .print = sprite_print,
    .make_new = sprite_make_new,
    .locals_dict = (mp_obj_dict_t*)&sprite_locals_dict,
};


STATIC mp_obj_t randint(mp_obj_t high) {
    return mp_obj_new_int(
        esp_random() % mp_obj_get_int(high)
    );
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(randint_obj, randint);

// Functions for the Module "sprites" and the Class "Sprite"
STATIC const mp_rom_map_elem_t sprites_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),            MP_ROM_QSTR(MP_QSTR_sprites) },
    { MP_ROM_QSTR(MP_QSTR_Sprite),              MP_ROM_PTR(&sprite_type) },
    { MP_ROM_QSTR(MP_QSTR_reset_sprites),       MP_ROM_PTR(&reset_sprites_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_imagestrip),      MP_ROM_PTR(&set_imagestrip_obj) },
    { MP_ROM_QSTR(MP_QSTR_randint),             MP_ROM_PTR(&randint_obj) },
};

STATIC MP_DEFINE_CONST_DICT (
    mp_module_sprites_globals,
    sprites_globals_table
);

const mp_obj_module_t mp_module_sprites = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_sprites_globals,
};