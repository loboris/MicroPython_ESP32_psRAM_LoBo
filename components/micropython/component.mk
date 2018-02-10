#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)


COMPONENT_ADD_INCLUDEDIRS := .  genhdr py esp32 lib lib/utils lib/mp-readline extmod extmod/crypto-algorithms lib/netutils drivers/dht \
							 lib/timeutils  lib/berkeley-db-1.xx/include lib/berkeley-db-1.xx/btree \
							 lib/berkeley-db-1.xx/db lib/berkeley-db-1.xx/hash lib/berkeley-db-1.xx/man lib/berkeley-db-1.xx/mpool lib/berkeley-db-1.xx/recno \
							 ../curl/include ../curl/lib ../zlib ../libssh2/include ../espmqtt/include
ifdef CONFIG_MICROPY_USE_MAIL
COMPONENT_ADD_INCLUDEDIRS += ../quickmail
endif

COMPONENT_PRIV_INCLUDEDIRS := .  genhdr py esp32 lib

BUILD = $(BUILD_DIR_BASE)

COMPONENT_OWNCLEANTARGET := clean

CFLAGS_MOD =
OBJ_ESPIDF =
SRC_MOD =
DRIVERS_SRC_C =
QSTR_AUTOGEN_DISABLE =
FROZEN_EXTRA_DEPS =

include $(COMPONENT_PATH)/py/mkenv.mk

# qstr definitions (must come before including py.mk)
QSTR_DEFS = $(COMPONENT_PATH)/esp32/qstrdefsport.h

MICROPY_PY_USSL = 0
MICROPY_SSL_AXTLS = 0
ifdef CONFIG_MICROPY_PY_USE_BTREE
MICROPY_PY_BTREE = 1
else
MICROPY_PY_BTREE = 0
endif

MICROPY_FATFS = 0

FROZEN_DIR = $(COMPONENT_PATH)/esp32/scripts
FROZEN_MPY_DIR = $(COMPONENT_PATH)/esp32/modules

# Includes for Qstr&Frozen modules
#---------------------------------
ESPCOMP = $(IDF_PATH)/components
MP_EXTRA_INC += -I.
MP_EXTRA_INC += -I$(COMPONENT_PATH)
MP_EXTRA_INC += -I$(PROJECT_PATH)/components/curl/include
MP_EXTRA_INC += -I$(PROJECT_PATH)/components/libssh2/include
MP_EXTRA_INC += -I$(PROJECT_PATH)/components/zlib
MP_EXTRA_INC += -I$(PROJECT_PATH)/components/espmqtt/include
MP_EXTRA_INC += -I$(COMPONENT_PATH)/py
MP_EXTRA_INC += -I$(COMPONENT_PATH)/lib/mp-readline
MP_EXTRA_INC += -I$(COMPONENT_PATH)/lib/netutils
MP_EXTRA_INC += -I$(COMPONENT_PATH)/lib/timeutils
MP_EXTRA_INC += -I$(COMPONENT_PATH)/esp32
MP_EXTRA_INC += -I$(COMPONENT_PATH)/build
MP_EXTRA_INC += -I$(COMPONENT_PATH)/esp32/libs
MP_EXTRA_INC += -I$(BUILD_DIR_BASE)
MP_EXTRA_INC += -I$(BUILD_DIR_BASE)/include
MP_EXTRA_INC += -I$(ESPCOMP)/spiffs/include
MP_EXTRA_INC += -I$(ESPCOMP)/bootloader_support/include
MP_EXTRA_INC += -I$(ESPCOMP)/driver/include
MP_EXTRA_INC += -I$(ESPCOMP)/driver/include/driver
MP_EXTRA_INC += -I$(ESPCOMP)/esp_adc_cal/include
MP_EXTRA_INC += -I$(ESPCOMP)/nghttp/port/include
MP_EXTRA_INC += -I$(ESPCOMP)/nghttp/nghttp2/lib/includes
MP_EXTRA_INC += -I$(ESPCOMP)/esp32/include
MP_EXTRA_INC += -I$(ESPCOMP)/soc/esp32/include
MP_EXTRA_INC += -I$(ESPCOMP)/soc/include
MP_EXTRA_INC += -I$(ESPCOMP)/ethernet/include
MP_EXTRA_INC += -I$(ESPCOMP)/expat/include/expat
MP_EXTRA_INC += -I$(ESPCOMP)/expat/port/include
MP_EXTRA_INC += -I$(ESPCOMP)/json/include
MP_EXTRA_INC += -I$(ESPCOMP)/json/port/include
MP_EXTRA_INC += -I$(ESPCOMP)/log/include
MP_EXTRA_INC += -I$(ESPCOMP)/newlib/include
MP_EXTRA_INC += -I$(ESPCOMP)/nvs_flash/include
MP_EXTRA_INC += -I$(ESPCOMP)/freertos/include
MP_EXTRA_INC += -I$(ESPCOMP)/tcpip_adapter/include
MP_EXTRA_INC += -I$(ESPCOMP)/lwip/include/lwip
MP_EXTRA_INC += -I$(ESPCOMP)/lwip/include/lwip/port
MP_EXTRA_INC += -I$(ESPCOMP)/lwip/include/lwip/posix
MP_EXTRA_INC += -I$(ESPCOMP)/mbedtls/include
MP_EXTRA_INC += -I$(ESPCOMP)/mbedtls/port/include
MP_EXTRA_INC += -I$(ESPCOMP)/spi_flash/include
MP_EXTRA_INC += -I$(ESPCOMP)/wear_levelling/include
MP_EXTRA_INC += -I$(ESPCOMP)/wear_levelling/private_include
MP_EXTRA_INC += -I$(ESPCOMP)/vfs/include
MP_EXTRA_INC += -I$(ESPCOMP)/newlib/platform_include
MP_EXTRA_INC += -I$(ESPCOMP)/xtensa-debug-module/include
MP_EXTRA_INC += -I$(ESPCOMP)/wpa_supplicant/include
MP_EXTRA_INC += -I$(ESPCOMP)/wpa_supplicant/port/include
MP_EXTRA_INC += -I$(ESPCOMP)/ethernet/include
MP_EXTRA_INC += -I$(ESPCOMP)/app_trace/include
MP_EXTRA_INC += -I$(ESPCOMP)/sdmmc/include
MP_EXTRA_INC += -I$(ESPCOMP)/fatfs/src
MP_EXTRA_INC += -I$(ESPCOMP)/heap/include
MP_EXTRA_INC += -I$(ESPCOMP)/openssl/include
MP_EXTRA_INC += -I$(ESPCOMP)/app_update/include
MP_EXTRA_INC += -I$(ESPCOMP)/mdns/include

