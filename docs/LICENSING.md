# whysho DE1 — Licensing & Copy Protection

This document describes the licensing architecture, the token format, and an
honest assessment of what the protection does and does not guarantee.

## Overview

```
 ┌────────────┐   license key    ┌──────────────────┐   signed token   ┌─────────────┐
 │  Customer  │ ───────────────▶ │  License Server  │ ───────────────▶ │   Plugin    │
 │ (activate) │                  │  (Ed25519 priv)  │                  │ (Ed25519    │
 └────────────┘                  └──────────────────┘                  │  pub, offl.)│
                                                                        └─────────────┘
```

1. Customer buys a license → gets a key `WSHO-XXXXX-XXXXX-XXXXX-XXXXX`.
2. Plugin computes a **machine id** (hashed hardware fingerprint) and POSTs
   `{licenseKey, machineId, productId}` to the server's `/api/activate`.
3. Server checks seats, binds the machine, and returns a **signed license token**.
4. Plugin **verifies the token offline** on every load using the embedded
   Ed25519 **public** key — no network needed after activation.
5. Periodic background re-validation (`/api/validate`) with an **offline grace
   period**, so a dropped connection never blocks a paying user.

The private signing key lives **only** on the server. DE1 has no server of
its own: it uses the **shared whysho license server**, whose source lives in
the [`whysho-eq1`](https://github.com/whyshocom/whysho-eq1) repo (`/server`).
The plugin ships with the same public key as EQ1
(`src/licensing/LicensePublicKey.h`) — rotate both together.

> **Server requirement:** the server must accept `productId = "whysho-de1"`
> and only activate keys that were sold for DE1 (license rows need a
> `product` column). See "Multi-product server" below.

## Token format

```
token = base64url(canonicalJSON) + "." + base64url(ed25519_signature)
```

- **canonicalJSON** — the license object with keys sorted ascending, compact,
  UTF-8. Signature is computed over these exact bytes.
- The plugin **does not re-serialize**: it base64url-decodes the first segment
  and verifies the signature over those raw bytes, then parses the JSON.

License payload fields:

| field        | meaning                                             |
|--------------|-----------------------------------------------------|
| `product`    | `whysho-de1`                                         |
| `licenseId`  | server license id (uuid)                             |
| `email`      | customer email                                       |
| `edition`    | `standard` (DE1 has one edition, all features)        |
| `machineId`  | hex SHA-256 of the client fingerprint (binds the seat)|
| `features`   | capability flags                                     |
| `issuedAt`   | unix seconds                                         |
| `expiresAt`  | unix seconds, `0` = perpetual                        |
| `activationId`| per-seat activation id                             |

Crypto: **Ed25519 (RFC 8032, SHA-512)**. Server signs with `@noble/ed25519`;
the plugin verifies with **Monocypher** `crypto_ed25519_check`. The two are
byte-compatible. Full server spec in `server/README.md`.

## The layered deterrent (what "non-crackable" really means)

No software that runs on a user's machine can be made truly uncrackable — the
CPU must eventually execute the audio path, so a determined attacker can patch
it. whysho DE1 therefore aims to make cracking **expensive and casual sharing
pointless**, via several independent layers:

1. **Signed, machine-bound licenses** — a shared key file is useless on another
   machine; forging one requires the server's private key.
2. **Offline verification** — no "crack the server call" shortcut; the check is
   a local Ed25519 verification.
3. **String obfuscation** — URLs, product ids and security strings are XOR-
   encrypted at rest (`Obfuscate.h`), so `strings` reveals nothing.
4. **Redundant audio-path gate** (`AntiTamper.h`) — the "licensed" state is held
   as two atomics that must agree; flipping a single boolean doesn't unlock it.
   When unlicensed, audio is periodically muted + noised so the plugin can be
   auditioned but not used in production.
5. **Anti-debug probes** — platform checks that never auto-block a legitimate
   DAW, used only as a reversing deterrent.

Honest limits: a skilled reverse engineer can defeat any of these given time.
These layers raise the cost; they are not a guarantee. Treat them as a lock on
a door, not a vault.

## Operating the server

See `server/README.md`. In short:

```bash
# in the whysho-eq1 repo
cd server
npm install
npm run keygen            # generates the Ed25519 keypair (prints the C++ snippet)
WHYSHO_LICENSE_PRIVATE_KEY=... WHYSHO_ADMIN_TOKEN=... npm start
```

**Before production**, rotate the dev key:
1. Run `npm run keygen` on the production host; keep the private key secret
   (env var / secret manager, never committed).
2. Paste the printed public-key snippet into `src/licensing/LicensePublicKey.h`.
3. Rebuild and ship the plugin; deploy the server with the matching private key.

Self-hosters can point the plugin at their own server with the
`WHYSHO_LICENSE_SERVER` environment variable.

## Multi-product server (needed before DE1 ships)

The server in `whysho-eq1/server` currently hard-codes `PRODUCT_ID =
'whysho-eq1'`, so a DE1 activation is rejected with `wrong-product`. Required
changes (in the EQ1 repo):

1. `licenses` table: add `product TEXT NOT NULL DEFAULT 'whysho-eq1'`.
2. `crypto.ts`: `PRODUCT_IDS = ['whysho-eq1', 'whysho-de1']`; the signed
   `product` field becomes the license's product.
3. `service.activate()`: reject unknown products, and reject a key whose
   `product` differs from the requested `productId` (an EQ1 key must not
   unlock DE1).
4. Admin "create license" endpoint: accept `product`; per-product editions in
   `editions.ts` (DE1 has no feature tiers — one edition, all features).
5. The whysho.com checkout must create the license with the right `product`.

DE1-side identifiers: product `whysho-de1`, machine-id salt
`whysho-de1-salt-v1`, data folder `Whysho/DE1/`.
