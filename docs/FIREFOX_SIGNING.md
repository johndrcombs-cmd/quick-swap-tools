# Firefox signing and distribution

Quick Swap Tools uses Mozilla's **unlisted/self-distributed** signing channel for its first releases. The signed XPI can be installed in Firefox Release without `about:debugging` and shared directly with trusted friends. It does not appear publicly in AMO search.

Mozilla's current requirements:

- Firefox Release and Beta require Mozilla-signed extensions.
- Signing is performed through addons.mozilla.org even for self-distribution.
- New extensions must declare their data-collection behavior. Quick Swap Tools declares `required: ["none"]` because all routing and Whatnot interaction stay on the user's computer.
- The signed release requires Firefox desktop 140 or newer so Firefox can display that built-in no-data-collection declaration.
- Every update must use the same extension ID and a higher manifest version.

The stable extension ID is:

```text
quick-swap-tools@onibyts.com
```

Do not change that ID after the first AMO submission.

## First-time Mozilla setup

1. Sign in or register at <https://addons.mozilla.org/developers/>.
2. Read and accept the Firefox Add-on Distribution Agreement when prompted.
3. Open <https://addons.mozilla.org/developers/addon/api/key/>.
4. Generate API credentials. AMO displays a JWT issuer and JWT secret.
5. Keep the secret private. Never paste it into chat, commit it, or save it inside this repository.

## Validate and create an unsigned review package

```bash
cd ~/quick-swap-tools
pnpm install
pnpm run check
pnpm run build:extension
```

The review/upload ZIP is created under `dist/unsigned/`. This package is human-readable and not minified, so a separate generated-source archive is not required.

## Sign from a local terminal

Run:

```bash
cd ~/quick-swap-tools
pnpm run sign:unlisted
```

The script prompts for the JWT issuer and secret. Secret input is hidden, is not stored, and is passed to `web-ext` through environment variables rather than command-line arguments. The signed XPI is written to `dist/signed/`.

For the initial version, Mozilla may return the signed XPI immediately or hold it for review. If review is required, AMO sends email when signing completes; download the signed file from **Developer Hub → My Add-ons → Quick Swap Tools → View all versions**.

## Install the signed build locally

The signed XPI and native host must use the same extension ID.

1. Run `./scripts/install.sh` to update the native-host manifest.
2. Remove the temporary Quick Swap Tools entry from `about:debugging` if it is still loaded.
3. Open the signed `.xpi` in Firefox and approve installation.
4. Confirm `quick-swap-host` is running and test only against a private stream.

## Sharing with friends

The signed XPI solves Firefox's extension-signing requirement, but Quick Swap Tools also needs its small native host for KDE global shortcuts. For the current source distribution, a friend must:

1. Use Linux with KDE Plasma/Wayland and Firefox.
2. Install the Qt 6 and KDE Frameworks 6 GlobalAccel runtime/build prerequisites.
3. Run `./scripts/install.sh` from this project.
4. Install the signed XPI in Firefox.

Before broad distribution, create a release archive containing a prebuilt native host and installer so friends do not need a compiler. Windows, macOS, GNOME, and other desktop environments need separate global-shortcut adapters.

## Updates

Before signing another release:

1. Increase `version` in `extension/manifest.json`.
2. Increase the project version in `package.json` to match.
3. Run `pnpm run check`.
4. Run `pnpm run sign:unlisted` with the same AMO account and extension ID.
5. Share the new signed XPI. Unlisted self-distributed extensions do not automatically update unless an `update_url` and update manifest are hosted separately.

For easy automatic updates and public discovery later, submit a future version as a **listed AMO extension**. That requires listing metadata, support information, icons/screenshots, and Mozilla review.
