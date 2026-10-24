# Security, Rate Limiting, and Encryption


<div class="arch-hero">

BigWorld's security controls span network entrances, login, Proxy message
budgets, Mercury Channel encryption, EntityDef exposed methods, and management
tools. Each boundary must be analyzed separately rather than reducing security
to the player-facing UDP entrance.

</div>

## Conclusion

The source contains explicit anti-abuse designs:

- `NetworkInterface` provides per-IP and per-IP:port rate limits.
- `PacketReceiver` applies address limits before Mercury parsing.
- BaseApp limits client messages per tick and can buffer later replay.
- `InitialConnectionFilter` validates the first BaseApp packet shape and accepts
  only the expected login request.
- LoginApp has a dedicated chain: login switch, IP bans, protocol version,
  pending/cache retries, login rate, DB readiness, overload, challenge, message
  length, and parameter decoding.
- `LoginChallenge` supports `delay`, `fail`, and `cuckoo_cycle`; the latter is
  login abuse resistance, not account authentication.
- `Channel::setEncryption()` abstracts encryption; `UDPChannel` installs an
  `EncryptionFilter`.
- `EncryptionFilter` encrypts packet bodies with a `BlockCipher` and validates a
  magic value after decryption.
- Client-to-server entity calls are not arbitrary RPC: they resolve only to
  Base/Cell exposed-method ranges declared by `.def`, and `OWN_CLIENT` checks
  source entity ownership.
- Watchers, probes, and the Message Logger are management-plane security
  boundaries.

Key sources:

- Network rate configuration:
  network_interface.hpp.
- Pre-parse rate check:
  packet_receiver.cpp.
- BaseApp message limits:
  rate_limit_message_filter.cpp.
- Initial connection filtering:
  initial_connection_filter.cpp.
- Login configuration:
  loginapp_config.cpp.
- Login flow:
  loginapp.cpp.
- Login challenge:
  loginapp.cpp.
- Exposed methods:
  proxy.cpp
  and
  entity.cpp.
- Encryption:
  channel.hpp,
  udp_channel.cpp,
  and
  encryption_filter.cpp.

## Defense Layers

<MermaidDiagram title="External traffic defenses">
flowchart TD
  A[External UDP Packet] --> B[NetworkInterface rate limit]
  B --> C{rate limited?}
  C -- yes --> D[drop before Channel dispatch]
  C -- no --> E{external entry}
  E -- LoginApp --> L0[allowLogin / IP ban / protocol]
  L0 --> L1[loginRateLimit / DB ready / overload]
  L1 --> L2[LoginChallenge / params / length]
  L2 --> L3[DBApp logOn]
  E -- BaseApp --> F[find UDPChannel]
  F --> G{PacketFilter?}
  G -- encryption --> H[decrypt and validate magic]
  G -- initial connection --> I[accept only baseAppLogin shape]
  H --> J[Mercury dispatch]
  I --> J
  J --> K[RateLimitMessageFilter]
  K --> M{per-tick budget}
  M -- enough --> N[dispatch now]
  M -- exceeded --> O[buffer or drop]
</MermaidDiagram>

This structure drops noisy sources early, constrains LoginApp before account
verification, protects Channel traffic after key establishment, prevents one
client from consuming a BaseApp tick, and applies stricter filtering before a
Channel has context.

## Address Rate Limiting

`NetworkInterface` tracks two dimensions: per IP and per IP:port. A periodic
reset clears counters. `PacketReceiver::processPacket()` checks both before
packet parsing, updates counters after acceptance, logs rejection, and drops
rather than forwarding the packet to Channel dispatch.

This protects the entrance independently of business handlers and covers both
NAT-shared addresses and a single endpoint flooding traffic.

Boundaries:

- State is process-local, not distributed.
- The policy is a coarse counter, not a token bucket.
- L4 balancing or multiple UDP sockets would require redesign.

## BaseApp Message Limits

Network limiting controls packet volume; BaseApp
`RateLimitMessageFilter` controls per-client business-message volume.
`RateLimitConfig` exposes warning and maximum values for messages and bytes per
second, per tick, and buffered.

The filter:

1. verifies the source address still belongs to the current Proxy;
2. updates message and byte counters;
3. dispatches immediately when the queue is empty and the budget allows;
4. otherwise copies the message into a `BufferedMessage` queue;
5. on buffer overflow invokes `Proxy::onFilterLimitsExceeded()` and deletes the
   message.

Its tick checks thresholds, replays executable buffered messages in order, and
resets per-tick statistics. This preserves bursts while preventing one client
from executing too many external calls in one tick.

## Initial Connection Filtering

