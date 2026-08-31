# Meter Data Sender requires libcurl's SFTP transport on the target. Keep the
# native and SDK variants at their upstream feature sets.
PACKAGECONFIG:append:class-target = " libssh2"
