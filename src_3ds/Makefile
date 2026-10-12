# Makefile - 3DS build of balatro_c (devkitARM + libctru + citro3d).
#
#   make            -> ../build_3ds/balatro_c.3dsx and ../build_3ds/balatro_c.cia
#   make 3dsx       -> only the .3dsx (Homebrew Launcher)
#   make cia        -> only the .cia (needs makerom and bannertool on PATH)
#   make clean
#
# Game assets are taken from the extracted original in ../src_lua and
# packed into romfs (1x textures, sounds, fonts, localization).

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitPro")
endif
include $(DEVKITPRO)/devkitARM/3ds_rules

TARGET      := balatro_c
APP_TITLE   := Balatro
APP_DESC    := Balatro (balatro_c 3DS port)
APP_AUTHOR  := LocalThunk / balatro_c

BUILD       := ../build_3ds
OBJDIR      := $(BUILD)/obj
ROMFS_DIR   := $(BUILD)/romfs
LUA_SRC     := ../src_lua
META        := meta

CFILES      := $(wildcard *.c) $(foreach d,core engine game gfx,$(wildcard $(d)/*.c))
OFILES      := $(CFILES:%.c=$(OBJDIR)/%.o) $(OBJDIR)/vshader.shbin.o

ARCH        := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS      := -g -O2 -std=gnu11 -mword-relocations -ffunction-sections -fno-strict-aliasing $(ARCH) \
               -D__3DS__ -Wall -Wno-unused-parameter -Wno-override-init -Wno-missing-field-initializers \
               -Wno-sign-compare -Wno-unused-function -Wno-format-truncation -Wno-comment \
               -I. -Icore -Iengine -Igame -Igfx -Ithird_party/stb -I$(OBJDIR) \
               -I$(CTRULIB)/include -I$(PORTLIBS)/include
LDFLAGS     := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,--gc-sections
LIBS        := -L$(PORTLIBS)/lib -L$(CTRULIB)/lib -lcitro3d -lctru -lz -lm

MAKEROM     ?= makerom
BANNERTOOL  ?= bannertool

.PHONY: all 3dsx cia romfs clean
all: 3dsx cia
3dsx: $(BUILD)/$(TARGET).3dsx
cia: $(BUILD)/$(TARGET).cia

# ---------------------------------------------------------------- code
$(OBJDIR)/vshader.shbin: gfx/vshader.v.pica
	@mkdir -p $(dir $@)
	picasso -o $@ $<

$(OBJDIR)/vshader_shbin.h $(OBJDIR)/vshader.shbin.o: $(OBJDIR)/vshader.shbin
	cd $(OBJDIR) && bin2s -a 4 -H vshader_shbin.h vshader.shbin > vshader.shbin.s
	$(CC) -x assembler-with-cpp $(ARCH) -c $(OBJDIR)/vshader.shbin.s -o $(OBJDIR)/vshader.shbin.o

$(OBJDIR)/%.o: %.c $(OBJDIR)/vshader_shbin.h
	@mkdir -p $(dir $@)
	@echo $<
	@$(CC) -MMD -MP $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) $(OFILES) $(LIBS) -o $@

-include $(OFILES:.o=.d)

# ---------------------------------------------------------------- assets
# 2x textures and the GoNoto fallback fonts are left out to keep the size
# (and memory use) down.
$(ROMFS_DIR)/.stamp: $(MAKEFILE_LIST) $(META)/gamepad_ui_3ds.png
	@mkdir -p $(ROMFS_DIR)/resources/textures $(ROMFS_DIR)/resources/fonts
	cp -r $(LUA_SRC)/resources/textures/1x $(ROMFS_DIR)/resources/textures/
	cp -r $(LUA_SRC)/resources/sounds $(LUA_SRC)/resources/shaders $(ROMFS_DIR)/resources/
	cp $(LUA_SRC)/resources/fonts/m6x11plus.ttf $(LUA_SRC)/resources/fonts/NotoSans-Bold.ttf \
	   $(LUA_SRC)/resources/fonts/NotoSansJP-Bold.ttf $(LUA_SRC)/resources/fonts/NotoSansKR-Bold.ttf \
	   $(LUA_SRC)/resources/fonts/NotoSansSC-Bold.ttf $(LUA_SRC)/resources/fonts/NotoSansTC-Bold.ttf \
	   $(ROMFS_DIR)/resources/fonts/
	cp -r $(LUA_SRC)/localization $(ROMFS_DIR)/
	cp $(META)/gamepad_ui_3ds.png $(ROMFS_DIR)/resources/textures/1x/gamepad_ui.png
	@touch $@
romfs: $(ROMFS_DIR)/.stamp

# ---------------------------------------------------------------- packages
$(BUILD)/$(TARGET).smdh: $(META)/icon.png
	smdhtool --create "$(APP_TITLE)" "$(APP_DESC)" "$(APP_AUTHOR)" $< $@

$(BUILD)/$(TARGET).3dsx: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).smdh $(ROMFS_DIR)/.stamp
	3dsxtool $< $@ --smdh=$(BUILD)/$(TARGET).smdh --romfs=$(ROMFS_DIR)

$(BUILD)/banner.bnr: $(META)/banner.png $(META)/banner.wav
	$(BANNERTOOL) makebanner -i $(META)/banner.png -a $(META)/banner.wav -o $@

$(BUILD)/icon.icn: $(META)/icon.png
	$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESC)" -p "$(APP_AUTHOR)" -i $< -o $@

$(BUILD)/$(TARGET).cia: $(BUILD)/$(TARGET).elf $(BUILD)/banner.bnr $(BUILD)/icon.icn $(META)/balatro_c.rsf $(ROMFS_DIR)/.stamp
	$(MAKEROM) -f cia -o $@ -target t -exefslogo -elf $(BUILD)/$(TARGET).elf -rsf $(META)/balatro_c.rsf \
	  -icon $(BUILD)/icon.icn -banner $(BUILD)/banner.bnr -DAPP_ROMFS="$(abspath $(ROMFS_DIR))" \
	  -DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="CTR-H-BLTC" -DAPP_UNIQUE_ID="0xBA1A7"

clean:
	rm -rf $(OBJDIR) $(ROMFS_DIR) $(BUILD)/$(TARGET).* $(BUILD)/banner.bnr $(BUILD)/icon.icn
