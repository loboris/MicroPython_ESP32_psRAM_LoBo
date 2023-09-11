#include <esp_system.h>
#include <stdint.h>
#include <stdbool.h>
#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/binary.h"
#include "py/objtype.h"

#include "sprites.h"

extern uint32_t* palette_pal;
