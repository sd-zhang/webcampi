################################################################################
#
# uvc-gadget
#
################################################################################

UVC_GADGET_VERSION = v0.4.4
UVC_GADGET_SITE = https://github.com/elcalzado/uvc-gadget.git
UVC_GADGET_SITE_METHOD = git
UVC_GADGET_LICENSE = NONE

UVC_GADGET_DEPENDENCIES = libcamera jpeg host-pkgconf

UVC_GADGET_CONF_OPTS += -Dwerror=false

UVC_GADGET_POST_INSTALL_TARGET_HOOKS += UVC_GADGET_INSTALL_SCRIPT
UVC_GADGET_POST_INSTALL_TARGET_HOOKS += UVC_GADGET_INSTALL_INIT_SYSV

define UVC_GADGET_INSTALL_SCRIPT
        $(INSTALL) -D -m 755 $(@D)/scripts/uvc-gadget.sh \
        $(TARGET_DIR)/usr/local/bin/uvc-gadget.sh
endef

define UVC_GADGET_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 755 $(BR2_EXTERNAL_WEBCAMPI_PATH)/package/uvc-gadget/S60uvc-gadget \
		$(TARGET_DIR)/etc/init.d/S60uvc-gadget
endef

# One source of truth shared with the microphone plugin; no extra runtime library.
define UVC_GADGET_COPY_AUDIO_CONTROLS
	cp $(BR2_EXTERNAL_WEBCAMPI_PATH)/package/pisight-mic/audio-control.h $(@D)/lib/
	cp $(BR2_EXTERNAL_WEBCAMPI_PATH)/package/pisight-mic/pisight-controls.h $(@D)/lib/
endef
UVC_GADGET_PRE_CONFIGURE_HOOKS += UVC_GADGET_COPY_AUDIO_CONTROLS

$(eval $(meson-package))
