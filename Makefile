CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2
WARNINGS = -Wall -Wextra -Werror
BUILD = build

.PHONY: all test clean
all: $(BUILD)/nm-sshuttle-service $(BUILD)/libnm-vpn-plugin-sshuttle.so

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/nm-sshuttle-service: src/service.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARNINGS) $< -o $@ $(shell $(PKG_CONFIG) --cflags --libs libnm gio-2.0)

$(BUILD)/libnm-vpn-plugin-sshuttle.so: src/editor.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARNINGS) -fPIC -shared $< -o $@ $(shell $(PKG_CONFIG) --cflags --libs libnm gtk4)

$(BUILD)/editor-smoke: tests/editor-smoke.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARNINGS) $< -o $@ $(shell $(PKG_CONFIG) --cflags --libs libnm gtk4 gmodule-2.0)

test: all $(BUILD)/editor-smoke
	$(BUILD)/editor-smoke $(abspath $(BUILD)/libnm-vpn-plugin-sshuttle.so)

clean:
	$(RM) -r $(BUILD)
