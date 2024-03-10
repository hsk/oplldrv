# テンポ指定とqコマンドが使える簡単なOPLLドライバを作り高速化とデータ圧縮を行う

テンポとqコマンドをコンパイラが処理する最小限の機能を備えたYM2416サウンドドライバとコンパイラを開発します。開発は独自エミュレータ上動くバイナリファイルを作って実行する形で行い、コンパイラはpython3で書き、サウンドドライバはC言語で書いてSDCCでコンパイルしてZ80のコードとして動きます。独自開発のエミュレータはコマンドライン上で実行するとwavファイルをすぐに生成するため開発が楽に行えます。また、クロック単位の高精度なベンチマークとパーセント表示で高速化を支援します。ドライバはC言語をリファレンス実装として書き、アセンブラ結果を参考にインラインアセンブラで高速化なバージョンを作ります。コンパイラはpython3で正規表現やパターンマッチング機能を使って複数パスで書きます。パーサは構文解析とオクターブや長さの処理を行いjsonの内部コードを生成し、意味解析は内部コードからバイトコードを出力します。

MML構文

```
mml  ::= line*            音楽データ
line ::= [DEFG] cmd*      1行データ
I    ::= [0-9]+           数値
L    ::= I(^I)*           長さ
cmd  ::=                  コマンド
       | o I              オクターブを指定します。
       | >                オクターブを１つ上げます。
       | <                オクターブを１つ下げます。
       | [cdefgab][+-]?L? トーンを指定します。
       | r L?             休符を指定します。
       | l L              デフォルト長を指定します。
       | v I              ボリュームを0-15で指定します。
       | t I              テンポを60秒に4分音符が何回鳴らせるかで指定します。
       | q I              ポルタメントを1-8で指定します。1で短く８だとなり続けます。
       | @ I              音色を指定します。 FM音源を使うので音色指定はできます。
```

MMLの構文を上に示します。MMLのデータは行データの集まりです。行データはチャンネルに対応し行のはじめにD E F G のうちどれかを書いてチャンネルを選択しその後にコマンドを連続して書きます。音階を表す cdefgab o<> とその他の rlvtq@ のコマンドが使えます。

コンパイラ内部構文

```
ir ::= i*            内部コードリスト
i  ::=               内部コード
     | ["tempo",  n] テンポ
     | ["wait",   n] ウエイト
     | ["keyoff", n] キーオフ
     | ["volume", n] ボリューム
     | ["tone",   n] トーン
     | ["@",      n] 音色
```

構文解析によって出力される内部コードを上に示します。長さやオクターブ、ポルタメントはパーサ側で処理するため内部コードに含まれません。

バイトコード構文

```
bytes   ::= byte*
byte    ::=
          | PKEYOFF b   キーオフ ウェイト時間を0-255 0は256の意味になります
          | PWAIT   b   ウェイト指定 ウェイト時間を0-255 0は256の意味になります
          | PTONE   b   トーン指定0-57
          | PVOLUME b   音色0-15 << 4 + 音量 0-15
          | PEND
PKEYOFF ::= 0
PWAIT   ::= 1
PTONE   ::= 2
PVOLUME ::= 3
PEND    ::= 4
b       ::= 0-255
```

バイトコードを上に示します。5種類のタグに続きいくつかデータが続いたバイトコードが複数存在します。バイトコードはキーオフとウェイト、トーン、音色音量、終了の５つのコードからなります。音量と音色は１つのコードで表されることに注意してください。トーンデータはバイトデータから表をドライバが引くので１バイトで済ませることができました。

## コンパイラの実装

ドライバ側でテンポ処理をするとなると掛け算を行わなければならなくなりそうなのでコンパイラ側でやったほうがよさそうです。テンポをコンパイラ側でうまく吸収し、ポルタメントの処理も行いたい。テンポ処理を行う簡潔な処理系を参考により簡潔なプログラムを組みたい。そこで我々は hra さんの bgm_driver https://github.com/hra1129/bgm_driver を参考にすることにしました。bgm_driver はC言語のワンパスコンパイラでコンパイルしZ80のアセンブラで書かれたMSXのPSG用のドライバです。テンポ管理は浮動小数点数で行われており、ポルタメントは音符の長さを８で割って書けるだけになってました。これは簡単です。テンポは`60*60/(tempo/4)` と書かれておりました。我々はより高レイヤーのpythonで複数パスに分けてコンパイラを開発することによりより理解しやすいものができるのではないかと考え開発しました。ワンパスコンパイラは１人カンバン方式のようなやり方をしているため看板を付けて持ち歩く手間がなく極めて高速に動作するので素晴らしい技術です。しかしながら我々には現代的な高速なコンピュータと高機能な言語処理系でコンパイルが可能です。pythonはMSX3の標準言語として使うという話もあるのでpythonで開発することを選択しました。

