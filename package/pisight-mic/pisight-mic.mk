################################################################################
#
# pisight-mic
#
################################################################################

PISIGHT_MIC_VERSION = 1
PISIGHT_MIC_SITE = $(BR2_EXTERNAL_WEBCAMPI_PATH)/package/pisight-mic
PISIGHT_MIC_SITE_METHOD = local
PISIGHT_MIC_DEPENDENCIES = alsa-lib alsa-utils

define PISIGHT_MIC_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -std=c99 -Wall -Wextra -Werror \
		$(@D)/pisight-mic.c $(TARGET_LDFLAGS) -lasound -o $(@D)/pisight-mic
	$(TARGET_CC) $(TARGET_CFLAGS) -std=c99 -Wall -Wextra -Werror -fPIC -DPIC \
		-shared $(@D)/pcm_pisight_voice.c $(TARGET_LDFLAGS) -lasound -lm \
		-o $(@D)/libasound_module_pcm_pisight_voice.so
	$(TARGET_CC) $(TARGET_CFLAGS) -std=c99 -Wall -Wextra -Werror \
		$(@D)/config-lock.c $(TARGET_LDFLAGS) -o $(@D)/isight-config-lock
endef

define PISIGHT_MIC_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pisight-mic $(TARGET_DIR)/usr/bin/pisight-mic
	$(INSTALL) -D -m 0755 $(@D)/libasound_module_pcm_pisight_voice.so \
		$(TARGET_DIR)/usr/lib/alsa-lib/libasound_module_pcm_pisight_voice.so
	$(INSTALL) -D -m 0755 $(@D)/isight-config-lock $(TARGET_DIR)/usr/bin/isight-config-lock
endef

$(eval $(generic-package))
