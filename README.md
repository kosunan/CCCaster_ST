# CCCaster_v10

CCCaster_v10 は、旧来の解析難易度が高く密結合だったツール構造（旧CCCaster）を見直し、「開発・拡張が容易な近代化された構造」へと再設計した次世代の格闘ゲーム用通信同期（ロールバック）ツールです。

本プロジェクトは **「対戦にかかわる処理はすべてゲーム内（インゲーム）で行う」** および **「妥協なきパフォーマンスチューニングの徹底」** を基本設計理念としています。

## 主な機能とモジュール構成

1. **`Network`**: UDP通信、冗長化パケット（Redundancy）、セッション管理
2. **`Sync (Rollback Engine)`**: GGPO由来のState保存・巻き戻し・再計算、高精度フレームタイマー
3. **`GameInterface`**: API（DirectX, Input）フック、シーン監視、メモリアドレスの直接操作
4. **`App / UI`**: アプリケーションライフサイクル、コマンドラインUI、およびDirectXオーバーレイ描画

## 開発に参加する方へ

大規模な変更を加える前には、本プロジェクトの設計理念および実装ルールに必ず従ってください。  
開発への参加方法、ブランチ・コミットのルールについては [CONTRIBUTING.md](CONTRIBUTING.md) を参照してください。  
また、ソースコード中の専門用語に関しては [GLOSSARY.md](GLOSSARY.md) で定義されています。

## プロジェクト構造

* `src/`: ソースコード（`core_dll`, `cli_launcher`, `launcher` に分離）
* `docs/`: 過去の要件定義・設計資料（現行仕様との乖離あり。[AGENTS.md](AGENTS.md) 参照）
* `tests/`: ユニットテストおよびベンチマークコード
* `changelogs/`: バージョンおよび月別の変更履歴

## ビルド方法

ビルドは CMake と C++20 を使用して行います。
詳細なビルド手順は `.agents/workflows/build.md`、または `docs/` 配下のアーキテクチャ資料を参照してください。

```bash
# 依存ライブラリの自動取得とビルド
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j12
```