ifdef CONFIG_MICROPY_USE_MAIL
MP_EXTRA_INC += -I$(PROJECT_PATH)/components/quickmail
endif

# CPP macro
# ------------
CPP = $(CC) -E
# ------------

# Clean MicroPython directories/files
# -----------------------------------
MP_CLEAN_EXTRA = $(BUILD_DIR_BASE)/drivers
MP_CLEAN_EXTRA += $(BUILD_DIR_BASE)/esp32
MP_CLEAN_EXTRA += $(BUILD_DIR_BASE)/extmod
MP_CLEAN_EXTRA += $(BUILD_DIR_BASE)/frozen_mpy
MP_CLEAN_EXTRA += $(BUILD_DIR_BASE)/genhdr
MP_CLEAN_EXTRA += $(BUILD_DIR_BASE)/home
MP_CLEAN_EXTRA += $(BUILD_DIR_BASE)/lib
MP_CLEAN_EXTRA += $(BUILD_DIR_BASE)/micropython/*
MP_CLEAN_EXTRA += $(BUILD_DIR_BASE)/py
MP_CLEAN_EXTRA += $(BUILD_DIR_BASE)/frozen_mpy.c
MP_CLEAN_EXTRA += $(BUILD_DIR_BASE)/frozen.c
MP_CLEAN_EXTRA += $(COMPONENT_PATH)/genhdr/qstrdefs.generated.h


# include py core make definitions
# --------------------------------
include $(COMPONENT_PATH)/py/py.mk

#CFLAGS += -std=gnu99
CFLAGS += $(CFLAGS_MOD)



# List of MicroPython source and object files
# for Qstr generation
# -------------------------------------------
SRC_C =  $(addprefix esp32/,\
	main.c \
	uart.c \
	gccollect.c \
	mphalport.c \
	help.c \
	modutime.c \
	moduos.c \
	machine_timer.c \
	machine_pin.c \
	machine_touchpad.c \
	machine_adc.c \
	machine_dac.c \
	machine_pwm.c \
	machine_uart.c \
	modmachine.c \
	modnetwork.c \
	modsocket.c \
	moduhashlib.c \
	machine_hw_spi.c \
	mpthreadport.c \
	mpsleep.c \
	machine_rtc.c \
	modymodem.c \
	machine_hw_i2c.c \
	machine_neopixel.c \
	machine_dht.c \
	machine_ow.c \
	)

ifdef CONFIG_MICROPY_USE_DISPLAY
SRC_C += esp32/moddisplay.c
endif

ifdef CONFIG_MICROPY_USE_CURL
SRC_C += esp32/modcurl.c
endif

ifdef CONFIG_MICROPY_USE_SSH
SRC_C += esp32/modssh.c
endif

ifdef CONFIG_MICROPY_USE_MQTT
SRC_C += esp32/modmqtt.c
endif

ifdef CONFIG_MICROPY_USE_GSM
SRC_C += esp32/modgsm.c
endif

ifdef CONFIG_MICROPY_USE_OTA
SRC_C += esp32/modota.c
endif

ifdef CONFIG_MICROPY_USE_MDNS
SRC_C += esp32/network_mdns.c
endif

ifdef CONFIG_MICROPY_USE_ETHERNET
SRC_C += esp32/network_lan.c
endif

EXTMOD_SRC_C = $(addprefix extmod/,\
	modbtree.c \
	)

LIB_SRC_C = $(addprefix lib/,\
	libm/math.c \
	libm/fmodf.c \
	libm/roundf.c \
	libm/ef_sqrt.c \
	libm/kf_rem_pio2.c \
	libm/kf_sin.c \
	libm/kf_cos.c \
	libm/kf_tan.c \
	libm/ef_rem_pio2.c \
	libm/sf_sin.c \
	libm/sf_cos.c \
	libm/sf_tan.c \
	libm/sf_frexp.c \
	libm/sf_modf.c \
	libm/sf_ldexp.c \
	libm/asinfacosf.c \
	libm/atanf.c \
	libm/atan2f.c \
	mp-readline/readline.c \
	netutils/netutils.c \
	timeutils/timeutils.c \
	utils/pyexec.c \
	utils/interrupt_char.c \
	utils/sys_stdio_mphal.c \
	)

LIBS_SRC_C = $(addprefix esp32/libs/,\
	espcurl.c \
	neopixel.c \
	esp_rmt.c \
	telnet.c \
	ftp.c \
	websrv.c \
	libGSM.c \
	ow/owb_rmt.c \
	ow/owb.c \
	ow/ds18b20.c \
	)

ifdef CONFIG_MICROPY_USE_DISPLAY
LIBS_SRC_C += \
	esp32/libs/tft/spi_master_lobo.c \
	esp32/libs/tft/tftspi.c \
	esp32/libs/tft/tft.c \
	esp32/libs/tft/comic24.c \
	esp32/libs/tft/DefaultFont.c \
	esp32/libs/tft/DejaVuSans18.c \
	esp32/libs/tft/DejaVuSans24.c \
	esp32/libs/tft/minya24.c \
	esp32/libs/tft/SmallFont.c \
	esp32/libs/tft/tooney32.c \
	esp32/libs/tft/Ubuntu16.c \
	esp32/libs/tft/def_small.c
endif

ifdef CONFIG_MICROPY_USE_EVE
LIBS_SRC_C += \
	esp32/libs/eve/FT8_commands.c
endif

ifeq ($(MICROPY_PY_BTREE),1)
LIB_SRC_C += \
	lib/berkeley-db-1.xx/btree/bt_open.c \
	lib/berkeley-db-1.xx/btree/bt_seq.c \
	lib/berkeley-db-1.xx/btree/bt_close.c \
	lib/berkeley-db-1.xx/btree/bt_debug.c \
	lib/berkeley-db-1.xx/btree/bt_get.c \
	lib/berkeley-db-1.xx/btree/bt_overflow.c \
	lib/berkeley-db-1.xx/btree/bt_put.c \
	lib/berkeley-db-1.xx/btree/bt_utils.c \
	lib/berkeley-db-1.xx/btree/bt_conv.c \
	lib/berkeley-db-1.xx/btree/bt_delete.c \
	lib/berkeley-db-1.xx/btree/bt_page.c \
	lib/berkeley-db-1.xx/btree/bt_search.c \
	lib/berkeley-db-1.xx/btree/bt_split.c \
	lib/berkeley-db-1.xx/mpool/mpool.c
endif

OBJ_MP =
OBJ_MP += $(PY_O)
OBJ_MP += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ_MP += $(addprefix $(BUILD)/, $(EXTMOD_SRC_C:.c=.o))
OBJ_MP += $(addprefix $(BUILD)/, $(LIB_SRC_C:.c=.o))
OBJ_MP += $(addprefix $(BUILD)/, $(LIBS_SRC_C:.c=.o))

# List of sources for qstr extraction
# ------------------------------------------------------------------------------
SRC_QSTR += $(SRC_C) $(EXTMOD_SRC_C) $(LIB_SRC_C) $(DRIVERS_SRC_C) $(LIBS_SRC_C)
# Append any auto-generated sources that are needed by sources listed in SRC_QSTR
SRC_QSTR_AUTO_DEPS +=

# Needed to generate Qstr
OBJ = $(OBJ_MP) $(OBJ_ESPIDF)

# Include mkrules make
#--------------------------------------
include $(COMPONENT_PATH)/py/mkrules.mk
