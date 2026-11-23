# Local source download cache (not included in Git)

Package recipes fetch source archives here; this directory is ignored except
for this README. Source archives are not included in the repository. The build
downloads missing archives, but some legacy URLs may no longer work. If a
compatible SDK cache is available, import it with:

```bash
bash scripts/import-sdk-assets.sh --from-sdk /path/to/existing-sdk --downloads
```

Alternatively copy source archives into this directory directly, preserving
filenames. The SDK verifies package hashes according to the recipes. This
cache is separate from compiled packages and images under `out/`.
