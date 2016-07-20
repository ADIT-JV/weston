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

DEPENDS += "libxkbcommon gdk-pixbuf pixman cairo glib-2.0 jpeg"
DEPENDS += "wayland wayland-protocols libinput virtual/egl pango wayland-native"
DEPENDS += "${@base_conditional('GPU_HW_VENDOR', 'IMGTEC', 'mesa', '', d)}"

RDEPENDS_${PN} += "${PN}-examples xkeyboard-config"
RRECOMMENDS_${PN} = "liberation-fonts"

PACKAGE_BEFORE_PN += "${PN}-examples"

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

EXTRA_OECONF_append = " \
  --enable-systemd-notify \
  --enable-setuid-install \
  --disable-rdp-compositor \
  --disable-rpi-compositor \
"

PACKAGECONFIG = "kms egl clients"

#
# Compositor choices
#
# Weston on KMS
PACKAGECONFIG[kms] = "--enable-drm-compositor,--disable-drm-compositor,drm udev virtual/libgles2 mtdev"
# Weston on Wayland (nested Weston)
PACKAGECONFIG[wayland] = "--enable-wayland-compositor,--disable-wayland-compositor,virtual/libgles2"
# Weston on X11
PACKAGECONFIG[x11] = "--enable-x11-compositor,--disable-x11-compositor,virtual/libx11 libxcb libxcb libxcursor cairo"
# Headless Weston
PACKAGECONFIG[headless] = "--enable-headless-compositor,--disable-headless-compositor"
# Weston on framebuffer
PACKAGECONFIG[fbdev] = "--enable-fbdev-compositor,--disable-fbdev-compositor,udev mtdev"
# weston-launch
PACKAGECONFIG[launch] = "--enable-weston-launch,--disable-weston-launch,pam"
# VA-API desktop recorder
PACKAGECONFIG[vaapi] = "--enable-vaapi-recorder,--disable-vaapi-recorder,libva"
# Weston with EGL support
PACKAGECONFIG[egl] = "--enable-egl --enable-simple-egl-clients,--disable-egl --disable-simple-egl-clients,virtual/egl"
# Weston with cairo glesv2 support
PACKAGECONFIG[cairo-glesv2] = "--with-cairo-glesv2,--with-cairo=image,cairo"
# Weston with lcms support
PACKAGECONFIG[lcms] = "--enable-lcms,--disable-lcms,lcms"
# Weston with webp support
PACKAGECONFIG[webp] = "--with-webp,--without-webp,libwebp"
# Weston with unwinding support
PACKAGECONFIG[libunwind] = "--enable-libunwind,--disable-libunwind,libunwind"
# Weston with systemd-login support
PACKAGECONFIG[systemd] = "--enable-systemd-login,--disable-systemd-login,systemd dbus"
# Weston with Xwayland support (requires X11 and Wayland)
PACKAGECONFIG[xwayland] = "--enable-xwayland,--disable-xwayland"
# colord CMS support
PACKAGECONFIG[colord] = "--enable-colord,--disable-colord,colord"
# Clients support
PACKAGECONFIG[clients] = "--enable-clients --enable-simple-clients --enable-demo-clients-install,--disable-clients --disable-simple-clients"

do_install_append() {
	# Weston doesn't need the .la files to load modules, so wipe them
	rm -f ${D}/${libdir}/weston/*.la

        # install weston service file
        install -d ${D}/${systemd_system_unitdir}
        install -m 0644 ${S}/recipes/weston/files/weston.service ${D}/${systemd_system_unitdir}
}

PARALLEL_MAKE = " "