## ドライバの実装

- v0.1.0
     - データサイズは519bytesです。
     - C言語とアセンブラの実装をデファイン値で変更して比較できるようにしました。
     - コンテキストをループする処理に掛け算が含まれるので激重なのでポインタに加える形にすると速くなります。
- v0.2.0
     - データサイズは450bytesに減りました。v0.1.0のサイズの87%のサイズになりました。
     - トーンテーブルをドライバに持たせトーンデータを１バイトにしました。
     - ウェイト時は dec (hl) $ ret nz $ inc (hl) とすることですぐに抜けることで大幅に高速化できました。
     - 終了時はウェイトを最大にして抜けるとウェイトの負荷がさげられます。
     - トーンのあとは必ずウェイトが入るからウェイト処理をしてすぐ抜けるようにしました。

## 計測結果

C言語による実装はテストデータに対して519バイトのデータを出力し、CPU使用率が5.113%になりました。アセンブラによって最適化した実装はCPU使用率が2.887% になりました。アセンブラによる実装で1.77倍速くなりました。C言語の外側のループが遅いので最適化してみると 2.564% > 2.433% と変化し 1.99倍、2.10倍にできました。もっと速くなってもいいと思いますが今後の課題とします。

| version | percent| speed | 説明        |
| ------- | ------ | ------|----------- |
| v0.1.0  | 5.113% | 1.00  | c言語       |
| v0.1.0a | 2.887% | 1.77  | アセンブラ   |
| v0.1.0b | 2.564% | 1.99  | ループ最適化  |
| v0.1.0c | 2.433% | 2.10  | ループ最適化2 |

v0.2.0 では C言語による実装が 5.522%,アセンブラとその他の高速化で1.153%と4.79倍速くなりました。

| version | percent| speed | 説明        |
| ------- | ------ | ------|----------- |
| v0.2.0  | 5.522% | 1.00  | c言語       |
| v0.1.0a | 1.681% | 3.28  | アセンブラ   |
| v0.1.0b | 1.329% | 4.16  | ループ最適化  |
| v0.1.0c | 1.153% | 4.79  | ループ最適化2 |

C言語は機能追加とアセンブラ追従による減速が生じているのに対して、アセンブラは如実に高速化の影響が出ました。

## 考察

8bit時代のゲームプログラミングにおいてはどうしてもアセンブラによる最適化が必要になります。しかしながらアセンブラだけで書かれたサウンドドライバを理解するのは難しいことです。C言語でその設計を理解してからアセンブラで書くことで理解がより容易になります。コンパイラ側でテンポを調整すればポルタメントの処理もスムーズにできるはずなので実装してみました。テンポが変化するドライバをC言語で作れたのでアセンブラで最適化して２倍の速度に最適化できました。

テンポとポルタメントを実装する技術を理解することができました。
大規模なアセンブラでの開発をC言語のコンパイル結果を元に動かしながら実装する手法は小さなプログラムでは有効でした。しかしそれなりの規模になると状態数が爆発するので書き換えは困難になり失敗しました。
小さなプログラムをいちからアセンブラで書けばつらい状況にはなりませんが動きを理解するのは難しくなります。そこでC言語によるリファレンス実装をコメントとして書いておき実装することでなんとか実装できました。
最初の実装時は初心者レベルでしたのでケアレスミスで長い時間悩みました。この状況を変えるには同じプログラムを何度も書いてみる手法が使えます。
最初は初心者特有のミスが多く発生するため多くの時間がかかってしまいます。動くプログラムとの差分を見ればミスの箇所をすぐに見つけられます。
何度も同じプログラムを書いていると次第に覚えるのでミスが減りますし、ミスをしてもどこでミスをしていそうかがわかってくるのですぐにバグに気がつけるようになります。
１５分、１０分、８分、４分と実装時間が十分短くできるようになるとそれ以上はスピードアップが無理というレベルになりました。
プログラムを修正すると混乱してしまいますがノウハウが貯まればアセンブラネイティブに近づけるはずです。

## メモ

- ブランチの一覧
     - git branch -a
- ブランチに切り替え
     - git checkout main
     - git checkout v\*.\*.\*
- ブランチを作成
     - git checkout -b v\*.\*.\*
- ブランチをリモートに登録
     - git push -u origin v\*.\*.\*
- リモートブランチからローカルブランチを作成
     - git checkout -b v\*.\*.\* origin/v\*.\*.\*
- main にマージ
     - git checkout main
     - git merge v\*.\*.\*
     - git checkout v\*.\*.\*
- ローカルブランチ削除
     - git branch -d v\*.\*.\*
- リモートブランチを削除
     - git push origin --delete v\*.\*.\*
- diff を見る
     - https://github.com/hsk/oplldrv/compare/v0.1.0..v0.2.0
