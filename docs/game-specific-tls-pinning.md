# Game-specific TLS pinning: what's missing, and what a 2026-09-20 decompile found

## The gap

`disable_ca_verification` and `disable_browser_ca_verification` (see the
main README's "Certificates & certificate trust" section) patch the
**system SSL service** and the **browser applet**. Neither touches a
game's own executable. Splatoon 3 — and, per public reporting, Mario Wonder
and Mario Party Jamboree — do TLS verification partly *inside their own
binary*, so the system-level patches alone are not enough to reach a
private server from these specific games. `switchnet-nro` currently
provisions no patch for this at all.

This document is independent research: a Ghidra decompile of the
operator's own, legitimately-dumped Splatoon 3 v11.3.0, cross-referenced
against curl's own public, MIT-licensed source. **None of it is derived
from any other private server project's compiled patches** — see the
"Why not just copy an existing patch" section below for why that boundary
matters here specifically.

## What Splatoon 3 actually does (confirmed by decompile + public docs)

Splatoon 3 statically links Nintendo's SDK build of real, unmodified
**libcurl 7.64.1** — confirmed by the literal embedded string
`libcurl/7.64.1 (HAC; nnEns; SDK 20.5.22.0)`. This isn't a Nintendo-modified
curl; it's the standard open-source library, built against the NintendoSDK
and wired to a custom TLS backend via `CURLOPT_SSL_CTX_FUNCTION`, which is
itself a well-documented, public curl feature (see
[reversing.live's SSL bypass writeup](https://reversing.live/sslbypass.html)
and [switchbrew's SSL services page](https://switchbrew.org/wiki/SSL_services)
for the general Switch-side mechanism this plugs into: NSS-backed
certificate validation inside the OS's own `ssl` sysmodule, with a
`VerifyOption` bitmask — `PeerCa`, `HostName`, `DateCheck`,
`EvCertPartial` — that the calling application sets per connection).

There are **three separate, independently-enforced checks**, not one:

1. **Peer name / SNI** — `nn::ssl::Connection::SetHostName`
   (`nnsslConnectionSetHostName` in the binary's import table).
2. **CA chain / date verification** — `nn::ssl::Connection::SetVerifyOption`
   (`nnsslConnectionSetVerifyOption`). The value passed is **not a
   constant**: in the connection-setup routine (`FUN_71006e2a48` in the
   v11.3.0 decompile), it's computed at runtime —
   `bVar4 = bVar3 & 3; if (config_flag) bVar4 |= 4;` — from fields at
   offsets `0xad0` and `0xa90` of a per-connection config structure. This
   is a shared routine used for more than one host, so the strictness
   level varies by what's calling it, not by a single hardcoded value.
3. **A separate, additional public-key pinning layer** — real curl
   `CURLOPT_PINNEDPUBLICKEY`, checked entirely independently of the two
   `nn::ssl` checks above, *after* the OS-level handshake already
   succeeds. This is the one most likely to be silently missed by a patch
   that only targets `nn::ssl`.

## The pinning check: exact location, this exact build

Curl's own pinning function, `Curl_pin_peer_pubkey` — publicly documented,
unmodified logic (verified against
[curl 7.64.1's actual source](https://github.com/curl/curl/blob/curl-7_64_1/lib/vtls/vtls.c)) —
is at **`0x71006e1f48`** in Splatoon 3 v11.3.0's `main` (post-ASLR-base
`0x7100000000`; adjust for whatever base your tool of choice uses).
Confirmed via the literal `"sha256//"` / `";sha256//"` / debug-log
`"\t public key hash: sha256//%s\n"` strings it references — all present
verbatim, matching upstream curl's own source exactly.

**Return value encoding, confirmed by decompile (matches curl's real
`CURLcode` enum exactly):**

- `0` — success (`CURLE_OK`). Also the immediate return if no pin is
  configured for that connection (`param_2 == NULL`) — meaning most
  connections likely already skip this check entirely, and only whichever
  specific host(s) Splatoon 3 pins actually enforce it.
- `0x5a` (90, `CURLE_SSL_PINNEDPUBKEYNOTMATCH`) — the computed SHA-256 of
  the peer's actual public key didn't match any pin in the (possibly
  semicolon-separated multi-pin) configured string.
- `0x1b` (27, `CURLE_OUT_OF_MEMORY`) — allocation failure in the hash
  buffer.

**The clean patch target**: this is one function, called from one site
(`0x71006e2fb4`, inside the certificate-verification path of the SSL
handshake completion routine), regardless of how many different hosts
pin different keys. Forcing it to return `0` unconditionally — the ARM64
equivalent of `mov w0, #0; ret` at the function's entry point — defeats
pinning for every connection this build makes, without needing to locate
or decode any per-host pin value individually.

**What is NOT yet found**: where the actual `sha256//<base64>` pin
*values* live (not found as plain strings in `.rodata` — likely loaded
from a bundled resource/config blob rather than hardcoded literals), and
the exact byte sequence needed to force `SetVerifyOption`'s dynamically-computed
argument to `0` at its one call site (same function, `0xad0`/`0xa90` config
read). Both are addressable with more work; neither was necessary once the
`Curl_pin_peer_pubkey` short-circuit was found, since defeating pinning
does not require also defeating the OS-level `VerifyOption` check if that
one already passes with a self-signed cert in the mix — untested, flagged
here rather than assumed.

## The generated patch

`romfs/sd/atmosphere/exefs_patches/s3certpin_bypass/` (named to keep it
clearly distinct from Nextendo's own `s3certbypass`) ships two copies of
the same 8-byte patch, for both Atmosphère filename conventions:

- `28C4287AEE36F7499DA60F3E68B54C70DA382D75.ips` (40-char build ID)
- `28C4287AEE36F7499DA60F3E68B54C70DA382D75000000000000000000000000.ips`
  (64-char, zero-padded)

The build ID is read directly out of `main`'s own NSO0 header (offset
`0x40`, 32 bytes) — not copied from anywhere — and happens to match one of
Nextendo's own filenames for the same build, which is a useful independent
cross-check that both projects are looking at the same v11.3.0 release.

**The patch**: at RVA `0x6E1F48` from `.text`'s start (Ghidra virtual
address `0x71006e1f48`, `.text` based at `0x7100000000`), replace the
function's first 8 bytes —

```
original: ff c3 01 d1  fd 7b 01 a9      sub sp, sp, #0x70
                                        stp x29, x30, [sp, #0x10]
patched:  00 00 80 52  c0 03 5f d6      mov w0, #0
                                        ret
```

— which short-circuits `Curl_pin_peer_pubkey` to unconditionally return
`CURLE_OK` before it touches the stack or any callee-saved register,
so nothing needs unwinding on the forced early return. Both mnemonics are
generic ARM64 (`MOVZ`/`RET`), not derived from any patch, this project's or
anyone else's.

Both the original and patched byte sequences above are independently
verified against Ghidra's own disassembly using `capstone`/`keystone`
(a completely separate ARM64 assembler/disassembler implementation) —
`keystone.asm("mov w0, #0; ret")` produces the exact patch bytes byte for
byte, and `capstone` disassembles the original bytes to the identical
instructions Ghidra showed. Two independent tools agreeing removes most of
the realistic risk of a transcription error in the patch itself; it does
not remove the risk that this is the wrong function to patch, or that a
second layer (`SetVerifyOption`, below) also needs patching — only real
hardware answers that.

`certs_provision()` in `source/certs.c` already generically mirrors
everything under `romfs/sd/` onto the SD card on every "Apply SwitchNet"
run — no code change was needed for this to auto-install; adding the files
to the bundled tree was sufficient.

## The second patch: `SetVerifyOption`

`romfs/sd/atmosphere/exefs_patches/s3verifyoption_bypass/` addresses the
OS-level `nn::ssl` check from earlier in this document. At RVA `0x6E2C54`
(inside `FUN_71006e2a48`, the connection-setup routine), the instruction
that selects between the plain and `|4` verify-option values —

```
original: 41 01 8b 1a      csel w1, w10, w11, eq
patched:  01 00 80 52      mov w1, #0
```

— is replaced with a constant zero, same instruction size, no branch or
stack-layout impact. `w1` is the second argument to the following
`nnsslConnectionSetVerifyOption` call regardless of which value the
original `csel` would have chosen, so this makes every call always pass
`VerifyOption = 0` (no CA-chain, hostname, date or partial-EV checks) no
matter which of the two runtime-computed values this shared routine was
about to use.

Both patches (pin bypass and verify-option bypass) are independently
verified the same way — `capstone` disassembly of the original bytes
matches Ghidra exactly, and `keystone` assembly of the intended
replacement instruction matches the patch bytes exactly.

## Correction (v0.4.5): the v0.4.0–v0.4.4 files never patched these addresses

The RVAs above are right; the `.ips` files were not. Atmosphère's loader
subtracts the 0x100-byte NSO header from every IPS record offset before
writing (`libstratosphere/source/patcher/patcher_api.cpp`,
`patch_offset -= offset` with `offset = sizeof(NsoHeader)`), so a record
must say **RVA + 0x100**. The hand-made files said the bare RVA, which
Atmosphère applied 0x100 bytes early:

| Patch | Meant for | What the old file actually overwrote |
| --- | --- | --- |
| `s3verifyoption_bypass` | `0x6E2C54` `csel w1, w10, w11, eq` | `0x6E2B54` `ldr x0, [x23, #0xa98]` |
| `s3certpin_bypass` | `0x6E1F48`, `Curl_pin_peer_pubkey`'s first two instructions | `0x6E1E48`, the middle of a different function's prologue |

The symptom on hardware (2026-09-23, Splatoon 3 v11.3.0, error 2321-4992
entering the lobby): a tcpdump showed the TLS 1.2 handshake to
`t-dce9377b-lp1.lp1.t.npln.srv.nintendo.net` stop right after the server's
certificate — the console closes without a ClientKeyExchange — because
`VerifyOption` was never zeroed and the game's own context (it calls
`nn::ssl::Context::ImportServerPki` with its own CA set) rejected the
SwitchNet CA. This routine is the game's only `nnsslConnectionSetVerifyOption`
call site (`0x6E2C58`), so the NPLN gRPC channel goes through it too.

`tools/make_exefs_ips.py` now builds both files from the RVA and adds the
header itself; `28C4287A…` records are `0x6E2D54` and `0x6E2048`.

## Status: both patches built, neither tested on real hardware

Whether one, both, or neither is sufficient to reach a SwitchNet server
from Splatoon 3 is genuinely unknown until someone actually launches the
game with these installed. They're independently toggleable (separate
`exefs_patches` directories), so if only one turns out to be necessary the
other can be removed without touching the one that worked.

## Why not just copy an existing patch

Nextendo's `Prelude-Nro` ships exactly this class of patch already
(`s3certbypass`, `s3peername`, plus `wondercertbypass`/`wonderpeername`/
`jamboreecertbypass`) — confirmed via public search to have originated in
their own `v3.3.6` release, appearing nowhere else on the public web. That
makes them Nextendo's own original work product, licensed under
**PolyForm Shield 1.0.0**, whose Noncompete clause explicitly forbids
using the software "to compete with the software or any product the
licensor... provides using the software." `switchnet-nro` is exactly that
kind of competing product (a private-server installer for the same
games), so copying those compiled `.ips` files here would violate that
license — the same boundary this project already draws around `nx-dauth`
elsewhere (facts taken, never code). Everything in this document is
independent: an original decompile of the operator's own dump, checked
against curl's actual public source, with no code or binaries taken from
any other project's implementation.

## Ban risk: not applicable to SwitchNet's actual threat model

A public TLS-interception guide notes real, documented account bans during
Splatoon 3's Splatfest World Premiere from players who patched the
executable to reach a private server on their *real* account/emuMMC-less
setup. That doesn't apply here: `switchnet-nro` only ever operates on a
fully isolated emuMMC that never talks to real Nintendo for anything (see
the main switchnet repo's `CLAUDE.md`) — there is no real account in the
blast radius to begin with, and the operator's baseline assumption is that
anyone using this is already running an emuMMC specifically because of
that isolation. The correctness bar that actually matters is the generated
hosts file being right, not whether Nintendo could theoretically detect a
patched executable on hardware that never reaches Nintendo's servers.