Before a UDP Channel has stable context, `InitialConnectionFilter::recv()`
accepts only packets with `FLAG_HAS_REQUESTS`, the exact `baseAppLogin` request
length, and the `BaseAppExtInterface::baseAppLogin` message id. All other shapes
are dropped. This whitelist reduces malformed-packet parsing before the normal
Mercury path and is especially important for connectionless UDP traffic.

## LoginApp Security Chain

`LoginAppConfig` controls login availability, probes, unencrypted fallback,
failed-reply rate, global login rate, address rate limits, username/password and
message sizes, passwordless mode, and challenge type.

`LoginApp::login()` proceeds through these defenses:

1. global login switch;
2. IP ban and expiration cleanup;
3. rejection of spoofed empty source addresses;
4. client/server protocol compatibility;
5. pending retry handling;
6. global login-rate budget;
7. DB readiness and system overload;
8. challenge processing when configured;
9. remaining login-message size;
10. `LogOnParams` decoding and username/password length;
11. resolved cached-attempt handling;
12. login-rate consumption only after successful decode;
13. encryption-key requirement unless unencrypted login is explicitly allowed;
14. passwordless-mode enforcement;
15. forwarding to `DBAppInterface::logOn`.

`loginRateLimit` is therefore not packet-level rate limiting; it is the
application-level login budget applied after protocol and pending checks.

### Challenges

`LoginChallenge` supports:

- `delay`: deterministic delay for testing;
- `fail`: deterministic failure for testing;
- `cuckoo_cycle`: proof-of-work abuse resistance.

`cuckoo_cycle` verifies a solution prefix, proof length, edge count, proof
validity, and difficulty. It increases login cost but does not authenticate an
account, replace encryption, or prevent replay by itself.

## EntityDef Exposed Methods

Client entity calls are constrained by generated interface ranges and `.def`
method declarations. Base and Cell dispatch through exposed indices rather than
arbitrary method lookup.

`OWN_CLIENT` additionally compares the source Proxy/entity relationship.
A message that reaches dispatch with the wrong source ownership must be blocked.

Security verification must cover both nonexistent message ids and valid exposed
ids invoked with invalid ownership.

## Channel Encryption

`Channel::setEncryption()` installs an encryption filter abstraction. For a
UDP Channel, this attaches `EncryptionFilter`, which uses a block cipher and a
per-channel key.

On receive it checks the key, decrypts in place, verifies a magic marker,
validates padding, and adjusts packet length. On send it pads and encrypts the
packet body.

## PacketFilter Boundary

Packet encryption protects Mercury Channel bodies; it is not a complete
transport-security protocol. Internal deployment still assumes trusted network
topology, the login challenge is not authentication for every internal RPC, and
management tools have their own boundaries.

Security analysis must also answer:

- where keys originate;
- how handshake and Channel binding occur;
- whether authentication is bound to identity;
- whether replay is prevented;
- whether internal RPC has authentication and authorization;
- whether management endpoints are reachable from untrusted networks.

## Management Plane Is an Attack Surface

Watcher supports remote GET/SET/TELL and forwarding paths across process
collections. LoginApp exposes `challengeType` as a watcher. Probe replies
include host and process-owner information, and production configurations reject
`allowProbe`. Message Logger receives process logs, reconnection state, filters,
and backend writes; exposure can leak topology, account names, stacks, and
runtime state.

Relevant sources:

- Watcher SET protocol:
  watcher_nub.hpp.
- ForwardingWatcher command paths:
  watcher_forwarding.cpp.
- LoginApp watcher registration:
  loginapp.cpp.
- Production probe check:
  loginapp.cpp.

These endpoints need an isolated management network and separate read-only
observation from writable control.

## Source Trade-offs

Security is distributed by component rather than represented as one policy
plane. Network and filters handle packet shape, encryption, and entrance rate.
LoginApp handles protocol, parameter size, challenge, retries, and DB handoff.
BaseApp enforces per-tick Proxy budgets. EntityDef defines exposed call ranges
and ownership semantics. Management services form a separate attack surface.

This fits the process model, but security review must cross all of those
boundaries.

## Verification Priorities

- `allowLogin=false` rejects before DBApp `logOn`.
- Incompatible protocol returns `LOGIN_BAD_PROTOCOL_VERSION`.
- Overlarge messages, usernames, and passwords return malformed request.
- Missing keys are rejected when unencrypted login is disabled.
- Invalid challenge prefix, proof length, and verification are rejected.
- Duplicate pending logins reuse state rather than creating another Proxy.
- Invalid initial packet shapes are dropped.
- Non-exposed ids cannot invoke Base/Cell methods.
- `OWN_CLIENT` calls from another source are blocked.
- Watcher SET, forwarding commands, probes, and Logger connections are reachable
  only from controlled management networks.

## Scope

This page covers security, rate limiting, login entry, and encryption. Watcher,
Profiler, and logging mechanisms are expanded in the next chapter, while
remaining management-plane attack surface.
