# Login, Session and Proxy Attach

> **English** | [中文](login-session-proxy-flow.md)

<div class="arch-hero">

BigWorld's login chain is not "verify the account and drop into the game". It is
a session takeover flow completed jointly by LoginApp, DBApp, BaseAppMgr,
BaseApp, Proxy and a client-side second handshake. The flow embodies security,
rate limiting, RPC, entity creation, NAT, SessionKey and Channel binding at the
same time.

</div>

## Conclusion First

The login chain splits into two stages:

- Stage one: the client logs in to `LoginApp`, which validates the protocol, rate
  limits, decrypts the login parameters and queries DBApp.
- Stage two: DBApp/BaseAppMgr/BaseApp create the Proxy, LoginApp returns the
  BaseApp address and session key to the client, and the client then sends
  `baseAppLogin` to BaseApp to complete the attach.

Key sources:

- `LoginApp::login()` is the login request entry, see
  loginapp.cpp.
- Initialisation of the RSA decoder for login parameters is at
  loginapp.cpp.
- LoginApp sends `DBAppInterface::logOn` to DBApp, see
  loginapp.cpp.
- The DBApp login reply handling is at
  database_reply_handler.cpp.
- Caching the successful reply and returning it encrypted is at
  loginapp.cpp.
- BaseAppMgr selects a BaseApp and requests creation of the Base/Proxy, see
  baseappmgr.cpp.
- After BaseApp creates the Proxy it generates the pending login key, see
  entity_creator.cpp.
- PendingLogins maintains a 30 second timeout, see
  pending_logins.cpp.
- The client-side second login entry is `BaseApp::baseAppLogin()`, see
  baseapp.cpp.
- `LoginHandler::login()` looks up the pending Proxy with the session key and
  calls `attachToClient()`, see
  login_handler.cpp.

## The End-to-End Flow

<MermaidDiagram title="Login to Proxy attach">
sequenceDiagram
  participant Client
  participant LoginApp
  participant DBApp
  participant BaseAppMgr
  participant BaseApp
  participant Proxy

  Client->>LoginApp: login(protocol + credentials)
  LoginApp->>LoginApp: allowLogin/IP ban/rate limit/protocol/challenge
  LoginApp->>LoginApp: read/decrypt LogOnParams
  LoginApp->>DBApp: DBAppInterface::logOn(source, params)
  DBApp->>BaseAppMgr: create entity / choose BaseApp
  BaseAppMgr->>BaseApp: createBaseWithCellData
  BaseApp->>Proxy: create Proxy
  Proxy->>BaseApp: prepareForLogin(clientAddr)
  BaseApp->>BaseApp: PendingLogins.add(proxy, loginAppAddr)
  BaseApp-->>DBApp: mailbox + loginKey
  DBApp-->>LoginApp: LoginReplyRecord
  LoginApp-->>Client: LOGGED_ON + BaseApp addr + session key
  Client->>BaseApp: baseAppLogin(session key)
  BaseApp->>BaseApp: find PendingLogin
  BaseApp->>Proxy: attachToClient(srcAddr)
</MermaidDiagram>

This flow explains a key point: LoginApp does not carry the player session long
term, it only completes the login entry and returns the result. The real client
game Channel attaches to BaseApp.

## The LoginApp Entry Defence Line

**Overview:** `LoginApp::login()` already performs multiple checks before
reading the business parameters. This is not a simple authentication function but
the defence line of the public entry point. Every check has a clear security or
rate-limiting purpose.

**Source entry:** loginapp.cpp:700

