# wspace_client_scout
# v1.0.0-1
#
# by Yijing Zeng

include $(TOPDIR)/rules.mk

PKG_NAME:=wspace_client_scout
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/wspace_client_scout
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=wspace_client_scout -- Wspace Client Scout
  DEPENDS:=+libstdcpp +libpthread
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)
endef

define Build/Configure
endef

TARGET_CFLAGS += $(FPIC)

define Package/wspace_client_scout/install
	$(INSTALL_DIR) $(1)/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/wspace_client_scout $(1)/bin/
endef

$(eval $(call BuildPackage,wspace_client_scout))


