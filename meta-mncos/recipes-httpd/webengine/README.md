# webengine

Builds the C++ Boost.Beast backend from the **Nginx front/back-end demo**
(`github.com/lesterlo/Nginx-front-back-end-demo`) and installs the whole on-target
web stack:

- `backend` → `${bindir}/backend`, started by `beast-backend.service` (enabled at boot)
- `nginx.target.conf` → `/opt/monutchee/mncos/conf/webserver/nginx.conf` + the web root
- the `worker`-owned `conf/webserver` and `runtime/webserver/ssl` dirs, plus a
  per-device self-signed TLS cert generated on first boot

The backend **owns nginx's lifecycle** (it starts/stops/reloads it via
`NginxController` — "Option A"), so the distro `nginx.service` stays disabled
(`recipes-httpd/nginx/nginx_%.bbappend`).

## Source switch: cloud vs local

The recipe can build from GitHub (default) or from a local git checkout. Set this
in `build/conf/local.conf`:

```sh
# Default — fetch the pinned revision from GitHub:
WEBENGINE_SRC = "cloud"

# Or build from a local checkout (for development):
WEBENGINE_SRC = "local"
# Optional — only if your checkout isn't at the recipe's default path:
#WEBENGINE_LOCAL_DIR = "/opt/monutchee/test/Nginx-front-back-end-interaction-demo"
```

After changing it, rebuild:

```sh
bitbake -c cleansstate webengine   # force a re-fetch/re-build after a source switch
bitbake webengine
```

### `local` mode — important
`local` uses the **file-protocol git fetcher**, so it builds the **committed**
state of your checkout, *not* the live working tree. Commit your changes before
building to test them. If you need to iterate on uncommitted edits, use
`devtool modify webengine` (or `externalsrc`) instead.

## Pinning the revision (reproducible / release builds)

`cloud` builds a pinned commit, not a moving branch tip:

```
SRCREV_webengine = "<commit sha>"     # in webengine_git.bb
WEBENGINE_GIT_BRANCH ?= "feature/new_glaze_install_method"
```

- Bump `SRCREV_webengine` whenever you push new commits you want picked up.
- For a stable release, set it to the commit a release **tag** points at.
- To temporarily track the branch tip instead, set `SRCREV_webengine = "${AUTOREV}"`
  (needs network at parse time, and builds become non-reproducible).

## Notes

- Glaze is packaged separately by `recipes-support/glaze/glaze_7.7.1.bb`. The
  upstream webengine CMake project uses `find_package(glaze CONFIG REQUIRED)`,
  so all source downloads happen in Yocto's fetch phase.
- `DEPENDS = boost glaze openssl`; `RDEPENDS = worker-user nginx openssl-bin`.
