MICROAMP_MOD_DIR := $(USERMOD_DIR)

# Add all C files to SRC_USERMOD.
SRC_USERMOD += $(MICROAMP_MOD_DIR)/microamp.c

# We can add our module folder to include paths if needed
# This is not actually needed in this example.
CFLAGS_USERMOD += -I$(MICROAMP_MOD_DIR) -ggdb
CMICROAMP_MOD_DIR := $(USERMOD_DIR)
