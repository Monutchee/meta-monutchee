# The webengine nginx configuration uses auth_request to gate protected files.
PACKAGECONFIG:append:pn-nginx = " http-auth-request"

# Do not auto-start nginx at boot.
#
# In the front/back-end design the C++ backend OWNS its reverse proxy: it
# starts, stops, reloads and re-ports nginx itself via
# webengine::WebServerController. If the distro nginx.service also managed
# nginx we'd get a double-start, and nginx-as-root would break the same-user
# signal/reload model the backend depends on. So we ship nginx.service but
# leave it disabled; the backend's own service brings nginx up after boot.
#
# The upstream recipe (meta-webserver) inherits systemd without setting
# SYSTEMD_AUTO_ENABLE, which defaults to "enable". Override it here.
SYSTEMD_AUTO_ENABLE:pn-nginx = "disable"
