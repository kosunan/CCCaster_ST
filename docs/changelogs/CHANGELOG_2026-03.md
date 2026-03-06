# chore: 不要 #include ディレクティブの一括削除 (21件/19ファイル)

## 2026-03-06

### Removed — 不要インクルード削除

静的解析＋手動検証により、以下の不要な標準ライブラリ/Windows ヘッダーを削除。

#### cli_launcher
| ファイル | 削除ヘッダー |
|----------|-------------|
| `controller/MainController.hpp` | `<vector>` |
| `network_wrapper/ConnectionHash.cpp` | `<chrono>` |
| `ConfigManager.cpp` | `<sstream>` |
| `ConfigManager.hpp` | `<vector>` |
| `test_connection_hash.cpp` | `<cassert>`, `<cstring>` |

#### core_dll/adapter_netplay
| ファイル | 削除ヘッダー |
|----------|-------------|
| `NetplayManager.hpp` | `<windows.h>` |
| `PacketRouter.cpp` | `<iostream>` |
| `SyncCoordinator.cpp` | `<cstring>` |

#### core_dll/adapter_network
| ファイル | 削除ヘッダー |
|----------|-------------|
| `UdpSocket.cpp` | `<iostream>` |
| `UdpSocket.hpp` | `<thread>` |

#### core_dll/adapter_os_hooks
| ファイル | 削除ヘッダー |
|----------|-------------|
| `api_hook/DxHook.cpp` | `<cstdio>` |
| `input/DirectInputHook.cpp` | `<map>` |

#### core_dll/feature_overlay_ui
| ファイル | 削除ヘッダー |
|----------|-------------|
| `ControllerMapper.cpp` | `<windows.h>`, `<cstdio>` |
| `Controller_Ui_Logic.cpp` | `<cmath>` |
| `Controller_Ui_Logic.hpp` | `<cstdint>` |
| `NetplayOverlay.cpp` | `<cstdio>` |

#### core_dll/game_memory_accessor
| ファイル | 削除ヘッダー |
|----------|-------------|
| `state/StateBuffer.hpp` | `<array>` (先行修正) |
| `state/StateBuffer.cpp` | `<iostream>` |

#### core_dll/pure_sync_engine
| ファイル | 削除ヘッダー |
|----------|-------------|
| `RollbackEngine.hpp` | `<cstring>`, `<iostream>` |