```cpp
// loginapp.cpp:700 - LoginApp login checks
bool LoginApp::login( const Mercury::Address & srcAddr,
    const Mercury::UnpackedMessageHeader & header,
    BinaryIStream & data )
{
    // 1. Check whether login is allowed
    if (!this->allowLogin())
    {
        ERROR_MSG( "LoginApp::login: Login not allowed\n" );
        return false;
    }
    
    // 2. Check whether the IP is banned
    if (bannedIPs_.contains( srcAddr.ip ))
    {
        ERROR_MSG( "LoginApp::login: IP %s is banned\n", 
            srcAddr.ipAsString() );
        return false;
    }
    
    // 3. Check for an empty source address (guard against a spoofed empty address)
    if (srcAddr.isNone())
    {
        ERROR_MSG( "LoginApp::login: Empty source address\n" );
        return false;
    }
    
    // 4. Check the protocol version
    if (!this->isClientAllowed( header ))
    {
        ERROR_MSG( "LoginApp::login: Client version not allowed\n" );
        return false;
    }
    
    // 5. Check whether this is a duplicate pending attempt
    if (pendingAttempts_.contains( srcAddr ))
    {
        ERROR_MSG( "LoginApp::login: Duplicate attempt from %s\n",
            srcAddr.ipAsString() );
        return false;
    }
    
    // 6. Check the rate limit
    if (rateLimit_.isLimited())
    {
        ERROR_MSG( "LoginApp::login: Rate limited\n" );
        return false;
    }
    
    // 7. Check whether DBApp is ready
    if (!dbApp_.isReady())
    {
        ERROR_MSG( "LoginApp::login: DBApp not ready\n" );
        return false;
    }
    
    // 8. Check whether the system is overloaded
    if (overloadCache_.isOverloaded())
    {
        ERROR_MSG( "LoginApp::login: System overloaded\n" );
        return false;
    }
    
    // 9. Check whether a login challenge is needed
    if (challengeRequired_ && !this->hasChallenge( srcAddr ))
    {
        // Send the challenge request
        this->sendChallenge( srcAddr );
        return true;
    }
    
    // 10. Read and decrypt the login parameters
    LogOnParams params;
    if (!params.read( data, maxLoginMessageSize() ))
    {
        ERROR_MSG( "LoginApp::login: Failed to read params\n" );
        return false;
    }
    
    // 11. Check the username/password length
    if (params.username().length() > maxUsernameLength() ||
        params.password().length() > maxPasswordLength())
    {
        ERROR_MSG( "LoginApp::login: Username/password too long\n" );
        return false;
    }
    
    // 12. Check the encryption requirement
    if (!allowUnencryptedLogins_ && !params.isEncrypted())
    {
        ERROR_MSG( "LoginApp::login: Unencrypted login not allowed\n" );
        return false;
    }
    
    // 13. Start the login request towards DBApp
    this->sendDBAppLogOn( srcAddr, params );
    
    return true;
}
```

**Flowchart:**

<MermaidDiagram title="LoginApp login check flow">
flowchart TD
    A[login request received] --> B{allowLogin?}
    B -- no --> Z[reject]
    B -- yes --> C{IP banned?}
    C -- yes --> Z
    C -- no --> D{empty source address?}
    D -- yes --> Z
    D -- no --> E{protocol version allowed?}
    E -- no --> Z
    E -- yes --> F{duplicate attempt?}
    F -- yes --> Z
    F -- no --> G{rate limit?}
    G -- yes --> Z
    G -- no --> H{DBApp ready?}
    H -- no --> Z
    H -- yes --> I{system overloaded?}
    I -- yes --> Z
    I -- no --> J{challenge needed?}
    J -- yes --> K[send challenge]
    J -- no --> L[read login params]
    L --> M{params valid?}
    M -- no --> Z
    M -- yes --> N{username/password length?}
    N -- too long --> Z
    N -- ok --> O{encryption required?}
    O -- not met --> Z
    O -- met --> P[send DBApp login request]
</MermaidDiagram>

**Detailed walkthrough:**

1. **allowLogin()**: a global switch that can forbid all logins (for example in
   maintenance mode)

2. **IP ban**: `bannedIPs_` stores banned IP addresses and supports dynamic
   addition

3. **Source address check**: prevents a spoofed empty address attack

4. **Protocol version check**: makes sure the client version is compatible with
   the server

5. **Duplicate attempt detection**: prevents the same client from repeatedly
   sending login requests

6. **Rate limit**: limits the login request rate to stop brute force

7. **DBApp ready check**: makes sure the database service is available

8. **Overload protection**: refuses new logins when the system is overloaded,
   protecting stability

9. **Challenge mechanism**: optional second verification, increasing security

10. **Parameter reading**: reads the login parameters from the binary stream,
    supporting encryption

11. **Length check**: prevents over-long username/password attacks

12. **Encryption requirement**: optionally requires encrypted logins, strengthening
    security

## Decryption of the Login Parameters

`LoginApp::initLogOnParamsEncoder()` reads the `loginApp/privateKey`
configuration, loads the private key and creates an `RSAStreamEncoder`. If
unencrypted logins are allowed, a failed private key load does not have to kill
the server. Source:
loginapp.cpp.

`LoginApp::login()` while parsing `LogOnParams`:

- Tries reading with the encoder first.
- If that fails and unencrypted logins are allowed, retries in plaintext.
- If it still fails, treats it as a malformed request.

Source: loginapp.cpp.

Design implications:

