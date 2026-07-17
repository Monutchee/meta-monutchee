# msap1-web-backend owns the nginx process lifecycle through WebEngine's
# NginxController. Keep the distribution-provided nginx.service available for
# diagnostics, but do not enable a second independent nginx master at boot.
SYSTEMD_AUTO_ENABLE = "disable"
