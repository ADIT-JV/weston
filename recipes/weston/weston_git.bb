SUMMARY = "Weston, a Wayland compositor"
DESCRIPTION = "Weston is the reference implementation of a Wayland compositor"
HOMEPAGE = "http://wayland.freedesktop.org"

LICENSE = "MIT"
LIC_FILES_CHKSUM = " \
  file://COPYING;md5=d79ee9e66bb0f95d3386a7acae780b70 \
  file://src/compositor.c;endline=23;md5=1d535fed266cf39f6d8c0647f52ac331 \
"

# define ADIT specifics
ADIT_SOURCE_GIT = "weston"
ADIT_SOURCE_PATH = ""

# include adit common definitions
require recipes/adit-package/adit-package-common.inc

inherit autotools pkgconfig useradd

DEPENDS += "libxkbcommon gdk-pixbuf pixman cairo glib-2.0 jpeg wayland virtual/egl pango libinput"
DEPENDS += " \
  ${@base_conditional('GPU_HW_VENDOR', 'VIVANTE', 'libdrm', '', d)} \
  ${@base_conditional('GPU_HW_VENDOR', 'IMGTEC', 'libdrm', '', d)} \
  ${@base_conditional('GPU_HW_VENDOR', 'INTEL', 'libdrm', '', d)} \
"

RDEPENDS_${PN} += "${PN}-examples xkeyboard-config"
RRECOMMENDS_${PN} = "liberation-fonts"

PACKAGES =+ "${PN}-examples"

FILES_${PN} += "${systemd_unitdir} ${datadir}/wayland-sessions"

FILES_${PN}-examples = " \
  ${bindir}/weston-calibrator \
  ${bindir}/weston-clickdot \
  ${bindir}/weston-cliptest \
  ${bindir}/weston-dnd \
  ${bindir}/weston-editor \
  ${bindir}/weston-eventdemo \
  ${bindir}/weston-flower \
  ${bindir}/weston-fullscreen \
  ${bindir}/weston-image \
  ${bindir}/weston-multi-resource \
  ${bindir}/weston-resizor \
  ${bindir}/weston-scaler \
  ${bindir}/weston-simple-damage \
  ${bindir}/weston-simple-egl \
  ${bindir}/weston-simple-keyboard-binding \
  ${bindir}/weston-simple-shm \
  ${bindir}/weston-simple-touch \
  ${bindir}/weston-smoke \
  ${bindir}/weston-stacking \
  ${bindir}/weston-subsurfaces \
  ${bindir}/weston-transformed \
"

USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM_${PN} = "--system weston-launch"

EXTRA_OEMAKE_append = "${@base_conditional('GPU_HW_VENDOR', 'VIVANTE', ' \
  COMPOSITOR_LIBS="-lGLESv2 -lEGL -lwayland-server -lxkbcommon -lpixman-1" \
  COMPOSITOR_CFLAGS="-I ${STAGING_DIR_HOST}/usr/include/pixman-1 -DLINUX=1 -DEGL_API_FB -DEGL_API_WL" \
  FB_COMPOSITOR_CFLAGS="-DLINUX=1 -DEGL_API_FB -DEGL_API_WL -I $WLD/include" \
  FB_COMPOSITOR_LIBS="-lGLESv2 -lEGL -lwayland-server -lxkbcommon" \
  EGL_TESTS_CFLAGS="-DLINUX -DEGL_API_FB -DEGL_API_WL" \
  CLIENT_CFLAGS="-I ${STAGING_INCDIR}/cairo -I ${STAGING_INCDIR}/pixman-1 -DLINUX -DEGL_API_FB -DEGL_API_WL" \
  SIMPLE_EGL_CLIENT_CFLAGS="-DLINUX -DEGL_API_FB -DEGL_API_WL"', '', d)}"

EXTRA_OECONF_append = " \
  --enable-clients \
  --enable-demo-clients-install \
  --enable-setuid-install \
  --enable-simple-clients \
  --enable-simple-egl-clients \
  --disable-libunwind \
  --disable-rdp-compositor \
  --disable-rpi-compositor \
  --disable-xwayland \
  --disable-xwayland-test \
"

EXTRA_OECONF_append += "${@base_conditional('GPU_HW_VENDOR', 'VIVANTE', 'WESTON_NATIVE_BACKEND=imx6drm-backend.so', '', d)}"
EXTRA_OECONF_append += "${@base_conditional('GPU_HW_VENDOR', 'INTEL', 'WESTON_NATIVE_BACKEND=drm-backend.so', '', d)}"
EXTRA_OECONF_append += "${@base_conditional('GPU_HW_VENDOR', 'IMGTEC', 'WESTON_NATIVE_BACKEND=drm-backend.so', '', d)}"
EXTRA_OECONF_append += "${@base_conditional('GPU_HW_VENDOR', 'LLVMPIPE', 'WESTON_NATIVE_BACKEND=fbdev-backend.so', '', d)}"

PACKAGECONFIG = "${@base_conditional('GPU_HW_VENDOR', 'VIVANTE', 'imx6 gal2d systemd-notify', '', d)}"
PACKAGECONFIG += "${@base_conditional('GPU_HW_VENDOR', 'INTEL', 'drm systemd-notify', '', d)}"
PACKAGECONFIG += "${@base_conditional('GPU_HW_VENDOR', 'IMGTEC', 'drm systemd-notify', '', d)}"
PACKAGECONFIG += "${@base_conditional('GPU_HW_VENDOR', 'LLVMPIPE', 'fbdev systemd-notify', '', d)}"

#
# Compositor choices
#
# Weston on KMS
PACKAGECONFIG[drm] = "--enable-drm-compositor,--disable-drm-compositor,drm udev mesa mtdev"
# Weston on imx6
PACKAGECONFIG[imx6] = "--enable-imx6drm-compositor,--disable-imx6drm-compositor,udev mtdev"
# Weston on imx6
PACKAGECONFIG[gal2d] = "--enable-gal2d-renderer,--disable-gal2d-renderer,udev mtdev"
# Weston on wayland
PACKAGECONFIG[wayland] = "--enable-wayland-compositor,--disable-wayland-compositor,mesa"
# Weston on X11
PACKAGECONFIG[x11] = "--enable-x11-compositor,--disable-x11-compositor,virtual/libx11 libxcb libxcursor cairo"
# Headless Weston
PACKAGECONFIG[headless] = "--enable-headless-compositor,--disable-headless-compositor"
# Weston on framebuffer
PACKAGECONFIG[fbdev] = "--enable-fbdev-compositor,--disable-fbdev-compositor,udev mtdev"
# weston-launch
PACKAGECONFIG[launch] = "--enable-weston-launch,--disable-weston-launch,libpam"
# weston-launch
PACKAGECONFIG[systemd-notify] = "--enable-systemd-notify,--disable-systemd-notify,systemd"

do_install_append() {
  # install weston service file
  install -d ${D}/${systemd_system_unitdir}
  install -m 0644 ${S}/recipes/weston/files/weston.service ${D}/${systemd_system_unitdir}
}

PARALLEL_MAKE = " "
