---
name: setup-remote-profile
description: Use when adding a new remote server target for syncing and building this project, or when validate_remote.sh fails for an existing profile
---

# Setup Remote Profile

## Overview

Remote targets are configured via `profiles/<alias>.env`. The scripts
`sync_remote.sh`, `build_remote.sh`, and `validate_remote.sh` all source
this file. Always validate before syncing.

---

## Step 1 — Pick an SSH alias or direct IP

| Mode | Use when | `REMOTE_HOST` value |
|------|----------|---------------------|
| SSH config alias | Preferred — host appears in `~/.ssh/config` | Short alias, e.g. `gpu-dev2` |
| Direct IP | No SSH config entry wanted | IP address, e.g. `192.168.1.50` |

Using an alias keeps the profile portable and centralises auth settings.

---

## Step 2 — Check or create the SSH config entry

```bash
grep -A6 "Host my-build-box" ~/.ssh/config
```

If no match, **add a block** to `~/.ssh/config`:

```
Host my-build-box
    HostName 192.168.1.50
    User devuser
    IdentityFile ~/.ssh/id_rsa   # omit if using the default key
```

Then verify the alias works:

```bash
ssh -o ConnectTimeout=5 -o BatchMode=yes my-build-box echo "SSH OK"
```

If `BatchMode=yes` hangs or asks for a password, fix key-based auth before
proceeding (copy public key: `ssh-copy-id my-build-box`).

> **Skip this step** only if you are using a direct IP (no alias needed).
> In that case set `REMOTE_HOST=192.168.1.50` and optionally
> `REMOTE_SSH_KEY=~/.ssh/id_rsa` in the profile.

---

## Step 3 — Create the profile file

Copy the template and fill in your values:

```bash
cp profiles/example.env profiles/my-build-box.env
```

Filename must be `profiles/<alias>.env` (match the alias exactly).

> **Never commit profile files to git.** `profiles/*.env` is gitignored because
> these files contain server hostnames, usernames, and key paths.
> Only `profiles/example.env` (the template with no real values) is tracked.
> Do not run `git add profiles/` — git will silently skip `.env` files, but
> `git add -f` or editing `.gitignore` would expose credentials.

```bash
REMOTE_HOST=my-build-box              # SSH alias or IP
REMOTE_USER=devuser                   # SSH login username
REMOTE_PATH='~/projects/ilink3-demo' # destination on remote
# REMOTE_SSH_KEY=~/.ssh/id_rsa       # only if NOT using ~/.ssh/config IdentityFile
```

**Profile variable reference:**

| Variable | Required | Notes |
|----------|----------|-------|
| `REMOTE_HOST` | Yes | SSH alias or IP; tested by validate script |
| `REMOTE_USER` | Yes | Must match `User` in SSH config (if using alias) |
| `REMOTE_PATH` | Yes | Single-quoted `'~/...'` is correct — remote shell expands `~` at runtime |
| `REMOTE_SSH_KEY` | No | Prefer `~/.ssh/config` IdentityFile over this |

---

## Step 4 — Validate (mandatory before sync/build)

```bash
PROFILE=profiles/my-build-box.env ./scripts/validate_remote.sh
```

Expected output:

```
[*] Checking SSH to devuser@my-build-box...
SSH OK
[*] Checking required tools on remote...
  [ok] docker
  [ok] make
  [ok] gcc
  [ok] unzip
```

**Do not proceed to Step 5 if any tool shows `[missing]`.**
Install missing tools on the remote:

```bash
ssh my-build-box sudo apt-get install -y docker.io make gcc unzip
# Docker group (avoids sudo in build script); requires logout/login to take effect:
ssh my-build-box sudo usermod -aG docker devuser
# Then log out and SSH back in, or reboot the remote.
```

---

## Step 5 — Sync and build

```bash
# Push source files to remote
PROFILE=profiles/my-build-box.env ./scripts/sync_remote.sh

# Build Docker image and run loopback test on remote
PROFILE=profiles/my-build-box.env ./scripts/build_remote.sh
```

Success ends with `[*] Loopback test complete.`
See `docs/loopback-container.md` for full expected output and Docker flag explanations.

---

## Common Mistakes

| Mistake | Fix |
|---------|-----|
| Skipping validate_remote.sh | Always run it — build failures are silent if tools are missing |
| `REMOTE_HOST` is IP but SSH config has an alias | Use the alias or the IP consistently; mixing causes auth failures |
| `REMOTE_SSH_KEY` set alongside `~/.ssh/config` IdentityFile | Pick one; `REMOTE_SSH_KEY` only applies inside the scripts, not the SSH alias itself |
| Profile filename doesn't match the alias | `PROFILE=profiles/X.env` — keep X identical to the Host alias |
| `REMOTE_USER` differs from `User` in SSH config | They must match, or use only one approach |
| Committing `profiles/*.env` to git | **Never do this** — files are gitignored for a reason; use `profiles/example.env` as the committed template |