- The login credentials and the session encryption key are separate.
- LoginApp can accommodate both encrypted and unencrypted logins, but modern
  production deployments should disable unencrypted logins.
- The error handling avoids exposing too much detail to the client.

## The DBApp Reply and Overload Propagation

`DatabaseReplyHandler::handleMessage()` reads the status first.

If it is not `LOGGED_ON`:

- An IP ban is converted into a LoginApp-local ban.
- The error message is returned to the client.
- A BaseApp/CellApp/DBApp overload sets `LoginApp::systemOverloaded()`, so
  subsequent logins are rejected outright for a short while.

Source: database_reply_handler.cpp.

On success:

- Reads the `LoginReplyRecord`.
- Performs NAT/firewall address translation for external clients.
- Calls `sendAndCacheSuccess()`.

Source: database_reply_handler.cpp.

This shows the login chain feeds cluster load state back to the entry point,
instead of letting LoginApp blindly accept every request.

## BaseAppMgr Selecting a BaseApp

When creating a Base/Proxy, BaseAppMgr picks a lightly loaded BaseApp. If all
BaseApps are overloaded it replies with an error:

- `CREATE_ENTITY_ERROR_BASEAPPS_OVERLOADED`
- `"All BaseApps overloaded."`

Source: baseappmgr.cpp.

On success:

- Writes the selected BaseApp's external address into `baseAppAddr`.
- Sends `BaseAppIntInterface::createBaseWithCellData` to the BaseApp.
- Transfers the original data stream to the BaseApp.
- Updates the BaseApp load estimate with `addEntity()`.

Source: baseappmgr.cpp.

Design implications:

- Login is not a single-point creation: the control plane selects the process
  that will host the Base/Proxy according to load.
- What is returned to the client is the BaseApp external address, not the
  LoginApp address.

## Pending Login and SessionKey

After BaseApp creates the Proxy, if it is a Proxy with a client address it will:

- Call `Proxy::prepareForLogin(clientAddr)`.
- Return a `loginKey`.
- Store an encryption key on the Proxy if there is one.

Source: entity_creator.cpp.

`BaseApp::addPendingLogin()` forwards to `LoginHandler::add()`, see
baseapp.cpp.

The key behaviours of `PendingLogins::add()`:

- Uses the current `Proxy::sessionKey()` as the loginKey.
- Immediately calls `regenerateSessionKey()` so it is never reused.
- Ensures the same Proxy appears only once in the pending list.
- Inserts `PendingLogin(proxy, loginAppAddr)`.
- Sets a 30 second timeout.

Source: pending_logins.cpp.

This shows the session key is the bridge of the second handshake from LoginApp to
BaseApp: the client must present it to BaseApp to complete the takeover.

## Why the Successful Reply Is Cached

`LoginApp::sendAndCacheSuccess()` stores the successful result in
`loginRequests_[addr]` and then calls `sendSuccess()`. Source:
loginapp.cpp.

The comment says it caches the success information so it can resend it when
necessary.

`sendSuccess()` writes:

- `LOGGED_ON`
- If there is an encryption key, encrypts the `LoginReplyRecord` with
  `EncryptionFilter`, because it contains the session key.
- Finally returns it to the client via `sendRawReply()`.

Source: loginapp.cpp.

This relates to the UDP login scenario: a successful reply may be lost and the
client may retry. The cache avoids creating a duplicate Proxy.

## The BaseApp Second Handshake

After the client obtains the BaseApp address and login key, it sends
`BaseAppExtInterface::baseAppLogin` to BaseApp.

`BaseApp::baseAppLogin()` only forwards:

```cpp
pLoginHandler_->login( extInterface_, srcAddr, header, args );
```

Source: baseapp.cpp.

`LoginHandler::login()` performs:

1. Look up the pending login with `args.key`.
2. If not found, `breakBundleLoop()`.
3. Take out the pending Proxy.
4. If a Channel from that address already exists, record the collision and
   reject.
5. Count NAT address/port changes.
6. Remove it from the pending list.
7. Call `Proxy::attachToClient(srcAddr, replyID, header.pChannel.get())`.

Source: login_handler.cpp.

This step is what actually binds the client's network Channel to the Proxy.

## The Full Login Call Chains

### The Client's First Login

The full call chain of the client logging in to LoginApp:

Source entry: loginapp.cpp:693

<div class="flow-strip">
  <span class="flow-node">LoginApp::login()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">decrypt the login request</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">validate the parameters</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DBAppInterface::logOn</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">wait for the DBApp reply</span>
</div>

