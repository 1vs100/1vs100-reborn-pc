# Gameserver mode

Developer notes for `online_service = "gameserver"`. In this mode the recompiled game runs its own online code against the [gameserver](https://github.com/1vs100/gameserver), the same way the Xbox 360 does with [1vs100-reborn-x360](https://github.com/1vs100/1vs100-reborn-x360). The game files must be the stock retail v2.0.1149.0 ones, including the stock Lua scripts.

## Settings

```
[Online]
online_service = "gameserver"
gameserver_host = "192.168.1.10"
```

The gameserver must run with `GAMESERVER_PROTOCOL=1149`.

To run several instances on one host, give each its own `guest_bind_address` (for example `127.0.0.1` and `127.0.0.2`). The title binds its game socket to port 1000, and the setting moves that bind off the shared any-address.

## Reborn patches and their PC counterparts

Rows follow reborn's 1149 production manifest (`verification/expected/production/manifest-1149-launcher-package.txt`). Patches that manifest skips on 1149 are left out.

| Reborn patch | Site | PC counterpart | State |
|---|---|---|---|
| NetDll_socket (insecure mark) | import | Not needed: host sockets have no XNet security. Guest sockets run on `guest_sockets.cpp`. | in progress |
| NetDll_XNetStartup (bypass security) | import | Not needed, same reason. | n/a |
| NetDll_XNetServerToInAddr | import | Not needed: the title wrapper at 8246AB10 is overridden instead. | n/a |
| NetDll_XNetQosLookup / ServiceLookup / Release | import | Fixed good QoS result from the system heap, which the runtime's release frees. | done |
| XamCreateEnumeratorHandle / XamEnumerate / NtClose (LSP list) | import | List the gameserver once. The enumerator handle and its private structure are made up, as the runtime cannot create them. | done |
| XMsgInProcessCall | import | Reborn only logs XLSP lookups on 1149. | n/a |
| XMsgStartIORequest (presence roster) | import | In-game presence resumes matchmaking. | done |
| XamShowSigninUI, XamUserCheckPrivilege, XamUserGetMembershipTypeFromXUID, XamUserGetSigninInfo, XamUserGetSigninState, XamUserGetXUID | import | `offline_profile.cpp` reports a Live-enabled Gold profile. Gameserver mode grants every privilege. | done |
| NTP XNetConnect skip | 82232950 | `NetDll_XNetConnect` succeeds for every address. | done |
| Grid default URL | 82018B70 | Written after the image loads. | done |
| XlspLookup | 824685C0 | Function override. | done |
| SbgRawResolve | 82239988 | Function override. | done |
| SbgSessionResolve | 8223ABB0 | Function override. | done |
| XNetServerToInAddr (title wrapper) | 8246AB10 | Function override. | done |
| SBG port (XLSP lookup) | 8223ADB0 | The built-in port 1000 is the 1149 gameserver's SBG port. | n/a |
| Massive server port | 824FB3C0 | Function override: ads go to the gameserver's port 1002. | done |
| local account locale | 8246A590 | Function override answering from the PC locale. | done |
| liveless XSessionCreate | 82572498 | Function override. | done |
| liveless XSession joined | 82516538 | Function override. | done |
| liveless remote update | 82514FC8 | Function override. | done |
| QNet leave / StartGame / EndGame ordering | 8222F538, 8222F6C0, 8222F7B0 | Function overrides; leaving removes native members and pauses matchmaking. | done |
| QNet native remote member | 8251CEC8 | Function override, fed by the matchmaking client. | done |
| QNet send data drop | 82231358 | Function override, relays through matchmaking. | done |
| main-thread pump (QNet relay delivery) | 82124150 | Function override. | done |
| avatar relay | 82118910 | Function override, from the Avatars the matchmaking client fetched. | done |
| PTNTP debug trap nop | 823312B0 | Check how the recompiled trap behaves. | open |

Reborn's loader thread also runs the matchmaking client and the avatar metadata cache. On the PC the client runs on a host thread (`matchmaking_client.cpp`), connects to the gameserver's matchmaking port 1001 and uses the shared [1vs100-mm-protocol](https://github.com/1vs100/1vs100-mm-protocol) library. Relayed Avatars go through the Avatar provider's checked remote store.

## Runtime socket defects

The ReXGlue 0.10.0 Linux runtime mishandles guest sockets:

- `bind`, `sendto` and `recvfrom` byte-swap the port and address of a guest `sockaddr_in`, which are already in network order.
- `select` passes the guest's `nfds` to the host, so host descriptors above it are never polled.
- `ioctlsocket` passes the Winsock command code (`FIONBIO` 0x8004667E) to the host `ioctl` unchanged.

Gameserver mode therefore implements the guest socket calls itself, on host BSD sockets, including overlapped `WSARecvFrom` and `WSASendTo`.

Other runtime gaps that gameserver mode covers:

- `XNetQosLookup` is a stub and `XNetQosServiceLookup` reports no results.
- `XNetQosListen` always fails, which makes hosting a QNet session fail.
- The XLSP server enumerator returns a garbage item count.
- XHttp is a stub.
- `XNetDnsLookup` always fails. Gameserver mode resolves `madserver.net` names to the gameserver and other names through the host.
- `XamCreateEnumeratorHandle` cannot create the XLSP server enumerator.
- `sprintf`-family calls read stack arguments from the wrong offset.

The recompile also missed 61 vtable this-adjustor thunks in vtables without RTTI. `native/boundaries-v3.toml` lists them.
