# Stable shared MNC service identities. Numeric IDs own persistent data.
# Never renumber or reuse IDs; recipes must reference these definitions.
# Vendor-prefixed names avoid collisions across layers and products.

MNC_GID_DATA      = "780"
MNC_GID_SETTINGS  = "781"
MNC_UID_SETTINGS  = "781"
MNC_UID_STREAM    = "782"
MNC_UID_HISTORIAN = "783"
MNC_GID_WEB       = "784"
MNC_UID_WEB       = "784"
MNC_UID_MODBUS    = "785"
MNC_UID_MQTT      = "786"
MNC_UID_DATA_SENDER = "787"

# Shared shape for every MNC service account: no login, no home directory.
MNC_SYSTEM_ACCOUNT = "--system --home /nonexistent --no-create-home --shell /sbin/nologin"

# mnc-data     - shared access to acquisition IPC objects and /data subtrees
# mnc-settings - the settings authority; also gates mutation over its socket
# mnc-web      - the network-facing web/proxy identity
# mnc-modbus   - the field-protocol identity; reads acquisition/settings IPC
# mnc-mqtt     - the outbound MQTT publisher; reads snapshots/settings IPC
# mnc-data-sender - outbound historian exporter; owns its durable outbox
MNC_GROUPADD_SHARED = "--system --gid ${MNC_GID_DATA} mnc-data; \
    --system --gid ${MNC_GID_SETTINGS} mnc-settings; \
    --system --gid ${MNC_GID_WEB} mnc-web"

# mnc-settings owns /data/mnc/settings, so it is the only account whose primary
# group is the authority group; the rest sit in mnc-data and are granted the
# authority group only as a supplementary where a unit needs the socket.
MNC_USERADD_SHARED = "${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_SETTINGS} --gid mnc-settings --groups mnc-data mnc-settings; \
    ${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_STREAM} --gid mnc-data mnc-stream; \
    ${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_HISTORIAN} --gid mnc-data mnc-historian; \
    ${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_WEB} --gid mnc-web mnc-web; \
    ${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_MODBUS} --gid mnc-data mnc-modbus; \
    ${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_MQTT} --gid mnc-data --groups mnc-settings mnc-mqtt; \
    ${MNC_SYSTEM_ACCOUNT} --uid ${MNC_UID_DATA_SENDER} --gid mnc-data --groups mnc-settings mnc-data-sender"
