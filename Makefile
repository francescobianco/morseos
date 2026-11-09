# MorseOS emulator. The sources come from bundle.ini through tools/bundle.py, like the firmware.
CC     ?= gcc
BUILD  := build
GEN    := _gen
BIN    := $(BUILD)/morseos-emu

.DEFAULT_GOAL := $(BIN)

MODULE_INIS := $(wildcard core/*/module.ini lib/*/module.ini apps/*/module.ini)

include $(GEN)/bundle.mk

$(GEN)/bundle.mk: bundle.ini tools/bundle.py $(MODULE_INIS)
	python3 tools/bundle.py . bundle.ini $(GEN)

GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   := $(shell pkg-config --libs gtk+-3.0)

CFLAGS += -std=gnu11 -O1 -g -Wall -MMD -MP -pthread -Iemulator/hal -Iemulator -I$(GEN) $(addprefix -I,$(MOS_INC))

SRC := $(MOS_SRC) $(GEN)/apps.c emulator/emu_core.c emulator/emu_script.c emulator/main.c emulator/gui.c
OBJ := $(SRC:%.c=$(BUILD)/%.o)

$(BIN): $(OBJ)
	$(CC) -pthread -o $@ $^ $(GTK_LIBS)

$(BUILD)/emulator/main.o $(BUILD)/emulator/gui.o: CFLAGS += $(GTK_CFLAGS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: run test clean
run: $(BIN)
	$(BIN)

test: $(BIN)
	@mkdir -p $(BUILD)/shots
	$(BIN) --headless --script tests/smoke.emu > $(BUILD)/smoke.log || { cat $(BUILD)/smoke.log; exit 1; }
	@echo "smoke test OK, schermate in $(BUILD)/shots"

clean:
	rm -rf $(BUILD) $(GEN)

-include $(OBJ:.o=.d)
