CAMERA_100ASK 	   ?= camera_100ask

override CFLAGS := -I$(LVGL_DIR)/$(CAMERA_100ASK) $(CFLAGS)

CSRCS += $(wildcard $(LVGL_DIR)/$(CAMERA_100ASK)/assets/*.c)
CSRCS += $(wildcard $(LVGL_DIR)/$(CAMERA_100ASK)/*.c)

