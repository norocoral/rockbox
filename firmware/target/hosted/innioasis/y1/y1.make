#             __________               __   ___.
#   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
#   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
#   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
#   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
#                     \/            \/     \/    \/            \/
#

LIBY1_DIR ?= $(patsubst %/include/liby1.h,%,$(firstword \
	$(wildcard $(ROOTDIR)/vendor/liby1/include/liby1.h) \
	$(wildcard $(ROOTDIR)/../../../!nugget/main/liby1/include/liby1.h) \
	$(wildcard $(ROOTDIR)/../../!nugget/main/liby1/include/liby1.h) \
	$(wildcard $(ROOTDIR)/hosted_linux_port/liby1/include/liby1.h)))
TARGET_INC += -I$(LIBY1_DIR)/include
INCLUDES += -I$(FIRMDIR)/include -I$(FIRMDIR)/export $(TARGET_INC) -I$(BUILDDIR) -I$(APPSDIR) -I$(LIBY1_DIR)/include

SIMFLAGS += $(INCLUDES) $(DEFINES) -DHAVE_CONFIG_H $(GCCOPTS)

.SECONDEXPANSION:

$(BUILDDIR)/firmware/target/hosted/innioasis/y1/liby1-rockbox.o $(OBJDIR)/firmware/target/hosted/innioasis/y1/liby1-rockbox.o: $(wildcard $(LIBY1_DIR)/src/*.c) $(wildcard $(LIBY1_DIR)/include/*.h)

$(BUILDDIR)/rockbox.elf : $$(OBJ) $(FIRMLIB) $(VOICESPEEXLIB) $(CORE_LIBS)
	$(call PRINTS,LD $(@F))$(CC) $(GCCOPTS) -o $@ $(OBJ) \
		-L$(BUILDDIR)/firmware -lfirmware \
		-L$(RBCODEC_BLD)/codecs $(call a2lnk, $(VOICESPEEXLIB)) \
		-L$(BUILDDIR)/lib $(call a2lnk,$(CORE_LIBS)) \
		$(LDOPTS) $(GLOBAL_LDOPTS) -Wl,-Map,$(BUILDDIR)/rockbox.map

$(BUILDDIR)/$(BINARY): $(BUILDDIR)/rockbox.elf
	$(call PRINTS,OC $(@F))$(call objcopy,$^,$@)

