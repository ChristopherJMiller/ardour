# Vendored nixcache-oci

Files copied verbatim from upstream [`cmspam/nixcache-oci`](https://github.com/cmspam/nixcache-oci) at the time of vendoring:

- `lib/cache-builder.sh` — pipeline that builds flake outputs, signature-filters out anything already on a public cache, and pushes the rest as OCI blobs to GHCR.
- `proxy/main.py` — local HTTP proxy used by the cache-builder (and by any consumer of the cache) to translate Nix's binary-cache protocol into GHCR's OCI Registry API.

The `lib/cache-builder.sh` script expects `proxy/main.py` to live at `../proxy/main.py` relative to itself, so the two are co-located here under `.github/nixcache-oci/` to preserve that layout without colliding with Ardour's own `lib/` directory.

Consumed by `.github/workflows/publish-cache.yml`. Consumer-side setup lives in this repo's top-level `README` once we have one, and in the user's nixos config under `services.nixcache-proxy`.

Upstream license at the time of vendoring: no LICENSE file. Treat as fair-use vendoring of CI tooling; revisit if upstream clarifies licensing.
