# deps/

Place the following AMD/Solarflare release packages here before building the Docker image.

## Required packages

### OpenOnload (required by TCPDirect)

**Filename:** `sf-122451-ls-17-openonload-deb-release-package.zip`

Download from:
```
https://www.amd.com/content/dam/amd/en/support/downloads/solarflare/onload/openonload/9_0_2_47/sf-122451-ls-17-openonload-deb-release-package.zip
```

### TCPDirect

**Filename:** `xn-201048-ls-9-tcpdirect-deb-release-package.zip`

Download from:
```
https://www.amd.com/content/dam/amd/en/support/downloads/solarflare/onload/tcpdirect/9_0_2_47/xn-201048-ls-9-tcpdirect-deb-release-package.zip
```

## Notes

- Both packages are for version 9.0.2.47.
- The Dockerfile builds both from source with `ZF_DEVEL=1` (TCPDirect emulation mode),
  which allows running without Solarflare hardware inside Docker.
- These ZIP files are not committed to git (see `.gitignore`).
