# libogg.  Used by libvorbis only.

include ../makeconf

VER := 1.3.3
URL := http://downloads.xiph.org/releases/ogg/libogg-$(VER).tar.xz
PKG := $(notdir $(URL))
DIR := libogg-$(VER)
LIB := libogg-ff.$(SO)

all: $(DIR)
	$(MAKE) -rR $(LIB)

# download
$(PKG):
	$(CURL) -o $(PKG) $(URL)

# unpack
$(DIR): $(PKG)
	$(UNTAR_XZ) $(PKG)
	touch $(DIR)

# build
CFLAGS += -I.. -I$(DIR)/include \
	-fvisibility=default

SRC := $(DIR)/src/bitwise.c
OBJ := $(SRC:.c=.o)

%.o: %.c
	$(C) $(CFLAGS) $< -o $@

$(LIB): $(OBJ)
	$(LINK) -shared $+ $(LINKFLAGS) -o $@

clean:
	$(RM) $(OBJ) $(DIR)
