# libALAC

include ../makeconf

VER :=
URL := https://github.com/macosforge/alac/archive/refs/heads/master.zip
PKG := $(notdir $(URL))
DIR := alac-master
LIB := libALAC-ff.$(SO)

all: $(DIR)
	cd $(DIR) && cat ../*.patch | patch -p1
	$(MAKE) -rR $(LIB)

# download
$(PKG):
	$(CURL) -o $(PKG) $(URL)

# unpack
$(DIR): $(PKG)
	$(UNZIP) -n $(PKG)
	touch $(DIR)

# build
CFLAGS += -Wall \
	-I$(DIR)/codec

SRC_CPP := \
	$(DIR)/codec/ALACDecoder.cpp
SRC := \
	$(DIR)/codec/EndianPortable.c \
	$(DIR)/codec/ALACBitUtilities.c \
	$(DIR)/codec/ag_dec.c \
	$(DIR)/codec/dp_dec.c \
	$(DIR)/codec/matrix_dec.c
# SRC_CPP := \
# 	$(DIR)/codec/ALACEncoder.cpp
# SRC += \
# 	$(DIR)/codec/ag_enc.c \
# 	$(DIR)/codec/dp_enc.c \
# 	$(DIR)/codec/matrix_enc.c
OBJ := ALAC-ff.o $(SRC_CPP:.cpp=.o) $(SRC:.c=.o)

%.o: %.c
	$(C) $(CFLAGS) $< -o $@

%.o: %.cpp
	$(CXX) $(CFLAGS) $< -o $@

$(LIB): $(OBJ)
	$(LINKXX) $(LINKXXFLAGS) -shared $+ -o $@

clean:
	$(RM) $(OBJ) $(DIR)