### DBApp Validation and BaseApp Allocation

The full call chain of DBApp validating the account and allocating a BaseApp:

Source entry: baseappmgr.cpp:947

<div class="flow-strip">
  <span class="flow-node">DBApp validates the account</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BaseAppMgr::allocateBaseApp()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">pick the least loaded BaseApp</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BaseApp creates the Proxy</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">generate the session key</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">join PendingLogins</span>
</div>

### The Client's Second Login

The full call chain of the client completing the second handshake with BaseApp:

Source entry: baseapp.cpp:2985

<div class="flow-strip">
  <span class="flow-node">BaseApp::baseAppLogin()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">LoginHandler::login()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">look up the pending login</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">validate the session key</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Proxy::attachToClient()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">send the successful reply</span>
</div>

### Login Failure Handling

The handling chain when login fails:

Source entry: loginapp.cpp:636

<div class="flow-strip">
  <span class="flow-node">login fails</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">record the failure reason</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">LoginApp::handleFailure()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">sendFailure()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">do not cache the failure reply</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">avoid a DoS vector</span>
</div>

## NAT and Firewall Semantics

The login chain handles NAT explicitly:

- After DBApp returns success, if the client is on an external address, LoginApp
  rewrites `LoginReplyRecord.serverAddr.ip` to the firewall external IP, see
  database_reply_handler.cpp.
- `LoginHandler::updateStatistics()` counts address NAT and port NAT, see
  login_handler.cpp.
- When a pending login expires it points out BaseApp external port and
  routing/firewall problems, see
  pending_logins.cpp.

This shows BigWorld's login flow already accounts for real datacentre networks,
rather than being designed only against a localhost model.

## Why the Failure Reply Is Unreliable

The comment of `LoginApp::handleFailure()` states plainly that a failed login
reply does not use reliable messages, because that would become a DoS
vulnerability. Source:
loginapp.cpp.

Design implications:

- A successful login needs reliability and caching, because it corresponds to
  server-side state that already exists.
- A failed login must not let an attacker induce the server to maintain large
  amounts of reliable retransmission state.
- The reliability policy of the entry protocol has to be combined with the
  security policy.

This is a notably instructive part of the MMO UDP protocol.

## Trade-offs in the Source

The reasons BigWorld splits the login chain show up in the responsibility
boundaries:

- As the public entry point, LoginApp concentrates on authentication, rate
  limiting, challenges and DB queries.
- BaseApp hosts the long-lived Proxy session, which keeps LoginApp from becoming
  a long-connection bottleneck.
- BaseAppMgr is responsible for choosing a BaseApp by load.
- Pending login makes the intermediate state "Proxy created but the client has
  not yet reached BaseApp" explicit.
- SessionKey prevents any client from attaching straight to a Proxy.
- Caching the successful reply handles UDP loss and client retries.
- The failure reply stays unreliable so an attacker cannot amplify state through
  reliable retransmission.

The source costs:

- The login chain spans several processes, so troubleshooting needs cross-process
  tracing.
- A Proxy that is created successfully but never connected by a client produces a
  pending timeout.
- A NAT/firewall misconfiguration shows up as "login succeeded but cannot enter".
- SessionKey, the encryption key, the BaseApp address and the DB reply structure
  must all stay strictly compatible.

## What Source-Verified Tests Should Cover

Verification of the login chain should cover cross-process state and failure
paths:

- With `allowLogin=false`, a mismatched protocol version, DBApp not ready or the
  system overloaded, the rejection should happen at the LoginApp stage.
- IP bans, the login rate limit and pending/cache retries should take effect
  before reaching DBApp.
- A failed LoginChallenge creation, a wrong proof or a LogOnParams decode failure
  must not proceed to Proxy creation.
- After DBApp/BaseAppMgr/BaseApp create the Proxy, the successful LoginApp reply
  should contain the correct BaseApp address and session key.
- The client's `baseAppLogin` must bind to the already created Proxy through the
  SessionKey.
- The successful reply cache should cover UDP loss retries; the failure reply
  should not create reliable retransmission state.
- A pending login timeout should clean up the intermediate state and record
  NAT/firewall diagnostics.
- The login wire format should have golden tests, so that SessionKey, the
  encryption key, the BaseApp address and the DB reply structure cannot drift in
  order.

## Scope of This Chapter

This chapter explains the end-to-end flow from login to Proxy attach. The
network backpressure chapter continues the analysis: after login completes, how
the long-lived UDP Channel operates across receive budgets, send queues,
artificial loss and the reliability layer.